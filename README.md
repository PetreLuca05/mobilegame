# Mobilegame

A small C11 game engine for iPhone and Android (portrait), and its first
milestone **M1 Dev Room**: a toon-styled test room where 2–4 players walk,
jump and see each other in real time over Supabase Realtime.

**Want to play it on your phone? See [docs/PLAY_ON_YOUR_PHONE.md](docs/PLAY_ON_YOUR_PHONE.md).**

## What's in M1

- **Modes** (launch screen): *Play offline*, *Private party* (create a room
  or join one with a 4-letter code, 2–4 players), *Open world* (public rooms,
  joined automatically; a full room sends you to the next one).
- **Dev room**: 24 × 24 m grid floor, coloured walls (N red, E green, S blue,
  W yellow), 0.25/0.5/0.75/1.0 m steps, 1.0/1.5/2.0 m platforms, 20° and 50°
  ramps, a pillar, four spawn discs, an axis gizmo at the origin.
- **Controls**: left thumb = floating stick (push to the edge to sprint),
  right thumb = drag to turn the camera, round button = jump. Gear icon =
  debug menu. Desktop: WASD, Shift, Space, arrows / right-mouse drag.
- **Look**: limbless toon avatars with a ball, sky gradient, clouds, sun
  glow, rolling hills, glossy floor with a real-time reflection.

## Stack

| Layer | Choice |
| --- | --- |
| Language | C11 |
| Platform / input / lifecycle | SDL3 3.4.20 (`third_party/SDL3`) |
| Rendering | SDL3 GPU: Metal (iOS), Vulkan (Android, Linux) |
| Shaders | HLSL → SPIR-V + MSL (`tools/build_shaders.sh`, SDL_shadercross or glslang + spirv-cross) |
| Math | HandmadeMath 2 |
| Physics | own kinematic capsule controller vs. oriented boxes |
| JSON / WebSocket | cJSON; NSURLSession (WebSocket + HTTPS, the OS's TLS) on iOS / macOS |
| Backend | Supabase anonymous Auth + Realtime (Broadcast + Presence) |
| Text | SDF atlas from Fredoka (OFL) |

Engine code is in `engine/`, game code in `game/`; see [CLAUDE.md](CLAUDE.md)
for the layout, conventions and every build command.

## Getting the code

```
git clone --recursive https://github.com/PetreLuca05/mobilegame.git
cd mobilegame
git config core.hooksPath tools/hooks      # pre-commit hook: book leak guard + desktop build of what is staged
cp config.local.h.example config.local.h   # then put in your Supabase host + publishable key
```
`config.local.h` is git-ignored. Only the Supabase host and the publishable
(anon) key go in the app; never the service key.

## Build and run

| Target | Command | Needs |
| --- | --- | --- |
| Desktop | `tools/run_desktop.sh` | CMake ≥ 3.28, Ninja (optional), Vulkan driver (Linux) |
| Android | `tools/run_android.sh` | Android SDK + NDK r27 (`ndk;27.3.13750724`), `cmake;3.31.6`, a phone with USB debugging |
| iPhone | `MG_IOS_TEAM_ID=<team> tools/run_ios.sh` | macOS, Xcode 15+, an iPhone with Developer Mode |
| Tests | `ctest --test-dir build/desktop` | desktop build |

### Device setup

**iPhone (iOS 17+).** Install Xcode, sign in with your Apple ID (Xcode >
Settings > Accounts) and note the Team ID. On the iPhone enable Settings >
Privacy & Security > Developer Mode, connect it with a cable and tap *Trust*.
Then `MG_IOS_TEAM_ID=XXXXXXXXXX tools/run_ios.sh`. With a free Apple ID the
app runs for 7 days; re-run the script to renew it. First launch: Settings >
General > VPN & Device Management > trust your developer certificate.

**Android (10+, Vulkan 1.1).** Install Android Studio (or the command-line
SDK) with NDK r27 and CMake 3.31.6, set `ANDROID_HOME`. On the phone:
Settings > About phone > tap *Build number* 7 times, then Developer options >
USB debugging on. Connect, accept the prompt, run `tools/run_android.sh`.

### Releases

- **TestFlight**: `MG_IOS_TEAM_ID=... ASC_KEY_ID=... ASC_ISSUER_ID=... ASC_KEY_PATH=... tools/archive_ios.sh --upload`
  (needs the paid Apple Developer Program and an App Store Connect app with
  bundle id `ro.docvex.mobilegame`).
- **Google Play internal testing**: create an upload key and
  `platform/android/keystore.properties` (see `tools/build_aab.sh`), run
  `tools/build_aab.sh`, upload `app-release.aab` in Play Console > Testing >
  Internal testing.
- **GitHub**: every push to `main` builds an APK and publishes it as the
  `android-latest` release (`.github/workflows/android.yml`; add the
  `MG_SUPABASE_HOST` and `MG_SUPABASE_ANON_KEY` repository secrets for
  online play).

## Supabase setup (once)

1. Dashboard > Authentication > Sign In / Providers > **Allow anonymous
   sign-ins: ON**. Without it, Party and World show "sign-in failed".
2. Apply `supabase/migrations/*.sql` (`supabase link --project-ref <ref>` then
   `supabase db push`): Realtime RLS so signed-in (including anonymous) users
   can use their own private `user:<uid>` channel and the `room:<token>` of a lobby they were let into.
3. Quota: the Free plan allows about 100 Realtime messages/s per project. M1
   sends 10 Hz with 2 players and 8 Hz with 3–4 (fan-out N×(N−1)×Hz ≤ 96),
   nothing while alone and 1 Hz while standing still. Lobbies are
   capped at 4 players for this reason; raise `SESSION_MAX_PLAYERS`
   (`game/net_session.h`) on a paid plan.

## Performance target

Reference "older" phones: **iPhone 12 (A14)** and **Samsung Galaxy S20
(Snapdragon 865 / Adreno 650)**: 60 fps, < 4 ms GPU per frame. Phones render
the 3D scene at 0.8× native resolution (UI stays native; adjustable in the
gear menu > Settings > Render scale) with 4× MSAA and a half-resolution
reflection. Measure GPU time with Xcode's Metal frame capture or Android GPU
Inspector; the HUD shows fps, CPU ms and draw counts.

## M1 checklist

| Item | Status |
| --- | --- |
| Portrait on iPhone (iOS 17+) and Android (10+, Vulkan 1.1) at 60 fps | Configured (portrait lock, iOS 17 target, minSdk 29, Vulkan 1.1 required). Android APK builds; **not yet run on a real phone**. iOS project not built (needs a Mac). |
| Walk, orbit, jump with thumbs; lands every platform; step/ramp checks | Passes: 26 controller checks against the real room (`ctest`) and an end-to-end multi-touch test (`--autotest touch`). |
| Two phones in one room see each other within ~150 ms, smooth, no teleporting | Verified with two desktop clients against the real Supabase project: 31–44 ms RTT + 100 ms buffer; replication tested under jitter and 20% loss. Signed-in private rooms need the anonymous sign-in switch above. |
| A closed app disappears from the other phone within 5 s | Verified on desktop: clean exit < 1 s (goodbye packet), killed process 3.5 s. |
| Single CMake tree, no platform-specific gameplay code, secrets outside git | Yes: one `CMakeLists.txt`; `game/` has no platform `#if`s; `config.local.h` is git-ignored. |

## Licences

Our code: no licence chosen yet. Third-party: SDL3 (zlib), cJSON (MIT),
HandmadeMath (CC0), Baloo 2 and Nunito (SIL OFL 1.1, `assets/fonts/`). The network
stack is Apple's (NSURLSession); there is no GPL code in the app. Details and the
audit: `docs/LICENSES.md`.
