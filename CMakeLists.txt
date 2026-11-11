cmake_minimum_required(VERSION 3.28)
project(mobilegame VERSION 0.1.0 LANGUAGES C)

# One source tree, three targets:
#   desktop  - fast iteration (Vulkan on Linux/Windows, Metal on macOS) + tests
#   ios      - generate with -G Xcode -DCMAKE_SYSTEM_NAME=iOS (see tools/run_ios.sh)
#   android  - built by Gradle (platform/android) which calls this file
set(MG_PLATFORM "desktop" CACHE STRING "Target platform: desktop | ios | android")
set_property(CACHE MG_PLATFORM PROPERTY STRINGS desktop ios android)
set(MG_BUNDLE_ID "ro.docvex.mobilegame" CACHE STRING "Application identifier")
set(MG_IOS_TEAM_ID "" CACHE STRING "Apple development team ID for code signing")
option(MG_GPU_DEBUG "Enable SDL GPU debug/validation layers" OFF)
option(MG_BAKE "Prototype: bake sun shadow and AO into the static world's vertex colours" OFF)
option(MG_PROF "CPU profiler, GPU fence timing, heap counter (off for store builds)" ON)
# Needs the "Sign In with Apple" capability on the App ID (see CLAUDE.md);
# without it the provisioning profile rejects the entitlement.
option(MG_TEST_AUTH "--autotest link: email + password stand in for the ID-token grant (Debug test builds only)" OFF)
option(MG_IOS_APPLE_SIGNIN "iOS: Sign in with Apple entitlement + button" OFF)
set(MG_SCENE "hospital" CACHE STRING "Default scene for direct starts: hospital | devroom (M1 engine test room; forces it everywhere). Levels choose theirs at run time (books.json)")
set_property(CACHE MG_SCENE PROPERTY STRINGS hospital devroom)
set(MG_RENDER "minimal" CACHE STRING
    "Renderer: minimal (one directional light, no shadows/lights/reflection) | full (M3 path, reference only)")
set_property(CACHE MG_RENDER PROPERTY STRINGS minimal full)
if(MG_RENDER STREQUAL "full")
  add_compile_definitions(MG_RENDER_FULL=1)
endif()
set(MG_DAYCYCLE "fixed" CACHE STRING
    "Lighting over time: fixed (always day) | continuous (M3 day/night cycle, reference only, not shipped)")
set_property(CACHE MG_DAYCYCLE PROPERTY STRINGS fixed continuous)
if(MG_DAYCYCLE STREQUAL "continuous")
  add_compile_definitions(MG_DAYCYCLE_CONTINUOUS=1)
endif()
set(MG_CAMERA "topdown" CACHE STRING "Game camera: topdown (M5) | thirdperson (M1-M4, engine tests only)")
set_property(CACHE MG_CAMERA PROPERTY STRINGS topdown thirdperson)
if(MG_BAKE)
  add_compile_definitions(MG_BAKE=1)
endif()
# build_info.h: the commit, regenerated on every build (--autotest perf records it)
set(MG_BUILDINFO ${CMAKE_BINARY_DIR}/generated/build_info.h)
execute_process(COMMAND ${CMAKE_COMMAND} -DSRC=${CMAKE_SOURCE_DIR} -DOUT=${MG_BUILDINFO} -P ${CMAKE_SOURCE_DIR}/tools/gen_buildinfo.cmake)
add_custom_target(mg_buildinfo COMMAND ${CMAKE_COMMAND} -DSRC=${CMAKE_SOURCE_DIR} -DOUT=${MG_BUILDINFO} -P ${CMAKE_SOURCE_DIR}/tools/gen_buildinfo.cmake)
function(mg_stamp_build target)
  add_dependencies(${target} mg_buildinfo)
  target_include_directories(${target} PRIVATE ${CMAKE_BINARY_DIR}/generated)
endfunction()
if(MG_SCENE STREQUAL "devroom")
  add_compile_definitions(MG_SCENE_DEVROOM=1)
endif()
if(MG_CAMERA STREQUAL "thirdperson")
  add_compile_definitions(MG_CAMERA_THIRDPERSON=1)
endif()

set(CMAKE_C_STANDARD 11)
set(CMAKE_C_STANDARD_REQUIRED ON)
if(CMAKE_SYSTEM_NAME STREQUAL "Linux")
  # glibc hides POSIX / BSD names (M_PI in engine/audio/audio_sdl.c, ...) under -std=c11
  add_compile_definitions(_DEFAULT_SOURCE)
endif()
set(CMAKE_C_EXTENSIONS OFF)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

if(NOT MG_PLATFORM MATCHES "^(desktop|ios|android)$")
  message(FATAL_ERROR "MG_PLATFORM must be desktop, ios or android (got '${MG_PLATFORM}')")
endif()

# ------------------------------------------------------------------ SDL3
set(SDL_SHARED OFF CACHE BOOL "" FORCE)
set(SDL_STATIC ON CACHE BOOL "" FORCE)
set(SDL_TEST_LIBRARY OFF CACHE BOOL "" FORCE)
set(SDL_TESTS OFF CACHE BOOL "" FORCE)
set(SDL_EXAMPLES OFF CACHE BOOL "" FORCE)
set(SDL_INSTALL OFF CACHE BOOL "" FORCE)
set(SDL_HIDAPI OFF CACHE BOOL "" FORCE) # no gamepads in M1; keeps SDL pure C on Android
add_subdirectory(third_party/SDL3 EXCLUDE_FROM_ALL)

# ------------------------------------------------------------ third party
add_library(mg_thirdparty STATIC
  third_party/cjson/cJSON.c
)
target_include_directories(mg_thirdparty SYSTEM PUBLIC
  third_party/cjson
  third_party/handmade_math
)
if(NOT MSVC)
  target_compile_options(mg_thirdparty PRIVATE -w)
endif()

# ------------------------------------------------- embedded assets (C arrays)
file(GLOB MG_SHADER_SPV CONFIGURE_DEPENDS ${CMAKE_SOURCE_DIR}/shaders/compiled/spv/*.spv)
file(GLOB MG_SHADER_MSL CONFIGURE_DEPENDS ${CMAKE_SOURCE_DIR}/shaders/compiled/msl/*.msl)
set(MG_EMBED_FILES
  ${MG_SHADER_SPV}
  ${MG_SHADER_MSL}
  ${CMAKE_SOURCE_DIR}/assets/font_atlas.mgf
  ${CMAKE_SOURCE_DIR}/assets/strings.json
  ${CMAKE_SOURCE_DIR}/assets/strings_ro.json
  ${CMAKE_SOURCE_DIR}/assets/npcs.json
  ${CMAKE_SOURCE_DIR}/assets/npc_names.json
  ${CMAKE_SOURCE_DIR}/assets/world/books.json
  ${CMAKE_SOURCE_DIR}/assets/world/kumar.island.json
  ${CMAKE_SOURCE_DIR}/assets/world/lawrance.island.json
  ${CMAKE_SOURCE_DIR}/assets/avatar/avatar.json
  ${CMAKE_SOURCE_DIR}/assets/menu/library.scene.json
)
find_package(Python3 REQUIRED COMPONENTS Interpreter)
# Pinned downloads (assets/ASSETS.lock): a missing file or a different hash fails configure and every build
execute_process(COMMAND ${Python3_EXECUTABLE} ${CMAKE_SOURCE_DIR}/tools/assets_lock.py ${CMAKE_SOURCE_DIR} RESULT_VARIABLE MG_LOCK_RESULT)
if(NOT MG_LOCK_RESULT EQUAL 0)
  message(FATAL_ERROR "assets/ASSETS.lock: a pinned asset is missing or differs (tools/assets_lock.py --fetch)")
endif()
add_custom_target(mg_assets_lock ALL COMMAND ${Python3_EXECUTABLE} ${CMAKE_SOURCE_DIR}/tools/assets_lock.py ${CMAKE_SOURCE_DIR}
                  COMMENT "Checking assets/ASSETS.lock" VERBATIM)
# WORLD-1: mgmesh text -> engine mesh binary at build time (tools/meshgen.py); a malformed line fails the build
set(MG_GEN_DIR ${CMAKE_BINARY_DIR}/generated)
foreach(mesh world/kumar_island world/lawrance_island world/ambulance world/vehicles avatar/avatar avatar/avatar_options menu/library)
  get_filename_component(mesh_dir ${mesh} DIRECTORY)
  set(out ${MG_GEN_DIR}/assets/${mesh}.mgmb)
  add_custom_command(
    OUTPUT ${out}
    COMMAND ${CMAKE_COMMAND} -E make_directory ${MG_GEN_DIR}/assets/${mesh_dir}
    COMMAND ${Python3_EXECUTABLE} ${CMAKE_SOURCE_DIR}/tools/meshgen.py ${CMAKE_SOURCE_DIR}/assets/${mesh}.mgmesh ${out}
    DEPENDS ${CMAKE_SOURCE_DIR}/assets/${mesh}.mgmesh ${CMAKE_SOURCE_DIR}/tools/meshgen.py
    COMMENT "meshgen ${mesh}.mgmesh"
    VERBATIM
  )
  list(APPEND MG_EMBED_FILES ${out})
endforeach()
# MAP-2: institute.map -> fixed-size records read in place (tools/mapc.py); a malformed line fails the build
set(MG_MAPB ${MG_GEN_DIR}/assets/world/institute.mapb)
add_custom_command(
  OUTPUT ${MG_MAPB}
  COMMAND ${CMAKE_COMMAND} -E make_directory ${MG_GEN_DIR}/assets/world
  COMMAND ${Python3_EXECUTABLE} ${CMAKE_SOURCE_DIR}/tools/mapc.py ${CMAKE_SOURCE_DIR}/content/gastro/institute.map
          ${CMAKE_SOURCE_DIR}/content/gastro/prop_kinds.json ${CMAKE_SOURCE_DIR}/content/gastro/institute_geometry.js ${MG_MAPB}
  DEPENDS ${CMAKE_SOURCE_DIR}/content/gastro/institute.map ${CMAKE_SOURCE_DIR}/content/gastro/prop_kinds.json
          ${CMAKE_SOURCE_DIR}/content/gastro/institute_geometry.js ${CMAKE_SOURCE_DIR}/tools/mapc.py
  COMMENT "mapc institute.map"
  VERBATIM
)
list(APPEND MG_EMBED_FILES ${MG_MAPB})
# the institute's detail textures (the reference page's ten patterns + mips), generated at build time (tools/gen_patterns.py)
set(MG_PATTERNS ${MG_GEN_DIR}/assets/world/inst_patterns.bin)
add_custom_command(
  OUTPUT ${MG_PATTERNS}
  COMMAND ${CMAKE_COMMAND} -E make_directory ${MG_GEN_DIR}/assets/world
  COMMAND ${Python3_EXECUTABLE} ${CMAKE_SOURCE_DIR}/tools/gen_patterns.py ${MG_PATTERNS}
  DEPENDS ${CMAKE_SOURCE_DIR}/tools/gen_patterns.py
  COMMENT "gen_patterns inst_patterns.bin"
  VERBATIM
)
list(APPEND MG_EMBED_FILES ${MG_PATTERNS})
if(MG_DAYCYCLE STREQUAL "continuous")
  list(APPEND MG_EMBED_FILES ${CMAKE_SOURCE_DIR}/assets/daycycle.json)
endif()
# BOOK-1: the content pack, embedded as content/gastro.mgpack (8-byte aligned: read in place, game/book_pack.c). The real pack
# lives in content_private/gastro/ (ignored, never committed); without it the public dummy pack is embedded and the game
# shows its "continut demo" chip (MG_CONTENT_DEMO). Store builds pass -DMG_REQUIRE_REAL_CONTENT=ON.
set(MG_CONTENT_DIR ${CMAKE_SOURCE_DIR}/content_private/gastro CACHE PATH "Folder holding the real BOOK-1 pack gastro.mgpack (never committed)")
option(MG_REQUIRE_REAL_CONTENT "Fail configure when the real content pack is missing (store builds)" OFF)
if(EXISTS ${MG_CONTENT_DIR}/gastro.mgpack)
  set(MG_CONTENT_PACK ${MG_CONTENT_DIR}/gastro.mgpack)
  set(MG_CONTENT_DEMO OFF)
  message(STATUS "BOOK-1: embedding the real pack ${MG_CONTENT_PACK}")
elseif(MG_REQUIRE_REAL_CONTENT)
  message(FATAL_ERROR "BOOK-1: the real pack ${MG_CONTENT_DIR}/gastro.mgpack is missing; a shipped build never carries the demo "
                      "(unpack the BOOK-1 engine bundle into content_private/gastro/ or set MG_CONTENT_DIR)")
else()
  set(MG_CONTENT_PACK ${CMAKE_SOURCE_DIR}/content/gastro_demo/gastro.mgpack)
  set(MG_CONTENT_DEMO ON)
  message(STATUS "BOOK-1: no real pack in ${MG_CONTENT_DIR}: embedding the dummy pack (MG_CONTENT_DEMO)")
endif()
set(MG_CONTENT_OUT ${MG_GEN_DIR}/content/gastro.mgpack)
add_custom_command(
  OUTPUT ${MG_CONTENT_OUT}
  COMMAND ${CMAKE_COMMAND} -E make_directory ${MG_GEN_DIR}/content
  COMMAND ${CMAKE_COMMAND} -E copy ${MG_CONTENT_PACK} ${MG_CONTENT_OUT}
  DEPENDS ${MG_CONTENT_PACK}
  COMMENT "content pack ${MG_CONTENT_PACK}"
  VERBATIM
)
add_custom_target(mg_content_pack DEPENDS ${MG_CONTENT_OUT})
list(APPEND MG_EMBED_FILES ${MG_CONTENT_OUT})
# BOOK-ENGINE figures (decision 7): the pack's crops (fig/*.png next to it) -> content/figures.mgfig (tools/book/figpack.py),
# read in place by game/book_figs.c and uploaded one at a time (gfx_set_figure). ASTC 4x4 where the GPU samples it (Apple;
# Android when built on a Mac: the encoder is macOS ImageIO), RGBA8 elsewhere.
if(MG_CONTENT_DEMO)
  set(MG_FIG_DIR ${CMAKE_SOURCE_DIR}/content/gastro_demo/fig)
else()
  set(MG_FIG_DIR ${MG_CONTENT_DIR}/fig)
endif()
if(APPLE OR (ANDROID AND CMAKE_HOST_APPLE))
  set(MG_FIG_FORMAT astc)
else()
  set(MG_FIG_FORMAT rgba8)
endif()
file(GLOB MG_FIG_PNGS CONFIGURE_DEPENDS ${MG_FIG_DIR}/*.png)
set(MG_FIG_OUT ${MG_GEN_DIR}/content/figures.mgfig)
add_custom_command(
  OUTPUT ${MG_FIG_OUT}
  COMMAND ${CMAKE_COMMAND} -E make_directory ${MG_GEN_DIR}/content
  COMMAND ${Python3_EXECUTABLE} -I ${CMAKE_SOURCE_DIR}/tools/book/figpack.py ${MG_FIG_DIR} ${MG_FIG_OUT} ${MG_FIG_FORMAT}
  DEPENDS ${MG_FIG_PNGS} ${CMAKE_SOURCE_DIR}/tools/book/figpack.py ${CMAKE_SOURCE_DIR}/tools/book/astc_encode.swift
  COMMENT "figpack ${MG_FIG_DIR} (${MG_FIG_FORMAT})"
  VERBATIM
)
list(APPEND MG_EMBED_FILES ${MG_FIG_OUT})
set(MG_EMBED_C ${CMAKE_BINARY_DIR}/generated/mg_assets_embedded.c)
add_custom_command(
  OUTPUT ${MG_EMBED_C}
  COMMAND ${CMAKE_COMMAND} -DOUT=${MG_EMBED_C} -DBASE=${CMAKE_SOURCE_DIR} -DGENBASE=${MG_GEN_DIR}
          "-DFILES=${MG_EMBED_FILES}" -P ${CMAKE_SOURCE_DIR}/cmake/embed_files.cmake
  DEPENDS ${MG_EMBED_FILES} ${CMAKE_SOURCE_DIR}/cmake/embed_files.cmake
  COMMENT "Embedding shaders, font atlas and CA bundle"
  VERBATIM
)

# ------------------------------------------------------------ local config
if(EXISTS ${CMAKE_SOURCE_DIR}/config.local.h)
  configure_file(${CMAKE_SOURCE_DIR}/config.local.h ${CMAKE_BINARY_DIR}/generated/config.local.h COPYONLY)
else()
  message(WARNING "config.local.h not found: online modes disabled. Copy config.local.h.example.")
  configure_file(${CMAKE_SOURCE_DIR}/config.local.h.example ${CMAKE_BINARY_DIR}/generated/config.local.h COPYONLY)
endif()

# ----------------------------------------------------------------- engine
set(MG_WARNINGS -Wall -Wextra -Wshadow -Wno-unused-parameter -Wno-missing-field-initializers -Wno-missing-braces)

add_library(mg_physics STATIC
  engine/physics/collide.c
  engine/physics/kinematic.c
)
target_include_directories(mg_physics PUBLIC engine)
target_link_libraries(mg_physics PUBLIC mg_thirdparty)
target_compile_options(mg_physics PRIVATE ${MG_WARNINGS})
if(NOT WIN32)
  target_link_libraries(mg_physics PUBLIC m)
endif()

add_library(mg_engine STATIC
  ${MG_EMBED_C}
  engine/core/arena.c
  engine/core/assets.c
  engine/core/log.c
  engine/core/png_write.c
  engine/core/prof.c
  engine/core/jobs.c
  engine/nav/navgrid.c
  engine/nav/astar.c
  engine/core/time.c
  engine/platform/app_sdl.c
  engine/platform/input_touch.c
  engine/audio/audio_sdl.c
  engine/world/lighting_day.c
  engine/world/spatial.c
  engine/gfx/camera.c
  engine/gfx/gpu.c
  engine/gfx/material.c
  engine/gfx/mesh.c
  engine/gfx/weld.c
  engine/gfx/floorfade.c
  engine/ui/text.c
  engine/ui/text_layout.c
  engine/ui/ui_textarea.c
  engine/ui/widgets.c
  engine/ui/ui_anim.c
  engine/ui/ui_fx.c
  engine/ui/haptics.c
  engine/ui/tilt.c
  engine/ui/ui_widgets.c
  engine/ui/touch_controls.c
  engine/ui/stick_view.c
  engine/ui/marker_hud.c
  engine/ui/hud.c
  engine/ui/prof_hud.c
  engine/ui/layout.c
  engine/platform/secure_store.c
  engine/platform/google_auth.c
  engine/net/net_util.c
  engine/net/supabase_auth.c
  engine/net/supabase_rt.c
  engine/net/supabase_rest.c
  engine/net/supabase_rpc.c
  engine/net/replication.c
  engine/ui/head_icons.c
  engine/ui/notice_panel.c
)
add_dependencies(mg_engine mg_content_pack) # Xcode's build system: the pack copy feeds both the embed step and mg_content_pack, so one must depend on the other
target_include_directories(mg_engine PUBLIC engine ${CMAKE_BINARY_DIR}/generated)
target_link_libraries(mg_engine PUBLIC SDL3::SDL3-static mg_thirdparty mg_physics)
# native Google sign-in, one starter per platform (engine/platform/google_auth.h)
if(MG_PLATFORM STREQUAL "ios")
  enable_language(OBJC)
  target_sources(mg_engine PRIVATE platform/ios/google_auth.m platform/ios/apple_auth.m platform/ios/haptics.m platform/ios/safe_area.m platform/ios/motion.m)
  set_source_files_properties(platform/ios/google_auth.m platform/ios/apple_auth.m platform/ios/haptics.m platform/ios/safe_area.m platform/ios/motion.m PROPERTIES COMPILE_OPTIONS
                              "-fobjc-arc")
  target_link_libraries(mg_engine PUBLIC "-framework AuthenticationServices" "-framework UIKit" "-framework CoreHaptics" "-framework CoreMotion")
elseif(MG_PLATFORM STREQUAL "android")
  target_sources(mg_engine PRIVATE engine/platform/google_auth_android.c)
else()
  target_sources(mg_engine PRIVATE engine/platform/google_auth_stub.c)
endif()
if(NOT MG_PLATFORM STREQUAL "ios")
  target_sources(mg_engine PRIVATE engine/platform/haptics_stub.c engine/platform/motion_stub.c)
endif()
if(MG_DAYCYCLE STREQUAL "continuous")
  target_sources(mg_engine PRIVATE engine/world/daycycle.c)
endif()
if(MG_RENDER STREQUAL "full")
  target_sources(mg_engine PRIVATE engine/gfx/shadows.c engine/gfx/reflection.c)
endif()
# --metal-capture: one-frame .gputrace (Apple only)
if(APPLE)
  enable_language(OBJC)
  target_sources(mg_engine PRIVATE platform/apple/metal_capture.m platform/apple/device_stats.m platform/apple/net_apple.m)
  set_source_files_properties(platform/apple/metal_capture.m platform/apple/device_stats.m platform/apple/net_apple.m PROPERTIES COMPILE_OPTIONS "-fobjc-arc")
  target_link_libraries(mg_engine PUBLIC "-framework Metal" "-framework Foundation")
else()
  target_sources(mg_engine PRIVATE engine/platform/metal_capture_stub.c engine/platform/device_stats_stub.c engine/net/net_stub.c)
endif()
target_compile_options(mg_engine PRIVATE ${MG_WARNINGS})
# the NPC sim must give the same floats in every build type: no fused multiply-add in only some of them
set_source_files_properties(game/npc_sim.c game/npc_pkg.c game/book_people.c engine/nav/astar.c engine/world/spatial.c PROPERTIES COMPILE_OPTIONS "-ffp-contract=off")
if(MG_PROF)
  target_compile_definitions(mg_engine PUBLIC MG_PROF=1)
endif()
string(TOUPPER "${MG_PLATFORM}" MG_PLATFORM_UPPER)
target_compile_definitions(mg_engine PUBLIC MG_PLATFORM_${MG_PLATFORM_UPPER}=1)
if(MG_CONTENT_DEMO)
  target_compile_definitions(mg_engine PUBLIC MG_CONTENT_DEMO=1)
endif()
if(MG_TEST_AUTH)
  if(CMAKE_BUILD_TYPE STREQUAL "Release")
    message(FATAL_ERROR "MG_TEST_AUTH is for test builds only")
  endif()
  target_compile_definitions(mg_engine PUBLIC MG_TEST_AUTH=1)
endif()
if(MG_GPU_DEBUG)
  target_compile_definitions(mg_engine PRIVATE MG_GPU_DEBUG=1)
endif()

set(MG_GAME_SOURCES
  game/main.c
  game/game.c
  game/scene.c
  game/scene_hospital.c
  game/scene_devroom.c
  game/scene_institute.c
  game/devroom.c
  game/devroom_layout.c
  game/hospital.c
  game/imap.c
  game/institute.c
  game/institute_furniture.c
  game/inst_patterns.c
  game/nav_check.c
  game/elevator.c
  game/elevator_panel.c
  game/route_test.c
  game/nav_debug.c
  game/markers.c
  game/autonav.c
  game/perf_run.c
  game/stress.c
  game/bake.c
  game/npc.c
  game/rooms.c
  game/interact.c
  game/preview_panel.c
  game/npc_data.c
  game/npc_sim.c
  game/npc_pkg.c
  game/npc_look.c
  game/stats.c
  game/player.c
  game/camera_td.c
  game/camera_tp.c
  game/settings_menu.c
  game/autotest.c
  game/avatar.c
  game/avatar_gallery.c
  game/menu_library.c
  game/lib_study.c
  game/lib_move.c
  game/lib_dressing.c
  game/book_progress.c
  game/book_figs.c
  game/book_exam.c
  game/script_player.c
  game/autotest_book_ui.c
  game/lib_nav.c
  game/lib_fidget.c
  game/lib_life.c
  game/autotest_avatar.c
  game/autotest_library.c
  game/scenery.c
  game/net_session.c
  game/friends.c
  game/friends_page.c
  game/account_link.c
  game/autotest_friends.c
  game/autotest_world.c
  game/autotest_ui.c
  game/autotest_stick.c
  game/autotest_haptics.c
  game/autotest_tilt.c
  game/lobby.c
  game/frontend.c
  game/creator.c
  game/prefs.c
  game/save.c
  game/strings.c
  game/world_data.c
  game/world_progress.c
  game/worldmap.c
  game/book_pack.c
  game/book_people.c
  game/autotest_book.c
  game/book_check.c
  game/missions.c
  game/missions_ui.c
  game/autotest_missions.c
)
# --------------------------------------------------------------- targets
if(MG_PLATFORM STREQUAL "desktop")
  add_executable(mobilegame ${MG_GAME_SOURCES})
  target_link_libraries(mobilegame PRIVATE mg_engine)
  mg_stamp_build(mobilegame)
  target_include_directories(mobilegame PRIVATE game)
  target_compile_options(mobilegame PRIVATE ${MG_WARNINGS})

  enable_testing()
  add_executable(mg_tests tests/test_main.c tests/test_kinematic.c tests/test_replication.c tests/test_interact.c tests/test_stick.c engine/ui/touch_controls.c
    game/devroom_layout.c engine/net/replication.c game/interact.c engine/world/spatial.c engine/core/arena.c)
  target_include_directories(mg_tests PRIVATE game)
  target_compile_definitions(mg_tests PRIVATE MG_STICK_GOLDEN="${CMAKE_SOURCE_DIR}/tests/stick_golden.txt")
  target_link_libraries(mg_tests PRIVATE mg_physics SDL3::Headers) # headers only: the stick test drives touch_controls without SDL
  target_compile_options(mg_tests PRIVATE ${MG_WARNINGS})
  add_test(NAME controller COMMAND mg_tests)
  # BOOK-ENGINE: the device's book progress file (writes into the build directory)
  add_executable(book_progress_test tests/book_progress_test.c game/book_progress.c)
  target_compile_options(book_progress_test PRIVATE -iquote ${CMAKE_SOURCE_DIR}/game) # quotes only: glibc's <string.h> pulls <strings.h>, which -I game would turn into game/strings.h
  target_link_libraries(book_progress_test PRIVATE SDL3::SDL3-static)
  target_compile_options(book_progress_test PRIVATE ${MG_WARNINGS})
  add_test(NAME book_progress COMMAND book_progress_test ${CMAKE_BINARY_DIR})
  # BOOK-ENGINE figures: the demo pack's crops through tools/book/figpack.py (ASTC via ImageIO on Apple, RGBA8 elsewhere)
  if(APPLE)
    set(MG_FIG_TEST_FORMAT astc)
  else()
    set(MG_FIG_TEST_FORMAT rgba8)
  endif()
  add_custom_command(OUTPUT ${CMAKE_BINARY_DIR}/generated/figtest.mgfig
    COMMAND ${Python3_EXECUTABLE} -I ${CMAKE_SOURCE_DIR}/tools/book/figpack.py ${CMAKE_SOURCE_DIR}/content/gastro_demo/fig
            ${CMAKE_BINARY_DIR}/generated/figtest.mgfig ${MG_FIG_TEST_FORMAT}
    DEPENDS ${CMAKE_SOURCE_DIR}/tools/book/figpack.py ${CMAKE_SOURCE_DIR}/tools/book/astc_encode.swift ${CMAKE_SOURCE_DIR}/content/gastro_demo/fig/fig_demo.png)
  add_custom_target(figtest_mgfig DEPENDS ${CMAKE_BINARY_DIR}/generated/figtest.mgfig)
  add_executable(book_figs_test tests/book_figs_test.c game/book_figs.c)
  target_compile_options(book_figs_test PRIVATE -iquote ${CMAKE_SOURCE_DIR}/game) # quotes only: glibc's <string.h> pulls <strings.h>, which -I game would turn into game/strings.h
  target_compile_options(book_figs_test PRIVATE ${MG_WARNINGS})
  add_dependencies(book_figs_test figtest_mgfig)
  add_test(NAME book_figs COMMAND book_figs_test ${CMAKE_BINARY_DIR}/generated/figtest.mgfig fig_demo)
  # BOOK-1: the pack loader on the embedded pack (the generated copy) and the committed dummy, plus corrupted copies
  add_executable(book_pack_test tests/book_pack_test.c game/book_pack.c)
  target_include_directories(book_pack_test PRIVATE engine)
  target_compile_options(book_pack_test PRIVATE -iquote ${CMAKE_SOURCE_DIR}/game) # quotes only: glibc's <string.h> pulls <strings.h>, which -I game would turn into game/strings.h
  target_compile_options(book_pack_test PRIVATE ${MG_WARNINGS})
  if(MG_CONTENT_DEMO)
    target_compile_definitions(book_pack_test PRIVATE MG_CONTENT_DEMO=1)
  endif()
  add_dependencies(book_pack_test mg_content_pack)
  add_test(NAME book_pack COMMAND book_pack_test ${MG_CONTENT_OUT} ${CMAKE_SOURCE_DIR}/content/gastro_demo/gastro.mgpack)
  # word wrap (engine/ui/text_layout.c) on a font with known advances
  add_executable(text_wrap_test tests/text_wrap_test.c engine/ui/text_layout.c)
  target_include_directories(text_wrap_test PRIVATE engine)
  target_link_libraries(text_wrap_test PRIVATE mg_thirdparty)
  target_compile_options(text_wrap_test PRIVATE ${MG_WARNINGS})
  add_test(NAME text_wrap COMMAND text_wrap_test)
  # LIB-1 modules: pure C, each test reads assets/menu/library.scene.json from the repo (lib_nav_test brings its own log/time stubs)
  add_executable(weld_test tests/weld_test.c engine/gfx/weld.c)
target_include_directories(weld_test PRIVATE engine)
target_link_libraries(weld_test PRIVATE mg_thirdparty)
target_compile_options(weld_test PRIVATE ${MG_WARNINGS})
add_dependencies(weld_test mobilegame) # the generated meshes
add_test(NAME weld COMMAND weld_test ${CMAKE_BINARY_DIR}/generated/assets)

# level scenes: mesh pool mark / reset, level arena, nav with several lift cars, five landings, save bits (pure C)
add_executable(level_mem_test tests/level_mem_test.c engine/nav/navgrid.c engine/nav/astar.c engine/core/arena.c game/elevator.c)
target_include_directories(level_mem_test PRIVATE game engine)
target_link_libraries(level_mem_test PRIVATE mg_physics m)
target_compile_options(level_mem_test PRIVATE ${MG_WARNINGS})
add_test(NAME level_mem COMMAND level_mem_test)

foreach(lib dressing nav fidget life)
    add_executable(lib_${lib}_test tests/lib_${lib}_test.c game/lib_${lib}.c)
    target_include_directories(lib_${lib}_test PRIVATE game engine)
    target_link_libraries(lib_${lib}_test PRIVATE mg_thirdparty m)
    target_compile_options(lib_${lib}_test PRIVATE ${MG_WARNINGS})
    add_test(NAME lib_${lib} COMMAND lib_${lib}_test WORKING_DIRECTORY ${CMAKE_SOURCE_DIR})
  endforeach()

  add_executable(mg_nettest tools/nettest.c)
  target_link_libraries(mg_nettest PRIVATE mg_engine)
  target_compile_options(mg_nettest PRIVATE ${MG_WARNINGS})

elseif(MG_PLATFORM STREQUAL "android")
  # SDLActivity loads libmain.so (SDL3 is linked in statically).
  add_library(main SHARED ${MG_GAME_SOURCES})
  target_link_libraries(main PRIVATE mg_engine android log)
  target_include_directories(main PRIVATE game)
  target_compile_options(main PRIVATE ${MG_WARNINGS})
  target_link_options(main PRIVATE "-Wl,-z,max-page-size=16384")

elseif(MG_PLATFORM STREQUAL "ios")
  set(MG_IOS_RESOURCES
    ${CMAKE_SOURCE_DIR}/platform/ios/LaunchScreen.storyboard
    ${CMAKE_SOURCE_DIR}/platform/ios/Assets.xcassets
  )
  add_executable(mobilegame MACOSX_BUNDLE ${MG_GAME_SOURCES} ${MG_IOS_RESOURCES})
  mg_stamp_build(mobilegame)
  target_link_libraries(mobilegame PRIVATE mg_engine "-framework Security" "-framework CoreFoundation")
  target_include_directories(mobilegame PRIVATE game)
  target_compile_options(mobilegame PRIVATE ${MG_WARNINGS})
  set_source_files_properties(${MG_IOS_RESOURCES} PROPERTIES MACOSX_PACKAGE_LOCATION Resources)
  set_target_properties(mobilegame PROPERTIES
    OUTPUT_NAME "Mobilegame"
    MACOSX_BUNDLE_INFO_PLIST ${CMAKE_SOURCE_DIR}/platform/ios/Info.plist.in
    MACOSX_BUNDLE_GUI_IDENTIFIER ${MG_BUNDLE_ID}
    MACOSX_BUNDLE_BUNDLE_NAME "Rezi"
    MACOSX_BUNDLE_SHORT_VERSION_STRING ${PROJECT_VERSION}
    MACOSX_BUNDLE_BUNDLE_VERSION "1"
    RESOURCE "${MG_IOS_RESOURCES}"
    XCODE_ATTRIBUTE_PRODUCT_BUNDLE_IDENTIFIER ${MG_BUNDLE_ID}
    XCODE_ATTRIBUTE_TARGETED_DEVICE_FAMILY "1,2"
    XCODE_ATTRIBUTE_IPHONEOS_DEPLOYMENT_TARGET "17.0"
    XCODE_ATTRIBUTE_CODE_SIGN_STYLE "Automatic"
    XCODE_ATTRIBUTE_DEVELOPMENT_TEAM "${MG_IOS_TEAM_ID}"
    XCODE_ATTRIBUTE_CODE_SIGN_IDENTITY "Apple Development"
    XCODE_ATTRIBUTE_ENABLE_BITCODE "NO"
    XCODE_ATTRIBUTE_GCC_C_LANGUAGE_STANDARD "c11"
    XCODE_ATTRIBUTE_ASSETCATALOG_COMPILER_APPICON_NAME "AppIcon"
    XCODE_ATTRIBUTE_SUPPORTED_PLATFORMS "iphoneos"
  )
  if(MG_IOS_APPLE_SIGNIN)
    set_target_properties(mobilegame PROPERTIES XCODE_ATTRIBUTE_CODE_SIGN_ENTITLEMENTS
                          "${CMAKE_SOURCE_DIR}/platform/ios/mobilegame.entitlements")
    target_compile_definitions(mg_engine PRIVATE MG_APPLE_SIGNIN=1)
  endif()
endif()
