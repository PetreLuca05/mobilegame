/* One-frame GPU capture to a .gputrace file (Apple only; Metal's
 * MTLCaptureManager). iOS needs MetalCaptureEnabled in Info.plist, macOS
 * the environment variable MTL_CAPTURE_ENABLED=1. The file goes to the
 * app's Documents folder (desktop: the pref path); pull it from a phone with
 * xcrun devicectl device copy from ... --domain-type appDataContainer. */
#ifndef MG_METAL_CAPTURE_H
#define MG_METAL_CAPTURE_H

#include <stdbool.h>
#include <stddef.h>

/* Start capturing every command buffer of the default Metal device; false
 * (with the reason logged) when capture is unsupported or not enabled. */
bool metal_capture_begin(const char *name);
void metal_capture_end(void);

#endif
