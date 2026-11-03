package org.sf2recomp.app;

import java.io.File;
import java.io.FileOutputStream;
import java.io.IOException;
import java.io.InputStream;
import java.nio.charset.StandardCharsets;
import java.nio.file.Files;
import java.util.Properties;
import java.util.zip.ZipEntry;
import java.util.zip.ZipInputStream;

/** Installs the locally packaged game; no import UI or conversion runs on Android. */
final class GameBundle {
    static boolean isInstalled(File target, String identity, Properties builds) throws IOException {
        File previous = new File(target.getParentFile(), target.getName() + GameConstants.GAMEBUNDLE_S_PREVIOUS_2518);
        GameImport.recover(target, previous);
        File marker = new File(target, GameConstants.GAMEBUNDLE_S_PACKAGE_ID_C002);
        if (!marker.isFile() || !new String(Files.readAllBytes(marker.toPath()), StandardCharsets.UTF_8).equals(identity)) return false;
        try { GameImport.validate(target, builds); return true; }
        catch (IOException invalid) { return false; }
    }

    static void install(InputStream archive, File target, String identity, Properties builds) throws IOException {
        File stage = new File(target.getParentFile(), target.getName() + GameConstants.GAMEBUNDLE_S_INSTALL_1C0F);
        File previous = new File(target.getParentFile(), target.getName() + GameConstants.GAMEBUNDLE_S_PREVIOUS_2518);
        GameImport.recover(target, previous);
        GameImport.removeTree(stage);
        if (!stage.mkdirs()) throw new IOException(GameConstants.GAMEBUNDLE_S_CANNOT_CREATE_GAME_STORAGE_82EB);
        try (ZipInputStream zip = new ZipInputStream(archive)) {
            String prefix = stage.getCanonicalPath() + File.separator;
            ZipEntry entry;
            byte[] buffer = new byte[GameConstants.GAMEBUNDLE_N_256_51E8 * GameConstants.GAMEBUNDLE_N_1024_E39E];
            while ((entry = zip.getNextEntry()) != null) {
                File file = new File(stage, entry.getName());
                if (entry.getName().contains(GameConstants.GAMEBUNDLE_S_EMPTY_687A) || !file.getCanonicalPath().startsWith(prefix))
                    throw new IOException(GameConstants.GAMEBUNDLE_S_INVALID_BUNDLED_GAME_PATH_B8D8);
                File directory = entry.isDirectory() ? file : file.getParentFile();
                if (!directory.mkdirs() && !directory.isDirectory())
                    throw new IOException(GameConstants.GAMEBUNDLE_S_CANNOT_CREATE_GAME_DIRECTORY_FCE4);
                if (!entry.isDirectory()) try (FileOutputStream output = new FileOutputStream(file)) {
                    int count;
                    while ((count = zip.read(buffer)) != -1) output.write(buffer, 0, count);
                }
                zip.closeEntry();
            }
            Files.write(new File(stage, GameConstants.GAMEBUNDLE_S_PACKAGE_ID_C002).toPath(), identity.getBytes(StandardCharsets.UTF_8));
            GameImport.commit(stage, target, previous, builds);
        } finally { GameImport.removeTree(stage); }
    }
}
