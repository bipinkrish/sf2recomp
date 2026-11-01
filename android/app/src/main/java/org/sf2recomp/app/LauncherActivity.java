package org.sf2recomp.app;

import android.app.Activity;
import android.content.Context;
import android.content.Intent;
import android.database.Cursor;
import android.net.Uri;
import android.os.Bundle;
import android.provider.DocumentsContract;
import android.widget.Button;
import android.widget.LinearLayout;
import android.widget.TextView;
import java.io.File;
import java.io.FileOutputStream;
import java.io.IOException;
import java.io.InputStream;
import java.util.Properties;
import java.util.concurrent.ExecutorService;
import java.util.concurrent.Executors;

/** Imports the user's extracted files through Android's folder picker. */
public final class LauncherActivity extends Activity {
    private static final int IMPORT_GAME = 1;
    private final ExecutorService worker = Executors.newSingleThreadExecutor();
    private TextView status;
    private Button launch, importGame;
    private boolean busy;

    static File gameDirectory(Context context) {
        File base = context.getExternalFilesDir(null);
        return new File(base != null ? base : context.getFilesDir(), "extracted");
    }

    private boolean hasGame() {
        File root = gameDirectory(this);
        return new File(root, "exefs/main").isFile() && new File(root, "exefs/sdk").isFile()
            && new File(root, "romfs").isDirectory();
    }

    @Override public void onCreate(Bundle state) {
        super.onCreate(state);
        LinearLayout layout = new LinearLayout(this);
        layout.setOrientation(LinearLayout.VERTICAL);
        int pad = (int)(24 * getResources().getDisplayMetrics().density);
        layout.setPadding(pad, pad, pad, pad);
        TextView title = new TextView(this);
        title.setText("SF2 Recomp · Android development build");
        title.setTextSize(24);
        layout.addView(title);
        TextView instructions = new TextView(this);
        instructions.setText("Import your supported Switch v1.1.0 extracted folder containing exefs and romfs. "
            + "Connect a controller for combat. Menu taps are supported; combat touch controls and movies are still in development.");
        layout.addView(instructions);
        importGame = new Button(this);
        importGame.setText("Import extracted game folder");
        importGame.setOnClickListener(v -> startActivityForResult(
            new Intent(Intent.ACTION_OPEN_DOCUMENT_TREE).addFlags(Intent.FLAG_GRANT_READ_URI_PERMISSION), IMPORT_GAME));
        layout.addView(importGame);
        launch = new Button(this);
        launch.setText("Start game");
        launch.setOnClickListener(v -> startGame());
        layout.addView(launch);
        status = new TextView(this);
        status.setTextIsSelectable(true);
        layout.addView(status);
        setContentView(layout);
        refresh();
    }

    @Override protected void onResume() { super.onResume(); if (!busy && status != null) refresh(); }

    private void refresh() {
        String recoveryError = null;
        File target = gameDirectory(this);
        try { GameImport.recover(target, new File(target.getParentFile(), "extracted.previous")); }
        catch (IOException error) { recoveryError = error.getMessage(); }
        launch.setEnabled(hasGame() && !busy);
        importGame.setEnabled(!busy);
        status.setText((hasGame() ? "Game files available.\n" : "No game imported.\n")
            + "Game folder: " + gameDirectory(this) + "\nSaves, configuration and log: " + getFilesDir());
        if (recoveryError != null) status.append("\n" + recoveryError);
    }

    private void setBusy(boolean value) {
        busy = value;
        importGame.setEnabled(!value);
        launch.setEnabled(!value && hasGame());
    }

    @Override protected void onActivityResult(int request, int result, Intent data) {
        super.onActivityResult(request, result, data);
        if (request != IMPORT_GAME || result != RESULT_OK || data == null || data.getData() == null) return;
        Uri tree = data.getData();
        setBusy(true);
        status.setText("Importing game files… Keep this app open until the copy finishes.");
        worker.execute(() -> {
            String error = null;
            try { importTree(tree); } catch (Exception e) { error = e.getMessage() != null ? e.getMessage() : e.toString(); }
            final String failure = error;
            runOnUiThread(() -> {
                setBusy(false);
                refresh();
                if (failure != null) status.append("\nImport failed: " + failure);
            });
        });
    }

    private void importTree(Uri tree) throws IOException {
        File target = gameDirectory(this);
        File stage = new File(target.getParentFile(), "extracted.import");
        File previous = new File(target.getParentFile(), "extracted.previous");
        // Recover a previous interrupted swap before starting a new import.
        GameImport.recover(target, previous);
        GameImport.removeTree(stage);
        try {
            copyDirectory(tree, DocumentsContract.getTreeDocumentId(tree), stage);
            Properties builds = new Properties();
            try (InputStream input = getAssets().open("game-builds.properties")) {
                builds.load(input);
            }
            GameImport.commit(stage, target, previous, builds);
        } finally { GameImport.removeTree(stage); }
    }

    private void copyDirectory(Uri tree, String id, File destination) throws IOException {
        if (!destination.mkdirs() && !destination.isDirectory()) throw new IOException("Cannot create " + destination);
        Uri children = DocumentsContract.buildChildDocumentsUriUsingTree(tree, id);
        String[] columns = {DocumentsContract.Document.COLUMN_DOCUMENT_ID,
            DocumentsContract.Document.COLUMN_DISPLAY_NAME, DocumentsContract.Document.COLUMN_MIME_TYPE};
        try (Cursor cursor = getContentResolver().query(children, columns, null, null, null)) {
            if (cursor == null) throw new IOException("Cannot read selected folder");
            while (cursor.moveToNext()) {
                String childId = cursor.getString(0), name = cursor.getString(1), type = cursor.getString(2);
                if (name == null || name.isEmpty() || name.equals(".") || name.equals("..") || name.contains("/") || name.contains("\\"))
                    throw new IOException("Invalid file name in selected folder");
                File child = new File(destination, name);
                if (DocumentsContract.Document.MIME_TYPE_DIR.equals(type)) copyDirectory(tree, childId, child);
                else try (InputStream input = getContentResolver().openInputStream(
                    DocumentsContract.buildDocumentUriUsingTree(tree, childId))) {
                    if (input == null) throw new IOException("Cannot read " + name);
                    copyFile(input, child);
                }
            }
        }
    }

    private static void copyFile(InputStream input, File target) throws IOException {
        try (FileOutputStream output = new FileOutputStream(target)) {
            byte[] buffer = new byte[256 * 1024];
            int count;
            while ((count = input.read(buffer)) != -1) output.write(buffer, 0, count);
        }
    }

    private void installAssets(String path) throws IOException {
        String[] children = getAssets().list(path);
        if (children != null && children.length > 0) {
            for (String child : children) installAssets(path.isEmpty() ? child : path + "/" + child);
        } else {
            File target = new File(getFilesDir(), path);
            if (path.equals("sf2recomp.ini") && target.exists()) return;
            if (!target.getParentFile().mkdirs() && !target.getParentFile().isDirectory())
                throw new IOException("Cannot create resource directory");
            try (InputStream input = getAssets().open(path)) { copyFile(input, target); }
        }
    }

    private void startGame() {
        setBusy(true);
        status.setText("Preparing resources…");
        worker.execute(() -> {
            try {
                installAssets("");
                runOnUiThread(() -> {
                    setBusy(false);
                    startActivity(new Intent(this, GameActivity.class));
                });
            } catch (Exception e) {
                runOnUiThread(() -> { setBusy(false); status.setText("Cannot prepare resources: " + e.getMessage()); });
            }
        });
    }

    @Override protected void onDestroy() { worker.shutdown(); super.onDestroy(); }
}
