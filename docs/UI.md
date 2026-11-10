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
