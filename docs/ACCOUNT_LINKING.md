# Account linking (LINK-1)

Confirmed against the source, not the docs summary: `supabase/auth` master (`internal/api/token_oidc.go`, `identity.go`, `api.go`, `apierrors/errorcode.go`) and `supabase-js` master (`auth-js/src/GoTrueClient.ts`, `linkIdentityIdToken`), read 2026-11-09.

## Request shapes

Link an ID token to the signed-in guest:

```
POST /auth/v1/token?grant_type=id_token
apikey: <publishable key>
Authorization: Bearer <guest access token>
{"provider":"google","id_token":"...","nonce":"<raw nonce>","link_identity":true}
```

`access_token` is optional (it is only checked when sent). A missing `Authorization` header is refused. Success is a normal session; the user keeps its id and `is_anonymous` becomes false (`linkIdentityToUser`).

Sign in to an existing identity: the same grant without `link_identity` and without the guest Bearer.

## Errors

Bodies are `{"code":422,"error_code":"identity_already_exists","msg":"..."}`. The client reads `error_code`. `identity_already_exists` is the Case B signal; it is also returned if the identity is already on the same user.

## Manual linking

`requireManualLinkingEnabled` only guards `GET /user/identities/authorize` (the OAuth redirect flow), in `api.go`. `IdTokenGrant` does not check it, so the native ID-token link used here does not depend on that setting. `manual_linking_disabled` should not occur on this path.

It is still worth having on, since it is the documented setting and a future OAuth flow would need it: Dashboard -> Authentication -> Sign In / Providers -> User Signups -> "Allow manual linking" ON -> Save. The publishable API does not report its state, and `supabase config push` would overwrite every other auth setting from a local `config.toml` this repo does not have, so it is not done from here.

## Reusing the ID token for Case B

The server verifies an ID token statelessly (signature, audience, expiry, nonce hash); nothing marks it as used. The token from the failed link is therefore valid for the sign-in that follows, until it expires. The iOS bridge does not use the GoogleSignIn SDK, so `restorePreviousSignIn` does not exist here; if the token is rejected the flow fails and the guest is untouched.

## Audit of the M2 linking call

`post_link` **does** send `link_identity:true` with the guest Bearer. The concern that it might have been creating a separate user does not apply: Case A has always linked in place.

The real defect was in the fallback. On any 4xx containing "already", `link_done` signed in to the existing account and set `delete_guest`, which called `delete_anonymous_self` with the guest's token: no confirmation, no merge, and the guest's progress was deleted. It matched on the message text as well. LINK-1 replaces this.

## Project state

Google and Apple are both `false` in `/auth/v1/settings`, and `config.local.h` has no `MG_GOOGLE_*`. Real Google needs `docs/GOOGLE_SIGN_IN.md` sections 1 to 4.

## Tables

`profiles`, `stats`, `friendships`, `player_status`, `lobby_members`, `lobby_join_attempts` are the user-owned tables. The learning, streak, quest, league and cosmetics tables named in the LINK-1 prompt (`learn_progress`, `learn_reviews`, `player_meta` and so on) do not exist yet; the coverage guard makes sure they get a rule when they arrive.

## RLS review for guests

Guests are role `authenticated`, the same as permanent users, and every policy keys on `auth.uid()`: `profiles` (public read, own write), `stats` (own), `friendships` (own, select only). `player_status`, `lobby_members`, `lobby_join_attempts` and `merge_tickets` have no client access. Realtime policies key on the uid and on lobby membership. Nothing should differ for guests, so no policy uses `is_anonymous` and none was changed; the only place the claim matters is the ticket pair, which checks it and re-checks the row.

