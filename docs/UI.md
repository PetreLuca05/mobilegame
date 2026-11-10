# UI

## Step 0 audit (UI-1, before the restyle)

Everything is immediate mode in points (pt), built from five primitives in `engine/ui/text.c` (`ui_rect`, `ui_round_rect`, `ui_circle`, `ui_line`, `text_draw`, plus `ui_textured_quad` for head icons), frosted panels (`gfx_ui_blur_panel`) and 3D insets.

### Screens and elements

| Element | Code | Drawn with | Blur | Notes |
|---|---|---|---|---|
| Main menu | `game/frontend.c` `main_menu` | `title_block` (44 pt text x3: shadow, white, fake bold), 1 px divider, `menu_row` (hand-drawn rounded rect r12, `k_row`), `ui_scroll`, Continue `ui_button`; overwrite dialog = dim rect + `ui_panel` + 2 buttons | no | request badge red circle |
| Welcome | `frontend.c` `welcome` | wrapped text, `pill` (hand RR r26; Google "G" as text, apple from 4 circles), guest link = text + underline | yes | |
| Username | `frontend.c` `username` | `ui_text_field`, `ui_button`, back = button + 2-line chevron | yes | |
| Creator / Locker | `frontend.c` `creator` -> `game/creator.c` `creator_ui` | tab buttons, tiles (hand RR r12), selection ring, swatch circles, horizontal scroll | yes | |
| Loading bar | `frontend.c` (FE_LOADING) | 2 RR + text | no | |
| Lobby list | `frontend.c` `lobby_panel` | text, hand-drawn switch (copy of `ui_toggle`, green), Invite button, rows (RR r12 + head icon + kick x from lines + crown) | no | |
| Toast | `frontend.c` `frontend_toast_draw` | RR r20 + text | no | |
| Settings page | `game/settings_menu.c` `settings_menu` | back (circle + chevron), tab buttons, scroll; Game tab: sliders, toggles, segmented buttons, Developer fold; Account tab: text field, buttons, head icon | yes (full screen) | |
| Pause button | `settings_menu.c` | hit area, blurred disc, 2 bars | yes | blur forced 2 extra passes on every play frame |
| Pause card | `settings_menu.c` `pause_card` | dim rect, `ui_panel`, button / toggle column in a scroll, lobby column (RR rows, heads, badge) | no | |
| Friends page | `game/friends_page.c` `friends_page_draw` | back, own head + name, tab buttons + badge, divider, scroll; rows: head icon, status dot, `small_button`, `disabled_chip`, hamburger (3 bars); search field; action modal and remove confirm (dim + panel + buttons, `soon_row`) | yes (full screen) | |
| Invite / notice panel | `engine/ui/notice_panel.c` | RR r14, head, two text lines, 2 buttons, timer bar | no | slides from the top, 0.18 s |
| Interaction preview card | `game/preview_panel.c` | 3D rounded card + model in insets; UI: 1 px rim, label, chip | no | slide 0.18 / 0.12, crossfade 0.15 |
| Greeting card | `game/game.c` (greet) | blur panel + 2 texts | yes | 2.5 s |
| Talk "..." cloud | `game.c` `draw_cloud_at` | RR + 3 circles | no | |
| Lift panel | `game/elevator_panel.c` | 3D only (slab, cylinder keys, text meshes) in an inset | no | |
| Marker HUD | `engine/ui/marker_hud.c` | circle ring + disc, chevron lines, 11 pt distance, floor badge | no | |
| Account chip, head icons | `engine/ui/head_icons.c` | chip RR r22 + head (atlas quad, circle clip in the shader) + 16 pt name | no | |
| Account-link dialog | `game/account_link.c` | dim, spinner (8 circles), panel, buttons, 64 pt head | no | |
| World map | `game/worldmap.c` | top bar RR + bar, level badges with a progress ring of line segments, `text_button`s, back chevron, voyage pill, 44 pt title, card (flat since WORLD-2) | no | |
| Perf HUD | `engine/ui/hud.c`, `prof_hud.c` | shadowed text | no | |
| Touch stick, jump | `engine/ui/touch_controls.c` | circles; jump = arrow glyph | no | |
| Name tags | `game.c` `name_tag` | 12 pt text with a 4-copy black outline | no | |
| Net status banner | `game.c` | RR + text | no | |

Shared widget layer before UI-1 (`engine/ui/widgets.c`): `ui_panel`, `ui_button`, `ui_slider`, `ui_toggle`, `ui_letter_keypad`, `ui_hit_area`, `ui_scroll_begin/end`, `ui_text_field`, `ui_find` (autotests). Hand-drawn outside it: menu rows, pills, the lobby switch, every back button, the pause button, friends chips and rows, creator tiles, world-map badges / bars / card, notice panel, preview card, greeting, toast, chip, marker HUD, stick, name tags, cloud.

### Rendering before UI-1

- One UI vertex stream (`GfxUiVertex`, 20 bytes, up to 8192 quads), one pipeline, one single-channel SDF atlas (font + a solid cell + a circle cell) plus the head atlas (u >= 2). `ui_round_rect` = 3 rects + 4 circle quarters = 7 quads; no outline or gradient support.
- Draws: one per batch; a new batch only at a scroll region's clip on / off. Typical frame: 1-5 UI draws.
- Where: inside the main pass when the 3D image is at native size; otherwise a separate UI pass (upscale + panels + UI). Before UI-1 that separate pass also stuck after any blur panel, preview or screenshot (fixed in b65cf9d: decided per frame).
- `gfx_ui_blur_panel`: quarter-res copy of the 3D image + 2 Gaussian passes + one draw per panel (up to 4). Call sites: welcome, username, creator, pause button, settings page, friends page, greeting card.
- Insets (`gfx_inset_begin`): picture-in-picture viewports inside the main pass (lift panel, preview card), no extra pass. Head icons: one offscreen pass on frames that render a head. `gfx_preview`: offscreen pass, no live caller.

### Icons before UI-1

Back chevrons (2 lines) in frontend, settings, friends, world map; close x; crown (7 lines); pause bars; hamburger; apple / "G"; spinner; progress ring from lines; marker chevron; status dot; toggle knobs; "..." cloud; atlas glyphs arrow-up (jump), gear (unused), degree, bullet; "<" / ">" as text. No check or plus icon.

### Font and colours before UI-1

Fredoka (weight 600), one SDF atlas 1024 x 512 (`tools/gen_font_atlas.py`), ASCII + Latin-1 / Extended-A + composed Romanian letters, sizes 10-44 pt per call, fake bold by double draw. One shared theme (`k_ui_theme`, widgets only); about 54 distinct colour literals elsewhere, radii 10-22 as literals, durations and easing per file.

## UI-1 "Jelly Pop" (after the restyle)

### Where things live

| File | What |
|---|---|
| `engine/ui/ui_style.h` | `k_ui`, the one token table: every colour, size, radius, duration, curve and effect number. Nothing else hard-codes them (exceptions: developer overlays, autotest cards, world-space marker colours). |
| `engine/ui/ui_anim.c` | 128-tween pool (`ui_anim_to`, `ui_anim_keys`, `ui_anim_loop`); a new tween starts from the current value; `ui_bezier` evaluates CSS-style curves. |
| `engine/ui/ui_fx.c` | pools: 64 particles, 4 rings, 8 reward tokens; banner, UI shake, procedural sounds (`audio_tone`), haptics, `ui_fx_tier`. |
| `engine/ui/ui_widgets.c` | components: buttons (primary, secondary, accent, destructive, ok, disabled), icon button, switch, tabs, counter, progress, row, answer row, card, surface, modal, screen enter, stagger, shake, pop. |
| `engine/ui/widgets.c` | input core: `ui_press` (press / release motion, T0, action on the touch-up), `ui_find`, `ui_rect_of`, `ui_remember`, scroll, slider, text field. |
| `shaders/ui.*.hlsl` | one SDF shader: rounded box, lip, highlight band, stroke, dashes, glyphs, head icons, clip. |

Fonts: Baloo 2 ExtraBold (display: titles, buttons, numbers), Nunito Bold (body), Nunito ExtraBold (labels), one MGF2 atlas (`tools/gen_font_atlas.py`); OFL files in `assets/fonts/`, hashes in `assets/ASSETS.lock`.

### Rendering

The whole UI is one instanced draw (`GfxUiInst`, 96 bytes, up to 8192 per frame) inside the existing pass; particles, rings, tokens and the banner are instances in the same draw. No blur anywhere: modals use the flat `scrim`. Scroll areas never clip across their axis; along it only an end that content extends past is clipped, and there content dissolves over the last 12 pt (`gfx_ui_clip_fade`, a per-instance value in the same draw). Touches use the exact area. `gfx_ui_group` squashes a widget about its origin (press, pop, breathe); `gfx_ui_screen` moves the whole layer (screen enter, stagger, UI shake). The 3D lift panel and the preview card stay 3D insets; only their colours changed (surface body, primary keys with the press scale on the key mesh; the preview card is surface on a lip).

### Motion

| Name | Spec (tokens) |
|---|---|
| press | (1.07, 0.86), origin bottom-centre, 90 ms ease-out, held |
| release | 480 ms keys 0 / 30 / 55 / 78 / 100 %: (1.07, 0.86) (0.93, 1.12) (1.04, 0.96) (0.99, 1.02) (1, 1) |
| state spring | 450 ms, bezier(.3, 1.8, .5, 1): switch knob, tab indicator |
| fill | 700 ms, same curve: progress bars |
| enter | 550 ms, bezier(.3, 1.6, .5, 1): screens slide in from the left, modals scale from 0.8, toasts from the top |
| exit | 180 ms ease-in, alpha out in the last 80 ms: toasts, notice panel |
| stagger | menu rows and friends rows 40 ms apart, at most 8 |
| bump / pop / shake | counter 1 -> 1.22 -> 1 in 200 ms; confirm 1 -> 1.05 -> 1 in 380 ms; x offsets 0 -1 1 -0.7 0.7 -0.35 0.35 0 over 320 ms |
| breathe / bob | the screen's main button 1 <-> 1.03 over 2.4 s; reward icons -3 pt and 8 deg over 2.2 s; stop under a modal |

Deferred: **exit-then-enter for screen changes.** Screens only enter (the layer slides in from the left, rows stagger); cards, modals, toasts and the notice panel have their own enter and toasts / notices their exit. A real screen exit needs the front end to keep drawing the old screen, input off, for 180 ms after the state has already changed, i.e. a drawn state separate from the logical one across `frontend.c` and the screen dispatch in `game.c`. Accepted as deferred at UI-1 sign-off (a8b1d90); do not restructure `game.c` for it until a milestone asks.

Bob is defined (`bob_y`, `bob_tilt_deg`, `bob_s`) but unused: no reward icon sits idle on any screen yet. Keep the constants for the first one.

Tokenised exception: the lift's floor display stays a dark plate (`lift_display_bg`, digit `lift_display_digit`), the one place a dark panel is right; the panel body is `lip_mint` sides under a `surface` face, the keys `primary`.

Reduced (Settings -> Animații interfață -> Reduse, or iOS Reduce Motion): no bursts, rings, shake, breathe or bob; press is a 4 % uniform scale; the rest are 150 ms fades or linear moves; the reward counts up over 400 ms.

### Tiers

| Tier | When | Motion | Effects | Sound | Haptic |
|---|---|---|---|---|---|
| T0 | touch down on a pressable | press | - | down | light |
| T1 | button action | release | burst 8 (90) + ring | tap | - |
| T1 tab | tab, stepper | release | - | tick | - |
| T1 toggle | switch | spring | burst 6 (54) when on | toggle on / off | medium |
| T2 | confirm, accept, correct | release + pop, ok | burst 16 + ring | success | success |
| T2- | wrong, refused, failed | shake 10 pt, bad | none | error | error |
| T3 | reward, completion | reward flight, banner | burst 14 at source; banner + burst 26 (200) + ring + UI shake 8 | coin ticks, fanfare | success, heavy |
| N | toast, invite | enter | - | notice | light |

### Adding a widget

1. Draw it from tokens only (`k_ui.*`); add a token to `ui_style.h` if none fits.
2. Take input through `ui_press(ui, "Label##id", ...)`: that gives press / release motion, T0, the touch-up action and an id `ui_find` can tap. A rect that is looked up but not tapped goes through `ui_remember`.
3. Wrap the drawing in `begin_squash` / `gfx_ui_group_off` so the press scale applies; read its tweens with `ui_anim_get(ui->anim, ui_key(id), UI_SX, 1)`.
4. Fire the tier for what the action means (`ui_fx_tier` or `ui_tier_at`); disabled controls neither move nor sound.
5. Targets are at least `k_ui.target_min` (44 pt), the lip included; `--autotest ui` checks every screen.

### Tests and numbers

`--autotest ui` (`game/autotest_ui.h`): test screen + every real screen; screenshots `ui_*` in `docs/autotest/ui/`. Perf scenario `ui_juice` (menu + lobby panel, a toast and a T3 reward every 2 s); the gate checks UI update (`ui.anim` + `ui.fx`) <= 2 x 0.05 ms, UI submit (`render.ui`) <= 2 x 0.15 ms and at most 3 UI draws on every scenario.

## The stick (UI-1b)

`engine/ui/stick_view.c` draws the movement stick; `engine/ui/touch_controls.c` still owns the input and is pinned by `tests/stick_golden.txt` (ctest feeds a scripted path to `touch_controls_update` for the iPhone, iPad and both dead-zone ends and compares every tick). The view only reads `TouchControls`.

- Geometry from the R = 75 design scaled by R / 75 (R = 60 pt, x1.4 max on iPad): base (white 60 %, 3 pt white border, ink 18 % lip 6 pt down), dashed ring at 3/4 R, centre dot, knob 68 (primary, primary_lip 7 pt, 4 pt highlight). Six shape instances (base and border are one: `stroke < 0` = filled with a border in colour 2); 12 for the 200 ms crossfade after a release.
- States: idle at the rest spot (bottom-left of the real safe rect, 55 %); touched (100 % in 80 ms, pop 0.8 -> 1.05 -> 1 in 260 ms, knob squash 90 ms blending into the stretch); dragging (no smoothing; stretch 1 + 0.12 k along, 1 - 0.08 k across); full speed (ring solid accent + 16 % fill in 200 ms, `UI_SND_TICK` + light haptic, re-armed below 0.70 R, at most every 400 ms); released (input zero on the release tick; the drawn knob springs 1 -> -0.2 -> 0.08 -> -0.02 -> 0 over 420 ms; the stick fades out in place over the spring's last 200 ms while the idle one fades in at the rest spot); hidden (menu, pause, friends, lift panel: 150 ms); autonav (idle, the dot turns accent). Reduced: no pop, squash, stretch or spring; home in 100 ms linear.
- Constants: `k_ui.stick_*`. Tests: ctest (golden), `--autotest stick`, `stickdir`, `layout`; pictures in `docs/autotest/stick/`.

## Haptics (HAPTIC-1)

One table, `engine/ui/haptic_table.h`; one way to play, `ui_haptic(HAPTIC_*)` (`engine/ui/haptics.c`). Only `engine/ui/haptics.c` and `platform/ios/haptics.m` (Core Haptics) touch haptic hardware (`grep haptic_hw_ CHHaptic UIImpactFeedback` finds nothing else).

- Ranks: R1 0.25 whisper (links, rows, tabs), R2 0.40 light, R3 0.60 medium, R4 0.80 strong (a screen's main action, confirmations), R5 1.0 heavy (destructive confirms and celebrations only). Sharpness: soft 0.2 (presses), crisp 0.6 (selections, mechanical), sharp 0.9 (errors, warnings). Composites: SUCCESS, ERROR, WARNING, NOTICE, ECHO (a main action's wobble), CELEBRATE (a continuous fade + sparkles).
- Widgets never name an intensity: each widget kind has a class (`k_haptic_class`: link, tab, secondary, primary, main, destructive, confirm, stepper, swatch, randomise, pill, answer, field) with a touch-down and an action entry, and `ui_press` plays them from its one call site each. A button's class comes from its variant and flags: `UI_F_BREATHE` / `UI_F_MAIN` = main, `UI_F_CONFIRM` = a destructive modal's confirm, `UI_F_STEPPER`, `UI_F_RANDOMISE`, `UI_F_LINK`. Toggles play on / off, sliders grab / step / end, text fields focus, modals open / close (`ui_modal_warning` = WARNING).
- Events call their entry where they happen: notices (by kind), toasts, invites, account linked, friend code copied / request sent, reward tokens (8 rising), completion (CELEBRATE via T3), stick down / full speed, marker tapped / reached, no path, navigation cancelled, interaction panel, lift key / arrival (riders only), hospital selected / arrived, voyage start / end / skip, pause opens / resumes. Streaks and learning answers have entries for when they exist (the UI test screen uses the answers).
- Limiter: one start per 50 ms; a frame's requests keep the strongest, which starts at the end of that frame (`ui_haptics_flush`); slider steps have their own 20-a-second channel. Nothing plays while the app is in the background.
- Vibrații (Settings -> Game): Oprit / Fin (x0.6, nothing at R1 or below) / Complet, pref `haptics` (old true / false -> 2 / 0), a preview on change, hidden without a Taptic Engine (`CHHapticEngine.capabilitiesForHardware.supportsHaptics`; iPad and desktop are silent no-ops).
- iOS: one engine for the app's lifetime, built with every pattern and cached player off the first frame; `CHHapticTimeImmediate` starts; the intensity scale is a per-player dynamic parameter made with the player; reset handler restarts and rebuilds; stop on background, start on foreground.
- Add an entry: an id in `HapticId`, its row in `k_haptics` (same order), a `ui_haptic` call where the event happens (or a class row for a new widget kind). Debug builds: Settings -> Developer -> Haptic lab lists and plays every entry. Tests: `--autotest haptics` (the recorder stands in for hardware: 256 entries of id, intensity, sharpness, frame).

## Tilt (TILT-1)

Visual only: layout, hit areas, input and the gameplay camera never use it.

- Source: Core Motion device-motion gravity (`platform/ios/motion.m`; SDL3's iOS sensors are only raw accelerometer / gyro, so no fused gravity). 60 Hz on its own queue into atomics, only while the app is active and S > 0; no Info.plist key (CMMotionManager needs none). `engine/ui/tilt.c` on the fixed tick (A1: a response to motion, not to the angle held): screen-space mapping per landscape rotation; each axis's angle minus a reference that follows it with a 0.35 s time constant (a tilt is a push that starts decaying at once); 1 deg dead zone, +-16 deg = +-1 (A1 asked 12; 16 gives its own "20 deg in 0.3 s peaks near 0.7"); a 4 Hz / 0.7 spring. Still (gyro rotation rate < 3 deg/s for 150 ms): the target is 0, the reference snaps, and once within 0.25 pt every offset is exactly 0. Ceiling: more than 1 pt off rest for 1.1 s forces 0 and logs once (a sustained turn longer than that hits it too). The reference snaps on activation, a rotation and a screen opening. Forced targets (`--debug tilt`, T + mouse, tests) hold as given. A shake above 6 / s at most every 2 s. Axis signs: `k_ui.tilt_sign_x / _y` (flip one if the hand check finds it inverted). Desktop: hold T and move the mouse; `--debug tilt <tx> <ty>`; perf `--tilt-sine` (1 Hz, full strength).
- Rendering: one uniform (tx, ty, S) + the safe rect for the UI draw; per instance a depth shift, a lip height and flags (`GfxUiInst.tilt`). The vertex shader shifts by `-t x depth`, swings lips by `(-0.7 L tx, -0.4 L ty)` (1.5x for the title) and clamps the whole shift so nothing crosses the safe rect (a rect resting inside may move up to its edge; a full-screen one never moves). The fragment shader moves the highlight band 2 pt against the lip and draws the gloss spot on primary faces (it fades in with the tilt, so tilt 0 is pixel-identical to no effect). 0 extra draws, passes or textures.
- Depths (`k_ui.tilt_depth`): 0 world-anchored (name tags, chat clouds, lobby crowns), 1 = 4 pt the card layer and the default for everything (cards, rows, HUD pills, text on cards), 2 = 7 pt controls (buttons, icon buttons, switches, tabs, answers, the account pill, the stick base; primary faces gloss), 3 = 10 pt badges / counters, toasts, notices, marker edge icons, 4 = 14 pt modals and everything in them, particles, tokens, the banner. The menu backdrop sways (+-1.2 deg yaw, +-0.8 deg pitch) opposite to the UI; HUD at half strength in play; the stick knob only swings its lip.
- Give a new widget a depth: `ui_depth(ui, level, key, flags)` before drawing it, `ui_depth_end(ui)` after (`key` lets a pressed control hold still); a lip drawn by `ui_surface` swings by itself; anything world-anchored: `gfx_ui_depth(g, 0, 0)` around it.
- Shake: every control visible last frame plays the release wobble once, 15 ms per 60 pt from the side that moved first, with the `tilt jiggle` haptic. Settings -> Efect 3D la înclinare (`ui_tilt` 0 / 1 / 2 = S 0 / 0.5 / 1); reduced motion forces 0, Low Power Mode at most 0.5.
- Tests: `--autotest tilt` (on every window preset); screenshots in `docs/autotest/tilt/`.
