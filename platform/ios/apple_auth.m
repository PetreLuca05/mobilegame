/* Sign in with Apple (step 2 fills this in). */
#include "platform/google_auth_internal.h"

bool apple_auth_platform_available(const GoogleAuth *g)
{
    (void)g;
    return false;
}

bool apple_auth_platform_start(GoogleAuth *g, struct SDL_Window *window)
{
    (void)g;
    (void)window;
    return false;
}
