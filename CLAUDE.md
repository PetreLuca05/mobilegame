# Mobilegame — engine, hospital, lighting, onboarding (M1-M4)

C11 engine for iOS / Android phones and iPad on SDL3 + SDL3 GPU, with a desktop
target for fast iteration. Current focus: desktop + iOS (Android code still
builds in CI but is not verified). **Landscape only**: LandscapeLeft +
LandscapeRight everywhere (SDL hint + Info.plist), never portrait; lay out
for a short, wide screen and respect `app->safe_*` on both sides (notch /
Dynamic Island flips sides with the rotation, home indicator at the bottom). Multiplayer over Supabase Realtime;
accounts (guest, linkable to Google), profiles and stats over Supabase.

## Layout

```
engine/core      arena allocators, log, fixed-step clock, embedded assets
engine/platform  SDL3 app lifecycle (app_sdl.c), touch/keyboard input, secure token store,
                 native Google sign-in (google_auth*.c; iOS part in platform/ios/google_auth.m)
engine/audio     procedural sound (elevator ding) through an SDL audio stream
engine/gfx       gfx.h interface; gpu.c = SDL3 GPU backend; meshes, camera, materials, planar
                 reflection, frosted UI panels, clipped UI batches, preview camera
engine/physics   capsule-vs-OBB queries (collide.c) and the kinematic controller (kinematic.c)
engine/net       net_transport.h interface; mongoose WebSocket + Supabase auth/Realtime/PostgREST; replication
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

Desktop test flags: `--offline`, `--party ABCD`, `--world`, `--profile NAME`,
`--spawn x,y,z`, `--cam x,y,z,yaw,pitch` (free camera), `--colliders`, `--menu [skin]`,
`--demo-walk`, `--window-pos x,y`, `--quit-after S`, `--log-events`,
`--autotest touch|route|onboarding|save|continue|stickdir|nav|autonav|markers|offline|freeroam-create|freeroam-join CODE|screenshot NAME`,
`--menu pause`, `--markers N` and `--marker x,z,storey` (not saved), `--log-remote`,
`--screenshot-at S:NAME[,S:NAME...]`, `--public-test`, `--skin P,H,A`
(local only, not saved), `--metal-capture [S]` (Apple: one frame at S s, default 6,
to `frame.gputrace` in Documents / the pref path; macOS needs `MTL_CAPTURE_ENABLED=1`).
Args also work on a phone: `xcrun devicectl device process launch --console
--terminate-existing --device iPhone ro.docvex.mobilegame --autotest onboarding`.
Any of the first group skips the launch screen. iOS: `MG_IOS_DEVICE=ipad|iphone|<id>`
picks the device for `tools/run_ios.sh`.

Scene: `-DMG_SCENE=hospital` (default) or `devroom` (M1 engine test room).
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
(Pillow, numpy, scipy). Icons: `python3 tools/gen_icons.py`.
Everything listed in `MG_EMBED_FILES` (CMakeLists.txt) is compiled into the
binary as C arrays; look assets up with `mg_asset_find("path/from/repo/root")`.

## Supabase

Project ref `kugytvcupelitbaksotg` (eu-central-1). Client config lives in the
git-ignored `config.local.h` (copy `config.local.h.example`): host + publishable
key only, never the service key.
Migrations in `supabase/migrations/` (0001 realtime, 0002 world shards,
0003 accounts: `profiles` + `stats` with RLS, sign-up trigger,
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
  faces step on the same vector (`cel_light_day`). Lamps are emissive meshes at
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

- Default: fixed diagonal top-down `game/camera_td.c` (M5-S) - pitch 62 deg,
  vertical FOV 32 deg, 16 m from a pivot 0.9 m above the current storey's
  floor, yaw `CAM_YAW_DEG` = 0 (north up: the long east-west axis runs
  left-right, the entrance and the gate at the bottom). No rotate / pitch /
  zoom; it only follows (spring 8/s + 1.2 m look-ahead) and rides the
  elevator. Stick / WASD: up = screen up = north.
- Touch zones: left 45 % = floating stick; the rest is UI (and marker taps).
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
  geometry show as the real model in its accent colour (full saturation
  x 0.85), dithered 60 %, with a 2 px pure-black rim (mesh pushed out along
  its normals, drawn first). Depth GREATER, no depth write. Draw order
  (`draw_bucket`): solid world -> x-ray -> avatars (`GFX_DRAW_DYNAMIC`) ->
  dithered geometry (`hide` > 0). Keep this order. The x-ray must run before
  the avatars: it then tests only the world's depth and the visible avatar
  paints over it; drawn after them, an avatar's own parts count as "in
  front" (head under hair) and x-ray through each other. Dithered geometry
  goes last so a faded wall's remaining pixels don't trigger the x-ray or
  hide the player. Eligible: the
  local player and remote players on the view floor; the floor-peek ghost
  is the same pass with depth ignored. Only meshes drawn with
  `avatar_draw_with(..., mat_xray(...))` are x-rayed: to keep a mesh out,
  just don't draw it with that material. Settings -> "Show players through
  walls"; Developer -> "X-ray always". Toon outlines are pure black.
- Floor peek (`engine/ui/floorpeek.c`): 36 pt storey rows on the left
  edge; holding a row views that storey, a tap keeps it for the hold time
  (Settings: 0 / 3 / 10 s, "Show floor peek"); the camera pivot and the
  fades follow the peeked storey. On a hidden storey the local avatar is
  the x-ray silhouette drawn on the peeked plan with depth ignored. Desktop `--peek F`.
- Name tags: 12 pt, white 0.6, black outline, 0.5 m over the head, only for
  avatars on the view floor; own tag off by default ("Show my name tag").
- Prefs keys (M5): `show_floor_peek`, `floor_peek_hold`, `show_own_name`
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
  tap target only; off-screen / in a reserved panel (gear, floor peek, floor
  menu, toast) = edge icon with chevron + distance (4 Hz), max 12, 48 pt apart;
  other storey = icon + floor badge.
- Touch arbitration (`touch_controls.c`): targets are tested before the zones;
  the touch is the marker's only if it ends within 250 ms having moved < 10 pt,
  else it goes back to its zone on that event (a stick spawns at the original
  point, which then cancels navigation).
- Feedback: scale punch, ripple, flash, shake, procedural `audio_cue` (tick,
  chime, dup, buzz), `haptic_impact` on iOS. Tapping the active marker cancels;
  tapping another re-targets. Walking onto a marker counts as reaching it.
- Debug: Developer -> Markers (Test markers, count 1-16, show paths for all,
  respawn) - on in debug builds. Test markers: 50 % rooms, 25 % corridors,
  25 % yard, replaced 0.5 s after being reached. Prefs: `haptics`,
  `marker_sounds`, `test_markers`, `test_marker_count`. `--autotest markers`.

- In-game menu: gear / Back = centred pause card, max 420 pt (Resume, Settings,
  Save & back to menu, Disconnect in a room); Settings' back arrow returns to
  it. Arrows are drawn with `ui_line` (the font has no left arrow).
- `--autotest offline`: an unresolvable host behaves like airplane mode; count
  `net: attempt` lines after `frontend: PLAY`. Do not mix `--screenshot-at` into
  tap-timed scripts: a capture stalls one frame, past the 250 ms tap window.

## Front end, accounts and persistence (M4)

- `game/frontend.c`: WELCOME -> USERNAME -> CREATOR -> MAIN_MENU (first
  launch; later boots go straight to the menu), SETTINGS, FREE_ROAM,
  LOADING, PLAY. Outside PLAY the locker-room scene is drawn (avatar on a
  turntable); the hospital is only built when play starts. Back = Android
  back / Escape (exits from the welcome screen). Desktop: `--frontend
  welcome|username|creator|menu|settings|freeroam`, `--play new|continue`.
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
  `last_mode`, `last_room`, `profile_cache` {name, skin, stale},
  `stats_queue` {sessions, play_seconds, distance_m, elevator_rides,
  rooms_joined} (offline play, added on the next load). Written atomically.
- Offline play sets `net_set_quiet`: after a failed sign-in (airplane mode)
  the network thread makes no new attempts until the player leaves play.
  Sign-out keeps settings + save, clears the cache and `onboarding_done`.
- `save_slot0.bin` (offline New Game / Continue): "MGS1", version u32,
  pos 3 x f32, yaw f32, floor u32, play_seconds u32, elevator_rides u32,
  visited_rooms u64 (bit = storey * 16 + chunk); every 30 s, on background,
  exit and "Save & back to menu". Offline play joins no Realtime room.
- Multiplayer modes: one row in `game/modes.c` (id, name/description string
  keys, players, enabled, entry state) + its strings; disabled rows show
  "Soon". Free Roam = room code create/join.

## Networking

- `engine/net/net_transport.h` is the only API the game uses; `supabase_rt.c`
  implements it on a network thread (fixed-size command/event queues, no
  allocation on the caller side).
- `ws_client.c`: mongoose TCP + built-in TLS 1.3, our own WebSocket framing
  and a one-shot HTTPS helper. Hostnames resolve with getaddrinfo (works on
  NAT64). Desktop honours `HTTPS_PROXY` + `SSL_CERT_FILE`; phones use the
  embedded `assets/cacert.pem` (regenerate: `python3 tools/gen_ca_bundle.py`).
- Auth: refresh token in Keychain / Android Keystore (`SecureStore.java`) /
  desktop pref file per `--profile`.
- Topics: party `room:ABCD` (max 4), open world `world:01`.. (cap 4 per
  shard, see `game/net_session.h`). RLS in `supabase/migrations`.
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
build/desktop/mg_nettest join ABCD alice 20          # signed-in join (needs anonymous sign-ins)
build/desktop/mg_nettest join ABCD alice 20 --public # public test channel, no sign-in
build/desktop/mobilegame --party ABCD --profile a    # two instances: different --profile
build/desktop/mobilegame --party ABCD --profile b --demo-walk --window-pos 600,90
```
`--public-test` makes the game use a public channel without sign-in (testing only).

## Tests

`ctest --test-dir build/desktop` runs the controller and replication checks.
`build/desktop/mobilegame --autotest touch` drives multi-touch input end to end;
`--autotest route` walks the hospital and rides the elevator to every storey
(fails on snags, a misplaced floor menu or a ride that is not 0.8 s + 0.6 s per storey).
Both need a display (Xvfb on headless Linux).
`build/desktop/mg_nettest profile PROFILE [NAME P H A]` reads/writes an account.
Menu scripts tap real widgets by id (`ui_find`), on desktop or device:
`--autotest onboarding` (fresh prefs: Welcome -> guest -> Username -> Creator,
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
  mongoose directly.
- Third-party code is compiled with warnings off; our code must build with no
  warnings (`-Wall -Wextra -Wshadow`).
- Before trusting memory of a dependency API, read its header in `third_party/`.

## Performance target

Reference devices for "older phones": **iPhone 12 (A14, iOS 17+)** and its
Android counterpart **Samsung Galaxy S20 (Snapdragon 865 / Adreno 650,
Android 10+)**. Budget: 60 fps with < 4 ms GPU per frame. SDL GPU has no GPU
timestamp queries, so measure GPU time with Xcode's Metal frame capture /
Instruments (iOS) and Android GPU Inspector or Snapdragon Profiler.
