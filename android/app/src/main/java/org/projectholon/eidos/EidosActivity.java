package org.projectholon.eidos;

import android.content.ActivityNotFoundException;
import android.content.Context;
import android.content.Intent;
import android.content.pm.PackageInfo;
import android.graphics.Insets;
import android.net.Uri;
import android.os.Build;
import android.os.Bundle;
import android.os.Environment;
import android.provider.DocumentsContract;
import android.provider.Settings;
import android.view.View;
import android.view.WindowInsets;

import org.libsdl.app.SDLActivity;
import org.libsdl.app.SDLSurface;

import java.io.File;
import java.io.FileInputStream;
import java.io.FileOutputStream;
import java.io.InputStream;
import java.nio.charset.StandardCharsets;

/*
 * eidos on Android: SDL's activity, with one change to what the game is
 * told about the screen's edges (SDL_GetWindowSafeArea), plus a folder
 * picker for external .sav storage.
 *
 * SDL counts the system's gesture areas (the swipe down from the top, the
 * home gesture at the bottom, the back gestures at the sides) as unsafe.
 * In a fullscreen game those are only places a swipe starts; nothing
 * covers the picture there, so the one-screen layout would leave empty
 * bands for nothing.  The game is told only about what does cover it: the
 * display's cutouts (a camera hole) and the system bars while they show.
 */
public class EidosActivity extends SDLActivity {

    private static final int REQ_MANAGE_STORAGE = 9101;
    private static final int REQ_PICK_SAVE_DIR = 9102;

    private native void nativeOnSaveFolderPicked(String path);

    static class EidosSurface extends SDLSurface {
        EidosSurface(Context context) {
            super(context);
        }

        @Override
        public WindowInsets onApplyWindowInsets(View v, WindowInsets insets) {
            WindowInsets out = super.onApplyWindowInsets(v, insets);

            if (Build.VERSION.SDK_INT >= 30 /* Android 11 (R) */) {
                Insets in = insets.getInsets(WindowInsets.Type.systemBars() |
                                             WindowInsets.Type.displayCutout());

                SDLActivity.onNativeInsetsChanged(in.left, in.right, in.top, in.bottom);
            }
            return out;
        }
    }

    @Override
    protected void onCreate(Bundle savedInstanceState) {
        super.onCreate(savedInstanceState);
        extractBundledSpritePack();
    }

    private void extractBundledSpritePack() {
        final String name = "pokebw_ow_sprites.bwsp";
        File dst = new File(getFilesDir(), name);
        File stampFile = new File(getFilesDir(), name + ".stamp");
        String expectedStamp = null;
        try {
            PackageInfo pi = getPackageManager().getPackageInfo(getPackageName(), 0);
            long vcode = Build.VERSION.SDK_INT >= 28 ? pi.getLongVersionCode() : pi.versionCode;
            expectedStamp = pi.lastUpdateTime + ":" + vcode;
            if (dst.exists() && dst.length() > 16 && stampFile.exists()) {
                byte[] sb = new byte[128];
                try (FileInputStream sin = new FileInputStream(stampFile)) {
                    int sn = sin.read(sb);
                    if (sn > 0) {
                        String cur = new String(sb, 0, sn, StandardCharsets.UTF_8).trim();
                        if (expectedStamp.equals(cur)) {
                            return;
                        }
                    }
                }
            }
        } catch (Exception ignored) {
            expectedStamp = null;
        }
        try (InputStream in = getAssets().open(name)) {
            File tmp = new File(getFilesDir(), name + ".tmp");
            long total = 0;
            try (FileOutputStream out = new FileOutputStream(tmp)) {
                byte[] buf = new byte[16384];
                int n;
                while ((n = in.read(buf)) > 0) {
                    out.write(buf, 0, n);
                    total += n;
                }
            }
            if (total <= 16) {
                tmp.delete();
                return;
            }
            if (dst.exists()) {
                dst.delete();
            }
            if (tmp.renameTo(dst) && expectedStamp != null) {
                try (FileOutputStream sout = new FileOutputStream(stampFile)) {
                    sout.write(expectedStamp.getBytes(StandardCharsets.UTF_8));
                } catch (Exception ignored) {
                }
            }
        } catch (Exception ignored) {
            /* No bundled .bwsp pack in APK assets; runtime falls back to ROM synthesis. */
        }
    }

    @Override
    protected SDLSurface createSDLSurface(Context context) {
        return new EidosSurface(context);
    }

    public void pickSaveFolder() {
        runOnUiThread(new Runnable() {
            @Override
            public void run() {
                if (Build.VERSION.SDK_INT >= 30 && !Environment.isExternalStorageManager()) {
                    try {
                        Intent intent = new Intent(Settings.ACTION_MANAGE_APP_ALL_FILES_ACCESS_PERMISSION);
                        intent.setData(Uri.parse("package:" + getPackageName()));
                        startActivityForResult(intent, REQ_MANAGE_STORAGE);
                        return;
                    } catch (ActivityNotFoundException e) {
                        try {
                            Intent fallback = new Intent(Settings.ACTION_MANAGE_ALL_FILES_ACCESS_PERMISSION);
                            startActivityForResult(fallback, REQ_MANAGE_STORAGE);
                            return;
                        } catch (Exception ignored) {
                            nativeOnSaveFolderPicked("ERR:Could not open All Files Access settings");
                            return;
                        }
                    }
                }
                launchFolderPicker();
            }
        });
    }

    private void launchFolderPicker() {
        try {
            Intent intent = new Intent(Intent.ACTION_OPEN_DOCUMENT_TREE);
            intent.addFlags(Intent.FLAG_GRANT_READ_URI_PERMISSION |
                            Intent.FLAG_GRANT_WRITE_URI_PERMISSION |
                            Intent.FLAG_GRANT_PERSISTABLE_URI_PERMISSION);
            startActivityForResult(intent, REQ_PICK_SAVE_DIR);
        } catch (Exception e) {
            nativeOnSaveFolderPicked("ERR:No folder picker available on this device");
        }
    }

    private String resolveTreeUri(Uri treeUri) {
        if (treeUri == null) {
            return null;
        }
        String docId = DocumentsContract.getTreeDocumentId(treeUri);
        if (docId == null || docId.isEmpty()) {
            return null;
        }
        String[] parts = docId.split(":", 2);
        String vol = parts[0];
        String rel = parts.length > 1 ? parts[1] : "";
        if ("primary".equalsIgnoreCase(vol)) {
            String base = Environment.getExternalStorageDirectory().getAbsolutePath();
            return rel.isEmpty() ? base : (base + "/" + rel);
        }
        if ("home".equalsIgnoreCase(vol)) {
            String docs = Environment.getExternalStoragePublicDirectory(
                Environment.DIRECTORY_DOCUMENTS).getAbsolutePath();
            return rel.isEmpty() ? docs : (docs + "/" + rel);
        }
        if (vol.indexOf('/') >= 0 || "downloads".equalsIgnoreCase(vol) ||
            "raw".equalsIgnoreCase(vol)) {
            if ("raw".equalsIgnoreCase(vol) && rel.startsWith("/")) {
                return rel;
            }
            return null;
        }
        return rel.isEmpty() ? ("/storage/" + vol) : ("/storage/" + vol + "/" + rel);
    }

    @Override
    protected void onActivityResult(int requestCode, int resultCode, Intent data) {
        if (requestCode == REQ_MANAGE_STORAGE) {
            if (Build.VERSION.SDK_INT >= 30 && Environment.isExternalStorageManager()) {
                launchFolderPicker();
            } else {
                nativeOnSaveFolderPicked("ERR:All Files Access permission is required to save outside app storage");
            }
            return;
        }
        if (requestCode == REQ_PICK_SAVE_DIR) {
            if (resultCode != RESULT_OK || data == null || data.getData() == null) {
                nativeOnSaveFolderPicked("");
                return;
            }
            try {
                Uri uri = data.getData();
                String path = resolveTreeUri(uri);
                if (path == null) {
                    nativeOnSaveFolderPicked("ERR:Choose a local folder on device storage (e.g. Documents)");
                    return;
                }
                File dir = new File(path);
                if (dir.isDirectory() && dir.canWrite()) {
                    nativeOnSaveFolderPicked(dir.getAbsolutePath());
                } else {
                    nativeOnSaveFolderPicked("ERR:Folder is not writable (" + path + ")");
                }
            } catch (Exception e) {
                nativeOnSaveFolderPicked("ERR:Could not access selected folder");
            }
            return;
        }
        super.onActivityResult(requestCode, resultCode, data);
    }
}
