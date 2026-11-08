package id.granturismo4.nativeapp;

import org.libsdl.app.SDLActivity;

/** Hosts the SDL3 native runtime test; ISO inspection remains in MainActivity. */
public final class NativeGameActivity extends SDLActivity {
    @Override
    protected String[] getLibraries() {
        return new String[] { "main" };
    }
}
