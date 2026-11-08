/* Desktop: no native Google sign-in (phones only in M2). */
#include "google_auth_internal.h"

bool google_auth_platform_available(const GoogleAuth *g)
{
    (void)g;
    return false;
}

bool google_auth_platform_start(GoogleAuth *g, struct SDL_Window *window)
{
    (void)g;
    (void)window;
    return false;
}
