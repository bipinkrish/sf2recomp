package org.sf2recomp.app;

import java.io.DataInputStream;
import java.io.File;
import java.io.FileInputStream;
import java.io.IOException;
import java.util.Properties;

/** Validate staged files before touching the installed game. No Android APIs. */
final class GameImport {
    static void validate(File root, Properties builds) throws IOException {
        if (!new File(root, GameConstants.GAMEIMPORT_S_ROMFS_24E9).isDirectory())
            throw new IOException(GameConstants.GAMEIMPORT_S_SELECT_THE_FOLDER_CONTAINING_4EA2);
        for (String module : new String[]{GameConstants.GAMEIMPORT_S_MAIN_E4A9, GameConstants.GAMEIMPORT_S_SDK_DE0B}) {
            String expected = builds.getProperty(module, GameConstants.GAMEIMPORT_S_EMPTY_12AE);
            if (!expected.matches(GameConstants.GAMEIMPORT_S_0_9A_F_64_6BA7))
                throw new IOException(GameConstants.GAMEIMPORT_S_THIS_APK_IS_MISSING_ITS_GAME_D65C);
            File file = new File(root, GameConstants.GAMEIMPORT_S_EXEFS_89BA + module);
            byte[] header = new byte[GameConstants.GAMEIMPORT_N_0X100_BE6E];
            try (DataInputStream input = new DataInputStream(new FileInputStream(file))) {
                input.readFully(header);
            } catch (IOException error) {
                throw new IOException(GameConstants.GAMEIMPORT_S_EXEFS_89BA + module + GameConstants.GAMEIMPORT_S_IS_MISSING_OR_INCOMPLETE_C350, error);
            }
            if (header[0] != GameConstants.GAMEIMPORT_CH_N_83AC || header[1] != GameConstants.GAMEIMPORT_CH_S_657E || header[GameConstants.GAMEIMPORT_N_2_D473] != GameConstants.GAMEIMPORT_CH_O_A846 || header[GameConstants.GAMEIMPORT_N_3_4E07] != GameConstants.GAMEIMPORT_CH_0_59A9)
                throw new IOException(GameConstants.GAMEIMPORT_S_EXEFS_89BA + module + GameConstants.GAMEIMPORT_S_IS_NOT_AN_EXTRACTED_NSO_FILE_231A);
            long flags = u32(header, GameConstants.GAMEIMPORT_N_0XC_DBAA), length = file.length();
            for (int segment = 0; segment < GameConstants.GAMEIMPORT_N_3_4E07; segment++) {
                int base = GameConstants.GAMEIMPORT_N_0X10_BF9C + segment * GameConstants.GAMEIMPORT_N_0X10_BF9C;
                long offset = u32(header, base);
                long bytes = u32(header, (flags & (1 << segment)) != 0 ? GameConstants.GAMEIMPORT_N_0X60_3D6E + segment * GameConstants.GAMEIMPORT_N_4_4B22 : base + GameConstants.GAMEIMPORT_N_8_2C62);
                if (bytes != 0 && (offset < header.length || offset > length || bytes > length - offset))
                    throw new IOException(GameConstants.GAMEIMPORT_S_EXEFS_89BA + module + GameConstants.GAMEIMPORT_S_HAS_AN_INCOMPLETE_SEGMENT_58FB);
            }
            StringBuilder actual = new StringBuilder(GameConstants.GAMEIMPORT_N_64_A68B);
            for (int index = GameConstants.GAMEIMPORT_N_0X40_1AF3; index < GameConstants.GAMEIMPORT_N_0X60_3D6E; index++) {
                int value = header[index] & GameConstants.GAMEIMPORT_N_255_9556;
                actual.append(Character.forDigit(value >>> GameConstants.GAMEIMPORT_N_4_4B22, GameConstants.GAMEIMPORT_N_16_B17E));
                actual.append(Character.forDigit(value & GameConstants.GAMEIMPORT_N_15_E629, GameConstants.GAMEIMPORT_N_16_B17E));
            }
            if (!actual.toString().equals(expected))
                throw new IOException(GameConstants.GAMEIMPORT_S_EXEFS_89BA + module + GameConstants.GAMEIMPORT_S_DOES_NOT_MATCH_THIS_APK_S_SU_9558);
        }
    }

    private static long u32(byte[] data, int offset) {
        long value = 0;
        for (int index = 0; index < GameConstants.GAMEIMPORT_N_4_4B22; index++) value |= (long)(data[offset + index] & GameConstants.GAMEIMPORT_N_255_9556) << (index * GameConstants.GAMEIMPORT_N_8_2C62);
        return value;
    }

    static void recover(File target, File previous) throws IOException {
        if (previous.exists() && !target.exists() && !previous.renameTo(target))
            throw new IOException(GameConstants.GAMEIMPORT_S_CANNOT_RECOVER_THE_PREVIOUS__B487);
    }

    static void commit(File stage, File target, File previous, Properties builds) throws IOException {
        validate(stage, builds);
        removeTree(previous);
        if (target.exists() && !target.renameTo(previous))
            throw new IOException(GameConstants.GAMEIMPORT_S_CANNOT_PRESERVE_THE_CURRENT__09A6);
        if (!stage.renameTo(target)) {
            recover(target, previous);
            throw new IOException(GameConstants.GAMEIMPORT_S_CANNOT_INSTALL_THE_IMPORTED__713C);
        }
        removeTree(previous);
    }

    static void removeTree(File root) throws IOException {
        if (!root.exists()) return;
        if (root.isDirectory()) {
            File[] children = root.listFiles();
            if (children == null) throw new IOException(GameConstants.GAMEIMPORT_S_CANNOT_LIST_F8DC + root);
            for (File child : children) removeTree(child);
        }
        if (!root.delete()) throw new IOException(GameConstants.GAMEIMPORT_S_CANNOT_REMOVE_FCA1 + root);
    }
}
