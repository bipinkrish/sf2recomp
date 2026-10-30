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
    public void LogMessage(string message) { Console.WriteLine("[lh] " + message); }
}

static class Program
{
    static void Hex(string tag, IStorage s, long offset, int n)
    {
        try
        {
            byte[] b = new byte[n];
            s.Read(offset, b);
            Console.WriteLine(tag + " @" + offset + ": " + Convert.ToHexString(b));
        }
        catch (Exception e) { Console.WriteLine(tag + "@" + offset + " ERR: " + e.Message); }
    }

    static byte[] HexBytes(string s)
    {
        var words = s.Split(' ', StringSplitOptions.RemoveEmptyEntries);
        return Convert.FromHexString(string.Concat(words));
    }

    static void PrintNcaDiag(string tag, Nca nca)
    {
        var hdr = nca.Header;
        byte[] rid = new byte[16];
        hdr.RightsId.CopyTo(rid);
        Console.WriteLine(tag + ": titleid=" + hdr.TitleId.ToString("x")
            + " keygen=" + hdr.KeyGeneration + " rid=" + Convert.ToHexString(rid));

    }

    static int Main(string[] args)
    {
        if (args.Length < 4)
        {
            Console.WriteLine("usage: Sf2Bktr <patchNca> <baseNca> <prod.keys> <title.keys>");
            return 1;
        }
        string patchPath = args[0], basePath = args[1], keysPath = args[2];

        var keySet = KeySet.CreateDefaultKeySet();
        ExternalKeyReader.ReadKeyFile(keySet, keysPath, null, null, new Log());
        if (File.Exists(args[3]))
        {
            using var ts = File.OpenRead(args[3]);
            ExternalKeyReader.ReadTitleKeys(keySet, ts, new Log());
        }
        keySet.DeriveKeys(new Log());

        using var baseStorage = new LocalStorage(basePath, FileAccess.Read);
        using var patchStorage = new LocalStorage(patchPath, FileAccess.Read);
        var baseNca = new Nca(keySet, baseStorage);
        var patchNca = new Nca(keySet, patchStorage);

        if (Environment.GetEnvironmentVariable("SF2BKTR_FORCE_TK") != null)
        {
            foreach (string line in File.ReadLines(args[3]))
            {
                int eq = line.IndexOf('=');
                if (eq < 0) continue;
                byte[] rid = HexBytes(line[..eq].Trim());
                byte[] key = HexBytes(line[(eq + 1)..].Trim());
                if (rid.Length != 16 || key.Length != 16) continue;
                keySet.ExternalKeySet.Add(new RightsId(rid), new AccessKey(key));
                Console.WriteLine("forced external key for rid " + Convert.ToHexString(rid));
            }
        }

        bool probe = Environment.GetEnvironmentVariable("SF2BKTR_PROBE") != null;
        if (probe)
        {
            PrintNcaDiag("base", baseNca);
            PrintNcaDiag("patch", patchNca);
            foreach (var nm in new[] { "base", "patch" })
            {
                var n = nm == "base" ? baseNca : patchNca;
                foreach (var t in new[] { NcaSectionType.Code, NcaSectionType.Data })
                {
                    try { using var s = n.OpenStorage(t, IntegrityCheckLevel.None); Hex(nm + ":" + t, s, 0, 0x20); }
                    catch (Exception e) { Console.WriteLine(nm + ":" + t + " OPEN-ERR: " + e.Message); }
                }
            }
        }

        // Raw AesCtrEx view of the patch romfs section: ivfc hash layers plus only
        // the patch's own data slots (base-relocated slots are placeholders here).
        string patchExOut = Environment.GetEnvironmentVariable("SF2BKTR_DUMP_PATCH_EX");
        if (!string.IsNullOrEmpty(patchExOut))
        {
            using var s = patchNca.OpenStorage(NcaSectionType.Data, IntegrityCheckLevel.None);
            Dump(s, patchExOut);
        }

        // Merged (base + patch) ivfc data storage: virtual 0 = romfs partition header.
        string dumpOut = Environment.GetEnvironmentVariable("SF2BKTR_DUMP_OUT");
        if (string.IsNullOrEmpty(dumpOut))
        {
            if (!probe) { Console.WriteLine("set SF2BKTR_DUMP_OUT (or SF2BKTR_PROBE)"); return 1; }
            return 0;
        }
        using var merged = baseNca.OpenStorageWithPatch(patchNca, NcaSectionType.Data, IntegrityCheckLevel.None);
        merged.GetSize(out long mlen);
        Console.WriteLine("merged romfs size: " + mlen);
        Dump(merged, dumpOut);
        return 0;
    }

    static void Dump(IStorage s, string outPath)
    {
        s.GetSize(out long len);
        Console.WriteLine("dump size: " + len);
        using var o = new FileStream(outPath, FileMode.Create);
        byte[] buf = new byte[1 << 20];
        long done = 0;
        while (done < len)
        {
            int n = (int)Math.Min(buf.Length, len - done);
            s.Read(done, buf.AsSpan(0, n));
            o.Write(buf, 0, n);
            done += n;
        }
        Console.WriteLine("dumped -> " + outPath);
    }
}
