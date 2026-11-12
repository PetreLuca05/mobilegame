# LIB-1 (complete), with the accepted changes

The product owner's complete LIB-1 text, kept in the repository as the reference. Changes accepted after it was written are
edited in and marked `[changed: ...]`; the list below says what each is and why.

## Accepted changes

- **Square hall** (user decision): the four bays stand in a 2 × 2 square (32 × 32 m) instead of a row (64 × 11 m). Each room is
  16 × 16 m (5 m more open floor in front); the front row's back wall with its doors parts the rows; the back row's doors lead outside.
  `mg-library 10`, exported by `tools/library_prototype.html`. Pad 15 × 15 on both islands. The menu camera backs off until the table
  fits (11.8 m), so with my table in the back row the front rooms dither out while the eye is behind the middle wall and below the wall
  tops, and back in above them (the Maps move); nothing else is skipped.
- **Jump rule** (accepted): camera distances per second, steps of at least half a frame, no judging across a picture capture; a stale
  eye injected into one Back must be caught (§15.11). Why: the old rule in metres flagged the smooth move from an island's overview
  (800 m out) and float noise over sub-frame steps.
- **Prototype replica** (user decision): the hall, its lighting (two-band toon ramp, sun 0.5 + ambient 0.5) and its behaviour follow
  the product owner's prototype; avatars without outlines, cel shading and smooth faces kept.
- **Erase progress** (user decision): New Game left the main menu; Settings -> Account -> "Erase progress" (destructive, confirmed)
  resets the slot, every island's ambulance and the last island; Maps opens the island overview when no hospital was played.
- **Memory baseline**: step 5 = 180 MB, step 6 (map preload) = 191 MB; the menu's allowance is +16 MB over step 5.
- **Rooms estimate**: 33.3 k triangles (the back rooms show through the open doors), in place of ~18 k.
- **Locker camera** (step 8, user decision 2026-11-12): a fixed 1.0 s quintic that starts and lands at rest, in place of the literal
  5 /s exponential (which starts at full speed on its first frame: a jump by §15.11). The no-jump rule wins.
- **Menu camera refit** (step 8, user decision): the fit's change (the free rect moving: the lobby panel, another screen's column) uses
  the same kind of curve, from rest to rest.
- **Sign-in during the walk-in** (step 8, user decision): a seed that arrives while my table is in view and I am alone does not re-dress
  it on screen; the new dressing applies once the table is out of view or someone joins.
- **First-launch bay** (step 8, user decision, with a condition): before any sign-in every fresh install walks into the same bay. When
  the account's seed later puts my table in a different bay, the change happens only when the menu is next opened from a cold start or
  from a game, never while the library is on screen (the library test signs in on a fresh install, stays 30 s with nothing moving, then
  reopens the menu on the seed's bay with no pop).
- **Triangles at rest** (user decision, 2026-11-12): **≈ 58 k** for the square hall in place of ≈ 26 k, stop line **75 k**
  (`tools/perf_gate.py`, `menu`). Measured with the wall-aware bay rule (93d2a65): 57.9 k average (61.1 k without it). No doorway
  culling of people and particles. Why: 57.9 k is below the old hospital's `stress200` scene, which passed every iPad budget at 87 k
  and more; draws, allocations and memory are within budget; the 26 k figure was an estimate for the first small room, written before
  the hall became four rooms. The iPad GPU trace at the combined gate decides whether the menu needs more work.

# LIB-1 (complete) — library hall: new avatar, living library, unique lobbies, walking between lobbies, zoom to the map

This one prompt **replaces every earlier LIB-1 text** (the original, and amendments A1–A8). If you have already built part of it (for example the avatar), keep that work and bring it in line with this text; report anything you had to change. Build only what is here, then STOP and report.

Perf runs are **Mac only** for this milestone; iPad numbers are taken in the one combined gate afterwards, as agreed.

## 0. Files and ground rules

Four files will be in the repo before you start. If any is missing or has a different format, stop and ask. Do not reshape meshes by hand; read every position, duration and angle from the JSON.

- `assets/avatar/avatar.mgmesh` and `assets/avatar/avatar.json` (`mg-avatar 1`)
- `assets/menu/library.mgmesh` (34,058 triangles) and `assets/menu/library.scene.json` (`mg-library 9`)

Conventions: metres, y up, +z towards the menu camera, yaw 0 faces +z and 90 faces +x. "Seat frame" = origin at the seat, forward +z, the sitter's left is +x. Alpha groups in the mesh are `beam`, `tank_glass`, `lamp_glow`, `lamp_cone`, `table_lamp_light` (alpha-blended, values in `life.alpha`) and `phone_glow` (additive). Everything in §3–§10 is local decoration: each device runs it itself, nothing is replicated, and it never delays a network action.

In `life.plane` ignore the `out` / `in` arrays (old); the plane paths are defined in §8. `life.lamp_motes[0]` is the template for table lamps and `[1]` for floor lamps; only their `centre` changes per lamp. `life.beam` is written for a window at x −1.75 in bay 1: shift it by (bay x + `window_x` + 1.75) for each bay.

## 1. The avatar (step 0, before the library)

The avatar in the prototype replaces the model and motion of **every player and NPC in the game**: hospitals, menu, onboarding, Locker, lobby, previews, HUD heads. Still limbless; gameplay, collision capsule, speeds, replication and `profiles.skin` are unchanged.

- **Parts** (`avatar.mgmesh`, all white, tinted per instance): `avatar_body` (origin at its base), `avatar_head`, `avatar_hair` (head space), `avatar_hand` ×2, `avatar_eye` ×2, plus `_lod1` versions of body, head, hair, hand. 570 triangles at LOD0, 154 at LOD1 (no eyes); LOD2 = the existing far representation, re-tinted; keep the PERF-1 LOD distances.
- **Assembly**: body base at `stand_y`; head centre 0.86 m above the base along the body's own up axis (it moves forward when the body leans); eyes at ±0.095, 0, 0.225 in head space, dark, unlit; hands rest at x ±0.34, 0.28 above the base, z 0.04. Body = suit colour, head and hands = skin, hair = hair colour. Match `shading` so avatars read as in the prototype.
- **Creator options keep their counts and saved values** (body 2, skin 8, hair 6, hair colour 8, suit 8, extras 5). Re-author them on the new base in the same faceted style: body type 1 = the same profile with the widest ring 8 % narrower and the top ring 6 % wider; the exported cap is hair style 0, rebuild the other five as low-poly caps (≤ 120 tris each) with today's silhouettes; suit details as vertex colours on the body; extras re-fitted to the 0.25 m head. Deliver a sheet of all options in `docs/autotest/avatar/options.png`.
- **NPC age classes**: child = whole avatar × 0.72, head × 1.18; elder = 0.12 rad forward lean, grey hair, glasses / cane re-fitted.
- **One pose function** for everyone, driven by `move` (0–1, eased at 12 /s), `sit` (0–1) and optional action weights; values in `avatar.json`:
  - idle: base bobs ±1.5 cm at 2.1 rad/s (random phase), body y-scale breathes ±1.2 % at 1.6 rad/s;
  - walk: phase 9 rad/s × move; extra bob 3.5 cm × |sin|; hands swing ±16 cm in opposite directions; 0.05 rad forward lean; heading turns to the travel direction at rate 10 /s, never snaps;
  - sit / stand: smoothstep blend over `sit_s` / `stand_s`;
  - standing actions (browse, push, look and the rest in §9) blend the standing hands and head; seated actions are in §3–§6.
- Every existing avatar state (elevator ride, TALK head pop, greeting, pushed squash, child smile, queue, escort…) is kept and re-targeted. List every state in `docs/AVATAR.md` with its mapping before coding; where no new animation exists use the closest pose (the head pop must stay); report anything that cannot be mapped.
- Everything that draws an avatar uses the new parts: the instanced path, the x-ray silhouette, the preview panel and NPC head bubble, the Locker, lobby and friends head icons, name-tag heights (`name_tag_y`).

## 2. The hall

The main menu and onboarding background is a library **hall of four bays in a 2 × 2 square** (`hall`: 16 m pitch, bay origins `bx` / `bz`, 32 × 32 m; front row bays 0 1 with no wall between them, back row 2 3 behind the front row's back wall; each bay 16 × 16 m, its back wall at local z −5.5 and open floor to +10.5) [changed: square hall]. The old hospital menu background is removed. Menu layout and UI do not change.

- Mesh: `room_0`…`room_3` are the bays, already placed; cull them, and each bay's people and props, by bay. `shell` (front wall and roof of the whole hall) is not drawn in the menu.
- Each bay has, at the same local place: a study table with four seats (`seats[]`, local to a bay, each with `stand`), a door (`door`, local; one `door` mesh per bay, hinge at its origin), three double bookcases, wall shelves, a sunlit window bay with window seat at `bays[i].window_x`, a ladder at `ladder_x`, round reading tables (`round`, `round2`) and four prop spots (`spots`) holding what `bays[i].ends` lists. Bay `hall.home` (1) is the one with the grandfather clock, aquarium, skeleton and librarian's desk.
- `obstacles` (81 rectangles) and `nav` (0.25 m cells, 128 × 136; the wall between the rows is open only at the front row's doors, the outside strip only through the back row's doors) cover the whole hall; see §7 [changed: square hall].
- Menu camera: `camera.menu` relative to the current table's bay (target, distance, pitch, yaw, FOV 32°). Increase the distance when needed so the table and its four seats fit in the free rect between the menu column, the lobby list and Continue.

## 3. Sitting and studying

Seated pose (`study`): body base at `seat_y` (the body-base height when seated), leaned forward `lean`, head a further `head_pitch` down, left hand on the left page, right hand on the right page. Each seat has in front of it (at `book.forward`): an open book (`book_open`, which includes a notebook and lies with the pencil at `fidget.notes.pencil_rest`), the two leaf halves, and a mug at `life.mug.rest`.

Loop per reader, own phase:

1. **Read** for a random `read_s`: head yaw eases from +`head_yaw` (left page) to −`head_yaw`; head pitch adds 0→0.05 rad once per page; body yaw = 25 % of head yaw.
2. **Turn** for `turn_s`, u = 0..1:
   - Leaf angle θ = π·smoothstep((u − `leaf_start`)/(`leaf_end` − `leaf_start`)) about the spine, from the right page (0) to the left (π). `book_leaf_a` is the inner half (angle θ), `book_leaf_b` the outer half (θ − `leaf_lag`·sin θ, pivoting at the end of the inner half). Both carry the page's text lines on both faces, so the text travels with the page; at u = 1 reset θ to 0 (invisible).
   - Right hand: until `reach_end` it eases to the leaf's outer edge (5 cm lift mid-way); then it stays on the leaf tip (+ `grab_lift`, at z = `grab_z`) until θ = `release_theta`; then eases back with a 6 cm arc.
   - Left hand: between u 0.45 and 0.85 lifts 7.5 cm and moves 6 cm outward, then pats down 1.2 cm at u 0.86–1.
   - Head: follows the hand right (−`head_yaw` − 0.1) by u 0.3, sweeps to +`head_yaw`, lifts 0.07 rad mid-turn.

## 4. Players carry their things

Empty seats are bare.

- A walking player carries a closed book (`book_closed`, 7.5 cm under the right hand) and the mug in the left hand, **held by its handle**: mug position = hand − `life.mug.grip`. Hands at `study.items.carry_right` / `carry_left` (x, height above the body base, z), no swing.
- After the sit blend comes **settle** (`place_s`, progress p): hands start at `seated_left` / `seated_right`; until p = `hands_arrive` they move to the mug's place (+ `grip`) and to `book_hand`; at p = `swap_at` the carried items vanish and the open book, mug and pencil appear on the table, the book popping from 30 % to full width over the next 0.15 of p; from p = `hands_leave` the hands go to the pages. Studying starts at p = 1.
- **Pack** is the reverse over `pack_s`, then stand and walk carrying both. A phone on the table is taken without an animation.
- Players already seated when the menu opens have their things on the table.

## 5. Every lobby is unique (`lobby_dressing`)

A lobby is a table. Its look is a pure function of its **seed** = FNV-1a 32-bit hash of the host's user id string, so it is the same on every device. Generator: `s += 0x6D2B79F5; t = imul(s ^ (s >>> 15), 1 | s); t = (t + imul(t ^ (t >>> 7), 61 | t)) ^ t; return ((t ^ (t >>> 14)) >>> 0) / 4294967296`, `s` starting at the seed. Draw in this order:

1. **Seat order**: Fisher–Yates on [0,1,2,3] from the end (i = 3..1: j = floor(next × (i + 1)), swap). Member n (host first, then by join time) sits in seat `order[n]`.
2. **Book covers**: per seat in that order, palette index floor(next × 9).
3. **Lamp**: `lamp_anchors[floor(next × 3)]`, yaw (next − 0.5) × 0.5. `table_lamp` plus `table_lamp_light` (light pool and a round cone with a brighter core) at the same place.
4. **Clutter**: `anchors` farther than `clear` from the lamp, shuffled; `pool` shuffled; count = `count[0]` + floor(next × (`count[1]` − `count[0]` + 1)); item k = pool[k] at free[k], moved (next × 2 − 1) × `jitter` in x then z, turned next × 2π. `globe` spins at `life.globe.spin_rad_s`.

**Which table**: bay = seed mod 4. My own lobby is at my seed's bay. If a lobby I join maps to the bay I am in, use the next bay. A table with no lobby is dressed from seed 9000 + 131 × bay and has nobody at it. `tables_now` holds the expected result for four seeds: reproduce it exactly in a unit test.

## 6. Life at the table (`life.fidget`)

Per seated reader (players and round-table readers) a timer of `every_s` runs while reading only. When it fires, take the next action from a **shuffle bag** of `bag` (reshuffle when empty; a new bag may not start with the action just played); reading pauses for `seconds[action]`. Nothing starts during a page turn, settle, pack or another action. "Bell" = in over the first part, out over the last part, smoothstep.

- **sip** (v = 0..1): 0–0.15 the left hand goes to the mug's handle; 0.15–0.85 the mug travels with the hand towards `mouth_hand` with up = smoothstep((v − 0.15)/0.25)·(1 − smoothstep((v − 0.6)/0.25)), tilting `sip_tilt`·up towards the face while staying attached at the handle (with tilt a: mug = hand − (grip.x, grip.y·cos a, grip.y·sin a)), head lifting 0.5 rad·up; 0.85–1 the hand returns. Steam: 3 puffs rise from every mug standing on the table (`steam_*`), none while it is carried.
- **notes**: right hand to `notes.hand`, writes `lines` lines (sweep `sweep_x` per line, step `line_step_z`, fast jitter); the pencil rides in the hand (`pencil_in_hand`, tilt `pencil_tilt`); head turns `head_yaw`.
- **phone**, **buzz**, **call**, **decline** (`fidget.phone`). The phone is either in the pocket (not drawn) or lying on the table at `table` (screen up). If the table is involved the right hand is used, otherwise a random hand (x values mirrored). A hand picks it up from `pick_above` over the table spot or from `pocket` (under the table); the phone slerps between its lying and held orientation.
  - phone: `phases` t0..t3: hand to the phone, up to `hold`, use (±6 mm jitter), back, hand to the page. Body back `lean`, head `head`, head yaw `head_yaw` × side. At the end it goes on the table with probability `leave_on_table_chance`, else the pocket.
  - buzz (only if on the table; otherwise play call): the phone rattles in bursts (`shake_*`) with the screen light up at `ring_glow`; the player looks over (`look_yaw`).
  - call (`call` t0..t4): rings until t1; hand to the phone, t1→t2 to the ear (`ear`), talks until t3 (head up `call_head`, lean `call_lean`, three white dots bouncing above the head), t3→t4 phone back where it came from.
  - decline (`decline` t0..t2): rings; hand to the phone, one 4 cm press, ringing stops, hand back, small quick head shake.
  - Screen light: `phone_glow`, a child of the phone, additive at `glow_strength`, no depth write; its colours already fade to nothing at the far end; its length scales with how far the phone is raised.
- **stretch**: both hands to `stretch_hands`, lean `stretch_lean`, head `stretch_head`, bell 30 % / 30 %.
- **think**: left hand to the chin (`think.hand_left`), head `head_yaw` / `head_pitch`.
- **glance**: head lifts `glance.head_pitch` and turns a random ±`glance.yaw`.
- **rage** (once per bag): until `crumple_until` the right hand scrunches a page (fast shake) and a paper ball appears in it; winds back to `wind`; at `release_at` snaps to `release` and lets go; head shakes ±`head_shake`; the `anger` mark pulses above the head (`mark`, `mark_scale`, ±20 % at 14 rad/s). The ball flies in a random direction (`speed`, `up`, `gravity`), bounces (`bounce`) off floor, table tops, walls and obstacle rectangles below 2 m, then rests; at most `max` balls, each gone after `life_s`.
- **pet**: only when the cat visits (§10).
- **throw**: only for paper planes (§8).

## 7. Walking: paths, and nobody through anybody

- Bake a walkability grid from `nav` and `obstacles`: a cell is blocked when its centre is inside a rectangle grown by `nav.radius`, outside the hall, or behind the back wall outside a bay's door corridor (`door_x`, local).
- Every walk (people, players, the cat's non-hop legs) is an A* path on it (8 neighbours, no corner cutting, string-pulled) through the existing path queue. If start or goal is in a blocked cell, path to the nearest free cell and walk the last stretch straight.
- **Agents**: every standing or walking person (radius 0.3), every cart (0.42, ignored by its pusher), the cat on the floor (0.2). Seated people are covered by furniture rectangles.
- **Hard rule**: never take a step that brings two agents closer than 96 % of the sum of their radii (unless already that close and moving apart). A cart pusher also checks the cart's place 0.75 m ahead.
- **Steering**: when another agent is within radii + 0.8 m and not behind, add a sideways component away from it (weight 1.6 × closeness; dead ahead: both turn to the same relative side). Try the steered direction, then straight, then a side step; steered steps stay on free cells except on the last stretch.
- **Blocked**: stand still in the idle pose. After 1.2 s ask for a new path with agents within 2.5 m stamped in as temporary obstacles (radius + 0.35 m); after 4 s ignore other agents for 1.2 s. If someone stands on my stop when I am within 0.85 m, I stop there.
- Use the existing M7 steering and spatial hash; the numbers above are the tuning.

## 8. Lobbies in motion (`travel`)

**I join a lobby** from the menu (my own table has no other players, as today):
1. At once: the network join as today; the destination table is dressed from the host's seed and its members are seated there with their things; my lobby list shows the host's lobby.
2. My hero packs, stands and walks a normal path through the hall to its seat at that table.
3. When the hero is off screen (screen x beyond ±`offscreen_at`): the camera travels along the hall to that table's framing (`pan_s_per_bay` per bay, at most `pan_s_max`, quintic ease t³(t(6t − 15) + 10), distance widened up to `zoom_out` mid-way), and the hero moves at `offscreen_speed` **only while off screen**.
4. The hero walks into frame, sits and settles. One unbroken shot: no cut, no fade.

**Leaving** a host's lobby (Ieși or kicked) is the same trip back to my own table; the members I leave stay seated until their table is out of view.

**A friend joins the lobby I am in**: their avatar starts at the stand point of seat 0 of **their own table** (their seed's bay; next bay if that is this table's), carrying book and mug, and walks here; off-screen speed rule as above. **A friend leaves or is kicked**: pack, stand, walk back towards their own table; remove them once off screen and more than `despawn_beyond_m` from the camera's focus, or on arrival. Members already there when I open the menu are seated.

**Paper planes** (decoration; nothing waits for them): sending a lobby invite or friend request plays `throw` (right hand back to `throw_wind`, flick to `throw_release` at `release_at`); from the hand position q and direction d towards the invited friend's bay (+1 right, −1 left) the `paper_plane` flies the Bézier q, q + (0.3d, 0.7, 1.8), (q.x + 6d, 2.6, 2.4), (q.x + 13d, 2.2, 1.4) over `out_s`, nose along the path, rolling ±0.3 rad. An incoming invite shows its panel immediately as today; a plane flies the mirrored path in over `in_s`, lands in front of my seat (seat frame 0, 0.79, 1.1), my hero glances at it, and it shrinks away when the panel closes or after `rest_s`. The cat, when awake, watches a flying plane.

Rules for all of §8: if the host starts the game mid-move, cut it short and load; Maps or a full-screen page finishes it instantly; Animații interfață = Reduse replaces camera travel with a 300 ms cross-dissolve and seats players directly.

## 9. Background people (`npcs`, 25 in the hall)

Each has a `route` of stops or is `seated` at a round table (`sleeper` flag for the sleepers). Route step: walk to `p` at `speed`; wait `wait_s` (−1 = forever) turned to `face_deg`; `outside: true` hides the NPC while waiting (door users); `book: true/false` sets whether it carries a book (a `book_closed` under the left hand at `carry_book`) from that step on; `leave_cart` parks the cart. Doors (rate 8 /s to `open_rad`) open for any walking person within `open_radius`; only background people use them. Acts, with q = 0..1 through the wait:

| act | During the stop |
|---|---|
| `browse` | right hand to the shelf (−0.16, 1.2 ± 0.1 at 1.3 rad/s, 0.44), head up 0.2 rad with a slow ±0.25 rad yaw |
| `read` | shelf reach until `read_standing.from`, then an open book (`book_open` at `book_scale`, tilted) held at `book` with both hands at `hands`, head down `head_pitch`, put back after `to`. With `own: true` the book is held for the whole stop |
| `shelve` | right hand alternates between `shelve.low` and `shelve.high`, period `period_s` |
| `ladder` | rises `ladder.height` while q < `up_until` with alternating hands, browses at the top, comes down from `down_from` |
| `take` | browse; from q = 0.7 it has a book |
| `checkout` / `return` | both hands at `desk.hands`, book flat at `desk.book`; `return` leaves it at q = 0.55 |
| `stamp` | right hand at `stamp.hand` dipping `dip_m` every `period_s` (dip = max(0, sin)^6), head down |
| `talk` (`turn`) | pair facing each other; speaker changes every `talk.turn_s`; speaker's hand gestures, head nods, three white dots above the head |
| `look` (`pitch`) | leans `look.lean`, hands behind the back, head pitched by `pitch` |
| push (`push_cart`) | both hands on the handle, cart 0.75 m ahead |

Seated readers run §3 and §6 with their own `read_s`. Sleepers (`life.sleeper`): leaned onto the table, head rolled, slow deep breathing, three `zee` meshes (with their dark backing) rising, growing 0.6×→1.8× and fading in the last 20 %.

People in bays that are not visible tick at 1/6 rate and are not drawn.

## 10. The room is alive (`life`)

- **Clock** (bays that list one): hands show the device's local time; pendulum ±`pendulum_rad`, period `pendulum_period_s`.
- **Skeleton**: hangs from `hang_y`, sways ±`sway_rad` at `sway_rad_s`, plus ±0.06 rad about y.
- **Aquarium**: 5 instanced `fish` tinted from `fish_colours`, x = centre + a·sin(w·t + φ) (a 45–95 % of the half-width, w 0.35–0.85, seeded), ±1.5 cm bob, turning with yaw = π/2·(1 − tanh(5·cos(w·t + φ))); 8 bubbles rise and loop.
- **Sunbeam** per bay: 30 dust motes drift from the window to the floor (100–280 s per pass), wandering ±5 cm.
- **Lamps**: floor lamps (`lamp_glow`, `lamp_cone`) and table lamps have a light pool, a round cone with a brighter core, and dust falling slowly inside the cone (`lamp_motes`).
- **Particles**: dust, bubbles, steam, talk dots, hearts and paper balls are one instanced draw of `puff` (opaque, unlit, tinted, scaled); cap 320 live, only for visible bays.
- **Alpha**: the static alpha groups merge into one alpha mesh per visible bay; table-lamp light and `phone_glow` are instanced. No depth write, drawn after opaque geometry (fish before the tank glass).
- **Cat** (`life.cat`; `cat_body`, `cat_head`, `cat_tail` ×3 chained): follows `route` (points local to the home bay) at `speed`; `hop` steps jump over `hop_s` with an arc `hop_arc_m`; a step with `wait` holds in `pose` facing `face_deg`. `poses` give body height and pitch, head position and yaw, tail base, lift, curl and swish per pose; blend at 6 /s. Loop: sleeps on the rug; **visits a random seated player** (`approach` → `visit` → `approach`: trots to the table edge nearest them, hops to `visit.table_forward` in front of their seat, sits facing them); watches the clock's pendulum (head follows it); naps on the window seat; visits again; back to the rug. `pet_delay_s` after it sits, that player plays **pet** (`fidget.pet`: right hand strokes ±`stroke_m` at `stroke_rad_s`, leans in); the cat squashes with the strokes, lifts its head, and `hearts` pink particles rise. It walks to whichever table the players are at.

## 11. Onboarding and Locker

Onboarding screens use the hall at my table. On the first launch the hero comes in through its bay's door carrying book and mug, once. Locker / creator: the camera eases (5 /s) to `camera.creator` framed on the hero's actual seat and facing it; the hero sits up (study blend 0.15), looks at the camera, both hands on the table; the loop pauses. Closing reverses both.

## 12. Maps: one camera move, no cut

The library stands on the island at `lib_scale` (0.35 map units per metre: 11.2 × 11.2 units) [changed: square hall]. Add `library {x, z, yaw_deg}` to each island JSON by rule and commit it: the point nearest `start` whose 15 × 15 pad is ≥ 30 units from every hospital plot centre, ≥ 8 from the road, on the grass plateau, front facing the road; remove props under the pad; add a 3.4-wide path to the road. Re-do this for both islands (the building is longer than before).

On **Maps**: c0 = current menu camera (already a map-space camera through the library transform), c1 = the camera the map scene opens with. With t = elapsed / `transition.duration_s` and e = t³(t(6t − 15) + 10): distance = exp(lerp(ln d0, ln d1, e))·(1 + `hump`·sin(π e)); pitch and yaw lerp by e; target = lerp(T0, T1, w(e)) with w(e) = ∫₀ᵉ distance ÷ ∫₀¹ distance from a 65-sample table; near = 0.05 × distance. `shell` dithers in with k = smoothstep((e − 0.10)/0.20); at k = 1 stop drawing and simulating the inside. The map scene is preloaded (time-sliced) when the menu opens and drawn from the first frame of the move; the Maps row is disabled until ready. Back runs it reversed; New Game uses c1 = the `BOOKS` overview. Reduse: 300 ms cross-dissolve.

## 13. Sound (procedural, menu only, under the UI sound toggle)

Clock tick 1.8 kHz 12 ms gain 0.012 once a second (visible clocks only); page turn (my hero) filtered noise 70 ms gain 0.03; stamp 140 Hz 40 ms gain 0.015. Silent while a modal is open.

## 14. Performance (PERF-1 rules apply)

- At rest (one bay and slivers of its neighbours): **≤ 46 draws, ≈ 26 k triangles** [changed: ≈ 58 k for the square hall, stop line 75 k]; today's `menu` scenario is 76 draws. During travel (two to three bays): **≤ 60 draws, ≈ 45 k triangles**. During the Maps move add one island (≤ 16). One pass throughout.
- Estimated on the iPad Air 4: GPU ≤ 1.7 ms at rest, ≤ 2.6 ms travelling or zooming; tick ≤ 0.15 ms (25 people with at most ~10 at full rate, one cat, fidget timers, closed-form props); nav bake < 4 ms at load; memory ≈ + 16 MB (hall + preloaded map). Avatar: 570 / 154 triangles, `stress200` must stay within budget (shorten the LOD0 distance first; do not change the model).
- Zero heap allocations per frame: fixed pools for particles (320), balls (6), paths. Dressing a table happens once per lobby change, off screen.
- If a Mac measurement is more than 2× an estimate, stop and report.

## 15. Autotests

`--autotest avatar`: assembly at lean 0 and 0.30; walk bob, opposite hand swing, 90° turn in 0.2–0.5 s without snapping; 200 sampled creator combinations with no missing or detached part, option sheet written; old `profiles.skin` rows load; age classes; remote player matches local; silhouette, previews and head icons use the new head; all earlier autotests pass.

`--autotest library` (debug hooks: `--debug turn-page`, `--debug fidget <name>`, `--debug phone-place <table|pocket>`):
1. Menu opens at my table: hero seated in its seeded seat with book, mug, pencil; each bay shows what `bays[i]` lists.
2. Page turn: θ rises 0→π; right hand within 2 cm of the leaf tip while θ < `release_theta`; text-line count on the leaf constant; leaf lines coincide with page lines at θ = 0 and π.
3. Dressing: the four seeds in `tables_now` reproduce exactly; 1,000 random seeds: nothing within `clear` of the lamp or on a seat's book, notebook, mug or phone place; 3–5 items; ≥ 200 distinct layouts; two devices show the same table for the same lobby.
4. Items: empty seats bare; walking players carry both; on the table within `place_s` + 1 frame of sitting; the hand is within 1 cm of the mug's handle whenever the mug is carried.
5. Fidget bag, 1,200 draws: every action appears, `rage` once per bag, no repeat across a bag boundary. Each action forced: hands back on the pages at the end; phone, glow, anger mark and pencil appear only in their action; phone ends on the table 40–60 % of 200 uses; `call` returns the phone where it started; `decline` stops the rattle on the press; balls come to rest inside the hall outside every obstacle.
6. Walking, 5 minutes, whole hall: no two agents closer than 90 % of their radii sum; no walker in a blocked cell except the last stretch; nobody blocked over 6 s; every NPC completes two loops and the cat one; each act seen at least once.
7. Joining (two instances, A joins B): A's camera does not move until A's hero is off screen; then travels with no jump and no blank frame; A seated at B's table within 12 s; both see the same seats, lamp and clutter. Ieși: A ends alone at its own unchanged table. B sees A arrive from the side of A's bay, never from a door, carrying book and mug.
8. Off-screen speed only while off screen; a leaving friend is removed only after leaving the frame.
9. Planes: the invite RPC fires on the tap frame and the panel shows on the arrival frame; the plane exits on the side of the friend's bay.
10. Cat: over 20 loops every seated player is visited; pet starts within `pet_delay_s` + 0.5 s; hearts only while petted.
11. Maps and Back: camera ends exactly on the target camera at `duration_s` ± 1 frame, no jump, no blank frame; host start, Maps and Reduce Motion mid-move end clean. **No jump** [changed: jump rule]: speed is measured in camera distances per second (metres per second ÷ the camera's distance to its target); a frame's speed may not exceed 1.5 × the previous judged frame's + 0.06; a step is judged only once it spans at least half a frame of the move's clock; steps across a picture capture are not judged (the first step after one only sets the baseline). The test also injects one stale eye into a Back and must see it caught.
12. Clock within one minute of device time; fish stay in the tank; particles ≤ 320; draws ≤ 46 at rest, ≤ 60 travelling; `--autotest layout` passes on the iPhone preset and iPad.

Screenshots → `docs/autotest/avatar/`, `docs/autotest/library/`.

## 16. Steps (commit after each; wip commit before destructive git operations)

0. Avatar: `docs/AVATAR.md`, parts and LODs, pose function, creator options and sheet, everything that draws an avatar; `--autotest avatar`; Mac perf for `stress200`, `lobby4`, `interact_npc`.
1. Load the library files; hall drawn per bay with the menu camera; old menu background removed.
2. Seats, study loop, leaf, mug, pencil; carried items, settle and pack.
3. Lobby dressing generator and unit test; table assignment.
4. Nav grid, steering and the no-overlap rule; background people and all acts; doors.
5. Table life: the fidget bag and every action including the phone.
6. Room life: clock, skeleton, aquarium, sunbeams, lamps, particles, alpha, cat with visits.
7. Lobby wiring: travel for me and for friends, leaving, paper planes.
8. Onboarding and Locker.
9. Library placement on both islands, map preload, the Maps / Back / New Game move, shell fade.
10. Sound; autotests.
11. **Perf audit (Mac)**: `--autotest perf` for all scenarios, `menu` re-baselined on the hall with four players, new `menu_travel` (trip there and back, looped) and `menu_to_map`; compare against `docs/perf/after/`; FAIL on any exceeded budget or > 10 % regression; update `docs/perf/` and `docs/PERF.md`. iPad numbers are deferred to the combined gate.
12. Update `CLAUDE.md`. Report PASS / FAIL / NOT VERIFIABLE per checklist line. STOP.

## 17. Done checklist

- [ ] Every player and NPC everywhere uses the new avatar and its motion; all creator options and saved looks work; every earlier avatar state is mapped.
- [ ] Menu and onboarding show the four-bay library hall; only visible bays are drawn; nothing about it is hard-coded.
- [ ] Readers study with pages turning, text staying on the page, hands following the leaf.
- [ ] Players carry book and mug, set them down and take them away; empty seats are bare.
- [ ] Each lobby's seats, covers, lamp and clutter come from the host's seed and match across devices.
- [ ] Seated players sip by the handle, write, use the phone (pocket or table, buzz, call, decline), stretch, think, glance and sometimes throw a crumpled page with the anger mark.
- [ ] Nobody walks through furniture or through each other; background people shelve, climb, borrow, return, chat, look and read.
- [ ] Clock, skeleton, aquarium, sunbeams, lamps with dust, steam and the cat (with visits and petting) all behave as specified.
- [ ] Joining and leaving lobbies is a walk across the hall for me and for friends, with the camera travelling in one unbroken move; paper planes accompany invites without delaying them.
- [ ] Maps, Back and New Game are one uninterrupted camera move; the library stands on both islands.
- [ ] `--autotest avatar`, `--autotest library`, `--autotest layout` pass on the Mac.
- [ ] `--autotest perf` within budget on the Mac; `docs/perf/` updated; iPad deferred to the combined gate.
- [ ] Desktop + iOS build warning-free; `CLAUDE.md` updated.
