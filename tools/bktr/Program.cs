// Sf2Bktr — composes the Shadow Fight 2 update NCA's BKTR romfs over the base NCA.
//
// Data section of the patch NCA is a BKTR (bucket tree relocation) delta:
// the tool decrypts it with the titlekey from title.keys, reassembles the
// merged ivfc data layer (base + patch against the relocation table) and
// dumps it to game/extracted/update/romfs.img. Virtual 0 of that storage is
// the romfs partition header — extract with:
//   nstool -t romfs -x <outdir> romfs.img
//
// Build: dotnet build -c Release
// Env:   SF2BKTR_DUMP_OUT=path       merged partition image (main output)
//        SF2BKTR_DUMP_PATCH_EX=path  also dump the raw CTR-EX section
//                                    (ivfc hash layers + patch data slots)
//        SF2BKTR_PROBE=1             print section diagnostics (never keys)
//        SF2BKTR_FORCE_TK=1          re-add title.keys entries as external keys
using System;
using System.IO;
using System.Linq;
using LibHac.Common;
using LibHac.Common.Keys;
using LibHac.Fs;
using LibHac.FsSystem;
using LibHac.Spl;
using LibHac.Tools.FsSystem;
using LibHac.Tools.FsSystem.NcaUtils;

class Log : IProgressReport
{
    public void Report(long value) { }
    public void ReportAdd(long value) { }
    public void SetTotal(long value) { }
    public void LogMessage(string message) { Console.WriteLine(Sf2Constants.PROGRAM_S_LH_DE56 + message); }
}

static class Program
{
    static void Hex(string tag, IStorage s, long offset, int n)
    {
        try
        {
            byte[] b = new byte[n];
            s.Read(offset, b);
            Console.WriteLine(tag + Sf2Constants.PROGRAM_S_EMPTY_BD70 + offset + Sf2Constants.PROGRAM_S_EMPTY_6A86 + Convert.ToHexString(b));
        }
        catch (Exception e) { Console.WriteLine(tag + Sf2Constants.PROGRAM_S_EMPTY_2F7A + offset + Sf2Constants.PROGRAM_S_ERR_71C4 + e.Message); }
    }

    static byte[] HexBytes(string s)
    {
        var words = s.Split(Sf2Constants.PROGRAM_CH_EMPTY_730D, StringSplitOptions.RemoveEmptyEntries);
        return Convert.FromHexString(string.Concat(words));
    }

    static void PrintNcaDiag(string tag, Nca nca)
    {
        var hdr = nca.Header;
        byte[] rid = new byte[Sf2Constants.PROGRAM_N_16_B17E];
        hdr.RightsId.CopyTo(rid);
        Console.WriteLine(tag + Sf2Constants.PROGRAM_S_TITLEID_2989 + hdr.TitleId.ToString(Sf2Constants.PROGRAM_S_X_BA2D)
            + Sf2Constants.PROGRAM_S_KEYGEN_8523 + hdr.KeyGeneration + Sf2Constants.PROGRAM_S_RID_BB75 + Convert.ToHexString(rid));

    }

    static int Main(string[] args)
    {
        if (args.Length < Sf2Constants.PROGRAM_N_4_4B22)
        {
            Console.WriteLine(Sf2Constants.PROGRAM_S_USAGE_SF2BKTR_PATCHNCA_BASEN_835B);
            return 1;
        }
        string patchPath = args[0], basePath = args[1], keysPath = args[Sf2Constants.PROGRAM_N_2_D473];

        var keySet = KeySet.CreateDefaultKeySet();
        ExternalKeyReader.ReadKeyFile(keySet, keysPath, null, null, new Log());
        if (File.Exists(args[Sf2Constants.PROGRAM_N_3_4E07]))
        {
            using var ts = File.OpenRead(args[Sf2Constants.PROGRAM_N_3_4E07]);
            ExternalKeyReader.ReadTitleKeys(keySet, ts, new Log());
        }
        keySet.DeriveKeys(new Log());

        using var baseStorage = new LocalStorage(basePath, FileAccess.Read);
        using var patchStorage = new LocalStorage(patchPath, FileAccess.Read);
        var baseNca = new Nca(keySet, baseStorage);
        var patchNca = new Nca(keySet, patchStorage);

        if (Environment.GetEnvironmentVariable(Sf2Constants.PROGRAM_S_SF2BKTR_FORCE_TK_919F) != null)
        {
            foreach (string line in File.ReadLines(args[Sf2Constants.PROGRAM_N_3_4E07]))
            {
                int eq = line.IndexOf(Sf2Constants.PROGRAM_CH_EMPTY_C874);
                if (eq < 0) continue;
                byte[] rid = HexBytes(line[..eq].Trim());
                byte[] key = HexBytes(line[(eq + 1)..].Trim());
                if (rid.Length != Sf2Constants.PROGRAM_N_16_B17E || key.Length != Sf2Constants.PROGRAM_N_16_B17E) continue;
                keySet.ExternalKeySet.Add(new RightsId(rid), new AccessKey(key));
                Console.WriteLine(Sf2Constants.PROGRAM_S_FORCED_EXTERNAL_KEY_FOR_RID_772B + Convert.ToHexString(rid));
            }
        }

        bool probe = Environment.GetEnvironmentVariable(Sf2Constants.PROGRAM_S_SF2BKTR_PROBE_84D7) != null;
        if (probe)
        {
            PrintNcaDiag(Sf2Constants.PROGRAM_S_BASE_751B, baseNca);
            PrintNcaDiag(Sf2Constants.PROGRAM_S_PATCH_B67B, patchNca);
            foreach (var nm in new[] { Sf2Constants.PROGRAM_S_BASE_751B, Sf2Constants.PROGRAM_S_PATCH_B67B })
            {
                var n = nm == Sf2Constants.PROGRAM_S_BASE_751B ? baseNca : patchNca;
                foreach (var t in new[] { NcaSectionType.Code, NcaSectionType.Data })
                {
                    try { using var s = n.OpenStorage(t, IntegrityCheckLevel.None); Hex(nm + Sf2Constants.PROGRAM_S_EMPTY_194A + t, s, 0, Sf2Constants.PROGRAM_N_0X20_A766); }
                    catch (Exception e) { Console.WriteLine(nm + Sf2Constants.PROGRAM_S_EMPTY_194A + t + Sf2Constants.PROGRAM_S_OPEN_ERR_B9B0 + e.Message); }
                }
            }
        }

        // Raw AesCtrEx view of the patch romfs section: ivfc hash layers plus only
        // the patch's own data slots (base-relocated slots are placeholders here).
        string patchExOut = Environment.GetEnvironmentVariable(Sf2Constants.PROGRAM_S_SF2BKTR_DUMP_PATCH_EX_B138);
        if (!string.IsNullOrEmpty(patchExOut))
        {
            using var s = patchNca.OpenStorage(NcaSectionType.Data, IntegrityCheckLevel.None);
            Dump(s, patchExOut);
        }

        // Merged (base + patch) ivfc data storage: virtual 0 = romfs partition header.
        string dumpOut = Environment.GetEnvironmentVariable(Sf2Constants.PROGRAM_S_SF2BKTR_DUMP_OUT_A432);
        if (string.IsNullOrEmpty(dumpOut))
        {
            if (!probe) { Console.WriteLine(Sf2Constants.PROGRAM_S_SET_SF2BKTR_DUMP_OUT_OR_SF2B_03BF); return 1; }
            return 0;
        }
        using var merged = baseNca.OpenStorageWithPatch(patchNca, NcaSectionType.Data, IntegrityCheckLevel.None);
        merged.GetSize(out long mlen);
        Console.WriteLine(Sf2Constants.PROGRAM_S_MERGED_ROMFS_SIZE_9E79 + mlen);
        Dump(merged, dumpOut);
        return 0;
    }

    static void Dump(IStorage s, string outPath)
    {
        s.GetSize(out long len);
        Console.WriteLine(Sf2Constants.PROGRAM_S_DUMP_SIZE_33F6 + len);
        using var o = new FileStream(outPath, FileMode.Create);
        byte[] buf = new byte[1 << Sf2Constants.PROGRAM_N_20_F5CA];
        long done = 0;
        while (done < len)
        {
            int n = (int)Math.Min(buf.Length, len - done);
            s.Read(done, buf.AsSpan(0, n));
            o.Write(buf, 0, n);
            done += n;
        }
        Console.WriteLine(Sf2Constants.PROGRAM_S_DUMPED_A615 + outPath);
    }
}
