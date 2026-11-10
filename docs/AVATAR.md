# Avatar inventory (LIB-1 A1)

What the old limbless avatar was, every state it showed and every place it
was drawn, and how each maps onto the new avatar (`assets/avatar/avatar.json`
+ `assets/avatar/avatar.mgmesh`). Sections 1-7 were written before the model
was replaced; section 8 is what A1 shipped. States that do not map cleanly are marked **NOT CLEAN** and
listed again in section 7.

## 1. Parts, meshes, triangles, LOD

All meshes are built at startup in `game/avatar.c:avatar_meshes_init` with the
`engine/gfx/mesh.c` builders, white (tinted per draw) unless noted. Triangle
counts are exact, from the builders' loops (pole triangles of spheres included):

| Builder (`engine/gfx/mesh.c`) | Triangles |
|---|---|
| `mb_sphere(rings, segs)`, `mb_sphere_cap(rings, segs)` | 2 x rings x segs |
| `mb_tapered_capsule(rings, segs)` | 2 x (rings + 1) x segs |
| `mb_cylinder(segs)` | 4 x segs (side + 2 caps) |
| `mb_cone(segs)` | segs (no base) |
| `mb_box` / `mb_sprite_quad` | 12 / 2 |

### Today's meshes (`AvatarMeshes`, `game/avatar.h`)

| Mesh | Built as | Tris | Used for |
|---|---|---|---|
| `body` | capsule rb 0.25, rt 0.19, h 0.9, 20 x 28 | 1176 | LOD0 body (male) |
| `body_female` | capsule x0.92 / x0.9 / x0.955, 20 x 28 | 1176 | LOD0 body (female) |
| `head` | sphere r 0.225, 20 x 28 | 1120 | LOD0 head |
| `hair[1]` short | cap 12 x 28 | 672 | |
| `hair[2]` bob | cap 14 x 28 | 784 | |
| `hair[3]` bun | short cap + sphere 10 x 14 | 952 | |
| `hair[4]` spiky | short cap + 7 cones x 8 | 728 | |
| `hair[5]` curly | cap 12 x 28 + 16 curls (6 x 10 spheres; 3 of 19 skipped over the forehead) | 2592 | |
| `eye` | flattened sphere 8 x 12, dark | 192 | x2, flat shaded |
| `nose` | sphere 6 x 10 | 120 | nose; also the 3 coat buttons |
| `mouth` | box, dark | 12 | adults/elders |
| `smile` | 3 boxes, dark | 36 | children |
| `brow` | box, dark | 12 | x2, elders |
| `hand` | sphere r 0.07, 10 x 16 | 320 | x2 |
| `decal` | cylinder x 24 | 96 | badge on the chest |
| `cross` / `pocket` | 2 boxes each | 24 / 24 | chest decals |
| `vneck` | body patch 8 x 8 | 128 | scrubs / coat trim |
| `scope` | 3 tubes (spheres 4 x 8 + cylinders x 8) + chest piece cyl x 20 | 1232 | stethoscope (coat or accessory) |
| `glasses` | 2 spheres 8 x 14 + box | 460 | sunglasses / reading glasses |
| `ear` | cone x 4 | 4 | cat ears, x2 |
| `mask` | face patch 24 x 24 + 4 loops 1 x 6 | 1200 | surgical mask |
| `hat` | cyl x 20 + cyl x 16 | 144 | gardener extra |
| `cane` | 2 cyl x 8 | 64 | elder extra |
| `bucket` | cyl x 12 | 48 | orderly extra |
| `shadow` | sprite quad | 2 | contact disc (also markers' disc) |
| `lod_body[0]` / `lod_head[0]` / `lod_hair` / `lod_hand` | capsule 9 x 12 / sphere 9 x 12 / cap 5 x 10 / sphere 4 x 6 | 240 / 216 / 100 / 48 | LOD1 |
| `lod_body[1]` / `lod_head[1]` | capsule 4 x 7 / sphere 4 x 7 | 70 / 56 | LOD2 |

Outlined parts (`avatar.c:outlined`, inverted hull `shaders/outline*.vert.hlsl`)
are drawn twice: LOD0 body, head, hair, mask, cat ears, hands; LOD1 body and head.

### Per avatar (as drawn, `avatar.c:draw_avatar` / `draw_avatar_low`)

| LOD | Parts | Tris (parts) | + outline hull | Submitted |
|---|---|---|---|---|
| 0, default doctor (`AVATAR_DEFAULT_SKIN`: white coat, short hair, mask) | body, vneck, scope, 3 buttons, cross, head, hair, 2 eyes, mask, 2 hands, disc | 6938 | 4808 | **11 746** |
| 0, plainest (plain suit + badge, no hair, no accessory) | body, decal, head, 2 eyes, nose, mouth, 2 hands, disc | 3550 | 2936 | 6486 |
| 0, heaviest player look (coat + curly + mask) | | 8858 | 6728 | 15 586 |
| 1 | `lod_body[0]`, `lod_head[0]`, `lod_hair` (if any), 2 `lod_hand`, disc | 654 | 456 | **1110** (1010 bald) |
| 2 | `lod_body[1]`, `lod_head[1]` (hair colour if it has hair, else skin), disc | 128 | 0 | **128** |
| x-ray (`avatar_draw_with`) | every LOD0 part, no hull, no disc, drawn twice (rim + fill) | 6936 | | 13 872 (default doctor) |

Cross-check: `docs/PERF.md` measured +31.7 k triangles for 3 extra lobby
avatars (~10.6 k each), in line with the LOD0 numbers above.

New avatar (`avatar.mgmesh`, counted from its `t` lines): LOD0 = body 144 +
head 80 + hair 90 + 2 hands x 80 + 2 eyes x 48 = **570**; LOD1 = body 64 +
head 20 + hair 30 + 2 hands x 20 = **154** (no eyes); LOD2 = today's 126 + disc.

### LOD choice

- `AvatarPose.lod` (0 full, 1 coarse, 2 body + head); `draw_avatar` takes
  `draw_avatar_low` when `lod > 0` and no override material is set (the x-ray
  always draws LOD0).
- `game/game.c:crowd_cull_job` (NPCs in `draw_npcs`, stress dummies in
  `draw_crowd`): above the view floor = not drawn; outside the frustum (point
  1 m up, 1.2 x margin) = not drawn; on another storey (below) = **LOD2**; on
  the view floor within `near_m` of the player = **LOD0**, beyond = **LOD1**.
  `near_m` = 22 m, 11 m when thermal state >= 2 (`update_thermal`).
  `--no-lod` (`g->no_lod`) forces LOD0. Run on the job system when >= 100 NPCs.
- Players (local, remote), menu hero, lobby slots, previews, head icons: always LOD0.

## 2. Pose and state fields today

`AvatarPose` (`game/avatar.h`), filled per draw by the caller:

| Field | Meaning | Set by |
|---|---|---|
| `feet` | root position | all callers |
| `yaw` | facing (avatar looks along **-Z**) | all |
| `vel` | velocity: drives stride, swing, lean | player kin / replicated / NPC hot |
| `grounded` | stride advances; off = hands raise | player, `REP_GROUNDED` |
| `jumping` | carried, unused by the draw (`PLAYER_JUMP_ENABLED` 0) | player, `REP_JUMPING` |
| `indoors` | tint alpha 0 = indoor light, 1 = sun | `scene_indoors`, NPC `storey >= 0` |
| `preview` | `GFX_DRAW_PREVIEW` flags | settings Skin preview |
| `flat_ground` | contact disc without a ray cast | NPCs, menu hero, lobby slots |
| `lod` | 0 / 1 / 2 | `crowd_cull_job` |
| `sit` | 0..1: body base -0.25 m, hands forward 0.2 m and 45 % narrower | `draw_npcs` (`POSE_SIT`, `sit_t`) |
| `lying` | 0..1: root +0.62 up, 0.98 forward, pitched 80 deg | `draw_npcs` (`POSE_BED`) |
| `crouch` | 0..1: body base -0.3 m (hands follow) | `draw_npcs` (`POSE_CROUCH`) |
| `sway` | root roll, 0.12 rad x sin(3 t) | `draw_npcs` (`POSE_MOP`) |
| `hide` | screen-door fade of every part | NPC spawn/leave, menu hero proximity, lobby fade, preview crossfade |
| `head_yaw`, `head_pitch` | look / nod | `npc_idle_touches`, preview panel |
| `weight_tilt` | body roll 2 deg x sin(1.3 t) | `npc_idle_touches` |
| `stretch` | 0..1 hands up 0.5 m | `npc_idle_touches` (seated) |
| `head_lift` | metres the head pops up | `npc_idle_touches` (0.12 x `vis_pop`) |
| `head_only` | head + hair + face + accessory only, at `feet` | preview panel, head icons |

`AvatarAnim` (`avatar.c:avatar_animate`, per frame): `stride` (distance,
grounded only; speed capped at 4.5 m/s, `WALK_POSE_SPEED`), `lean_fwd` /
`lean_side` (10 deg x speed / 7, so 6.4 deg max at walk, approach 10/s),
`swing` (speed / 4.5, approach 8/s; 0.3 airborne), `raise` (1 airborne),
`idle_t`. Draw: phase = stride x 2 pi / 1.6 m; walk bob 0.02 m at 2 x phase;
idle bob 0.008 m x sin(2.4 t); the head counter-bobs (-2 x bob); hands swing
+-0.16 m (right x0.6, -0.04 offset).

`AvatarLook` (`game/avatar.h`): `skin, outfit, hair, accent, trim` colours,
`body`, `decal`, `outfit_style` (plain / coat / scrubs), `hair_style`,
`accessory`, `age` (adult / child / elder), `extras` (cane, hat, bucket,
reading glasses).

### Every avatar state

| State | Where | What it does today |
|---|---|---|
| Idle | `avatar_animate`, `draw_avatar` | 0.008 m bob, hands at rest |
| Walk | same | stride bob, hand swing, forward + side lean |
| Sprint | none: `PLAYER_SPRINT_ENABLED` 0, speed clamped to the walk pose | — |
| Airborne / jump | `raise` when `!grounded` (jump off by default) | hands rise 0.5 m and spread 0.08 m |
| Elevator ride | `game/elevator.c` moves the rider with `car_y`; no pose change; `REP_RIDING` only closes doors on peers | idle pose carried up |
| Talk: head pop + turn | `game.c:npc_idle_touches` (`chatting`) | head up 0.12 m, yaw to the player +-70 deg, "..." cloud (`draw_chat_bubbles`) |
| Greeting nod | `npc_idle_touches` (`talk_t`, set by `on_npc_interact`) | head pitch 14 deg over 0.6 s |
| Preview head tap (squash) | `game/preview_panel.c` (`head_tap`) | the inset viewport squashes 30 % and springs; head pitch -0.4 rad nod |
| Greets the player (turn) | `game.c:draw_npcs` (`vis_yaw`) | standing NPC within 2.5 m turns to face the player |
| Side-step | `draw_npcs` (`vis_x/z`) | walking NPC shifts up to 0.5 m sideways from the player |
| Glance / look at passer | `npc_idle_touches` | head yaw to the nearest mover within 3 m (+-40 deg) or 20 deg x sin(0.45 t) |
| Weight shift | `npc_idle_touches` | body roll 2 deg |
| Seated stretch | `npc_idle_touches` | hands up, 0.9 s, one seat in ten every 20 s |
| Receptionist nod | `npc_idle_touches` (`queue_waiting`) | pitch 12 deg once per 9 s |
| Queue | `game/npc_pkg.c` `STEP_QUEUE` | stand in slot, walk forward when the place frees |
| Escort | `npc_pkg.c:escort_update` (`STEP_ESCORT`) | child walks beside the leader, sits when the leader sits |
| Sit | `POSE_SIT` (`STEP_SIT`, slot pose), `sit_t` over 0.3 s (`SEAT_TIME`), slides onto `seat_x/z` | see `sit` |
| Lying / bed | `POSE_BED` (`STEP_BED`, inpatient) | see `lying`; no contact disc, cane/bucket hidden |
| Crouch | `POSE_CROUCH` (gardener patrol, `assets/npcs.json`) | see `crouch` |
| Sway / mop | `POSE_MOP` (orderly patrol) | see `sway`; bucket in the left hand |
| Spawn / leave fade | `npc_pkg.c` `fade` (0.6 s, `FADE_TIME`) -> `pose.hide` | dither |
| Hidden storey | `draw_npcs` / `crowd_cull_job`, remote `hidden_storey` | not drawn above the view floor |
| head_only | preview panel, head icons | head part subset |
| X-ray silhouette | `game.c:draw_xray` -> `avatar_draw_with` (`mat_xray`, `shaders/xray*.hlsl`) | black rim (normals pushed ~2 px) + 60 % dithered black fill |
| Name tag | `game.c:draw_name_tags`, `frontend_flow` (lobby) | 12 pt at feet + 2.2 m |
| Child | `npc_look.c:npc_make_look` (`AGE_CHILD`), `draw_avatar` | width x0.75, height x0.6, head x0.92, base 0.32, tilt -4 deg (back), eyes x1.2, bob x1.5, smile, disc x0.75, hair colours 0-5 only |
| Elder | same (`AGE_ELDER`) | x0.95, base 0.46, tilt +6 deg (stoop), hands -0.08 m, eyes x0.9, bob x0.5, grey brows, grey hair (none/short/bun), 50 % reading glasses, 50 % cane (not staff) |
| NPC looks | `npc_make_look` (`LOOK_*`) | doctor (coat + mask or scope), coat, nurse (scrubs, 50 % mask), security (lab coat + sunglasses), orderly (bucket), gardener (green, hat), gown, civilian (8 colours) |
| Low density | `draw_npcs` | odd ids not drawn, all simulated |

## 3. Mapping onto the new avatar

New parts: body (base pivot, floats at `stand_y` 0.42), head centre 0.86 m up
the body's own up axis, hair + 2 eyes children of the head, 2 floating hands.
One shared pose function: `move` 0..1, `sit` 0..1, optional `browse`, `push`,
`look` (head pitch / yaw); idle bob 0.015 m @ 2.1 rad/s + 1.2 % breathing @
1.6 rad/s; walk phase 9 rad/s, bob 0.035 m, hand swing 0.16 m, lean 0.05 rad,
heading easing 10/s, `move` easing 12/s.

| State | New mapping | Clean? |
|---|---|---|
| Idle | `move` 0 | yes |
| Walk | `move` = horizontal speed / 4.5, clamped (`WALK_POSE_SPEED`); forward lean from the pose function | yes (side lean: see below) |
| Sprint | none exists | n/a |
| Elevator ride | idle (vel.xz = 0), feet carried by `car_y` | yes |
| Greeting nod, receptionist nod, glance, passer look, TALK turn | `look` pitch / yaw (same angles, same easing in `npc_idle_touches`) | yes |
| Greets the player, side-step | yaw / feet offsets before the pose function (heading easing replaces the 8/s `vis_yaw` blend or stacks with it: pick one) | yes |
| Preview head tap | viewport squash unchanged + `look` pitch | yes |
| Queue, escort | `move` + `sit` from the same sim | yes |
| Sit | `sit` = `sit_t`; hands to the lap from the pose function | yes, but see seat height in 7 |
| Spawn/leave fade, hidden storey, low density | material `hide` / culling, outside the pose | yes |
| head_only | draw head + hair + eyes only | yes |
| X-ray | same parts through `mat_xray` | yes, but see flat normals in 7 |
| Child | non-uniform body scale (0.75 / 0.6) applied in body space, so the 0.86 m head offset scales too; head x0.92; eyes x1.2 | yes; **smile NOT CLEAN** (no mouth part) |
| Elder | lean bias +6 deg; bob x0.5 | lean yes; **hand drop -0.08 m and brows NOT CLEAN** (no hand-offset input, no brow part) |
| TALK head pop (`head_lift` 0.12 m) | head is fixed at 0.86 m up the body | **NOT CLEAN**: needs a head offset term |
| Weight shift (`weight_tilt`), side lean (`lean_side`) | body roll | **NOT CLEAN**: lean is forward only |
| Sway / mop | root roll + bucket on the left hand | **NOT CLEAN**: no roll input (could be a root rotation outside the pose) |
| Crouch | body lowered 0.3 m, hands down | **NOT CLEAN**: no crouch input (`sit` without a seat is the nearest) |
| Lying / bed | root lifted 0.62, moved 0.98 forward, pitched 80 deg | **NOT CLEAN**: no input; needs a root transform before the pose function |
| Seated stretch (`stretch`) | hands up | **NOT CLEAN**: only browse / push move hands |
| Airborne `raise` | hands up | **NOT CLEAN**: no input (jump is compiled out; only falls trigger it) |
| Head counter-bob (-2 x bob) | head rides the body rigidly | changes the look, not a state |
| `browse`, `push`, creator `look` | new: no state uses them today (no carts are pushed; the creator hero only spins, `frontend.c:draw_scene` `hero_turn`) | new |

## 4. Where avatars are drawn

| Site | File:function | LOD / notes |
|---|---|---|
| Local player | `game/game.c:game_frame_inner` -> `avatar_draw` | LOD0, disc by ray cast (`&g->world`) |
| Remote players | `game_frame_inner` (loop over `session.remotes`) | LOD0; culled above the view floor |
| X-ray (local + remote on the view floor) | `game.c:draw_xray` -> `avatar_draw_with` twice (rim, fill) | Settings "Show players through walls", Developer "X-ray always"; NPCs never |
| NPC roster (play + menu backdrop) | `game.c:draw_npcs` (+ `npc_idle_touches`) | `crowd_cull_job` LODs; menu hero fade (`menu_world_on`) |
| Stress dummies | `game.c:draw_crowd`, looks from `game/stress.c` | `crowd_cull_job` LODs |
| Menu hero (Welcome, Username, Creator, Menu, Settings, Friends) | `game/frontend.c:draw_scene` (`world_behind`) | `flat_ground` disc; spun in the Creator |
| Lobby slots | `frontend.c:draw_scene` (`FeAvatar`, `lobby_visible`); `game.c:lobby4_fake` for perf | 0.9 m apart, fade via `hide` |
| Locker room (Creator from the game) | `frontend.c:draw_scene` (no world) | own camera and key light, no disc |
| Settings Skin preview | `game_frame_inner` (`menu.preview_visible`) | `preview` flag; dead today (`settings_menu.c` only sets it false) |
| Interaction preview, NPC head | `game/preview_panel.c:preview_panel_draw_3d` (`head_only`, nod, squash, `IA_DISABLED` fade 0.35) | inset viewport, head at origin, camera `HEAD_DIST` |
| "..." cloud | `game.c:draw_chat_bubbles` / `draw_one_cloud` | UI; top at feet + 2.1 (stand) / 1.85 (sit) / 1.1 (bed) + 0.35 m |
| Head icon atlas (friends rows, lobby rows, account chip, notice panel, link dialog, settings) | `engine/ui/head_icons.c:head_icons_update` -> `game.c:draw_head_icon` (`head_only`, yaw pi + 15 deg); users `head_icon_draw` / `head_chip_draw` in `friends_page.c`, `frontend.c:lobby_panel`, `game_frame_inner`, `notice_panel.c`, `account_link.c`, `settings_menu.c` | camera `CAM_DIST` 1.6, FOV 28, sized for a 0.215-0.225 m head |
| Name tags | `game.c:draw_name_tags` (play), `frontend_flow` (lobby, crown) | feet + 2.2 m; new `name_tag_y` 1.72 / 1.62 |
| Contact disc | `avatar.c:draw_contact_shadow` | 0.46 m x scale, alpha 0.45, fades with height; skipped lying, in x-ray, head_only, and with no world and no `flat_ground`; new `shadow` r 0.3, alpha 0.16 |
| Debug labels | `draw_npcs` (`show_packages`) | feet + 2.0 / 1.0 m |

## 5. Creator options

`game/creator.c:creator_ui` (tabs body, skin, hair, suit, extras) writes
`NetSkin` (`engine/net/net_transport.h`); `avatar.c:avatar_look_from_skin`
turns it into an `AvatarLook`.

| Option | Today | On the new base |
|---|---|---|
| Body 0 / 1 | male capsule / female capsule (x0.92 bottom, x0.9 top, x0.955 tall) + head x0.956 | 0 = `avatar_body`; 1 = widest ring 8 % narrower, top ring 6 % wider, head unchanged |
| Skin (8, `k_skin_colors`) | head, nose, hands | tint of head + hands |
| Hair style (6, `k_hair_names`) | 0 none, 1 short, 2 bob, 3 bun, 4 spiky, 5 curly (672-2592 tris) | 0 = no cap; 1-5 re-authored as low-poly caps <= 120 tris in head space (today's `avatar_hair`, 90 tris, is the short cap) |
| Hair colour (8, `k_hair_colors`) | hair, cat ears, LOD2 head | tint of the hair cap |
| Suit (8, `k_suits`) | outfit colour + accent + trim + cut (plain / coat / scrubs) + decal (cross / pocket / badge); coats add buttons, V-neck and stethoscope | vertex-colour sets on the one body mesh (collar, lapels, V-neck, pocket, cross, buttons painted in); instance tint = suit colour. The tint multiplies vertex colour, so detail colours must be chosen against the tint (or carried by a second tint) |
| Extras (5, `k_accessory_names`) | none, sunglasses (replace eyes), cat ears, mask (replaces nose + mouth, 1200 tris), stethoscope (body-space tubes, 1232 tris) | re-fit to the 0.25 m head (today 0.215-0.225 m) and the 0.64 m bean; mask and stethoscope need a low-poly rebuild; NPC-only hat, reading glasses, cane, bucket likewise |
| Palette (legacy `NetSkin.palette`, `k_palettes`) | read for old profiles only | unchanged |

## 6. Replication

The state packet does not change: `engine/net/replication.c:rep_pack` /
`rep_unpack`, 34 bytes = seq u16, tick u32, pos 3 x f32, vel 3 x f32, yaw
i16, flags u8 (`REP_GROUNDED`, `REP_JUMPING`, `REP_SPRINTING` always 0,
`REP_LEAVING`, `REP_RIDING`), floor u8. Presence and `profiles.skin` keep
`NetSkin` as is. Nothing about the pose is sent.

Remote `move`: `rep_receiver_sample` -> `raw_sample` gives `RepSample.vel`
(lerped between the two bracketing snapshots, 100 ms `INTERP_DELAY`; the last
packet's vel while extrapolating up to `MAX_EXTRAPOLATE` 200 ms).
`net_session.c` stores it in `r->sample`; `game_frame_inner` passes
`r->sample.vel` as the pose velocity. `move` = |vel.xz| / 4.5 clamped to 1,
eased by the pose function at 12/s, exactly as for the local player
(`g->player.kin.vel`) and NPCs (`h->vx`, `h->vz`). Heading = `r->yaw`
(already interpolated with `lerp_angle`), eased at 10/s.

## 7. Not cleanly mappable, and open points

States (reported, not dropped):

1. **Lying / bed** (`lying`): needs a root transform (lift, forward, 80 deg pitch) outside the pose function.
2. **Crouch** (gardener): no crouch input.
3. **Sway / mop** (orderly): no body roll input; bucket hangs from the left hand.
4. **Weight shift** (`weight_tilt`) and **side lean** (`lean_side`): roll, not in the forward-only lean.
5. **TALK head pop** (`head_lift` 0.12 m): head fixed at 0.86 m.
6. **Seated stretch** (`stretch`): no free hand target.
7. **Airborne hand raise** (`raise`): no input (jump compiled out).
8. **Elder hand drop** (-0.08 m): no hand offset input.
9. **Face details with no new part**: nose, mouth, child smile, elder brows; the mask and sunglasses were placed relative to them.

Other points found:

- Axes: the new asset faces **+Z**; today's avatar faces **-Z** (callers add pi
  to face a camera: `preview_panel.c`, `draw_head_icon`). One of the two flips.
- Flat normals: `tools/meshgen.py` gives faceted meshes flat normals; the
  outline hull and the x-ray rim push vertices along normals, so they crack
  at every facet edge unless the hull gets smooth normals.
- Height: head top drops from 1.88 m to ~1.53 m (+ hair). Name tags (2.2 m),
  "..." clouds (2.1 / 1.85 / 1.1 + 0.35), the preview framing (`cy` 0.72,
  `dist` 5.6), the head icon camera and the menu/lobby cameras were tuned to
  the old size.
- Seat: today the body base drops to 0.25 m when seated; `avatar.json` has
  `seat_y` 0.5 but a seated name tag (1.62) below the standing one (1.72),
  which implies a lower body. Meaning of `seat_y` to confirm against chair
  heights in `game/hospital.c`.
- LOD2 re-tint: `lod_body[1]` / `lod_head[1]` keep the old 0.9 m body
  floating at 0.5 m; they need the new proportions (scale) or the LOD switch
  at a storey change pops.
- Head icon cache: the atlas is keyed by skin hash; the model change needs a
  flush (the merge path already clears it, `account_link.c`).

## 8. As shipped (A1)

`game/avatar.c`: one pose function, `avatar_solve(meshes, look, pose, anim, &solved)`, used by every draw
and by `--autotest avatar`; `avatar_animate` eases `move` (12 /s, full stride from 1 m/s), the heading
(10 /s, shorter arc), browse / push / look (their rates). The caller API is unchanged except
`avatar_animate` takes the meshes; new `AvatarPose` fields: `seat_y`, `browse`, `push`, `look`, `lean`,
`hands_w` + `hand_l` / `hand_r`, `breath_k`.

**Shading** (user decision): smooth + cel. Every part gets smooth normals (70 deg crease, so a cane's cap
or a hat's brim stays sharp); the suit body takes its lathe profile's normal everywhere, so the suit's
zone patches shade like the body under them. The shipped 2-step toon ramp; black outlines from
smooth-normal hulls (the x-ray rim uses the same hulls).

**Suits**: one body mesh (`body_suit`, 419 tris) with the details as zones (vertex alpha = 255 - zone):
1 V-neck (trim), 2 coat front, 3 buttons, 4 cross (accent), 5 pocket, 6 badge (accent), 7 stethoscope.
`GfxMaterial.extra` = outfit, trim, accent, zone mask (+ body type 1); the vertex shader colours the
zones and collapses the ones off the mask (`common.hlsli` `zone_apply`). Body type 1 is the same mesh
(widest ring x 0.92, top ring x 1.06 in the shader): no extra draw.

**Seats** (user decision): `seat_y` (0.5) is the body base seated on the library chairs. Their seat top
is read from `library.mgmesh` at load (0.50 m), so the clearance is 0; on any other seat the body base
goes to that seat's top + the clearance: hospital chairs 0.48 (`HOSP_CHAIR_TOP`), yard benches 0.47
(`HOSP_BENCH_TOP`). Pictures: `docs/autotest/avatar/seat_*.png`.

**Canes** (user request): an avatar with `AVATAR_EXTRA_CANE` walks in steps: each 0.9 s step the body
and the cane move together in the first 45 %, then stand planted; bob and swing happen in the step;
a lean into each step. Drawn only: the sim's position moves evenly and the drawn feet swing round it
(mean 0, at most 0.275 x speed x step, 0.22 m at 0.9 m/s).

### The nine states that did not map cleanly

All kept; pictures with the old avatar above and the new one below, same camera:
`docs/autotest/avatar/substitutes/row_old_vs_new.png` (left to right: crouch, mop sway, weight shift x4,
talk pop, seated stretch, airborne hands, elder, child) and `bed_old_vs_new.png`.

| State | Shipped as |
|---|---|
| Lying / bed | root transform before the pose: on the back, the body axis 0.855 m up (its underside on the 0.6 m mattress), raised 7.6 deg so the head rests on the 0.72 m pillow, head towards the pillow end behind the slot's facing; hands beside the body; no contact disc |
| Crouch (gardener) | body base -0.3 m, +0.3 rad lean, hands -0.3 m: the old numbers on the new parts |
| Mop sway | root roll, as before; the bucket hangs from the left hand |
| Weight shift, side lean | body roll after the forward lean (`weight_tilt`, `lean_side` from the velocity) |
| Talk head pop | **not substituted** (user decision): `head_lift` moves the head along the body's own up axis, the turn to the player is `head_yaw` |
| Seated stretch | both hands +0.5 m (`stretch`), as before |
| Airborne hands | `raise`, hands up 0.5 m and 8 cm wider, as before |
| Elder hand drop | hands -0.08 m; elders lean 0.12 rad (A1) instead of 6 deg |
| Face details | the new face is two eyes: no nose, mouth, child smile or elder brows; sunglasses / reading glasses still replace the eyes |

### Triangles per avatar, old and new

| | Old (submitted, default doctor) | New |
|---|---|---|
| LOD0 | 11 746 (6 938 parts + 4 808 hull) | body_suit 419 + head 80 + hair 90 + 2 hands 160 + 2 eyes 96 + mask 117 = 962 parts (570 for the plain exported body, no extras), + hulls 144 + 80 + 90 + 160 = 474: **1 436** |
| LOD1 | 1 110 | 154 parts + 84 hull |
| LOD2 | 128 | 70 body + 56 head |
