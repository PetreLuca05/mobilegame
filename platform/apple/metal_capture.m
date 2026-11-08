/* engine/platform/metal_capture.h for iOS and macOS. SDL3 GPU does not
 * expose its MTLDevice; with one GPU (every iPhone / iPad, Apple silicon
 * Macs) MTLCreateSystemDefaultDevice() is the same device. Compiled with ARC. */
#import <Metal/Metal.h>

#include <SDL3/SDL.h>

#include "core/log.h"
#include "platform/metal_capture.h"

bool metal_capture_begin(const char *name)
{
    MTLCaptureManager *mgr = [MTLCaptureManager sharedCaptureManager];
    if (![mgr supportsDestination:MTLCaptureDestinationGPUTraceDocument]) {
        mg_log_warn("metal capture: .gputrace not supported (iOS: MetalCaptureEnabled, macOS: MTL_CAPTURE_ENABLED=1)");
        return false;
    }
#if TARGET_OS_IPHONE
    NSURL *dir = [[NSFileManager defaultManager] URLsForDirectory:NSDocumentDirectory
                                                        inDomains:NSUserDomainMask].firstObject;
#else
    char *pref = SDL_GetPrefPath("docvex", "mobilegame");
    NSURL *dir = [NSURL fileURLWithPath:@(pref ? pref : ".") isDirectory:YES];
    SDL_free(pref);
#endif
    NSURL *url = [dir URLByAppendingPathComponent:[NSString stringWithFormat:@"%s.gputrace", name]];
    [[NSFileManager defaultManager] removeItemAtURL:url error:nil];
    MTLCaptureDescriptor *d = [[MTLCaptureDescriptor alloc] init];
    d.captureObject = MTLCreateSystemDefaultDevice();
    d.destination = MTLCaptureDestinationGPUTraceDocument;
    d.outputURL = url;
    NSError *err = nil;
    if (![mgr startCaptureWithDescriptor:d error:&err]) {
        mg_log_warn("metal capture: %s", err.localizedDescription.UTF8String);
        return false;
    }
    mg_log_info("metal capture: recording one frame to %s", url.path.UTF8String);
    return true;
}

void metal_capture_end(void)
{
    MTLCaptureManager *mgr = [MTLCaptureManager sharedCaptureManager];
    if (mgr.isCapturing) {
        [mgr stopCapture];
        mg_log_info("metal capture: done");
    }
}
