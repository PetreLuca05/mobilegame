/* Self-tests that drive the real app through injected SDL touch events:
 *   touch       stick + camera + jump, multi-touch (starts in the hospital)
 *   onboarding  Welcome -> guest -> Username -> Creator (a tile in every
 *               tab, Randomise, Done) -> Main menu; run on a fresh install
 *   save        Main menu -> New Game, walk + ride to floor 2 (route test),
 *               then background + hard exit, like a swipe-kill
 *   continue    Main menu -> Continue; passes when back on the saved floor
 *   stickdir    stick up/right/down/left walk north/east/south/west
 *   autonav     auto-nav across the yard and into a room, a stick cancel,
 *               then yard -> floor 2 and floor 2 -> yard through the car
 *   offline     New Game on an unresolvable host (like airplane mode), play 120 s:
 *               count "net: attempt" lines after "frontend: PLAY" (must be 0)
 *   markers     tap a marker (walks), tap again (cancels), tap and arrive
 *               (reached, respawn), a drag from a marker becomes the stick
 *   interact    walk to a marker and the receptionist (panel up, a drag does
 *               nothing, a tap fires once), then the lift's 3D buttons, an auto-ride with no
 *               panel, desktop hover, Pause -> Performance; scan lag against brute force
 *   text        a line with the ten Romanian letters (and the cedilla forms): screenshot
 *   hudclamp    16 markers, every panel forced up, walking: no icon may touch a panel
 *   worldmap    WORLD-1: the island from New Game to Join, Continue, Maps, completion (autotest_world.h)
 *   ui          UI-1: press / release / tiers / reduced motion / targets / screenshots (autotest_ui.h)
 *   worldmap2   WORLD-2: two islands, the four vehicles, the shuffle rule (tools/worldmap2_test.sh adds the relaunch)
 *   nav         every room reachable, rides, timings (no walking)
 *   screenshot NAME  wait 3 s, save the frame as NAME.png, quit
 * Any script can take a picture with AutotestOut.screenshot; any run can add
 * timed ones with --screenshot-at S:NAME[,S:NAME...] (Documents on iOS,
 * the pref path on desktop; pull with xcrun devicectl device copy from).
 * Menus are tapped by widget id (ui_find), so they work on phones too
 * (xcrun devicectl device process launch ... --autotest onboarding). */
#ifndef MG_AUTOTEST_H
#define MG_AUTOTEST_H

#include <SDL3/SDL.h>
#include "autotest_friends.h"
#include "autotest_ui.h"
#include "autotest_world.h"
#include "platform/app.h"
#include "ui/marker_hud.h"
#include "ui/widgets.h"

typedef struct AutotestProbe {
    /* --autotest link */
    struct AccountLink *link;
    bool is_guest;
    struct Stats *stats;
    /* --autotest friends */
    struct Friends *friends;
    struct Lobby *lobby;
    struct FriendsPage *fpage;
    struct NoticePanel *notice;
    struct HeadIcons *heads;
    NetSkin *my_skin;
    const char *my_name;
    uint32_t npc_sync_tick, npc_sync_hash;
    HMM_Vec3 player_pos;
    HMM_Vec3 player_vel;
    float cam_yaw_deg;
    uint32_t jump_count;
    bool sprinting;
    int players_seen;
    const Ui *ui;      /* last frame's widgets */
    int fe_state;      /* FeState */
    bool playing;
    int floor;
    bool riding;
    uint32_t skin_hash; /* changes whenever the look changes */
    int route_step;    /* route test progress (-1 = not running) */
    int nav_state;     /* AutonavState */
    bool has_save;
    int rides;         /* elevator rides this run */
    float distance_m;  /* walked this run */
    uint32_t markers_reached;
    uint32_t active_marker;
    bool stick_active;
    int hud_count;
    const MarkerHudOut *hud;
    int nav_storey;    /* player's storey, -1 yard */
    bool lift_panel_open;
    bool interact_visible;     /* the preview panel is fully in */
    int interact_kind;         /* ia_kind_t of what it shows, -1 none */
    float interact_x, interact_y; /* its centre, pt */
    bool show_stats;
    int active_slot;           /* -1 none */
    /* --autotest interact only */
    uint32_t ia_fired[3];
    int ia_enter_lag, ia_exit_lag; /* worst, ticks */
    bool greeting;
    int it_slot[2];            /* marker, receptionist */
    HMM_Vec3 it_stand[2];
    HMM_Vec3 lift_at;
    float lift_btn_x[3], lift_btn_y[3];
    int hover_slot;            /* -1: none on screen, or no mouse */
    float hover_x, hover_y;
    uint32_t active_flags;
    const float *hud_res;      /* what marker icons stay out of: x, y, w, h */
    int hud_res_count;
    bool text_ok;              /* --autotest text: every Romanian letter has its glyph */
    struct Frontend *fe;
    const struct WorldMap *wm; /* WORLD-1: NULL until the island has been opened */
    struct WorldRides *world_rides; /* WORLD-2: the game's shuffle (prefs) */
    int play_level;            /* the level being played (book 0) */
    UiTest *uitest;            /* --autotest ui: the test screen's state */
} AutotestProbe;

/* What a script asks the game to do this frame. */
typedef struct AutotestOut {
    bool start_route; /* save: start walking the route test */
    bool kill;        /* save: background (saves) and exit at once */
    char screenshot[48]; /* save this frame as <name>.png ("" = none) */
    bool respawn_markers;
    bool nav_go;      /* start auto-nav to nav_pos / nav_to */
    HMM_Vec3 nav_pos;
    int nav_to;
    char debug_level[24];  /* worldmap: level-progress <level> <debug_pct> */
    int debug_pct;
    int force_ride;        /* worldmap2: 1 + the vehicle for the next trip, 0 none */
    char last_book[24];    /* worldmap2: make this the island played last */
    bool save_prefs;       /* worldmap2: write prefs.json now (the ride bag) */
} AutotestOut;

void autotest_finger(MgApp *app, SDL_EventType type, SDL_FingerID id, float x_pt, float y_pt);

typedef struct TapStep {
    int state;       /* FeState the step waits for */
    const char *key; /* widget to tap (ui_find) */
    bool look;       /* the tap must change the avatar */
} TapStep;

typedef struct Autotest {
    int kind; /* 0 none */
    double start;
    int stage;
    AutotestProbe at_start;
    float max_speed;
    bool done;
    bool passed;
    /* menu scripts */
    int step;
    double wait_until, step_start;
    int finger_down; /* finger id held, 0 none */
    float tap_x, tap_y;
    uint32_t skin_before;
    int skin_changes;
    char shot_name[48];  /* screenshot kind */
    uint32_t pick;       /* markers: the one being tapped */
    TapStep steps[12];
    int step_count;
    uint32_t it_fired, it_reached;
    bool it_hidden, it_priority;
    FriendsTest ft;
    int it_rides, it_hover;
    int clamp_frames, clamp_edge, clamp_hidden;
    WorldTest wt;
    char wt_phase[16]; /* worldmap2: "" or "relaunch" */
    WtTap map_tap; /* save / offline: through the island after New Game */
} Autotest;

void autotest_init(Autotest *t, int argc, char **argv);
/* Call every frame; pushes events and evaluates. Returns true when finished. */
bool autotest_update(Autotest *t, MgApp *app, const AutotestProbe *probe, AutotestOut *out);
/* --autotest touch|route start in the hospital; the menu scripts do not. */
bool autotest_starts_in_game(int argc, char **argv);
void autotest_tap_interact(MgApp *app, const AutotestProbe *p);
void autotest_tap_interact_up(MgApp *app, const AutotestProbe *p);

#endif
