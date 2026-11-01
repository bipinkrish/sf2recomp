"""Cross-link the real Android platform layer using Bionic, for both ABIs."""
import os
from pathlib import Path
import shutil
import subprocess
import tempfile
import unittest

ROOT = Path(__file__).resolve().parents[1]
SDK = Path(os.environ.get("ANDROID_HOME", Path.home() / "Android/Sdk"))
TOOLCHAIN = SDK / "ndk/28.2.13676358/toolchains/llvm/prebuilt/linux-x86_64/bin"
SDL = ROOT / "build/dependencies/SDL3-3.4.16/include"


@unittest.skipUnless(shutil.which("javac") and shutil.which("java"), "Java JDK required")
class AndroidImportTests(unittest.TestCase):
    def test_invalid_import_preserves_game_and_failed_swap_recovers(self):
        source = r'''
package org.sf2recomp.app;
import java.io.*;
import java.nio.file.*;
import java.util.*;
public class GameImportTest {
    static Properties builds = new Properties();
    static File target, previous, stage;
    static void game(File root) throws Exception {
        new File(root,"exefs").mkdirs(); new File(root,"romfs").mkdirs();
        for (String module : new String[]{"main","sdk"}) {
            byte[] header = new byte[0x104];
            System.arraycopy("NSO0".getBytes("US-ASCII"),0,header,0,4);
            header[0x11]=1; header[0x18]=4; // uncompressed text: 4 bytes at file offset 0x100
            Arrays.fill(header,0x40,0x60,(byte)(module.equals("main") ? 0xab : 0xcd));
            Files.write(new File(root,"exefs/"+module).toPath(),header);
        }
    }
    static void rejected() throws Exception {
        try { GameImport.commit(stage,target,previous,builds); throw new AssertionError("accepted invalid game"); }
        catch (IOException expected) {}
        assert new File(target,"old").isFile();
        assert new File(previous,"backup").isFile();
    }
    public static void main(String[] args) throws Exception {
        File root=new File(args[0]);
        target=new File(root,"extracted"); previous=new File(root,"extracted.previous");
        stage=new File(root,"extracted.import");
        target.mkdirs();previous.mkdirs();
        Files.write(new File(target,"old").toPath(),new byte[]{1});
        Files.write(new File(previous,"backup").toPath(),new byte[]{2});
        builds.setProperty("main","ab".repeat(32)); builds.setProperty("sdk","cd".repeat(32));
        game(stage);
        Files.write(new File(stage,"exefs/main").toPath(),new byte[12]); rejected();
        game(stage);
        byte[] truncated=Files.readAllBytes(new File(stage,"exefs/main").toPath());
        Files.write(new File(stage,"exefs/main").toPath(),Arrays.copyOf(truncated,0x101));rejected();
        game(stage);
        byte[] header=Files.readAllBytes(new File(stage,"exefs/sdk").toPath());
        header[0x5f]^=1;Files.write(new File(stage,"exefs/sdk").toPath(),header); rejected();
        game(stage);
        header=Files.readAllBytes(new File(stage,"exefs/main").toPath());
        header[0]='X';Files.write(new File(stage,"exefs/main").toPath(),header); rejected();
        game(stage);GameImport.removeTree(new File(stage,"romfs"));rejected();
        game(stage);builds.remove("sdk");rejected();builds.setProperty("sdk","cd".repeat(32));
        // Simulate a failed rename after the old installation has been moved.
        File refused=new File(stage.toString()) { public boolean renameTo(File dest) { return false; } };
        try { GameImport.commit(refused,target,previous,builds);throw new AssertionError("swap succeeded"); }
        catch (IOException expected) {}
        assert new File(target,"old").isFile() && stage.isDirectory() && !previous.exists();
        Files.write(new File(stage,"new").toPath(),new byte[]{3});
        GameImport.commit(stage,target,previous,builds);
        assert new File(target,"new").isFile() && !new File(target,"old").exists();
        assert !stage.exists() && !previous.exists();
        // Recover if Android ended the process between the two renames.
        assert target.renameTo(previous);
        GameImport.recover(target,previous);
        assert new File(target,"new").isFile() && !previous.exists();
        System.out.println("Import validation, replacement, rollback and interrupted-swap recovery PASS");
    }
}
'''
        with tempfile.TemporaryDirectory(prefix="sf2-android-import-") as temporary:
            directory = Path(temporary)
            harness = directory / "GameImportTest.java"
            harness.write_text(source)
            implementation = ROOT / "android/app/src/main/java/org/sf2recomp/app/GameImport.java"
            subprocess.run(["javac", "-d", str(directory), str(implementation), str(harness)], check=True)
            subprocess.run(["java", "-ea", "-cp", str(directory), "org.sf2recomp.app.GameImportTest",
                            str(directory / "data")], check=True)


@unittest.skipUnless(TOOLCHAIN.is_dir() and SDL.is_dir(), "Android NDK r28c and prepared SDL sources required")
class AndroidPlatformTests(unittest.TestCase):
    def test_bionic_platform_links_for_phone_and_emulator(self):
        with tempfile.TemporaryDirectory(prefix="sf2-android-platform-") as temporary:
            for abi, compiler in (("arm64-v8a", "aarch64-linux-android33-clang"),
                                  ("x86_64", "x86_64-linux-android33-clang")):
                with self.subTest(abi=abi):
                    command = [str(TOOLCHAIN / compiler), "-O1", "-Wall", "-Wno-unused-function",
                               "-I" + str(ROOT / "runtime"), "-I" + str(SDL)]
                    if abi == "x86_64": command.append("-mcx16")
                    command += [str(ROOT / "tests/android_platform.c"), str(ROOT / "runtime/platform/android.c"),
                                "-latomic", "-lm", "-ldl", "-Wl,-z,max-page-size=16384", "-o", str(Path(temporary) / abi)]
                    subprocess.run(command, cwd=ROOT, check=True, capture_output=True, text=True)


if __name__ == "__main__":
    unittest.main()
