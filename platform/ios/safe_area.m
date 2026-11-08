#import <UIKit/UIKit.h>

#include <SDL3/SDL.h>

/* The UIKit view's own insets, for when SDL's safe area is zero or stale after a rotation. */
bool ios_safe_insets(SDL_Window *window, float *left, float *top, float *right, float *bottom)
{
    UIWindow *w = (__bridge UIWindow *)SDL_GetPointerProperty(SDL_GetWindowProperties(window), SDL_PROP_WINDOW_UIKIT_WINDOW_POINTER, NULL);
    UIView *view = w.rootViewController.view;
    if (!view) {
        return false;
    }
    UIEdgeInsets e = view.safeAreaInsets;
    *left = (float)e.left;
    *top = (float)e.top;
    *right = (float)e.right;
    *bottom = (float)e.bottom;
    return true;
}
