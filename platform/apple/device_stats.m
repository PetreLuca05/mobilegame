#import <Foundation/Foundation.h>
#include <TargetConditionals.h>
#if TARGET_OS_OSX
#import <AppKit/AppKit.h>
#else
#import <UIKit/UIKit.h>
#endif
#include <mach/mach.h>
#include <pthread.h>

#include "platform/device_stats.h"

int device_boost_render_thread(void)
{
#if TARGET_OS_IOS
    struct sched_param param = {.sched_priority = 45};
    if (pthread_setschedparam(pthread_self(), SCHED_RR, &param) != 0) {
        return 0;
    }
    int policy = 0;
    pthread_getschedparam(pthread_self(), &policy, &param);
    return policy == SCHED_RR ? param.sched_priority : 0;
#else
    return 0;
#endif
}

int device_thermal_state(void) { return (int)[[NSProcessInfo processInfo] thermalState]; }

float device_resident_mb(void)
{
    task_vm_info_data_t info;
    mach_msg_type_number_t count = TASK_VM_INFO_COUNT;
    if (task_info(mach_task_self(), TASK_VM_INFO, (task_info_t)&info, &count) != KERN_SUCCESS) {
        return 0.0f;
    }
    return (float)info.phys_footprint / (1024.0f * 1024.0f);
}

float device_screen_max_hz(void *native_window)
{
#if TARGET_OS_OSX
    NSWindow *w = (__bridge NSWindow *)native_window;
    NSScreen *s = w ? w.screen : NSScreen.mainScreen;
    if (@available(macOS 12.0, *)) {
        return s ? (float)s.maximumFramesPerSecond : 0.0f;
    }
    return 0.0f;
#else
    UIWindow *w = (__bridge UIWindow *)native_window;
    UIScreen *s = w ? w.screen : UIScreen.mainScreen;
    return s ? (float)s.maximumFramesPerSecond : 0.0f;
#endif
}

int device_display_asleep(void)
{
#if TARGET_OS_OSX
    if (CGDisplayIsAsleep(CGMainDisplayID())) {
        return 1;
    }
    CFDictionaryRef d = CGSessionCopyCurrentDictionary();
    int locked = 0;
    if (d) {
        CFBooleanRef v = (CFBooleanRef)CFDictionaryGetValue(d, CFSTR("CGSSessionScreenIsLocked"));
        locked = v && CFBooleanGetValue(v);
        CFRelease(d);
    }
    return locked;
#else
    return 0;
#endif
}
