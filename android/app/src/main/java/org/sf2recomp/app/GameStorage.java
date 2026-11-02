package org.sf2recomp.app;

import java.io.File;
import java.io.IOException;
import java.nio.file.Files;

/** User-editable state lives in Android/data; bundled assets have separate storage. */
final class GameStorage {
    static File prepare(File internal, File external) throws IOException {
        if (external == null || (!external.mkdirs() && !external.isDirectory()))
            throw new IOException("Android/data storage is unavailable");
        File marker = new File(external, ".internal-state-migrated");
        if (!marker.exists()) {
            for (String name : new String[]{"sf2recomp.ini", "save"})
                copyMissing(new File(internal, name), new File(external, name));
            Files.write(marker.toPath(), new byte[]{1});
        }
        return external;
    }

    private static void copyMissing(File source, File target) throws IOException {
        if (Files.isSymbolicLink(source.toPath())) throw new IOException("Invalid saved-data link");
        if (!source.exists()) return;
        if (source.isDirectory()) {
            if (!target.mkdirs() && !target.isDirectory()) throw new IOException("Cannot migrate save directory");
            File[] children = source.listFiles();
            if (children == null) throw new IOException("Cannot read previous save directory");
            for (File child : children) copyMissing(child, new File(target, child.getName()));
        } else if (!target.exists()) {
            File temporary = new File(target.getParentFile(), target.getName() + ".migrating");
            Files.copy(source.toPath(), temporary.toPath(), java.nio.file.StandardCopyOption.REPLACE_EXISTING);
            try { Files.move(temporary.toPath(), target.toPath()); }
            finally { Files.deleteIfExists(temporary.toPath()); }
        }
    }
}
