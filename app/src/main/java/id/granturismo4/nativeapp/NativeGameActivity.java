package id.granturismo4.nativeapp;

import android.net.Uri;
import android.os.Bundle;
import android.os.ParcelFileDescriptor;
import android.util.Log;

import org.libsdl.app.SDLActivity;

import java.io.FileNotFoundException;

/** SDL3 host. A SAF file descriptor is transferred to native code without copying the ISO. */
public final class NativeGameActivity extends SDLActivity {
    public static final String EXTRA_DISC_URI = "id.granturismo4.nativeapp.DISC_URI";
    private static final String TAG = "GT4Native";
    private static native void nativeSetDisc(int ownedFd, String displayName);

    static {
        // Load the SDL entry library before handing it the selected document descriptor.
        System.loadLibrary("main");
    }

    @Override
    protected void onCreate(Bundle savedInstanceState) {
        String uriText = getIntent().getStringExtra(EXTRA_DISC_URI);
        if (uriText != null) {
            ParcelFileDescriptor descriptor = null;
            try {
                Uri uri = Uri.parse(uriText);
                descriptor = getContentResolver().openFileDescriptor(uri, "r");
                if (descriptor != null) {
                    int fd = descriptor.detachFd();
                    nativeSetDisc(fd, uri.getLastPathSegment() == null ? "Selected disc image" : uri.getLastPathSegment());
                    descriptor = null; // detachFd transfers ownership to native code.
                }
            } catch (Exception error) {
                Log.e(TAG, "Could not pass selected ISO to native runtime", error);
            } finally {
                if (descriptor != null) {
                    try { descriptor.close(); } catch (Exception ignored) {}
                }
            }
        }
        super.onCreate(savedInstanceState);
    }

    @Override
    protected String[] getLibraries() {
        return new String[] { "main" };
    }
}
