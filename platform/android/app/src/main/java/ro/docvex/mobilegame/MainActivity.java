package ro.docvex.mobilegame;

import org.libsdl.app.SDLActivity;

/** SDL activity; SDL3 is linked statically into libmain.so. */
public class MainActivity extends SDLActivity {
    @Override
    protected String[] getLibraries() {
        return new String[] { "main" };
    }
}
