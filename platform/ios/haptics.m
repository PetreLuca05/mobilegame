// HAPTIC-1: Core Haptics behind engine/platform/haptics.h. One CHHapticEngine for the app's lifetime,
// one pattern + player per table entry built once (off the main thread), started with
// CHHapticTimeImmediate. A play never creates an object: the per-player intensity parameter and the
// array that carries it are built with the player and only their value changes.
#import <CoreHaptics/CoreHaptics.h>
#import <Foundation/Foundation.h>

#include <stdatomic.h>

#include "platform/haptics.h"
#include "ui/haptic_table.h"

static CHHapticEngine *g_engine;
static NSMutableArray<id<CHHapticPatternPlayer>> *g_players;
static NSMutableArray<CHHapticDynamicParameter *> *g_params;
static NSMutableArray<NSArray<CHHapticDynamicParameter *> *> *g_param_lists;
static const HapticEntry *g_table;
static int g_count;
static atomic_bool g_ready;
static atomic_int g_rebuilds;
static dispatch_queue_t g_queue;
static float g_last_scale[HAPTIC_COUNT]; /* what each player's intensity parameter holds */

bool haptic_hw_supported(void)
{
    static int cached = -1;
    if (cached < 0) {
        cached = CHHapticEngine.capabilitiesForHardware.supportsHaptics ? 1 : 0;
    }
    return cached == 1;
}

static CHHapticPattern *pattern_of(const HapticEntry *e)
{
    NSMutableArray<CHHapticEvent *> *events = [NSMutableArray array];
    NSMutableArray<CHHapticParameterCurve *> *curves = [NSMutableArray array];
    for (int i = 0; i < e->n; i++) {
        const HapticEvent *v = &e->ev[i];
        NSArray *params = @[[[CHHapticEventParameter alloc] initWithParameterID:CHHapticEventParameterIDHapticIntensity value:v->intensity],
                            [[CHHapticEventParameter alloc] initWithParameterID:CHHapticEventParameterIDHapticSharpness value:v->sharpness]];
        if (v->dur > 0.0f) { /* continuous, fading to 0 */
            [events addObject:[[CHHapticEvent alloc] initWithEventType:CHHapticEventTypeHapticContinuous parameters:params relativeTime:v->t duration:v->dur]];
            NSArray *points = @[[[CHHapticParameterCurveControlPoint alloc] initWithRelativeTime:0 value:1.0f],
                                [[CHHapticParameterCurveControlPoint alloc] initWithRelativeTime:v->dur value:0.0f]];
            [curves addObject:[[CHHapticParameterCurve alloc] initWithParameterID:CHHapticDynamicParameterIDHapticIntensityControl controlPoints:points relativeTime:v->t]];
        } else {
            [events addObject:[[CHHapticEvent alloc] initWithEventType:CHHapticEventTypeHapticTransient parameters:params relativeTime:v->t]];
        }
    }
    NSError *err = nil;
    CHHapticPattern *p = [[CHHapticPattern alloc] initWithEvents:events parameterCurves:curves error:&err];
    return err ? nil : p;
}

static void build_players(void)
{
    atomic_store(&g_ready, false);
    NSMutableArray *players = [NSMutableArray arrayWithCapacity:(NSUInteger)g_count];
    NSMutableArray *params = [NSMutableArray arrayWithCapacity:(NSUInteger)g_count];
    NSMutableArray *lists = [NSMutableArray arrayWithCapacity:(NSUInteger)g_count];
    for (int i = 0; i < g_count; i++) {
        CHHapticPattern *p = pattern_of(&g_table[i]);
        NSError *err = nil;
        id<CHHapticPatternPlayer> pl = p ? [g_engine createPlayerWithPattern:p error:&err] : nil;
        CHHapticDynamicParameter *dp = [[CHHapticDynamicParameter alloc] initWithParameterID:CHHapticDynamicParameterIDHapticIntensityControl value:1.0f relativeTime:0];
        [players addObject:pl ? (id)pl : (id)[NSNull null]];
        [params addObject:dp];
        [lists addObject:@[dp]];
        g_last_scale[i] = 1.0f;
    }
    g_players = players, g_params = params, g_param_lists = lists;
    atomic_fetch_add(&g_rebuilds, 1);
    atomic_store(&g_ready, true);
}

void haptic_hw_init(const HapticEntry *table, int count)
{
    g_table = table, g_count = count;
    if (!haptic_hw_supported()) {
        atomic_store(&g_rebuilds, 1);
        return;
    }
    g_queue = dispatch_queue_create("mg.haptics", DISPATCH_QUEUE_SERIAL);
    dispatch_async(g_queue, ^{ /* off the first frame: launch time does not change */
        NSError *err = nil;
        g_engine = [[CHHapticEngine alloc] initAndReturnError:&err];
        if (!g_engine) {
            return;
        }
        g_engine.autoShutdownEnabled = YES;
        g_engine.playsHapticsOnly = YES;
        g_engine.stoppedHandler = ^(CHHapticEngineStoppedReason reason) {
            (void)reason; /* restarted on the next play or foreground */
        };
        g_engine.resetHandler = ^{ /* the server restarted: start again and rebuild the players */
            dispatch_async(g_queue, ^{
                [g_engine startAndReturnError:nil];
                build_players();
            });
        };
        [g_engine startAndReturnError:nil];
        build_players();
    });
}

void haptic_hw_play(int index, float scale)
{
    if (!atomic_load(&g_ready) || index < 0 || index >= g_count) {
        return; /* still building, or after a reset: dropped, never waited for */
    }
    id pl = g_players[(NSUInteger)index];
    if (pl == [NSNull null]) {
        return;
    }
    if (scale != g_last_scale[index]) { /* only the value changes: the parameter and its array were built with the player */
        g_last_scale[index] = scale;
        g_params[(NSUInteger)index].value = scale;
        [(id<CHHapticPatternPlayer>)pl sendParameters:g_param_lists[(NSUInteger)index] atTime:CHHapticTimeImmediate error:nil];
    }
    [(id<CHHapticPatternPlayer>)pl startAtTime:CHHapticTimeImmediate error:nil];
}

void haptic_hw_active(bool active)
{
    if (!g_engine) {
        return;
    }
    if (active) {
        dispatch_async(g_queue, ^{ [g_engine startAndReturnError:nil]; });
    } else {
        dispatch_async(g_queue, ^{ [g_engine stopWithCompletionHandler:nil]; });
    }
}

void haptic_hw_simulate_reset(void)
{
    if (g_engine && g_engine.resetHandler) {
        g_engine.resetHandler();
    } else {
        atomic_fetch_add(&g_rebuilds, 1);
    }
}

int haptic_hw_rebuilds(void) { return atomic_load(&g_rebuilds); }
