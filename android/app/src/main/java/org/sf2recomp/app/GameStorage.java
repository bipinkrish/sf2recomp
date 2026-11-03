package org.sf2recomp.app;

import java.io.File;
import java.io.IOException;
import java.nio.file.Files;

/** User-editable state lives in Android/data; bundled assets have separate storage. */
final class GameStorage {
    static File prepare(File internal, File external) throws IOException {
        if (external == null || (!external.mkdirs() && !external.isDirectory()))
            throw new IOException(GameConstants.GAMESTORAGE_S_ANDROID_DATA_STORAGE_IS_UNAV_261E);
        File marker = new File(external, GameConstants.GAMESTORAGE_S_INTERNAL_STATE_MIGRATED_6BF0);
        if (!marker.exists()) {
            for (String name : new String[]{GameConstants.GAMESTORAGE_S_SF2RECOMP_INI_9F07, GameConstants.GAMESTORAGE_S_SAVE_38AA})
                copyMissing(new File(internal, name), new File(external, name));
            Files.write(marker.toPath(), new byte[]{1});
        }
        return external;
    }

    private static void copyMissing(File source, File target) throws IOException {
        if (Files.isSymbolicLink(source.toPath())) throw new IOException(GameConstants.GAMESTORAGE_S_INVALID_SAVED_DATA_LINK_8408);
        if (!source.exists()) return;
        if (source.isDirectory()) {
            if (!target.mkdirs() && !target.isDirectory()) throw new IOException(GameConstants.GAMESTORAGE_S_CANNOT_MIGRATE_SAVE_DIRECTOR_47A7);
            File[] children = source.listFiles();
            if (children == null) throw new IOException(GameConstants.GAMESTORAGE_S_CANNOT_READ_PREVIOUS_SAVE_DI_7CB2);
            for (File child : children) copyMissing(child, new File(target, child.getName()));
        } else if (!target.exists()) {
            File temporary = new File(target.getParentFile(), target.getName() + GameConstants.GAMESTORAGE_S_MIGRATING_9CC9);
            Files.copy(source.toPath(), temporary.toPath(), java.nio.file.StandardCopyOption.REPLACE_EXISTING);
            try { Files.move(temporary.toPath(), target.toPath()); }
            finally { Files.deleteIfExists(temporary.toPath()); }
        }
    }
}
