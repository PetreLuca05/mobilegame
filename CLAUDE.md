# Mobilegame — engine, hospital, lighting, onboarding (M1-M4), world map (WORLD-1), library menu (LIB-1)

C11 engine for iOS / Android phones and iPad on SDL3 + SDL3 GPU, with a desktop
target for fast iteration. Current focus: desktop + iOS (Android code still
builds in CI but is not verified). **Landscape only**: LandscapeLeft +
LandscapeRight everywhere (SDL hint + Info.plist), never portrait; lay out
for a short, wide screen and respect `app->safe_*` on both sides (notch /
Dynamic Island flips sides with the rotation, home indicator at the bottom) on Settings, Friends and overlays; the in-game HUD (`hud_frame`), the pause button, marker edge icons, the main menu and onboarding respect them on phones (`ui_hud_frame` in `engine/ui/layout.h`: a screen under 600 pt tall gets the real insets + 12 pt, 8 pt at the top) and ignore them on the iPad (12 pt from the screen edge). Multiplayer over Supabase Realtime;
accounts (guest, linkable to Google), profiles and stats over Supabase.

## Layout

```
engine/core      arena allocators, log, fixed-step clock, embedded assets
engine/platform  SDL3 app lifecycle (app_sdl.c), touch/keyboard input, secure token store,
                 native Google sign-in (google_auth*.c; iOS part in platform/ios/google_auth.m)
engine/audio     procedural sound (elevator ding) through an SDL audio stream
engine/gfx       gfx.h interface; gpu.c = SDL3 GPU backend; meshes, camera, materials, planar
                 reflection, one instanced SDF UI draw (UI-1), preview camera
engine/physics   capsule-vs-OBB queries (collide.c) and the kinematic controller (kinematic.c)
engine/net       net_transport.h interface; ws_client.h (platform WebSocket + HTTPS) + Supabase auth/Realtime/PostgREST; replication
engine/ui        SDF text + UI shapes, HUD, touch controls
game/            scene.h (hospital.c default, devroom.c via MG_SCENE=devroom), elevator, player,
                 avatar + skins, camera, settings menu, stats, net session, autotests, main.c
shaders/         HLSL sources; shaders/compiled/{spv,msl} are generated AND committed
assets/          font atlas (.mgf), CA bundle, icon — embedded into the binary at build time
platform/ios     Info.plist template, launch storyboard, asset catalog, google_auth.m
platform/android Gradle project (calls the root CMakeLists.txt, SDL Java from the submodule;
                 SecureStore.java, GoogleAuth.java)
tools/           build/run scripts and asset generators
supabase/        SQL migrations
```

## Build and run

Desktop (Linux: Vulkan; macOS: Metal):
```
tools/run_desktop.sh                     # configure + build + run
cmake -S . -B build/desktop -G Ninja -DMG_PLATFORM=desktop && cmake --build build/desktop
build/desktop/mobilegame --quit-after 10 # auto-exit, handy for scripted runs
MG_WINDOW=ipad build/desktop/mobilegame  # 1180x820 iPad aspect (default 844x390 iPhone)
```
Headless Linux (CI / cloud): `Xvfb :99 &` then `DISPLAY=:99 build/desktop/mobilegame`
(Mesa lavapipe provides Vulkan; `import -window root shot.png` for screenshots).

iOS (macOS + Xcode 15+, device with Developer Mode):
```
MG_IOS_TEAM_ID=<team id> tools/run_ios.sh         # build, install, launch on the connected iPhone
MG_IOS_TEAM_ID=<team id> tools/run_ios.sh --open  # just generate build/ios/mobilegame.xcodeproj and open Xcode
```

Android is not built locally for now (the `android` GitHub workflow still
builds `platform/android`; see git history for the local commands).

Releases: `tools/archive_ios.sh [--upload]` (TestFlight), `tools/build_aab.sh [--apk]`
(Play internal testing; needs `platform/android/keystore.properties`), and the
`android` GitHub workflow publishes `android-latest` on every push to main.
Player-facing install guide: `docs/PLAY_ON_YOUR_PHONE.md`.

Desktop test flags: `--offline`, `--profile NAME`,
`--spawn x,y,z`, `--cam x,y,z,yaw,pitch` (free camera), `--colliders`, `--menu [skin]`,
`--demo-walk`, `--window-pos x,y`, `--quit-after S`, `--log-events`,
`--autotest touch|route|onboarding|save|continue|stickdir|stick|haptics|tilt|nav|autonav|markers|interact|text|hudclamp|offline|worldmap|worldmap2|ui|levelswitch|institute|screenshot NAME`, `--scene hospital|institute|devroom` (every level load),
`--orientation left|right` (iOS: rotates the window without turning the device), `--menu pause`, `--markers N` and `--marker x,z,storey` (not saved), `--log-remote`,
`--screenshot-at S:NAME[,S:NAME...]`, `--skin P,H,A`
(local only, not saved), `--metal-capture [S]` (Apple: one frame at S s, default 6,
to `frame.gputrace` in Documents / the pref path; macOS needs `MTL_CAPTURE_ENABLED=1`).
Args also work on a phone: `xcrun devicectl device process launch --console
--terminate-existing --device iPhone ro.docvex.mobilegame --autotest onboarding`.
Any of the first group skips the launch screen. iOS: `MG_IOS_DEVICE=ipad|iphone|<id>`
picks the device for `tools/run_ios.sh`.

Scene: chosen per level at load (`game/scene.h` `SceneKind`, books.json level `scene`: `hospital_v1`, `institute`;
`game/scene.c` dispatches to `scene_<kind>_*`, declared in `game/scene_impl.h`). `-DMG_SCENE=hospital` (default) is
only the kind of a direct start into play; `devroom` (M1 engine test room) forces it everywhere. Level scenes live in
their own arena (`Game.level`, 64 MB reserved in perm) and above a GPU mesh mark (`gfx_mesh_mark` /
`gfx_mesh_reset_to`): building another kind frees the old one (the world map, UI, avatar, library stay under the mark).
A scene has N lifts (`scene_elevator_count` / `scene_elevator(s, i)`, landing f = floor f, nav car i = lift i); the
panel works the car the player stands in, autonav rides the car its path chose (shortest walk). `--autotest levelswitch`
(hospital -> institute -> dev room -> hospital x 3, identical pools each time); `level_mem` in ctest.
`PLAYER_JUMP_ENABLED` (game/player.h, default 0) brings jumping back.
`PLAYER_SPRINT_ENABLED` (game/player.h, default 0): one speed, walk 4.5 m/s
(full speed from a 3/4 stick push, slower below; Shift does nothing; the
replication sprint bit stays reserved and is always 0).

## Shaders

HLSL in `shaders/*.vert.hlsl` / `*.frag.hlsl` (entry points `VSMain` / `PSMain`).
After editing, run `tools/build_shaders.sh` and commit `shaders/compiled/`.
It uses SDL_shadercross when `shadercross` is on PATH (or `SHADERCROSS=...`),
otherwise glslangValidator + spirv-cross with the same SDL binding layout:
vertex uniforms `register(bN, space1)`, fragment textures/samplers `space2`,
fragment uniforms `space3`; vertex inputs use `[[vk::location(N)]]`.
Uniform/sampler counts per shader are declared in `engine/gfx/material.c`
and `gpu.c` (`load_shader`) — keep them in sync when adding resources.

## Assets

`assets/font_atlas.mgf` is generated by `python3 tools/gen_font_atlas.py`
(Pillow, numpy, scipy, fonttools): 1024x512, ASCII + what Fredoka has of Latin-1/Extended-A. Fredoka has no ă ș ț: the generator composes them (its letters + a drawn breve / its comma); the cedilla forms share the comma glyphs (`text_init`). Text is UTF-8. Icons: `python3 tools/gen_icons.py`.
Everything listed in `MG_EMBED_FILES` (CMakeLists.txt) is compiled into the
binary as C arrays; look assets up with `mg_asset_find("path/from/repo/root")`.

## Supabase

Project ref `kugytvcupelitbaksotg` (eu-central-1). Client config lives in the
git-ignored `config.local.h` (copy `config.local.h.example`): host + publishable
key only, never the service key.
Migrations in `supabase/migrations/`, named by the version recorded on the project (`<timestamp>_<name>.sql`, so `supabase db push` lines up): realtime, world shards, friends (`supabase/tests/friends_test.sql`; it always ends by raising, `ALL FRIENDS SQL TESTS PASSED` is the pass), and
accounts: `profiles` + `stats` with RLS, sign-up trigger,
`delete_anonymous_self()`). Apply with the CLI:
```
supabase link --project-ref kugytvcupelitbaksotg
supabase db push
```
(or paste the file into the SQL editor; the Supabase MCP `apply_migration`
records the same names). Dashboard settings that SQL cannot change:
Authentication → Sign In / Providers → "Allow anonymous sign-ins" ON,
"Allow manual linking" ON, and the Google provider (client ids, nonce check
off-switch stays OFF) — step by step in `docs/GOOGLE_SIGN_IN.md`.
Google client ids go in `config.local.h` (`MG_GOOGLE_WEB_CLIENT_ID`,
`MG_GOOGLE_IOS_CLIENT_ID`); the Web client secret only goes into Supabase.

## Lighting

- Always day: one set of constants in `engine/world/lighting_day.c` (sun
  `#FFFFFF` x 1.0, ambient `#9FB7D9` x 0.8, both through the toon-ramp scales
  0.5 / 0.72; sky `#BFE3FF` -> `#3F8FE8`; white clouds). Sun vector
  (+0.28, +0.819, +0.50), elevation 55 deg: the wall faces the north-looking
  camera sees point south (+Z, dot 0.50 = lit step), north faces and west
  faces are dark, east faces the middle step, so rooms read as boxes. Indoor
  faces step on the same vector (`cel_light_day`). Both are decisions, not
  defaults: the lit faces are the ones the camera sees, and the toon-ramp
  scales stay x 0.5 / x 0.72 (the literal constants blow out). Lamps are emissive meshes at
  a constant 0.3 (`LAMP_DAY_EMISSIVE`), no switching.
- `-DMG_DAYCYCLE=continuous` compiles the old device-clock cycle
  (`daycycle.c`, `assets/daycycle.json`, moon, stars, time rows, `--time`) for
  reference only: not shipped, not maintained.
- Renderer (M5-S): CMake `MG_RENDER=minimal` (default) | `full`.
  minimal = one directional key light (sun/moon from the day cycle) + flat
  ambient through the toon ramp (`shaders/toon_flat.frag.hlsl`,
  floors `floor_flat.frag.hlsl` with procedural 60 cm tiles). No shadow
  maps, no point/spot lights, no planar reflection, no tiers, no fog; the
  only "shadow" is the avatar contact disc. The floor fade slots:
  storeys + roof, tree canopies, wall pieces (`HOSP_FADE_*`). Lamps (lamp posts, ceiling
  panels) are emissive meshes; they light nothing.
  Architecture has no rim light (on big walls the view-angle rim made a
  dark circle that followed the camera); avatars and props keep theirs.
  The log line "gfx: targets WxH: ..." lists every render target (no
  shadow or reflection textures in minimal). HUD: draws + "main X ms" (CPU
  encode; measure GPU time with Instruments, see Tests).
- `MG_RENDER=full` keeps the M3 path compiling for reference (not shipped,
  not tested): light list (`gfx_add_light`, `game/hospital.c`
  `place_lights`), CSM + spot/point atlas shadows (`shadows.c`), planar
  reflection (`reflection.c`), RAM tiers and their Developer rows.

## Camera (M5)

- Default: fixed diagonal top-down `game/camera_td.c` (M5-S) - pitch 55 deg,
  vertical FOV 40 deg, 19 m from a pivot 0.9 m above the current storey's
  floor, yaw `CAM_YAW_DEG` = 0 (north up: the long east-west axis runs
  left-right, the entrance and the gate at the bottom). No rotate; it only follows (spring 8/s + 1.2 m look-ahead) and rides the
  elevator. Stick / WASD: up = screen up = north.
  These are fixed: no Settings sliders; old `td_*` pref keys are ignored and dropped on the next write.
- Touch zones: left 45 % = floating stick (60 pt radius, scaled up on iPad; dead zone pref `stick_dead_zone`, default 30 pt); the rest is UI (and marker taps).
- Stick look (UI-1b, `engine/ui/stick_view.c`, docs/UI.md): drawn only; the input in `touch_controls.c` is pinned by `tests/stick_golden.txt` (ctest). Never change `touch_controls_update` without re-recording on purpose (`mg_tests --record-stick tests/stick_golden.txt`) and saying so. Idle stick in the real safe rect. `--autotest stick`.
- `-DMG_CAMERA=thirdperson` builds the M1-M4 orbit camera (`camera_tp.c`,
  wall sphere-cast, right-half camera drag) for engine tests; not shipped.
- `--autotest stickdir --spawn -12,0.1,10`: stick up/right/down/left walk
  north/east/south/west and the camera never turns.

- Storeys (`game/hospital.h`): every mesh group has a storey id
  (`hospital_group_storey`): -1 yard (always drawn), 0..2, 3 roof. A slab,
  ceiling panel or car ceiling is tagged with the storey whose *floor* it is
  (the ceiling of f is f + 1), so the view floor draws without a ceiling.
  New meshes must go into a tagged group (Developer -> Storey colours /
  `--storeys`: yard grey, G green, 1 blue, 2 orange, roof magenta).
  Slabs from storey 1 up have the stairwell open
  visually but collide as one box. Rooms have floor-painted labels.

- Floor fade (`engine/gfx/floorfade.c`, `GfxMaterial.hide`): screen-door
  dither, ordered 4x4 Bayer `dither_clip()` in `common.hlsli` (toon, floor,
  text, unlit, ghost), driven by `DrawFS.u_draw_misc.x`; draws with hide >
  0.5 skip the shadow pass (full). Scene fade slots (`HOSP_FADE_*`): storeys
  0-2 + roof, tree canopies over the player, wall pieces; 350 ms per change,
  snapped on spawn/teleport/load. Inside the building: storeys above the
  view floor and the roof go; outside: everything is drawn (interiors only
  while the roof is not opaque). Remote avatars above the view floor are not drawn.

- Elevator ride (`game/elevator.c`): doors 0.4 s, car travels 0.6 s per
  storey (ease in-out, `car_y`), ding, doors 0.4 s = 1.4 s / 2.0 s. The
  rider is moved with `car_y` (no physics through the slabs) and the camera
  pivot follows, so the view floor and the fades switch as it passes each
  slab. No fade to black; Developer -> Teleport stays instant.

- Occlusion (M5-S): with north up, east-west walls stand between camera
  and player. Interior east-west walls and the south facade are built as
  wall pieces (`HospSeg`, cut halfway between door/window centres, full
  height, colliders unchanged); only the piece right in front of the
  player (nearest east-west wall south of them, overlapping +-1 m) fades,
  as a whole, to 0.2 over 350 ms - walking into a room fades that room's
  front wall, nothing else. No half-height walls. `--autotest route` logs
  "occlusion: player hidden behind walls in N %" (11 %: elevator front
  wall, door frames - the x-ray covers those).
- X-ray silhouettes (M5-X, `shaders/xray*.hlsl`, `GFX_MAT_XRAY_RIM` /
  `GFX_MAT_XRAY`, `mat_xray`): the parts of an avatar hidden behind
  geometry show as the real model in solid black,
  dithered 60 %, with a 2 px pure-black rim (mesh pushed out along
  its normals, drawn first). Depth GREATER, no depth write. Draw order
  (`draw_bucket`): solid world -> x-ray -> avatars (`GFX_DRAW_DYNAMIC`) ->
  dithered geometry (`hide` > 0). Keep this order. The x-ray must run before
  the avatars: it then tests only the world's depth and the visible avatar
  paints over it; drawn after them, an avatar's own parts count as "in
  front" (head under hair) and x-ray through each other. Dithered geometry
  goes last so a faded wall's remaining pixels don't trigger the x-ray or
  hide the player. Eligible: the
  local player and remote players on the view floor. Only meshes drawn with
  `avatar_draw_with(..., mat_xray(...))` are x-rayed: to keep a mesh out,
  just don't draw it with that material. Settings -> "Show players through
  walls"; Developer -> "X-ray always". Toon outlines are pure black.
- Name tags: 12 pt, white 0.6, black outline, 0.5 m over the head, only for
  avatars on the view floor; own tag off by default ("Show my name tag").
- Prefs keys: `perf_hud` (default off; Pause -> Performance, Settings -> "Show performance stats", Developer -> "Performance stats"; the top-left text under the account chip, saved at once), `show_own_name`
  (the M5 camera keys are ignored and dropped on the next write).

- Save slot v2: `yaw` is the camera yaw; v1 saves still load (player facing).

## Navigation (M6)

- `engine/nav/navgrid.c` bakes 0.25 m walkability grids from the static
  colliders at scene load (each box rasterises into the cells it covers;
  elevator doors count as open): grid 0 = ground level (yard + storey G,
  one connected area, `NAV_OUTDOOR` outside the footprint), one grid per upper
  storey. Flags: `NAV_WALKABLE`, `NAV_DOOR`, `NAV_ELEVATOR`, `NAV_OUTDOOR`,
  `NAV_NEAR_WALL`. `engine/nav/astar.c`: 8 neighbours, no corner cutting,
  octile heuristic, +0.5 near walls, then string-pulled by grid line of sight.
  `nav_find_path(n, from, from_storey, to, to_storey, &path)`: points tagged
  with storey (-1 yard), `ride_at` / `ride_from` / `ride_to` for the car;
  `NAV_UNREACHABLE` if nothing walkable within 1 m or not connected.
  Doors are 1.2 m wide = 2 walkable cells after the capsule inflation.
  The game does not call that directly: `nav_request` + `nav_service(n, 1 ms)`
  (once per frame, one search at a time, all requests share the budget) +
  `nav_poll` (READY / UNREACHABLE release the request; PENDING can hand over a
  partial path with the first string-pulled waypoint, which is what the
  player starts walking on). Last 16 room-to-room paths cached (rooms =
  areas bounded by door cells). `nav_can_reach` answers O(1) from region labels.
- Debug: Developer -> Nav grid / desktop `--nav` (red blocked, green free,
  olive near walls, yellow narrow, blue car; yellow line = last path). Desktop
  right-click (no drag) on the floor. `--autotest nav` checks every room
  from the gate, rides, the blocked stairwell, and logs bake and path ms.

- `game/autonav.c`: walks a path by feeding a synthetic stick into the normal
  input (controller, replication, animation and stats are unaware). Any real
  stick / WASD cancels at once; Settings cancels; room change / load clears.
  Arrival 0.6 m, stuck 1.5 s -> one re-path, then FAILED. Cross-storey: walks to
  the car centre, waits 0.3 s (a stick push still cancels and the floor menu
  appears), rides with `elevator_request`. Desktop right-click = walk there.
  `--autotest autonav`.

- Markers (`game/markers.c`): `marker_add(ms, pos, storey, kind, rgba)`,
  `marker_remove`, `marker_clear(kind)`, callbacks `on_tapped` / `on_reached`;
  32 max. World: spinning diamond, ground ring (0.6 coverage via dither),
  contact disc, storey-tagged like the scene (above the view floor: gone).
  HUD (`engine/ui/marker_hud.c`): on-screen on the view floor = invisible 44 pt
  tap target only; off-screen / in a reserved panel (gear, floor
  menu, toast) = edge icon with chevron + distance (4 Hz), max 12, 48 pt apart;
  other storey = icon + floor badge.
- Touch arbitration (`touch_controls.c`): targets are tested before the zones;
  the touch is the marker's only if it ends within 250 ms having moved < 10 pt,
  else it goes back to its zone on that event (a stick spawns at the original
  point, which then cancels navigation).
- Feedback: scale punch, ripple, flash, shake, procedural `audio_cue` (tick,
  chime, dup, buzz), `haptic_impact` on iOS. Tapping the active marker cancels;
  tapping another re-targets. Since M8, walking to a marker only stops the walk: it fires (`on_reached`) when the player taps the interact panel.
- Debug: Developer -> Markers (Test markers, count 1-16, show paths for all,
  respawn) - on in debug builds. Test markers: 50 % rooms, 25 % corridors,
  25 % yard, replaced 0.5 s after being reached. Prefs: `haptics`,
  `ui_sounds` (was `marker_sounds`), `test_markers`, `test_marker_count`. `--autotest markers`.

- In-game menu: pause button (top right in play, 54 pt accent icon button), account chip (avatar + name; top left in play, top right on the main menu, where tapping it opens Friends) / Back = centred pause card, max 420 pt (Resume, Settings,
  Save & back to menu, Disconnect in a room); Settings' back arrow returns to
  it. Arrows are drawn with `ui_line` (the font has no left arrow).
- `--autotest offline`: an unresolvable host behaves like airplane mode; count
  `net: attempt` lines after `frontend: PLAY`. Do not mix `--screenshot-at` into
  tap-timed scripts: a capture stalls one frame, past the 250 ms tap window.

## Front end, accounts and persistence (M4)

- `game/frontend.c`: WELCOME -> USERNAME -> CREATOR -> MAIN_MENU (first
  launch; later boots go straight to the menu), SETTINGS,
  LOADING, PLAY. Welcome, Username, Creator, Main menu, Settings and Friends draw the library (LIB-1, below)
  behind the menu (`menu_world` in `game/game.c`); controls in the left column; the Creator opened from the game
  keeps the locker room. The hospital is no longer built for the menu: a game builds it while loading. Back = Android
  back / Escape (exits from the welcome screen). Desktop: `--frontend
  welcome|username|creator|menu|settings`, `--play new|continue`.
- Text lives in `assets/strings.json` (`str("key")`), never in C.
- Sign-in: Google (both platforms) and Sign in with Apple (iOS,
  `platform/ios/apple_auth.m`), both linking the guest via the id_token
  grant. Apple needs a **paid** Apple Developer Program team (personal teams
  cannot have the capability): enable "Sign In with Apple" for the App ID
  `ro.docvex.mobilegame` (developer.apple.com -> Identifiers, or Xcode
  Signing & Capabilities), then build with `MG_IOS_APPLE_SIGNIN=ON
  tools/run_ios.sh`. Supabase: Authentication -> Sign In / Providers ->
  Apple -> enable, Client IDs = `ro.docvex.mobilegame` (native flow: no
  Services ID or secret) -> Save.
- Per account (Supabase): display name, `profiles.skin` v2
  `{body, skin, hair, hair_color, suit, accessory, palette}` (migration
  0004; missing keys = 0), stats. Per device: `prefs.json` and the save slot
  in `SDL_GetPrefPath("docvex","mobilegame")` (desktop: `<profile>-` prefix).
- `prefs.json` keys: `settings` {yaw_sens, pitch_sens, invert_y,
  cam_distance, cam_pitch, fov, dead_zone, render_scale, tier, shadows,
  sound}, `onboarding_done`, `auth_provider` (google/apple/guest),
  `lobby_open`, `profile_cache` {name, skin, stale},
  `stats_queue` {sessions, play_seconds, distance_m, elevator_rides,
  rooms_joined} (offline play, added on the next load). Written atomically.
- Offline play sets `net_set_quiet`: after a failed sign-in (airplane mode)
  the network thread makes no new attempts until the player leaves play.
  Sign-out keeps settings + save, clears the cache and `onboarding_done`.
- `save_slot0.bin` (offline New Game / Continue): "MGS1", version u32,
  pos 3 x f32, yaw f32, floor u32, play_seconds u32, elevator_rides u32,
  visited_rooms u64 (bit = storey * 16 + chunk), npc_seed, npc_tick, (v5) book_id, level_id char[16], (v6) rides, (v7) scene_id char[16]
  + visited_rooms_hi u64 (storeys 4..7); (v8) same layout, written after the book content was removed (the slot never held mission or book progress); a Continue whose save is in another scene than its level's now starts at the entrance; every 30 s, on background,
  exit and "Save & back to menu". Offline play joins no Realtime room.

## Friends and lobbies (FRIENDS-1)

Every game is a lobby owned by its player; only friends get in, after the host says yes. No modes, no room codes.

- Tables: `profiles.friend_code` (6 chars, trigger), `friendships` (select own), `player_status` and `lobby_members` and `lobby_join_attempts` (RPCs only: the token never leaves through a select). `profiles` has column grants: clients write only name + skin. Everything goes through `security definer` RPCs: `my_friend_code`, `friend_request_by_code`, `friend_search`, `friend_respond`, `friend_remove` (also cancels), `friends_list`, `status_heartbeat`, `status_clear`, `lobby_join_request`, `lobby_join_respond`, `lobby_kick`, `lobby_leave`. Errors are `raise exception '<code>'`, mapped by `friend_err_<code>` / `join_err_<code>` strings.
- Topics: `user:<uid>` (receive only, whole app run: `friend_request`, `friend_accepted`, `join_request`, `join_accepted`, `join_declined`, `kicked`) and `room:<token>` (Presence + state), one socket. Host-announced member list on the room (broadcast `m`, empty = host closed); state from anyone off it is dropped.
- A token lives for a session; `status_clear` shorter than 2 min (backgrounding) keeps it. Older than 90 s = offline. `start_tick` is the wall-clock ms of NPC tick 0.
- `game/lobby.c`: `OFFLINE -> SOLO -> HOSTING`, `JOINING -> GUEST`; `lobby_start_host`, `lobby_start_guest`, `lobby_stop`, `lobby_set_open`, `lobby_ask_join`, `lobby_cancel_join`, `lobby_respond`, `lobby_kick`. `LOBBY_JOIN_APPROVAL` in `game/lobby.h`. A join's 15 s run from when its panel shows; join requests go before friend notices in the queue.
- `game/friends.c`: `friends_page`, `friends_refresh`, `friends_request_code`, `friends_search`, `friends_respond`, `friends_remove`; `friends_cache.bin` is written by the network thread. `game/friends_page.c` is the page. `engine/ui/notice_panel.c` the top-centre panel (its rect is reserved from marker icons).
- `engine/ui/head_icons.c` (A1): 64-cell atlas of account heads, keyed by skin hash, max 2 rendered per frame through one offscreen pass; 0 extra draws because `ui.frag` samples it as a second texture when u >= 2.
- Prefs: `lobby_open` (default on). Strings: `assets/strings_ro.json` first, then `strings.json` as the fallback.
- `tools/friends_test.sh OUT` runs `--autotest friends` on desktop: roles a1, a2, a3 (restarts), b, c, offline, solo (0 room packets in 60 s), a4 (prefs and cache wiped, same account), plus a fourth account; c adds by name search, b and a3 cover friend-request decline and cancel; files in `$MG_FT_SYNC` keep them in step. SQL: `supabase/tests/friends_test.sql`.

## Menu lobby (FRIENDS-2)

The main menu is the lobby. `player_status.state`: 0 none, 1 menu lobby, 2 in game. `lobby.c` adds `MENU_SOLO`, `MENU_HOSTING`, `MENU_GUEST` on the same `room:<token>`: Presence only in the menu (`session_set_menu`), no state packets. Only a real game publishes its NPC tick 0 (the menu's backdrop must not).

- Room events: `m` (members, host first, the order of the list), `c` = `net_send_room_cmd` (`NET_ROOM_START {map, seed, tick, save_hash}`, `NET_ROOM_TO_MENU`). The host's New Game / Continue takes everyone in; "Save & back to menu" brings everyone back (`lobby_return_to_menu`). Quitting, signing out or losing the connection still ends the session. A guest's "Ieși" = `lobby_stop`, back to their own solo lobby.
- Invites: `lobby_invites` (60 s, RLS, no policies; named in `merge_ticket_redeem`). `lobby_invite(guest)` returns `ok` / `full` / `busy` / `offline` / `rate_limited` (one per guest per 10 s) and only reaches a friend alone in their own menu. `invite_respond(host, accept)`; accepting is the invited path of `lobby_join_request`, which skips the lock and the host's panel, deletes the invite and pushes `member_joined`. `invite_declined` tells the host. The invitee's panel (`NOTICE_INVITE`) declines itself after 30 s. The host's button says "Trimisă" until it resolves or expires (`lobby_invite_sent`).
- `lobby_return_to_menu()` host only. `friends_list` gains `in_menu`; `open` / `locked` / `full` / `in_other_lobby` cover states 1 and 2.
- Menu (`frontend.c` `lobby_panel`, `game.c` `menu_layout`): header + rows under the account chip (40 pt heads, procedural crown and x in the UI batch), host-only switch, Invită and kick. Every player sits at the library's table (LIB-1: seat 0 me, 1-3 the others in list order), name chips over the others; the camera backs off until the table and its seats fit between the menu column and the panel. Guests get a disabled "Gazda pornește jocul…" row. Offline: hero alone, chip says Offline.
- Friends page: Join when their lobby is open, Invite when they are online and not in your lobby, side by side. `invite_mode` (from the lobby's Invită): invitable friends first, reasons for the rest. Adaugă: your friend code with copy, one search bar (a typed code gets its own + row). Block and report are tagged "Curând".
- Tests: `tools/friends2_test.sh OUT` (roles ma, mb, mc, md, four instances), `tools/friends2_two_device.sh OUT` (d2h on the iPad, d2g on the Mac), `--autotest lobbylayout` (full lobby: iPhone, iphone-flip, iPad), `--autotest perf menu` (a demo 4-player lobby without the network).

## Account linking (LINK-1)

**Merge coverage rule, for every later migration:** a table with a foreign key to `auth.users` or `profiles` must be named (`public.<table>`) in `merge_ticket_redeem` with an explicit rule. `supabase/tests/link_test.sql` fails otherwise.

Flow (`game/account_link.c`, stages in `engine/net/supabase_rt.c`, sessions in `supabase_auth.c`): the provider token, then a `link_identity` grant with the guest JWT. Success = Case A, same user. `identity_already_exists` = Case B: dialog, then ticket (as the guest), `status_clear`, sign-in (held in memory, not persisted), redeem. Any failure before the redeem restores the guest. If the connection dies during the redeem, `merge_ticket_used` settles whether it committed. The merge clears `profile_cache`, `friends_cache.bin` and the head atlas; settings and the save slot stay. Menu only; the pause card saves and returns to the menu first. `tools/link_test.sh OUT CLI` runs `--autotest friends l1..l5` from a `-DMG_TEST_AUTH=ON` desktop build (`build/desktop_link`), where email + password stand in for the ID token.

Request shapes and the audit: `docs/ACCOUNT_LINKING.md`. RPCs: `merge_ticket_create` (guest), `merge_ticket_redeem` (existing account), `merge_ticket_used` (did it commit), `link_cleanup` (nightly, `pg_cron`).

## World map (WORLD-1)

Books are islands; a book's levels are hospitals on it (book 1 Kumar, 9 levels, the first Gastro). Nothing is locked.

- Assets (`assets/world/`): `books.json` (books, their island files, levels `{id, name, map_id, scene}`; `map_id: null` = "Coming soon"; `scene` is what Join loads, `hospital_v1` for Gastro as a stand-in for the institute map (MAP-2 changes only that field; while `scene` != `map_id` the card shows "Hartă provizorie")), `<book>.island.json` (format `mg-island 2`: levels with stop sample and hospital, road polyline, prop instance lists, ambulance, camera) and `.mgmesh` text meshes (`c` palette, `g` group, `t` triangle, `i` instance; y-up, CCW outward).
- `tools/meshgen.py` (build time, CMake): mgmesh -> `.mgmb` (one vertex-coloured mesh per group, flat normals, the `i` lines), embedded as `assets/world/<name>.mgmb`; a malformed line fails the build. `game/world_data.c` loads all three.
- `game/worldmap.c` = front-end screen `FE_MAP` (the hospital is neither ticked nor drawn while it is up; the map's meshes stay uploaded, ~1 MB, the renderer has no mesh free). States BOOKS / OVERVIEW / TRAVEL / ARRIVED; camera from the JSON (fixed yaw, north up, exponential ease 3.2/s; overview fitted to the safe rect); the ambulance drives the road (distance / speed, clamped, ease in-out; a new target re-routes), wheels, bob, lean, roof lights. Draws: sea, island, road, one per prop group, hospitals (instanced cubes behind `world_hospital_mesh[level]`), rings, body, wheels, lights, disc = 16; +1 x-ray of the body when a cube hides the parked ambulance (5 of 9 stops are north of their hospital). Props: static transforms sorted into an 8 x 5 grid, cells culled against the frustum. Labels, top bar, card in the HUD batch; label taps hit the badge only.
- `game/world_progress.c`: `world_level_completion(wp, book, level)` -> `{done, total}` (0/0 until content packs exist; test builds, `MG_PROF`, take `--debug level-progress <level> <percent>` and the pause card's "+25 %"). Island = mean of the hospitals' percentages, "—" while every one is 0/0. Per book `{last_level, ambulance_at, updated_ms}` in `prefs.json` `world`; `book_state` on Supabase (owner-only RLS, `book_state_set(p_book, p_level, p_at_ms)`, the newer choice wins, merged in `merge_ticket_redeem`), read with the profile, written on Join / Continue.
- Menu: New Game (overwrite confirm, clean slot, ambulance on the start pad, island in BOOKS; a tap drives to the last played hospital) / Maps (`menurow4`, tag "Kumar 23%": ARRIVED on the last played one) / Locker / Settings, big Continue (straight into the hospital: level, floor, position; the room's first spawn no longer moves a Continue). Pause card: "Map" (`p_map`, saves, back to the menu, the island). Join: same level as the save -> Continue, else the level from its entrance (save totals carry on); in a lobby `map_id` = the level index. Save v5 adds `book_id`, `level_id` (older saves load as kumar / gastro).
- Desktop: `--frontend books|map`. Tests: `--autotest worldmap` (steps in `game/autotest_world.h`; screenshots in `docs/autotest/worldmap/`); `save`, `offline` and the friends scripts drive the island after New Game. SQL: `supabase/tests/world_test.sql` (`ALL WORLD SQL TESTS PASSED`). Perf scenarios `worldmap` (overview), `worldmap_travel` (driving round the island), `island` (parked, card up).

## Islands and voyages (WORLD-2)

- `books.json` lists every book (Kumar, Lawrance) plus the shared `ambulance` and `vehicles` meshes. Each `<book>.island.json` has `id`, `book_number`, `world_pos`; its layout is local to that position. Each island has its own palette in its mesh, its own props, ambulance (shared mesh), last hospital, parked spot and completion.
- `meshgen` v2: `i <group> x y z axis <x|y|z>` = a spinning part and its axis (instances carry a kind: point, box, spinner). `vehicles.mgmesh`: plane, helicopter, boat, submarine bodies and their spinners.
- `game/worldmap.c`: `WorldMap.isles[]`, `cur`, `wm_cur()`. An island is drawn only when its bounds are in the frustum. BOOKS has the previous / next island's button (`wm_isl_prev`, `wm_isl_next`, arrow keys; a tap on the other island works too); the top bar's `wm_islands` returns to BOOKS. `VOYAGE`: no input; a cubic Bezier between coast points (the farthest terrain vertex above y 0.5 within 10 deg of the other island, x 0.965 air / 1.2 sea), bowed 70 / 110 to a random side; per-vehicle progress, height and attitude profiles, spinners, a wake behind the boat, the submarine under the opaque sea with a periscope wake and a surface shadow; camera A overview -> follow -> B overview at ease 2.2/s. Ends in BOOKS on the destination.
- `world_next_ride` (`game/world_progress.c`): a bag of the four in a random order on the game's xorshift stream (`npc_rand`), redrawn if it starts with the last trip's vehicle or is a rotation of either of the two previous bags. Stored in `prefs.json` `rides` (and a copy in save v6). `--debug ride <plane|helicopter|boat|submarine>` forces the next one. `prefs.json` `last_book`: Maps opens that island. In a lobby `map_id` = book * 64 + level.
- Tests: `tools/worldmap2_test.sh` (`--autotest worldmap2`, then `--autotest worldmap2 relaunch`; pictures `worldmap2_<vehicle>`), perf scenario `worldmap_voyage` (by plane back and forth).

## UI look (UI-1 "Jelly Pop")

Details, motion table, tiers and how to add a widget: [docs/UI.md](docs/UI.md).

- One token table `engine/ui/ui_style.h` (`k_ui`): no colour, radius, duration or curve literal in screens. Components in `engine/ui/ui_widgets.c` (buttons by variant, icon button, switch, tabs, counter, progress, row, answer, card, modal), motion in `ui_anim.c` (128 tweens), effects / sounds / haptics / tiers in `ui_fx.c` (64 particles, 4 rings, 8 tokens). Fonts: Baloo 2 + Nunito (`TEXT_FACE_DISPLAY` / `BODY` / `BOLD`, one MGF2 atlas).
- The whole UI is one instanced SDF draw in the existing pass; no blur pass exists any more (modals use `scrim`). The lift panel and the preview card stay 3D insets.
- Input: `ui_press` = press motion + T0 + the action on the touch-up frame; targets >= 44 pt; `ui_remember` for rects that are not targets. Screens enter (layer slides in, rows stagger); toasts and notices also exit.
- Settings -> Animații interfață (`ui_motion` 0 full / 1 reduced; iOS Reduce Motion forces 1); `ui_sounds` (was `marker_sounds`, migrated), `haptics`.
- `--autotest ui` (all windows incl. `MG_WINDOW=iphone|iphone-flip|ipad`), perf scenario `ui_juice`; the perf gate checks `ui.anim` + `ui.fx` and `render.ui` against the UI-1 estimates and at most 3 UI draws.

## Tilt (TILT-1)

- Visual only. Core Motion gravity (`platform/ios/motion.m`) -> `engine/ui/tilt.c` on the fixed tick -> one UI uniform; depths and lips per instance, shifts in the shaders, clamped to the safe rect. Default depth = the card layer (4 pt); widgets set theirs with `ui_depth` / `ui_depth_end`; world-anchored UI at 0. Pref `ui_tilt` (Efect 3D la înclinare). `--debug tilt tx ty`, T + mouse on desktop, `--tilt-sine` for perf, `--autotest tilt`. A1: tilt reacts to motion and returns to rest (0.35 s leaky reference, +-16 deg, 4 Hz / 0.7 spring, exact 0 when still, 1.5 s ceiling (counted only while still), 4 s backstop). Details: docs/UI.md.

## Haptics (HAPTIC-1)

- One table `engine/ui/haptic_table.h`, one call `ui_haptic(HAPTIC_*)`; never touch haptic hardware anywhere else (only `engine/ui/haptics.c` and `platform/ios/haptics.m`, Core Haptics). Widgets play through their class (`k_haptic_class`) from `ui_press`; new events get a table row and a `ui_haptic` call. Limiter one start per 50 ms (`ui_haptics_flush` at the end of the frame). Vibrații pref `haptics` 0 / 1 / 2 (Oprit / Fin / Complet). Debug: Developer -> Haptic lab. `--autotest haptics`. Details: docs/UI.md.

## Networking

- `engine/net/net_transport.h` is the only API the game uses; `supabase_rt.c`
  implements it on a network thread (fixed-size command/event queues, no
  allocation on the caller side).
- `ws_client.h` (NET-1): the platform's own network stack. Apple (iOS and macOS): `platform/apple/net_apple.m`,
  NSURLSessionWebSocketTask for Realtime and NSURLSession data tasks for PostgREST / Auth; TLS, certificates,
  proxies and DNS (NAT64) from the OS; no third-party code, no CA bundle. The session's callbacks only queue
  events; `net_io_poll` delivers them on the network thread, so the protocol code stays single-threaded.
  Linux desktop and Android: `engine/net/net_stub.c` (every connection fails: offline) until they get a
  native transport. `mg_nettest probe` checks HTTPS + WebSocket + heartbeat.
- Auth: refresh token in Keychain / Android Keystore (`SecureStore.java`) /
  desktop pref file per `--profile`.
- Topics: `user:<uid>` (private, receive only, whole app run) and `room:<token>` (a lobby, max 4), one socket. RLS in `supabase/migrations`.
- Accounts: the network thread signs in as a guest at launch (no room
  needed), loads `profiles` + `stats` in one PostgREST GET
  (`supabase_rest.c`), writes name/skin through at once and stats totals
  every 60 s / on background / on exit (`game/stats.c`). Google linking:
  id_token grant with the guest JWT (`supabase_auth.c`).
- Presence payload `{name, skin:{palette,hair,accessory}, slot}`.
- Replication (`engine/net/replication.c`): 34-byte state (+`floor`), 10 Hz (2 players)
  / 8 Hz (3-4), 1 Hz keepalive when still, 100 ms interpolation, 200 ms
  extrapolation, correction smoothing. Peers silent for 3.5 s are dropped;
  leaving clients send a final `REP_LEAVING` packet.

Diagnostics (desktop):
```
build/desktop/mg_nettest probe                       # HTTPS + WebSocket + RTT, no sign-in
build/desktop/mg_nettest join ABCD alice 20 --public # public test channel, no sign-in
build/desktop/mg_nettest friends PROFILE SEC [CODE]  # user:<uid> channel, list, pushes; accepts requests, with CODE sends one
```

## Tests

`ctest --test-dir build/desktop` runs the controller and replication checks.
`build/desktop/mobilegame --autotest touch` drives multi-touch input end to end;
`--autotest route` walks the hospital and rides the elevator to every storey
(fails on snags, a misplaced floor menu or a ride that is not 0.8 s + 0.6 s per storey).
Both need a display (Xvfb on headless Linux).
`build/desktop/mg_nettest profile PROFILE [NAME P H A]` reads/writes an account.
Menu scripts tap real widgets by id (`ui_find`), on desktop or device:
`--autotest onboarding` (without `--profile` it uses its own `autotest-onboarding` profile, wiped first incl. the guest token: Welcome -> guest -> Username -> Creator,
a tile per tab + Randomise -> Done -> Main menu), `--autotest save` (New
Game, route walk + ride to floor 2, background + hard exit) then
`--autotest continue` (back on floor 2). Logs carry `frontend: <SCREEN>`,
`session: ... joined/left` and every connection as `net: attempt ...`.

Screenshots (works on phones): `--autotest screenshot NAME` (3 s settle, then
quit), `--screenshot-at S:NAME,...` in any run, or `AutotestOut.screenshot`
inside a script. The frame (3D + UI, at the render size) goes through the
game's own scene texture - the swapchain's storage orientation differs
between iOS devices - to `NAME.png` in Documents on iOS, the pref path on
desktop. Pull with `xcrun devicectl device copy from --device iPhone
--domain-type appDataContainer --domain-identifier ro.docvex.mobilegame
--source Documents/NAME.png --destination out.png`. Party codes are 4 letters
(a digit is refused by RLS).

GPU measurement rule: iOS clocks the GPU down when frames are light, so
raw ms is meaningless on its own. Per scene and device report GPU frame ms,
GPU-busy % and effective ms = ms x busy %, compare effective ms with the
budget and name the dominant clock state (`gpu-performance-state-intervals`).
GPU time on a phone: `--metal-capture` writes a .gputrace, but `devicectl
device copy from` fails on its symlinks; use Instruments instead:
`xcrun xctrace record --template 'Metal System Trace' --device <UDID from
xctrace list devices> --time-limit 10s --output x.trace --launch --
ro.docvex.mobilegame --offline --time 19`, then export the
`metal-gpu-intervals` table. Check `gpu-performance-state-intervals`: a GPU at
"Minimum" clock makes ms look high.

## UI layout (M8)

- `engine/ui/layout.h`: `ui_frame_t`, the screen and its real safe-area insets (SDL, then UIKit on iOS if SDL has nothing), `ui_pause_rect`. No fixed insets: anchor to the safe rect with an 8-16 pt margin. The 3D view is always full-screen.
- Desktop presets: `MG_WINDOW=iphone` / `iphone-flip` simulate the island and home indicator in each landscape direction. Developer -> Show safe rect. `--autotest layout`.

## Interaction (M8)

- `game/interact.h`: `ia_add` / `ia_remove` / `ia_set_pos` / `ia_set_flags` (NPCs, markers). A 10 Hz scan through the spatial hash picks one active target (enter 2.0 m, exit 2.5 m, in front of the player unless `IA_FACING_FREE`).
- NPCs that want to talk (`wants_chat`, re-evaluated at 2 Hz: receptionist, doctor at a desk, nurse at the station, anyone seated): the head lifts and a "..." cloud shows; up to 6 faint clouds within 4 m (`chat_bubbles`: off / active / all). Tap "Talk": a greeting card for 2.5 s (`assets/npc_names.json` greetings), `on_interact`, a nod. All of it is drawn, never simulated.
- The scan prefers a usable target over a greyed one standing closer (a queue must not hide the receptionist), then the nearest. Desktop: the mouse over something shows it too (`ia_near_point`), E or a click uses it.
- `preview_desc_t` (per interactable: `mesh` or `look`, `scale`, `yaw_offset`, `y_offset`, `color`) is what the card draws. Debug: Developer -> Show ranges, Show active id, Force elevator panel, Preview any mesh, Show safe rect.
- `--autotest interact`: a marker and the receptionist (panel up, drag does nothing, a tap fires exactly once, walking onto the marker does not), the lift's 3D buttons to floor 2 and back, an auto-ride with no panel, desktop hover, Pause -> Performance; the scan is compared per tick with brute force (panel up/down within 2 scans = 12 ticks). Screenshots in `docs/autotest/interact/`. Perf: scenarios `lift_panel` and `interact_npc` (the gate fails if the panel was up in under 90 % of the frames), `--no-panels` for the A/B.
- Priority: an archetype with `interact_priority` in `assets/npcs.json` (true, or a list of slot tags it applies at) comes before nearer NPCs without it, inside the same range (`IA_PRIORITY`; receptionist, nurse at the station, doctor at a desk). Only among NPCs.
- `--autotest text`: a line with the ten Romanian letters, screenshot `text_romanian`. `--autotest hudclamp`: 16 markers with every HUD panel forced up, walking; no icon may touch a panel (edge icons take the nearest free spot along the edge, hidden if there is none).
- `game/elevator_panel.c`: while standing in the car, a floating 3D rectangle anchored to the bottom right of the safe area, turned inward about the vertical only so the button face looks at the middle of the screen and its upright edges stay parallel to the screen edge (its area is 71 % of the safe height, at most 276 pt, an inset drawn before the world), with the column of buttons on its front face and each label a text mesh printed on its cap (sinks and turns with it); the column comes from the lift's floor count. A tap sinks and lights the button and dings, the ride starts 0.4 s later. Desktop: keys 0-9. Developer -> Force elevator panel. Auto-ride shows no panel; cancelling in the car does. Everything you tap to interact lives on the right (the preview, the lift rectangle, the greeting card beside the preview); the left is the stick.
- NPCs in the preview are only a floating head (`AvatarPose.head_only`), no card or chip, with the "..." cloud beside it if they want to talk; a tap squashes and nods the head. Non-talkers are faded and do nothing.
- `game/preview_panel.c`: the bottom-right card. `gfx_inset_begin` / `gfx_inset_end` draw a small 3D picture inside the main pass with its own viewport, camera and depth range (no render target). Tap-only through `TouchControls.targets`. Desktop: E.

## NPCs (M7)

- `game/rooms.h`: room graph (rooms, door nodes, slots with tags and an occupant, lift cells). The hospital fills it in
  `hospital_rooms_build` (`game/hospital.c`); NPC code only uses this API. `rooms_validate` snaps slots to the nav grid at
  load and logs any on a blocked cell as an error. A slot can have a separate seat point (chair, bed) from the point an NPC
  walks to. Developer -> Show slots.
- `assets/npcs.json`: archetypes and roster coefficients; `assets/npc_names.json`: names. Rows with `"enabled": false` are
  reserved data (night shift) and ignored; there is no time-of-day logic.
- Sim (`game/npc_sim.c`): `npc_tick` on the fixed tick; steering runs on the job system, reading last tick and writing this
  tick; paths (`NAV_CLASS_NPC`, at most 2 requests and 2 finished per tick) and packages (`game/npc_pkg.c`) run on the main
  thread in index order. Packages compile to step lists (goto, post, sit, bed, queue, wander, escort, leave). Rates: every
  tick near the player on the view floor, 1/3 far, 1/6 on other storeys (path following only).
- Hooks: `npc_spawn`, `npc_despawn`, `npc_set_package`, `npc_find`, `on_arrived`, `on_package_changed`.
- Roster: computed from the room graph in `compute_targets` (47 today), steady-state at start, refilled as people leave.
  NPCs are not replicated: the seed is the room topic's hash online and `npc_seed` in the save offline (v4, with `npc_tick`).
- The sim depends only on seed and tick (see `docs/M7.md`): never read the player or the frame rate in it. Reactions to the
  local player and idle touches belong in `draw_npcs`. Late joiners and Continue replay from the seed (`npc_catch_up`, 1 ms per
  frame); tick 0 of a room is the earliest `npc_epoch` in Presence. `--npc-seed N`, `--npc-sync-log` (hash every 600 ticks),
  `--autotest npcs-save` then `npcs-continue`.
- Ages and roles: `AvatarLook.age` / `extras`, `AvatarPose.sit` / `lying` / `crouch` / `sway` / `hide` (`game/npc_look.c`).
- Players walk straight through NPCs; NPCs side-step. Name tags are for real players only; NPC names wait for interaction (M8).
- Settings -> NPC density (`npc_density`). Developer: Show slots, Show NPC packages, Show NPC paths, Freeze NPCs, Respawn NPCs. Low density draws half and simulates all.
- `--autotest npcs [S]` (default 300 s, also a perf scenario): at least 44 alive, no NPC stuck over 2 s, none off the grid,
  children escorted and only in allowed rooms, route completed. `--autotest npcs-bench`: real sim at 44 and 200 NPCs on 1 and
  all threads, positions compared. Other autotests run with no NPCs so their numbers stay comparable.

## Performance (PERF-1)

Standing rules and budgets: [docs/PERF_RULES.md](docs/PERF_RULES.md); measurements: [docs/PERF.md](docs/PERF.md).

Devices: test on this Mac and the iPad Air 4 only; the iPhone is no longer tested (user decision). Full gate:
`tools/perf_run.sh OUT --gpu --gate` with a clean tree and HEAD installed.

- Profiler: `PROF_SCOPE("name")` (`engine/core/prof.h`), main thread, compiled out with `-DMG_PROF=OFF` (store archives).
  Developer -> Profiler shows the counters and the top 12 scopes (avg / p95 / max over 120 frames). SDL GPU has no
  timestamps: "GPU approx" is submit -> fence on a helper thread, use Instruments for real GPU time. The heap counter
  sees what goes through SDL_malloc on the main thread, not the OS or the Metal driver.
- `--autotest perf SCENARIO SECONDS` (`yard`, `lobby`, `corridor_f2`, `stress200`, `lobby4`, `menu`, `ui_juice`, `worldmap`, `worldmap_travel`, `island`, `worldmap_voyage`) writes `perf_SCENARIO.json` to
  Documents on iOS, the pref path on desktop; `--autotest stress200 [S]` is the same scenario. Measure with
  `CONFIG=Release tools/run_ios.sh`, then `tools/perf_run.sh OUTDIR [--gpu] [--resume]` (waits up to 10 min for a dropped device, retries; `--resume` continues a run) (fails hard on stale output: run id, commit,
  mtime) and `tools/perf_gate.py OUTDIR` (budgets + 10 % regression against `docs/perf/after`): Debug is `-O0`. `--msaa N` / `--no-msaa` (default 2x, see PERF.md), `--no-instancing`, `--no-lod`, `--force-thermal N` for tests.
- Jobs: `app->jobs`, `jobs_run(js, fn, ctx, count, batch)` + `jobs_wait`; workers never touch render state.
  `--autotest determinism` runs 400 NPCs on 1 and N threads and compares every position.
- NPC data: `game/npc.h` (`npc_hot_t` one cache line, double-buffered; `npc_cold_t`). `game/stress.c` = dummy crowd,
  Developer -> Stress 0-400.
- Renderer: same mesh + pipeline on the main pass = one instanced draw (`shaders/inst.hlsli`, `build_instances` in
  `gpu.c`) for toon, outline, unlit and ground sprites (`GFX_DRAW_GROUND`). Each has an opaque pipeline with no `clip`
  and a dithered one used only when 0 < hide < 0.98; hide >= 0.98 is dropped in `gfx_draw`. The draw list is radix-sorted
  on a u64 key (bucket | kind | mesh or depth | position). Static hospital meshes are merged per storey and material;
  wall pieces are index ranges of them (`gfx_draw_range`). Keep the `draw_bucket` order (x-ray note above).
- Triangle soups (mgmb: the library, islands, vehicles, avatar parts) are uploaded through `mesh_upload_welded` (`engine/gfx/weld.c`):
  vertices merge only when all 28 bytes match (position, normal, colour + zone), so every drawn triangle is the soup's (`weld` ctest
  rebuilds each one; `MG_NO_WELD=1` uploads the plain soup for comparisons). Mesh pools 5 MB vertices + 2 MB indices.
- Avatar LOD: `AvatarPose.lod` 0 full, 1 coarse, 2 body + head; chosen in `crowd_cull_job`, with frustum culling.
- Frame: main pass + UI pass; the UI pass starts by upscaling the 3D image (`present.frag`). Metal shaders get real
  `half` from `min16float` (`tools/build_shaders.sh` compiles MSL with `--hlsl-enable-16bit-types`; SPIR-V stays 32-bit).
- `-DMG_BAKE=ON`: prototype that bakes sun shadow + AO into static vertex colours, cached in the pref path. Off: the
  meshes have too few vertices for it to look right.
- Mac perf validity: a desktop `--autotest perf` refuses to start on battery power (`MG_PERF_ASSUME_BATTERY=1` tests the
  path) and writes `"valid": false` over the old result; every run records `display_hz_start` / `_end` (the screen's real
  maximum: `NSScreen.maximumFramesPerSecond`, SDL's mode otherwise), `fps` and `paced_below_refresh`; a changed refresh makes
  the run invalid. Runs also record `paced_above_refresh`, `display_asleep_s` (macOS: display asleep or session locked, checked once a second), `window_occluded_frames` and `window_unfocused_frames`; a sleeping display, running above the refresh, or a window covered or inactive for more than 0.5 % of the recorded frames makes the run invalid (a desktop perf run raises its own window once a second while it is behind, and `tools/perf_mac.sh` brings it to the front too). macOS paces the game to the 60 Hz screen only while its window is the active app: with another app in front the loop runs at ~120 fps (the step 6 runs), so a valid Mac run needs the game in front for its 60 s. Mac runs go through `tools/perf_mac.sh OUT SCENARIO [RUNS] [SECONDS]`: it refuses a dirty tree or a binary whose embedded `MG_BUILD_COMMIT` is not HEAD and keeps only results labelled HEAD and valid (`build_info.h` is a byproduct of `mg_buildinfo`, so the first build after a commit carries that commit). `tools/perf_gate.py` fails invalid runs, warns on paced ones and prints each run's refresh and fps. `--log-frame-stats` also logs fps and whether the window is visible. `--lib-worst-trip` (+ `--lib-worst-seated`): the §8 trip's worst case for draws (a full lobby across the diagonal, two friends walking or seated, a plane in, the pan); `--log-frame-stats` logs the per-window maximum (44-45 draws after the sort key keeps a mesh's dithered instances apart from its solid ones). This Mac's screen is 60 Hz while the loop
  presents ~120 times a second (SDL's Metal display sync does not hold it to the screen here), so `fps` above `display_hz` is normal.
- Late frames (tracked bug, rare Mac stalls): `engine/core/frame_notes.c`. Expensive one-off work calls `frame_note("...")`
  (GPU idle waits, mesh uploads, figure upload, screenshot, MSAA change, level build, map preload step); a frame over 50 ms
  logs `late frame: <ms> at <s>, screen, scene, during it: <notes>` at the start of the next one, and the last 32 go into the
  perf JSON (`late_frames`), so device runs bring them to the iPad gate.
- Thermal state serious or worse: NPC detail radius 22 -> 11 m and MSAA off, logged (`update_thermal` in `game.c`).

## Commit rules

- `tools/hooks/pre-commit` (`git config core.hooksPath tools/hooks`) runs `tools/precommit.sh`: it builds exactly what is staged for desktop and refuses the commit on any error or warning. Never use `--no-verify`.
- Before any `checkout`, `reset`, `stash drop`, `clean` or branch switch, commit the working tree as `wip: <step>` (squash later).
- Every background agent works in its own git worktree and branch, no exceptions (never in the main tree); merge them one at a time and run the affected autotests + ctest after each merge.

## Conventions

- C11, plain C, small files. No heap allocation in the frame loop: use
  `app->perm` (startup) and `app->frame` (reset per frame) arenas. The only
  global is the app context in `engine/platform/app_sdl.c`.
- Simulation runs at a fixed 60 Hz (`game_tick`); rendering interpolates with
  `alpha`. Never put frame-rate dependent movement in `game_frame`.
- Y up, right-handed, metres. North = -Z, East = +X.
- UI units are points (pt): window pixels / display scale. Respect
  `app->safe_*` insets.
- Game code talks to `gfx.h` and `net_transport.h` only, never SDL_GPU or
  the platform network APIs directly.
- Third-party code is compiled with warnings off; our code must build with no
  warnings (`-Wall -Wextra -Wshadow`).
- Before trusting memory of a dependency API, read its header in `third_party/`.

## Performance target

Reference devices for "older phones": **iPhone 12 (A14, iOS 17+)** and its
Android counterpart **Samsung Galaxy S20 (Snapdragon 865 / Adreno 650,
Android 10+)**. Budget: 60 fps with < 4 ms GPU per frame. SDL GPU has no GPU
timestamp queries, so measure GPU time with Xcode's Metal frame capture /
Instruments (iOS) and Android GPU Inspector or Snapdragon Profiler.

## Avatar (LIB-1 A1)

- `game/avatar.c`: the library avatar for every player and NPC: parts from `assets/avatar/avatar.mgmesh` (+ `avatar_options.mgmesh`
  from `tools/gen_avatar_options.py`: the other hair styles, suit details, extras), sizes and motion from `assets/avatar/avatar.json`.
  Smooth + cel shaded (smooth normals, 70 deg crease; the suit body takes its lathe profile's normals), black outlines from
  smooth-normal hulls. One pose function, `avatar_solve`; `avatar_animate(meshes, anim, pose, dt)` eases move (12 /s, full from 1 m/s),
  heading (10 /s), browse / push / look. Hair style 0 stays "None" (the exported cap is style 1, Short): saved skins keep their meaning.
- Suits are zones of one body mesh (vertex alpha = 255 - zone): `GfxMaterial.extra` = outfit, trim, accent, zone mask (+ `GFX_ZONE_BODY1`),
  coloured / collapsed in the vertex shader (`common.hlsli` `zone_apply`), so no extra draw per suit.
- Seats: the body base goes to the seat's top + the library's clearance (`AvatarRig.seat_clearance`, measured from `library.mgmesh`):
  `HOSP_CHAIR_TOP` 0.48, `HOSP_BENCH_TOP` 0.47. Canes walk in steps (drawn only: the sim's position moves evenly).
- The states that did not map cleanly and their substitutes: `docs/AVATAR.md` section 8, pictures in `docs/autotest/avatar/substitutes/`.
- Tests: `--autotest avatar` (assembly, walk, turn, cane, the remote path through replication, 200 option samples, old skins);
  `tools/avatar_test.sh` (also the pictures: option sheet, ages, poses, seats, bed, x-ray); `--avatar-gallery NAME`, `--log-npc-poses TICK`.

## Library menu (LIB-1)

- `game/menu_library.c`: `assets/menu/library.mgmesh` + `library.scene.json` (mg-library 10, exported by the prototype; every position, angle and duration comes
  from it). A cutaway (the shell is not drawn in the menu); the camera from `camera.menu`, backed off and re-centred until the table and
  its four seats fit the free rect. `game/lib_study.c` = STUDY (read / page turn, the right hand rides the leaf tip). Seats: walk in
  through the door along the seat's path, sit (`sit_s`, the chair slides), study; leavers stand and walk out; already-listed members
  are seated on the frame the menu opens.
- Onboarding and Locker (step 8, §11): Welcome / Username / Creator show the hall at my table. The first launch only (pref
  `lib_walked_in`, kept on sign-out; installs from before default to `onboarding_done`) the hero walks in from its bay's
  `door.outside` (an A* walk carrying book and mug, then sit / settle). My own table keeps its dressing and seat order while it is in
  view and I am alone (`hold_seed`: the first sign-in changes my seed mid-onboarding); the new seed applies once it is out of view or
  someone joins. Locker: `camera.creator` moved with seat 0 onto the hero's own seat (framed on it, facing it), the orbit's target,
  log distance, pitch and yaw blended by a quintic over 0.92 s (= a 5 /s exponential's 1 % settle time; the exponential's first frame
  is a §15.11 jump), the target travelling with the distance as in the Maps move; the menu camera keeps the opener's fit while the
  Locker is up, and every refit of the free rect (lobby panel, screen columns) is a quintic too (`FIT_RATE`); the hero's pose (`creator_w`: study blend 0.15, look, hands on the table) follows once no page is in the air, its
  loop paused until it closes. Reduse: at once under the 300 ms cross-dissolve. `--autotest library` with `--frontend welcome`
  checks it (`--lib-walked`: a second launch, no walk-in); pictures docs/autotest/library/step8/.
- People (step 4, `game/lib_people.c`, pure C): the scene's 25 `npcs` (routes, stops, `wait_s` / `face_deg` / `outside` / `book` /
  `leave_cart`, every act of LIB1.md §9 with the prototype's eased weights; seated readers, the sleeper with zees) and the members'
  walks, all `lib_nav` agents (96 % hard rule, also during the ignore-others spell; steering; re-path at 1.2 s; spell at 4 s; stop
  within 0.85 m of a taken stop; cart 0.75 m ahead, parked and slid back). Fixed 60 Hz ticks, one path search per tick (deterministic:
  ticks + the visible-bay mask); a bay outside the frustum ticks 1 in 6 and is not drawn. Doors (rate 8 /s) in the tick. ctest
  `lib_nav` runs the real 25 + three members (96 %, stuck <= 4 s + one step, every act, two loops, determinism) and the rules alone.
- The hall is square (user decision): 2 x 2 bays of 16 x 16 m = 32 x 32 m (`hall.bx` / `bz`, front row 0 1, back row 2 3, home bay 1).
  A bay's back wall is at local z -5.5, the extra depth is open floor up to +10.5; the front row's back wall (doors) parts the rows, the
  back row's doors lead outside. Draw rule: the menu camera backs off to 11.8 m, so with my table in the back row the eye stands 0.3-1.1 m
  south of the middle wall; then the front rooms (and doors) dither out while the eye is below the wall tops, and dither back in over 2 m of
  height above them (the Maps move). Wall-aware bays (`bays_occluded`): a bay on the far side of the middle wall whose box, projected from the eye onto the wall's plane, stays within the wall, misses every open front doorway and has nothing over the wall top on screen is not visible: its people tick at 1/6 and are not drawn, it emits no particles, its dressing, extras and alpha are skipped, and its room draws only the triangles that reach through the wall (sorted first at load, `room_near`). Pixel-identical (`--lib-still --lib-wall-ab`, frames two apart; `--lib-no-wall-vis` turns it off); `--log-frame-stats` logs draws, triangles by category and per-bay visibility every 120 frames (counts, valid on battery). Desktop `--lib-bay N` puts my table in bay N, `--lib-guest B` seats a stand-in player at bay B's table (pictures:
  docs/autotest/library/bays/).
- The library stands on the island played last (island JSON `library`, `library_pad`, `library_path`; `tools/place_library.py`: the spot
  nearest `start` with the 15 x 15 pad (the 32 x 32 m hall x 0.35 + 1.9 round it) >= 30 from every hospital plot and >= 8 from the road
  (measured from the pad's edge), on the plateau, facing the road, on bare grass with no scenery triangle under it; the path crosses no
  water, plot, raised ground or scenery; props within 1.0 of the pad / 0.5 of the path removed; the pad and the path are part of the
  island's road mesh) at `lib_scale`. `library` is the hall's origin, `library_pad.x/z` the pad's centre (the hall's centre is off its origin).
  `python3 -I tools/place_library.py --check` measures every number again on the committed JSON of both islands (PASS / FAIL per point)
  and fails if the rule would now write anything different (pictures: docs/autotest/library/step9/).
- The map preloads behind the menu (`worldmap_load_step`, one island per frame; the Maps row waits). Maps / Back / New Game are one camera
  move (`game/lib_move.c`: log-distance + hump, target along the cumulative distance, the shell dithers in, the map UI enters at t 0.9);
  Animații interfață = Reduse: a 300 ms cross-dissolve (`gfx_dissolve_capture` / `gfx_dissolve`). The pause card's Map keeps its scene switch
  (its Back reopens the library open: the menu always opens on the cutaway). Mid-move: the game starting (a host's start) cuts the move
  short (`move_cut`), Reduse switched on ends it at once under the dissolve, Maps during a §8 trip finishes the trip and starts from the
  trip's camera (`--autotest library maps-mid-trip`; `--lib-move-cut start|reduce|start-back|reduce-back` with `--lib-move-demo`).
- Desktop: `--debug turn-page`, `--lib-closeup [side]`, `--lib-demo-join`, `--lib-move-demo`, `--ui-motion reduced`, `--lib-back-loop N`
  (Maps + Back N times, every Back's camera checked), `--lib-still` (the library at dt 0, comparison pictures), `--lib-no-nav` (perf A/B: straight legs),
  `--lib-look x,z,d` (the menu camera on a hall point, pictures: docs/autotest/library/step4/), `--lib-log-acts` (each person's act as it starts).
- The Maps / Back move changes nothing in a step (user decision: no visible switch, no jitter): the library's light
  (two-band ramp, its sun) and the map's are both evaluated per surface and the results mixed along the move
  (`GfxSceneParams.sun2_* / ambient2 / two_band2 / light_mix`, `cel_light_day`), so no toon band flips as the light changes;
  sky, fog and discs blend; the shell fades with real transparency (`mat_depth_only` + `mat_toon_fade`: its depth, then its
  colour blended once per pixel, draw buckets 4 and 5) instead of a screen-door; NPC LODs are taken from the menu's resting
  camera for the whole move. NPC LOD at rest: by size (a fraction of the view's height, full above 0.215, coarse below
  0.195), a switch is a 0.25 s dithered crossfade of the full mesh over the coarse one. Picture: docs/autotest/library/light_move_before_after.png.
- Guards: `gfx_allow_fade` (the blended toon fade only on the move; perf JSON `fade_violations`, the gate fails on any);
  `gfx_settle_uploads` after each batch of mesh uploads (else Metal holds the staging buffers as reclaimable driver memory);
  `--lib-lod-fade N` (perf: NPC N held mid-crossfade, the draw count's worst case).
- Room life (step 6, §10, `game/lib_life.c`, pure C): the clock (device local time, re-read once a second), pendulum, skeleton, 5 fish
  (closed-form swim and turn) and 8 bubbles, 30 sunbeam motes per bay, lamp dust, and the cat. It ticks on the people's fixed ticks
  (`life_pre_tick` / `life_post_tick` around `lib_people_tick`); `lib_life_emit` once a frame. The cat's ground legs are walked by its
  `lib_people` agent (radius 0.2, `lib_people_cat_*`: placed when it lands on the floor, none on a table or the window seat); a visit
  calls `menu_library_pet` and the seat's fidget pet weight drives the squash, head lift and hearts. Every particle (dust, bubbles,
  steam, talk dots, hearts, paper balls) goes through one pool (`puff_at`, cap 320, visible bays only) and is one instanced unlit
  `puff` draw. Moving parts are zones of two meshes (`upload_zoned`: clock hands, pendulum, skeleton, fish; cat body, head, tail),
  one instanced toon draw each through the zone mask. Static alpha (beam, tank glass, floor lamp glow and cone) is merged per row of
  bays with each group's opacity in the vertex alpha (the frustum sees all four bays from the menu camera, so per-bay meshes would
  cull nothing); table-lamp light and `phone_glow` are instanced. `GFX_MAT_ALPHA` is drawn in bucket 3 (after the avatars), mesh first.
  ctest `lib_life` (+ the cat on the real grid among the people); `--autotest library` step 13 (the scripted steps before it hold the
  cat's visits, `AutotestOut.lib_cat_hold`); `--lib-log-acts` logs the cat's states too. Pictures: docs/autotest/library/step6/.
- My own table's bay per account (§11, user decision): `prefs.json` `lib_bays` {seed, bay} remembers it once per account on a device
  (kept on sign-out). The table moves to the account's bay only on the first library open after a cold start or a game
  (`lib_open_kind`, used up by every open) with me at my own table; a sign-in mid-menu, Back from the map, Settings / Friends or a
  friend's table never move it. `--autotest rebay` (+ `--rebay-cold`): fresh install, sign-in, 30 s still, Back, back from a game,
  sign-out / in, a friend's table, friends in my lobby, a cold start; `--lib-seed-bay N` forces the seed's bay.
- Sound (step 10, §13): `menu_library_draw` asks (`L->snd`, `lib_sounds`), `library_sounds` in game.c plays after the frame's UI on
  the existing audio voices (`audio_tone`; wave 2 = band-passed noise): the clock tick (1.8 kHz, 12 ms, 0.012) on each second of the
  device-time clock (lib_life's), only with a clock face on screen in a drawn bay; the page (noise 3.2 -> 1.4 kHz, 70 ms, 0.03) when
  my hero's leaf leaves the page (`LibStudy.lifts`); the stamp (140 Hz, 40 ms, 0.015) at the bottom of each dip of the librarian's
  `stamp` act, in view. Menu only (the open cutaway with my hero, not the map), silent under a modal, under Sound + `ui_sounds`;
  `asked` / `played` / `dropped` counters for the tests (`--autotest library` step 14).
- The camera move's no-jump rule (accepted spec change, docs/LIB1.md §15.11): speed in camera distances per second (m/s over the
  camera's distance to its target), at most 1.5 x the previous judged frame's + 0.06; a step is judged only once it spans half a frame
  of the move's clock; no step across a picture capture is judged. `--autotest library` runs a third Maps / Back whose Back carries one
  stale camera frame (`AutotestOut.stale_eye_once`) and fails if the rule does not see it. The full LIB-1 text with the accepted
  changes: docs/LIB1.md. Tests:
  `--autotest library` (`tools/library_test.sh`: + Reduse, a fresh install, the free rect on the iPhone presets and the iPad), the
  two-instance join / kick in `tools/friends2_test.sh` (role ma), perf scenario `menu_to_map` (the move looped).
- Lobbies in motion (step 7, §8, `travel` in the scene JSON): my own table is my seed's bay (`lib_table_bay`, taken when the menu
  opens; `--lib-bay` overrides), a lobby I join sits at its host's seed's bay (the next bay if that is mine). `menu_library_lobby(rows,
  snap, instant)` each menu frame: a table change is a trip for my hero (pack, stand, walk; `LibTravel`: the camera waits until the
  hero is off screen by the real projection (|x| > `offscreen_at`), then pans (`pan_s_per_bay`, max `pan_s_max`, quintic, distance
  x (1 + `zoom_out` sin(pi e))), the hero walks at `offscreen_speed` only while off screen), the new table dressed and its members
  seated at once; the members I leave stay seated (`ghost`, slots 4..7) until they are out of view, their table re-dressed only off
  screen. Member slots: 0 me, 1..3 the lobby (`tseats`), 4..7 people leaving; every member carries its own seat copy (`at`, `bay`,
  `seat_k`, `cover`) and `home` (seat 0's stand point at its own table). A friend joining walks from its home, a friend leaving walks
  back there and is removed off screen and > `despawn_beyond_m` from the camera's focus, or on arrival (faded if in view). The front
  rooms' fade follows the camera's framing point (`focus`), not the lobby's table. Maps, a full-screen page (Settings, Friends, the
  Locker) and the game loading finish every move (`menu_library_travel_finish`); Reduse: the trip happens at once under the 300 ms
  cross-dissolve (`dissolve_armed`: the frame is captured first), friends are seated / removed directly (faded in view). Paper planes
  (`LibPlane`): an invite or friend request sent (`Lobby.invites_sent`, `Friends.requests_sent`; the RPC is not delayed) throws one
  from my seat (sent from the Friends page: once back on the menu, within 30 s) towards the friend's table's side; an incoming
  invite's panel shows at once and a plane lands in front of my seat (the hero glances; it shrinks after `rest_s` or when the panel
  closes); the cat watches a flying one. `--autotest library` 15.7-15.9 (stages 20-22, `AutotestOut.lib_demo` / `lib_plane`; pictures
  docs/autotest/library/step7/); the camera's speed on a trip is judged by the trip's own clock (`LibTravel.cam_clock`). Desktop
  `--lib-demo-id ID` gives `--lib-demo-join`'s friend another id (so another own table).

## Book content (BOOK-1)

- **No book content lives in this repository or in its builds** (user decision, 2026-11-12: the Gastro missions, quests,
  scripts, items, facts, cast, dialogue, figures, people, the pack, its dummy and the pictures of them were removed; git
  history was not rewritten). The engine stays: the pack format (`game/mgpack_format.h`), reader (`game/book_pack.c`), missions,
  script player, journal, dialogue card, pill, figures, progress, the load-time data and reachability checks.
- A build embeds a pack only when one is supplied: `-DMG_CONTENT_DIR=<folder with gastro.mgpack + fig/>` (never inside the
  repository). Without one the game runs with no book content: the institute is an empty walkable building, no missions,
  no pack people, island completion "—". `MG_REQUIRE_REAL_CONTENT=ON` (the store scripts) fails without a pack.
- Tests use a synthetic fixture with invented placeholder text, generated into the build folder at build time
  (`tests/fixtures/make_fixture_pack.py` -> `build/<dir>/fixture/fixture.mgpack` + `figures.mgfig`; target `mg_fixture`): ctest
  `book_pack` / `book_figs` read it, the book autotests (`book_ui`, `missions`, `book`, `bookpeople`) load it with
  `--content build/<dir>/fixture`.
- Leak guard: `tools/precommit.sh` refuses a commit holding a run of 12 or more consecutive words of the book
  (`tools/book/leakguard.py` + the hashed shingles in `tools/book/leakguard.txt`, hashes only); `leakguard.py repo GUARD .`
  scans every tracked file. `tools/book/` keeps only what the engine and tests need: the leak guard and its hashes,
  `packbin.py` (the pack writer, used by the fixture generator) and `figpack.py` + `astc_encode.swift` (figure packs); the rest of the
  content pipeline is not in the repository.

## Book engine (BOOK-ENGINE)

- Institute (MAP-2): books.json Gastro `scene` = `institute`. `tools/mapc.py` compiles `assets/world/institute/institute.map` (the building only) to
  `assets/world/institute.mapb` (MGMP v1, read in place by `game/imap.c`); `game/institute.c` (+ `institute_furniture.c`)
  builds the shell, furniture merged per storey and material, 3 lifts x 5 landings (G 1 2 3 R), wall pieces, open doorways (frames only; only the lifts have doors). The map's `door_leaf` record name is historical: no leaf exists, the records only mark where doorways are (the pack's furniture table must keep matching the map; `nav_check` reports them as doorways),
  nav at the capsule's 0.35 m, room graph. Nav vs the reference grid: `MG_NAV_CHECK=<file>` (`game/nav_check.c`, last report
  `docs/autotest/institute/nav_check.txt`). Pictures: `tools/institute_shots.sh`. No ambient NPC roster in the institute.
- Landing call (`lifts_call` in game.c): with several lifts a car stays where it was used; standing within 1.6 m of an idle
  car's plan on another storey calls it there (the hospital's single car is always on the player's floor).
- `--autotest institute` (sets the scene itself): auto-nav to a room on every storey, the roof deck and back to the
  forecourt, a picture per stop (`inst_at_*`), worst frame <= 150 draws, 0 frame-loop allocations, >= 4 rides. Perf scenario
  `institute` (G lobby, both wings, a ride to floor 2). `save` / `continue` play Gastro = the institute (auto-nav to floor 2).
- Pack: `game/book_pack.c` (the reader's checks, zero copy; `book_pack_test`), `MG_CONTENT_DIR` (none = no content), `MG_REQUIRE_REAL_CONTENT=ON` in the release scripts. Text: `text_wrap` (`engine/ui/text_layout.c`),
  `ui_text_area` (re-wraps on change, draws only visible lines). Glyphs „ ” – ’ › → − † α β γ (Greek from Comfortaa).
- Progress (decision 4): `game/book_progress.c`, one file per account `book_gastro_<uid>.bin` (MGBP: facts bitset + 2 bits
  per case, atomic write from a fixed buffer); on account load the bits go to `book_progress_merge` and the merged row comes
  back (`NET_EV_BOOK_PROGRESS`); Erase progress resets file and row; Gastro's map completion = facts set / facts.
  `--debug book-fact N` sets one. ctest `book_progress`.
- Figures (decision 7): `tools/book/figpack.py FIGDIR OUT astc|rgba8` -> `.mgfig` (MGFG v1, sorted ids, 16-aligned, top row
  first); ASTC 4x4 through macOS ImageIO (`tools/book/astc_encode.swift`, flips the image: ImageIO writes the bottom row
  first), RGBA8 decoded without Pillow. `game/book_figs.c` reads it in place; ctest `book_figs` on the fixture's figures.
- People (S4b, `game/book_people.c`): in the institute `g->npcs` runs no roster; it holds exactly the pack's `people` rows,
  spawned in table order with `npc_spawn_at` (`NPC_FLAG_BOOK`: no package; NPC index = person row). Static ones stand / sit
  (record `y` = seat top) / lie (`y` = mattress top) at their post; walkers wander their storey's corridors on the NPC steering
  at rate 1 (a viewer-dependent rate would break the seed + tick rule). Drawn by `draw_npcs` on the view storey only. Looks:
  `book_people_look` (S4b table; surgeon `AVATAR_EXTRA_SCRUB_CAP` = the short-hair cap in the outfit colour, no hair under it).
  Talking: every cast person is usable (name + role card under the head in the preview), the objective's has `IA_PRIORITY` +
  "...", a tap calls `book_people_on_talk`'s callback; background people greet with the pack's greetings by kind.
  `AvatarPose.talk` = the speaking gesture (`book_people_speaking`). Missions API: `book_people_find_cast / set_objective /
  stage / stage_patient / unstage / pos`. `--autotest bookpeople` (pictures `docs/autotest/book/`), `--avatar-gallery book`.
- Missions (S4): `game/missions.c` = the state machine over the pack (arcs > cases > quests > msteps): tracked case, objective
  (offer at the giver / next undone step in book order at its presenter / debrief at the giver), step done = every fact of its
  script's items set (no other step state), quest / case / arc done = all steps; the two gates only (a case closes at its giver
  once its steps are done; `missions_exam_open` = every topic of the chapter complete). Talk flow: `missions_talk(cast)` (the
  people layer's talk callback) -> dialogue (arc intro + case intro + case line, Accept / Later; quest intro on its first step +
  the step line, Start / Later; reply after_good / after_mixed + the quest's done line; outro + arc outro -> closed, T3 reward).
  Staging: the objective step's presenter at its stand point, a case step's patient at its place; a case step starts only with
  the patient there (`ME_NO_PATIENT`). Events (`missions_take_events`): `ME_COMMIT` -> `progress_commit(g, true)`.
  `game/missions_ui.c`: objective pill (under the account chip; tap = journal), dialogue card (head-icon portrait, name, role,
  lines, Continue / two choices), journal (pause card -> Misiuni: arc tabs, case cards with quest stages, Track / Take me there).
  One `MARKER_OBJECTIVE` marker in the chapter colour (`k_ui.chapter`) on whoever the objective is, no marker interactable (the
  NPC's Talk is the interaction). `game/book_check.c`: the load-time data check (`book: data check: ...`), including reach
  from each storey's lift for every step location, person, NPC point and book prop. `--autotest missions` (on the fixture). Progress saves with POSIX open / write / rename (SDL's file streams allocate in the frame loop).
  Embedded as `content/figures.mgfig` (CMake runs figpack on the pack's `fig/`, the demo's without it; astc on Apple and on
  Android built on a Mac, else rgba8). GPU: one figure resident (`gfx_figure_reserve` at start at the largest size,
  `gfx_set_figure` into its corner through a fixed 2 MB staging buffer, RGBA8 in strips of rows; no allocation on a change),
  sampled by `GFX_UI_FIGURE` quads (`ui_figure_quad`) as `ui.frag`'s third texture: still the one UI draw. A format the GPU
  cannot sample logs once and the card shows a placeholder.
- Script player (S5, `game/script_player.c`): `script_player_init` (start), `_open(sp, pack, script, presenter)`, `_active`,
  `_frame(sp, ui, app, progress, now_ms)` (1 = ended: commit progress), `_finished`, `_close`. One modal card in the safe rect
  (`ui_modal_rect`), steps predict / teach / check, widgets for the 8 item types, explanation + multiplu whys; only a check
  answered right sets the item's facts. Pack text wraps and scrolls (per-step line cache, visible lines only); chrome strings
  `sp_*`. Back closes it; the stick and the UI under it take no touches. Widget ids in `script_player.h`.
  Sizes it must take (the real pack's worst case, logged at load as `book: worst cases ...`, numbers only, by
  `script_player_survey`): 22 sort entries in up to 5 bins (one list tinted by bin + compact bins in a grid that stay beside
  the list while it scrolls), 14 order rows, 13 labels on a figure of up to 35 boxes (each box numbered on the figure, placed
  through numbered 44 pt buttons or a drop on the figure = the nearest box), up to 32 options per item (`SP_OPTS`).
- Exams (S7, `game/book_exam.c`): the 7 pools as constants, `book_room_complete`, `book_chapter_complete`, `book_pool_open`
  (shut while a count is a `TODO(author)` 0).
- `--autotest book_ui` (`tools/book_ui_test.sh`: default, iphone, iphone-flip, ipad, on the fixture; pictures stay in the build folder): every
  item type answered wrong (facts stay unset), then right (facts set), by taps and real drags; fails on a target < 44 pt or
  outside the safe rect, > 3 UI draws, a frame allocation with the card up. `MG_BOOK_UI_FROM=N` starts at run N. Per type the
  wrong run plays the item with the most text, the right run the one with the most options / entries / labels. The fixture
  carries one item per type at a large worst-case size (`WORST` in `tests/fixtures/make_fixture_pack.py`, invented filler,
  a 1386 x 1233 figure with 35 boxes), so the tests play the hardest layouts. Real-pack runs: `MG_BOOK_UI_TAG=real` keeps their
  pictures apart (they hold book text: never in docs/).

## Institute look (INST-LIGHT)

- The Gastro institute is fully lit with no shadows. The reference page's unlit interior (MeshBasicMaterial) and its soft outdoor
  shadows were left out on purpose (user decision: "lit, but don't cast shadows"): no shadow maps, pass, bake or decal.
- `GFX_MAT_PATTERN` = vertex colour x one sample of a detail pattern x (hemisphere 0xffffff / 0xb8d6a0 at 0.5 + sun 0xfff6e0 x
  0.62 from (-45, 85, 55) through the two-step ramp 165 / 255 -> 1). Building, wall pieces, furniture, props, lifts, site, roof
  and the terrain beyond the fence (`SceneryShape.grass_pattern`); avatars keep the day light (`cel_light_day`). An up face gets
  1.12, so light floors read near white (the page's light); `--debug inst-fill-1` (test only, not saved, default off) drops the
  building's hemisphere to 0.38 so sun + fill = 1.0 facing up (material params.x).
- Patterns: `tools/gen_patterns.py` at build time (the page's seeded canvas drawing, + mips) -> embedded
  `assets/world/inst_patterns.bin`, one R8 2D array (873 KB), uploaded once (`gfx_set_patterns`). Vertex alpha = pattern id
  (`inst_pattern_of`, KIND_OF by palette colour) | 16 lit; UVs from the face's dominant axis over the pattern's size (pattern.hlsli).
- Glass (COL.glass triangles, blankets included as on the page, the lift cars' panel too): `GFX_MAT_GLASS`, the page's glass shader,
  blended, drawn in bucket 3 (after the avatars); one draw per lift car drawn. The groups merge by draw condition (slab with the
  facade, floors + furniture with the interior), so the institute draws fewer calls than before. Back faces stay culled.
  Sky #BFE6F5, no fog. Pictures: docs/autotest/institute/lighting/.
- iPad gate: every institute pixel now samples the pattern texture and runs the light set (and glass blends), so the institute
  needs a GPU trace on the iPad at the combined gate (docs/PERF_RULES.md).
