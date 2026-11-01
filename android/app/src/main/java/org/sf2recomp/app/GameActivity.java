package org.sf2recomp.app;

import android.content.Context;
import android.net.wifi.WifiManager;
import org.libsdl.app.SDLActivity;

/** SDL owns the game thread, surface, controller, audio and touch event bridge. */
public final class GameActivity extends SDLActivity {
    private WifiManager.MulticastLock discoveryLock;

    @Override protected String[] getLibraries() { return new String[]{"SDL3", "main"}; }

    @Override protected String[] getArguments() {
        return new String[]{LauncherActivity.gameDirectory(this).getAbsolutePath()};
    }

    @Override protected void onResume() {
        super.onResume();
        WifiManager wifi = (WifiManager)getApplicationContext().getSystemService(Context.WIFI_SERVICE);
        if (wifi != null && discoveryLock == null) {
            discoveryLock = wifi.createMulticastLock("sf2-lan-discovery");
            discoveryLock.setReferenceCounted(false);
            discoveryLock.acquire();
        }
    }

    @Override protected void onPause() {
        if (discoveryLock != null) { discoveryLock.release(); discoveryLock = null; }
        super.onPause();
    }

    @Override protected void onStop() {
        super.onStop();
        // SDL can park its native event thread before it consumes an app
        // background event. Until guest/surface suspension is supported, end
        // the dedicated game process when this Activity is no longer visible.
        finish();
        android.os.Process.killProcess(android.os.Process.myPid());
    }

    @Override protected void onDestroy() {
        super.onDestroy();
        // Each launch needs fresh native globals and a new guest reservation,
        // including when validation returned before graphics initialization.
        android.os.Process.killProcess(android.os.Process.myPid());
    }
}
