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
        long started = android.os.SystemClock.elapsedRealtime();
        AssetManager assets = context.getAssets();
        Properties builds = new Properties();
        try (InputStream input = assets.open(GameConstants.GAMEASSETS_S_GAME_BUILDS_PROPERTIES_E9AD)) { builds.load(input); }
        String identity;
        try (InputStream input = assets.open(GameConstants.GAMEASSETS_S_GAME_PACKAGE_ID_TXT_68B6)) {
            identity = new String(input.readAllBytes(), StandardCharsets.UTF_8).trim();
        }
        if (!identity.matches(GameConstants.GAMEASSETS_S_0_9A_F_64_6BA7)) throw new IOException(GameConstants.GAMEASSETS_S_INVALID_GAME_PACKAGE_IDENTIT_705F);
        File root = GameStorage.prepare(context.getFilesDir(), context.getExternalFilesDir(null));
        File game = new File(context.getFilesDir(), GameConstants.GAMEASSETS_S_GAME_12B5);
        boolean installed = GameBundle.isInstalled(game, identity, builds);
        android.util.Log.i(GameConstants.GAMEASSETS_S_SHADOWFIGHT2_A0A7, GameConstants.GAMEASSETS_S_STARTUP_BUNDLE_CHECK_DA54
            + (android.os.SystemClock.elapsedRealtime() - started) + GameConstants.GAMEASSETS_S_MS_INSTALLED_6346 + installed);
        if (!installed) {
            try (InputStream input = assets.open(GameConstants.GAMEASSETS_S_GAME_ZIP_1102, AssetManager.ACCESS_STREAMING)) {
                GameBundle.install(input, game, identity, builds);
            }
        }
        android.util.Log.i(GameConstants.GAMEASSETS_S_SHADOWFIGHT2_A0A7, GameConstants.GAMEASSETS_S_STARTUP_GAME_PREPARED_9A7A
            + (android.os.SystemClock.elapsedRealtime() - started) + GameConstants.GAMEASSETS_S_MS_89CB);
        installResources(assets, root, GameConstants.GAMEASSETS_S_EMPTY_12AE);
        android.util.Log.i(GameConstants.GAMEASSETS_S_SHADOWFIGHT2_A0A7, GameConstants.GAMEASSETS_S_STARTUP_RESOURCES_PREPARED_19FF
            + (android.os.SystemClock.elapsedRealtime() - started) + GameConstants.GAMEASSETS_S_MS_89CB);
        return game;
    }

    private static void installResources(AssetManager assets, File root, String path) throws IOException {
        if (path.equals(GameConstants.GAMEASSETS_S_GAME_ZIP_1102)) return;
        String[] children = assets.list(path);
        if (children != null && children.length > 0) {
            for (String child : children) installResources(assets, root, path.isEmpty() ? child : path + GameConstants.GAMEASSETS_S_EMPTY_DF7E + child);
        } else {
            File target = new File(root, path);
            if (path.equals(GameConstants.GAMEASSETS_S_SF2RECOMP_INI_9F07) && target.isFile()) return;
            if (!target.getParentFile().mkdirs() && !target.getParentFile().isDirectory())
                throw new IOException(GameConstants.GAMEASSETS_S_CANNOT_CREATE_RESOURCE_DIREC_B3CF);
            try (InputStream input = assets.open(path); FileOutputStream output = new FileOutputStream(target)) {
                byte[] buffer = new byte[GameConstants.GAMEASSETS_N_256_51E8 * GameConstants.GAMEASSETS_N_1024_E39E];
                int count;
                while ((count = input.read(buffer)) != -1) output.write(buffer, 0, count);
            }
        }
    }
}
