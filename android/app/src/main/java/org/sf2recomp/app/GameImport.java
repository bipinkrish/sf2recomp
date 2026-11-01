package org.sf2recomp.app;

import java.io.DataInputStream;
import java.io.File;
import java.io.FileInputStream;
import java.io.IOException;
import java.util.Properties;

/** Validate staged files before touching the installed game. No Android APIs. */
final class GameImport {
    static void validate(File root, Properties builds) throws IOException {
        if (!new File(root, "romfs").isDirectory())
            throw new IOException("Select the folder containing exefs and romfs");
        for (String module : new String[]{"main", "sdk"}) {
            String expected = builds.getProperty(module, "");
            if (!expected.matches("[0-9a-f]{64}"))
                throw new IOException("This APK is missing its game build identifiers; rebuild it");
            File file = new File(root, "exefs/" + module);
            byte[] header = new byte[0x100];
            try (DataInputStream input = new DataInputStream(new FileInputStream(file))) {
                input.readFully(header);
            } catch (IOException error) {
                throw new IOException("exefs/" + module + " is missing or incomplete", error);
            }
            if (header[0] != 'N' || header[1] != 'S' || header[2] != 'O' || header[3] != '0')
                throw new IOException("exefs/" + module + " is not an extracted NSO file");
            long flags = u32(header, 0xc), length = file.length();
            for (int segment = 0; segment < 3; segment++) {
                int base = 0x10 + segment * 0x10;
                long offset = u32(header, base);
                long bytes = u32(header, (flags & (1 << segment)) != 0 ? 0x60 + segment * 4 : base + 8);
                if (bytes != 0 && (offset < header.length || offset > length || bytes > length - offset))
                    throw new IOException("exefs/" + module + " has an incomplete segment");
            }
            StringBuilder actual = new StringBuilder(64);
            for (int index = 0x40; index < 0x60; index++) {
                int value = header[index] & 255;
                actual.append(Character.forDigit(value >>> 4, 16));
                actual.append(Character.forDigit(value & 15, 16));
            }
            if (!actual.toString().equals(expected))
                throw new IOException("exefs/" + module + " does not match this APK's supported game build");
        }
    }

    private static long u32(byte[] data, int offset) {
        long value = 0;
        for (int index = 0; index < 4; index++) value |= (long)(data[offset + index] & 255) << (index * 8);
        return value;
    }

    static void recover(File target, File previous) throws IOException {
        if (previous.exists() && !target.exists() && !previous.renameTo(target))
            throw new IOException("Cannot recover the previous import");
    }

    static void commit(File stage, File target, File previous, Properties builds) throws IOException {
        validate(stage, builds);
        removeTree(previous);
        if (target.exists() && !target.renameTo(previous))
            throw new IOException("Cannot preserve the current game");
        if (!stage.renameTo(target)) {
            recover(target, previous);
            throw new IOException("Cannot install the imported files");
        }
        removeTree(previous);
    }

    static void removeTree(File root) throws IOException {
        if (!root.exists()) return;
        if (root.isDirectory()) {
            File[] children = root.listFiles();
            if (children == null) throw new IOException("Cannot list " + root);
            for (File child : children) removeTree(child);
        }
        if (!root.delete()) throw new IOException("Cannot remove " + root);
    }
}
