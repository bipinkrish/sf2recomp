package org.sf2recomp.app;

import android.content.Context;
import android.content.res.AssetManager;
import java.io.File;
import java.io.FileOutputStream;
import java.io.IOException;
import java.io.InputStream;
import java.nio.charset.StandardCharsets;
import java.util.Properties;

final class GameAssets {
    static File prepare(Context context) throws IOException {
        AssetManager assets = context.getAssets();
        Properties builds = new Properties();
        try (InputStream input = assets.open("game-builds.properties")) { builds.load(input); }
        String identity;
        try (InputStream input = assets.open("game-package-id.txt")) {
            identity = new String(input.readAllBytes(), StandardCharsets.UTF_8).trim();
        }
        if (!identity.matches("[0-9a-f]{64}")) throw new IOException("Invalid game package identity");
        File root = GameStorage.prepare(context.getFilesDir(), context.getExternalFilesDir(null));
        File game = new File(context.getFilesDir(), "game");
        if (!GameBundle.isInstalled(game, identity, builds)) {
            try (InputStream input = assets.open("game.zip", AssetManager.ACCESS_STREAMING)) {
                GameBundle.install(input, game, identity, builds);
            }
        }
        installResources(assets, root, "");
        return game;
    }

    private static void installResources(AssetManager assets, File root, String path) throws IOException {
        if (path.equals("game.zip")) return;
        String[] children = assets.list(path);
        if (children != null && children.length > 0) {
            for (String child : children) installResources(assets, root, path.isEmpty() ? child : path + "/" + child);
        } else {
            File target = new File(root, path);
            if (path.equals("sf2recomp.ini") && target.isFile()) return;
            if (!target.getParentFile().mkdirs() && !target.getParentFile().isDirectory())
                throw new IOException("Cannot create resource directory");
            try (InputStream input = assets.open(path); FileOutputStream output = new FileOutputStream(target)) {
                byte[] buffer = new byte[256 * 1024];
                int count;
                while ((count = input.read(buffer)) != -1) output.write(buffer, 0, count);
            }
        }
    }
}
