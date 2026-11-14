# Google sign-in setup

## What was set up (2026-11-14) — working on the iPhone

- Google Cloud project **MobileGame-Rezi** (`mobilegame-rezi`), Google Auth Platform: app name Rezi, audience External,
  publishing status **Testing** (not submitted for verification), one test user (the owner's Gmail), no scopes added (only
  the basic openid / email / profile).
- OAuth clients: **Mobilegame Web (Supabase)** (Web application, redirect `https://kugytvcupelitbaksotg.supabase.co/auth/v1/callback`)
  and **Mobilegame iOS** (bundle `ro.docvex.mobilegame`, team `9FPB67U7DJ`, App Check off). Client ids are public; they are in
  the git-ignored `config.local.h` as `MG_GOOGLE_WEB_CLIENT_ID` / `MG_GOOGLE_IOS_CLIENT_ID`.
- Supabase → Sign In / Providers → Google: enabled, Client IDs = Web id, iOS id; **Client Secret left empty** (the app signs in
  natively with an ID token, which needs only the client ids; the secret would only matter for a web OAuth redirect flow);
  Skip nonce checks OFF; Allow users without an email OFF. User Signups: **Allow manual linking ON** (was off), anonymous
  sign-ins ON.
- The Web client secret is not stored anywhere in this project. If the web redirect flow is ever added, create a new secret in
  Google Cloud, paste it into Supabase yourself, and delete the old one.
- Chrome's password manager auto-fills a saved login into the Supabase Google panel's Client IDs / Client Secret fields; clear
  them before saving.
- Release blocker: App Store review requires **Sign in with Apple** whenever a third-party sign-in such as Google is offered
  (App Review Guideline 4.8). Apple sign-in stays disabled until the paid developer team exists, so the store build is
  blocked on it; TestFlight builds are not.
- To add testers before the app is published, add their Gmail under Audience → Test users (max 100 while in Testing).

The original step-by-step notes follow.

Players start as guests. "Continue with Google" (Settings → Skin → Account)
links the guest to a Google account in place: same user id, so name, skin and
stats carry over. These steps are one-time and need your Google and Supabase
accounts. Nothing here goes into git except the public client IDs.

Bundle id / package name: `ro.docvex.mobilegame`. Supabase project ref:
`kugytvcupelitbaksotg`.

## 1. Android signing key (SHA-1)

Google identifies the Android app by the SHA-1 of the key that signs the APK.
CI builds use a random debug key unless you give it a fixed one.

On any machine with Java (`keytool` comes with it):

```
keytool -genkeypair -v -keystore debug.keystore -storepass android \
  -alias androiddebugkey -keypass android -keyalg RSA -keysize 2048 \
  -validity 10000 -dname "CN=Android Debug,O=Android,C=US"
keytool -list -v -keystore debug.keystore -alias androiddebugkey -storepass android | grep SHA1
base64 -i debug.keystore | tr -d '\n' > debug.keystore.b64
```

Keep the `SHA1: ...` line for step 2. Keep `debug.keystore` private (also copy it
to `~/.android/debug.keystore` if you build Android locally).

## 2. Google Cloud Console

1. Open https://console.cloud.google.com, project picker → **New project** →
   name `Mobilegame` → **Create**, then select it.
2. Menu → **APIs & Services** → **OAuth consent screen** (shown as
   **Google Auth Platform**) → **Get started**:
   - App name `Rezi`, user support email = yours → **Next**
   - Audience: **External** → **Next**
   - Contact email = yours → **Next** → agree → **Create**
3. **Audience** → **Test users** → **Add users** → your Gmail → **Save**
   (the app stays in **Testing**; only test users can sign in).
4. **Clients** → **Create client**, three times:
   - **Web application**, name `Mobilegame Web (Supabase)`, under
     **Authorized redirect URIs** add
     `https://kugytvcupelitbaksotg.supabase.co/auth/v1/callback` → **Create**.
     Copy the **Client ID** and **Client secret**.
   - **Android**, name `Mobilegame Android`, package `ro.docvex.mobilegame`,
     SHA-1 from step 1 → **Create**. (Later also add the release key and the
     Play App Signing key SHA-1s as separate Android clients.)
   - **iOS**, name `Mobilegame iOS`, bundle ID `ro.docvex.mobilegame`,
     Team ID `9FPB67U7DJ` → **Create**. Copy its **Client ID**.

## 3. Supabase dashboard

https://supabase.com/dashboard/project/kugytvcupelitbaksotg/auth/providers

1. **User Signups**: "Allow anonymous sign-ins" ON and **"Allow manual
   linking" ON** → **Save**.
2. **Auth Providers** → **Google** → enable:
   - **Client IDs**: `<web client id>,<ios client id>` (comma separated, Web first)
   - **Client Secret (for OAuth)**: the Web client secret
   - **Skip nonce check**: leave **OFF** — both apps send a hashed nonce
     (the iOS flow here does not use the GoogleSignIn SDK, so it can).
   - **Save**.

## 4. App config

In `config.local.h` (git-ignored):

```c
#define MG_GOOGLE_WEB_CLIENT_ID "<web client id>"
#define MG_GOOGLE_IOS_CLIENT_ID "<ios client id>"
```

Rebuild iOS with `MG_IOS_TEAM_ID=9FPB67U7DJ tools/run_ios.sh`.

(History: Android was dropped on 2026-11-14 and its CI build removed; the steps below no longer apply.)
For the Android CI build, add repository secrets (GitHub → Settings → Secrets
and variables → Actions → **New repository secret**):

- `MG_GOOGLE_WEB_CLIENT_ID` = the Web client id
- `MG_ANDROID_DEBUG_KEYSTORE_B64` = contents of `debug.keystore.b64`

Push to main; the `android-latest` release then signs with that key.

## 5. Check

- Settings → Skin → **Continue with Google** → pick your test account →
  "Signed in as <name>". Supabase → Authentication → Users shows the same
  user id as before, now with a Google identity; Table Editor → `profiles`
  has no extra row.
- Delete the app, reinstall, Continue with Google → your name, skin and stats
  come back (the fresh guest made by the reinstall is deleted).
- If nothing shows up in Supabase → Logs → Auth when you sign in, the problem
  is on the Google side: wrong SHA-1 (Android), wrong client id type
  (Android must use the **Web** id), or your account is not a test user.
