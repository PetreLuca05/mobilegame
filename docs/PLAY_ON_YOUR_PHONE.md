# Play Mobilegame on your phone

## Before you start (once): turn on online play

Party and World modes sign every player in anonymously. In the Supabase
dashboard open the **mobilegame** project → **Authentication** → **Sign In /
Providers** → switch on **Allow anonymous sign-ins** → **Save**.
Without it the game still works in *Play offline*, and online modes show a
red "sign-in failed" message.

---

## Android (Android 10 or newer)

Almost every Android phone from 2019 onwards works (it needs Vulkan 1.1).

### 1. Get the APK file

Pick one:

- **The file from this chat.** Tap `mobilegame.apk` and download it on your
  phone.
- **From GitHub (always the newest build).** Open
  <https://github.com/PetreLuca05/mobilegame/releases> on your phone (sign in
  to GitHub if the repository is private), open **Android build (latest
  main)** and tap **mobilegame.apk**.
  For online play in these builds, first add two repository secrets once:
  GitHub → repository → Settings → Secrets and variables → Actions →
  *New repository secret*:
  `MG_SUPABASE_HOST` = `kugytvcupelitbaksotg.supabase.co` and
  `MG_SUPABASE_ANON_KEY` = your publishable key (from `config.local.h`).
  Then rerun the "android" workflow under the *Actions* tab.
- **Build it yourself** with a USB cable: see `README.md` → Device setup →
  Android, then `tools/run_android.sh`.

### 2. Install it

1. Open the downloaded `mobilegame.apk` (from the notification or the
   **Files / Downloads** app).
2. Android asks to allow installs from this app (Chrome / Files). Tap
   **Settings**, switch on **Allow from this source**, go back.
3. Tap **Install**. If Google Play Protect warns about an unknown app, tap
   **More details → Install anyway** (it is your own app, not from the Play
   Store).
4. Tap **Open**. Later you'll find **Mobilegame** in your app list.

Updating: install a newer APK over the old one. If Android says *App not
installed* / *package conflicts*, the new file was signed with a different
key: uninstall Mobilegame first, then install again.

---

## iPhone (iOS 17 or newer)

Apple does not allow installing apps from a downloaded file, so an iPhone
needs one of these two routes.

### Option A: free, with a Mac (good for testing)

1. On a Mac install **Xcode** from the App Store and open it once.
2. Xcode → Settings → Accounts → **+** → sign in with your Apple ID. Select
   the "Personal Team" and note its **Team ID** (10 characters).
3. In Terminal:
   ```
   git clone --recursive https://github.com/PetreLuca05/mobilegame.git
   cd mobilegame
   cp config.local.h.example config.local.h
   ```
   Edit `config.local.h` and fill in `MG_SUPABASE_HOST` and
   `MG_SUPABASE_ANON_KEY`.
4. iPhone: Settings → Privacy & Security → **Developer Mode** → on (the
   phone restarts). Connect it to the Mac with a cable and tap **Trust**.
5. Run `MG_IOS_TEAM_ID=YOURTEAMID tools/run_ios.sh` (install CMake first
   with `brew install cmake` if needed).
6. First launch only: iPhone Settings → General → **VPN & Device
   Management** → your Apple ID → **Trust**.

With a free Apple ID the app stops opening after **7 days**; run the script
again to renew it.

### Option B: TestFlight (paid Apple Developer Program, $99/year)

Best for friends: they install with the **TestFlight** app, no cable.

1. Join the Apple Developer Program, then in App Store Connect create an app
   with bundle ID `ro.docvex.mobilegame`.
2. App Store Connect → Users and Access → Integrations → create an API key
   (App Manager); download the `.p8` file.
3. On the Mac:
   ```
   MG_IOS_TEAM_ID=YOURTEAMID ASC_KEY_ID=KEYID ASC_ISSUER_ID=ISSUER-UUID \
   ASC_KEY_PATH=~/Downloads/AuthKey_KEYID.p8 tools/archive_ios.sh --upload
   ```
4. In App Store Connect → TestFlight add yourself and friends as testers.
   Everyone installs **TestFlight** from the App Store and accepts the invite.

Note: before publishing beyond your own testing, read the mongoose licence
note in `README.md`.

---

## How to play

- **Launch screen**: *Play offline*, *Private party* (**Create room** shows a
  4-letter code at the top of the screen; friends choose **Join with code**
  and type it), or *Open world* (public rooms, up to 4 players each).
- **Move**: put your left thumb anywhere on the lower-left half of the screen
  and slide; push to the edge of the circle to sprint.
- **Look around**: drag with your right thumb.
- **Jump**: the round arrow button bottom-right (hold for a higher jump).
- **Pause button** (top-right): lobby open or locked, players, friends, leave / main menu, camera
  sensitivity, field of view, render scale, show colliders.

All three work at the same time, so you can run, turn and jump together.
