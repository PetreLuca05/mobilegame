/* HAPTIC-1: every haptic the game plays, one table. Patterns are transient taps (intensity,
 * sharpness) and, for CELEBRATE, one continuous buzz that fades to 0. Nothing else in the code
 * names an intensity: widgets pick an entry through their class (k_haptic_class), events name
 * their entry. Add an entry: a HAPTIC_* id here, its row in k_haptics (same order), then call
 * ui_haptic(HAPTIC_*) where it happens. */
#ifndef MG_HAPTIC_TABLE_H
#define MG_HAPTIC_TABLE_H

/* intensity ranks */
#define HR1 0.25f /* whisper: links, rows, tabs */
#define HR2 0.40f /* light: secondary actions */
#define HR3 0.60f /* medium: primary actions, state changes */
#define HR4 0.80f /* strong: the main action of a screen, confirmations */
#define HR5 1.00f /* heavy: celebrations and destructive confirmations only */
/* sharpness */
#define HS_SOFT 0.2f  /* the Jelly Pop default for presses */
#define HS_CRISP 0.6f /* selections, mechanical things */
#define HS_SHARP 0.9f /* errors and warnings only */

typedef enum HapticId {
    HAPTIC_NONE = -1,
    /* widgets (k_haptic_class) */
    HAPTIC_LINK, HAPTIC_TAB,
    HAPTIC_SECONDARY_DOWN, HAPTIC_SECONDARY,
    HAPTIC_PRIMARY_DOWN, HAPTIC_PRIMARY,
    HAPTIC_MAIN_DOWN, HAPTIC_MAIN,
    HAPTIC_DESTRUCTIVE_DOWN, HAPTIC_DESTRUCTIVE,
    HAPTIC_CONFIRM_DOWN, HAPTIC_CONFIRM,
    HAPTIC_TOGGLE_ON, HAPTIC_TOGGLE_OFF,
    HAPTIC_SLIDER_GRAB, HAPTIC_SLIDER_STEP, HAPTIC_SLIDER_END,
    HAPTIC_STEPPER, HAPTIC_FIELD_FOCUS,
    HAPTIC_SWATCH, HAPTIC_RANDOMISE, HAPTIC_LOCKER_DETENT,
    /* surfaces */
    HAPTIC_SURFACE_OPEN, HAPTIC_SURFACE_CLOSE, HAPTIC_WARNING_MODAL, HAPTIC_NOTICE,
    HAPTIC_INVITE_ACCEPTED, HAPTIC_INVITE_DECLINED, HAPTIC_PAUSE,
    /* account */
    HAPTIC_PILL, HAPTIC_CODE_COPIED, HAPTIC_LINKED, HAPTIC_REQUEST_SENT, HAPTIC_REQUEST_RECEIVED,
    /* learning and rewards */
    HAPTIC_ANSWER_SELECT, HAPTIC_CORRECT, HAPTIC_WRONG,
    HAPTIC_REWARD_0, HAPTIC_REWARD_1, HAPTIC_REWARD_2, HAPTIC_REWARD_3, HAPTIC_REWARD_4, HAPTIC_REWARD_5, HAPTIC_REWARD_6, HAPTIC_REWARD_7,
    HAPTIC_COMPLETE, HAPTIC_STREAK_KEPT, HAPTIC_STREAK_LOST,
    /* world */
    HAPTIC_STICK_DOWN, HAPTIC_STICK_FULL, HAPTIC_MARKER_TAP, HAPTIC_MARKER_REACHED, HAPTIC_REFUSED, HAPTIC_NAV_CANCEL,
    HAPTIC_INTERACT, HAPTIC_LIFT_KEY, HAPTIC_LIFT_ARRIVE,
    /* world map */
    HAPTIC_HOSPITAL, HAPTIC_VOYAGE_START, HAPTIC_VOYAGE_END, HAPTIC_VOYAGE_SKIP,
    /* TILT-1 */
    HAPTIC_TILT_JIGGLE,
    /* the lab's rank demos */
    HAPTIC_DEMO_R1, HAPTIC_DEMO_R2, HAPTIC_DEMO_R3, HAPTIC_DEMO_R4, HAPTIC_DEMO_R5,
    HAPTIC_COUNT
} HapticId;

typedef struct HapticEvent {
    float t;          /* s from the start */
    float intensity;  /* 0..1 */
    float sharpness;  /* 0..1 */
    float dur;        /* 0: a transient tap; > 0: continuous, fading to 0 over dur */
} HapticEvent;

#define HAPTIC_MAX_EVENTS 5

typedef struct HapticEntry {
    const char *name;
    int rank;         /* 1..5, 0 below R1 (Fin drops rank <= 1) */
    int n;
    HapticEvent ev[HAPTIC_MAX_EVENTS];
} HapticEntry;

#define TAP(i, s) {0.0f, (i), (s), 0.0f}
#define ECHO {0.06f, 0.3f, HS_SOFT, 0.0f} /* the jelly wobble after a main tap */
#define SUCCESS_EV {0.0f, HR3, HS_CRISP, 0.0f}, {0.10f, 0.9f, HS_CRISP, 0.0f}
#define ERROR_EV {0.0f, 0.7f, HS_SHARP, 0.0f}, {0.08f, 0.7f, HS_SHARP, 0.0f}, {0.16f, 0.4f, HS_SHARP, 0.0f}
#define WARNING_EV {0.0f, HR3, HS_SHARP, 0.0f}, {0.14f, HR3, HS_SHARP, 0.0f}
#define NOTICE_EV {0.0f, 0.5f, HS_SOFT, 0.0f}, {0.11f, 0.5f, HS_SOFT, 0.0f}
#define REWARD(k) {"reward token " #k, (k) == 7 ? 4 : 2, 1, {TAP((k) == 7 ? HR4 : 0.25f + 0.35f * (float)(k) / 7.0f, HS_CRISP)}}

static const HapticEntry k_haptics[HAPTIC_COUNT] = {
    {"link / row", 1, 1, {TAP(HR1, HS_SOFT)}},
    {"tab / segment", 1, 1, {TAP(HR1, HS_CRISP)}},
    {"secondary down", 1, 1, {TAP(HR1, HS_SOFT)}},
    {"secondary / icon", 2, 1, {TAP(HR2, HS_SOFT)}},
    {"primary down", 2, 1, {TAP(HR2, HS_SOFT)}},
    {"primary", 3, 1, {TAP(HR3, HS_SOFT)}},
    {"main action down", 3, 1, {TAP(HR3, HS_SOFT)}},
    {"main action", 4, 2, {TAP(HR4, HS_SOFT), ECHO}},
    {"destructive down", 2, 1, {TAP(HR2, HS_SHARP)}},
    {"destructive", 4, 1, {TAP(HR4, HS_SHARP)}},
    {"destructive confirm down", 3, 1, {TAP(HR3, HS_SHARP)}},
    {"destructive confirm", 5, 1, {TAP(HR5, HS_SHARP)}},
    {"toggle on", 3, 1, {TAP(HR3, HS_CRISP)}},
    {"toggle off", 2, 1, {TAP(HR2, HS_SOFT)}},
    {"slider grab", 1, 1, {TAP(HR1, HS_SOFT)}},
    {"slider step", 0, 1, {TAP(0.2f, HS_CRISP)}},
    {"slider end", 3, 1, {TAP(HR3, HS_CRISP)}},
    {"stepper / previous / next", 2, 1, {TAP(HR2, HS_CRISP)}},
    {"text field focus", 1, 1, {TAP(HR1, HS_SOFT)}},
    {"creator option", 1, 1, {TAP(HR1, HS_CRISP)}},
    {"randomise", 3, 2, {TAP(HR3, HS_SOFT), ECHO}},
    {"locker wheel detent", 0, 1, {TAP(0.25f, HS_CRISP)}}, /* an item passes the wheel's middle (as a slider step) */
    {"card / panel / modal opens", 2, 1, {TAP(HR2, HS_SOFT)}},
    {"card / panel / modal closes", 0, 1, {TAP(0.2f, HS_SOFT)}},
    {"warning modal", 3, 2, {WARNING_EV}},
    {"toast / invite arrives", 2, 2, {NOTICE_EV}},
    {"invite accepted", 4, 2, {SUCCESS_EV}},
    {"invite declined / expired", 1, 1, {TAP(HR1, HS_SOFT)}},
    {"pause opens / resumes", 2, 1, {TAP(HR2, HS_SOFT)}},
    {"account pill", 2, 1, {TAP(HR2, HS_SOFT)}},
    {"friend code copied", 3, 2, {TAP(HR3, HS_CRISP), ECHO}},
    {"account linked", 4, 2, {SUCCESS_EV}},
    {"friend request sent", 3, 1, {TAP(HR3, HS_SOFT)}},
    {"friend request received", 2, 2, {NOTICE_EV}},
    {"answer selected", 2, 1, {TAP(HR2, HS_SOFT)}},
    {"correct answer", 4, 2, {SUCCESS_EV}},
    {"wrong answer", 3, 3, {ERROR_EV}},
    REWARD(0), REWARD(1), REWARD(2), REWARD(3), REWARD(4), REWARD(5), REWARD(6), REWARD(7),
    {"level / island complete", 5, 5,
     {{0.0f, HR5, 0.5f, 0.0f}, {0.0f, 0.4f, HS_SOFT, 0.3f}, {0.12f, 0.4f, HS_SHARP, 0.0f}, {0.22f, 0.4f, HS_SHARP, 0.0f}, {0.33f, 0.4f, HS_SHARP, 0.0f}}},
    {"streak kept", 4, 2, {TAP(HR3, HS_CRISP), {0.12f, HR4, HS_CRISP, 0.0f}}},
    {"streak lost", 2, 1, {TAP(HR2, HS_SOFT)}},
    {"stick down", 1, 1, {TAP(0.3f, HS_SOFT)}},
    {"stick full speed", 2, 1, {TAP(HR2, HS_CRISP)}},
    {"marker tapped", 3, 1, {TAP(HR3, HS_CRISP)}},
    {"marker reached", 4, 2, {SUCCESS_EV}},
    {"no path / refused", 3, 3, {ERROR_EV}},
    {"navigation cancelled", 1, 1, {TAP(HR1, HS_SOFT)}},
    {"interaction panel", 3, 1, {TAP(HR3, HS_SOFT)}},
    {"lift floor key", 4, 1, {TAP(0.7f, HS_CRISP)}},
    {"lift arrives", 3, 1, {TAP(HR3, HS_SOFT)}},
    {"hospital selected / arrived", 3, 1, {TAP(HR3, HS_SOFT)}},
    {"voyage starts", 4, 2, {TAP(HR4, HS_SOFT), ECHO}},
    {"voyage ends", 3, 1, {TAP(HR3, HS_SOFT)}},
    {"voyage skipped", 1, 1, {TAP(HR1, HS_SOFT)}},
    {"tilt jiggle", 2, 1, {TAP(HR2, HS_SOFT)}},
    {"rank R1 whisper", 1, 1, {TAP(HR1, HS_SOFT)}},
    {"rank R2 light", 2, 1, {TAP(HR2, HS_SOFT)}},
    {"rank R3 medium", 3, 1, {TAP(HR3, HS_SOFT)}},
    {"rank R4 strong", 4, 1, {TAP(HR4, HS_SOFT)}},
    {"rank R5 heavy", 5, 1, {TAP(HR5, HS_SOFT)}},
};

/* Widget classes: what a widget kind plays on touch down and on its action. One row per kind;
 * the widget layer calls ui_haptic with these, nothing else picks a widget haptic. */
typedef enum HapticClass { HC_LINK, HC_TAB, HC_SECONDARY, HC_PRIMARY, HC_MAIN, HC_DESTRUCTIVE, HC_CONFIRM, HC_STEPPER, HC_SWATCH, HC_RANDOMISE,
                           HC_PILL, HC_ANSWER, HC_FIELD, HC_NONE, HC_COUNT } HapticClass;

static const struct { HapticId down, up; } k_haptic_class[HC_COUNT] = {
    [HC_LINK] = {HAPTIC_NONE, HAPTIC_LINK},
    [HC_TAB] = {HAPTIC_NONE, HAPTIC_TAB},
    [HC_SECONDARY] = {HAPTIC_SECONDARY_DOWN, HAPTIC_SECONDARY},
    [HC_PRIMARY] = {HAPTIC_PRIMARY_DOWN, HAPTIC_PRIMARY},
    [HC_MAIN] = {HAPTIC_MAIN_DOWN, HAPTIC_MAIN},
    [HC_DESTRUCTIVE] = {HAPTIC_DESTRUCTIVE_DOWN, HAPTIC_DESTRUCTIVE},
    [HC_CONFIRM] = {HAPTIC_CONFIRM_DOWN, HAPTIC_CONFIRM},
    [HC_STEPPER] = {HAPTIC_NONE, HAPTIC_STEPPER},
    [HC_SWATCH] = {HAPTIC_NONE, HAPTIC_SWATCH},
    [HC_RANDOMISE] = {HAPTIC_NONE, HAPTIC_RANDOMISE},
    [HC_PILL] = {HAPTIC_NONE, HAPTIC_PILL},
    [HC_ANSWER] = {HAPTIC_NONE, HAPTIC_ANSWER_SELECT},
    [HC_FIELD] = {HAPTIC_FIELD_FOCUS, HAPTIC_NONE},
    [HC_NONE] = {HAPTIC_NONE, HAPTIC_NONE},
};

#endif
