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
option(MG_IOS_APPLE_SIGNIN "iOS: Sign in with Apple entitlement + button" OFF)
set(MG_SCENE "hospital" CACHE STRING "Playable scene: hospital | devroom (M1 engine test room)")
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
if(MG_CAMERA STREQUAL "thirdperson")
  add_compile_definitions(MG_CAMERA_THIRDPERSON=1)
endif()

set(CMAKE_C_STANDARD 11)
set(CMAKE_C_STANDARD_REQUIRED ON)
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
  third_party/mongoose/mongoose.c
)
target_include_directories(mg_thirdparty SYSTEM PUBLIC
  third_party/cjson
  third_party/mongoose
  third_party/handmade_math
)
target_compile_definitions(mg_thirdparty PUBLIC
  MG_TLS=MG_TLS_BUILTIN      # mongoose built-in TLS 1.3, no OpenSSL needed on phones
  MG_ENABLE_LINES=0
  MG_ENABLE_IPV6=1           # NAT64 / IPv6-only mobile networks
)
set_target_properties(mg_thirdparty PROPERTIES C_EXTENSIONS ON) # mongoose needs POSIX/GNU declarations
if(NOT MSVC)
  target_compile_options(mg_thirdparty PRIVATE -w)
endif()
if(WIN32)
  target_link_libraries(mg_thirdparty PUBLIC ws2_32)
endif()

# ------------------------------------------------- embedded assets (C arrays)
file(GLOB MG_SHADER_SPV CONFIGURE_DEPENDS ${CMAKE_SOURCE_DIR}/shaders/compiled/spv/*.spv)
file(GLOB MG_SHADER_MSL CONFIGURE_DEPENDS ${CMAKE_SOURCE_DIR}/shaders/compiled/msl/*.msl)
set(MG_EMBED_FILES
  ${MG_SHADER_SPV}
  ${MG_SHADER_MSL}
  ${CMAKE_SOURCE_DIR}/assets/font_atlas.mgf
  ${CMAKE_SOURCE_DIR}/assets/cacert.pem
  ${CMAKE_SOURCE_DIR}/assets/strings.json
  ${CMAKE_SOURCE_DIR}/assets/npcs.json
  ${CMAKE_SOURCE_DIR}/assets/npc_names.json
)
if(MG_DAYCYCLE STREQUAL "continuous")
  list(APPEND MG_EMBED_FILES ${CMAKE_SOURCE_DIR}/assets/daycycle.json)
endif()
set(MG_EMBED_C ${CMAKE_BINARY_DIR}/generated/mg_assets_embedded.c)
add_custom_command(
  OUTPUT ${MG_EMBED_C}
  COMMAND ${CMAKE_COMMAND} -DOUT=${MG_EMBED_C} -DBASE=${CMAKE_SOURCE_DIR}
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
  engine/gfx/floorfade.c
  engine/ui/text.c
  engine/ui/widgets.c
  engine/ui/touch_controls.c
  engine/ui/marker_hud.c
  engine/ui/hud.c
  engine/ui/prof_hud.c
  engine/ui/layout.c
  engine/platform/secure_store.c
  engine/platform/google_auth.c
  engine/net/ws_client.c
  engine/net/supabase_auth.c
  engine/net/supabase_rt.c
  engine/net/supabase_rest.c
  engine/net/supabase_rpc.c
  engine/net/replication.c
)
target_include_directories(mg_engine PUBLIC engine ${CMAKE_BINARY_DIR}/generated)
target_link_libraries(mg_engine PUBLIC SDL3::SDL3-static mg_thirdparty mg_physics)
# native Google sign-in, one starter per platform (engine/platform/google_auth.h)
if(MG_PLATFORM STREQUAL "ios")
  enable_language(OBJC)
  target_sources(mg_engine PRIVATE platform/ios/google_auth.m platform/ios/apple_auth.m platform/ios/haptics.m platform/ios/safe_area.m)
  set_source_files_properties(platform/ios/google_auth.m platform/ios/apple_auth.m platform/ios/haptics.m platform/ios/safe_area.m PROPERTIES COMPILE_OPTIONS
                              "-fobjc-arc")
  target_link_libraries(mg_engine PUBLIC "-framework AuthenticationServices" "-framework UIKit")
elseif(MG_PLATFORM STREQUAL "android")
  target_sources(mg_engine PRIVATE engine/platform/google_auth_android.c)
else()
  target_sources(mg_engine PRIVATE engine/platform/google_auth_stub.c)
endif()
if(NOT MG_PLATFORM STREQUAL "ios")
  target_sources(mg_engine PRIVATE engine/platform/haptics_stub.c)
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
  target_sources(mg_engine PRIVATE platform/apple/metal_capture.m platform/apple/device_stats.m)
  set_source_files_properties(platform/apple/metal_capture.m platform/apple/device_stats.m PROPERTIES COMPILE_OPTIONS "-fobjc-arc")
  target_link_libraries(mg_engine PUBLIC "-framework Metal" "-framework Foundation")
else()
  target_sources(mg_engine PRIVATE engine/platform/metal_capture_stub.c engine/platform/device_stats_stub.c)
endif()
target_compile_options(mg_engine PRIVATE ${MG_WARNINGS})
# the NPC sim must give the same floats in every build type: no fused multiply-add in only some of them
set_source_files_properties(game/npc_sim.c game/npc_pkg.c engine/nav/astar.c engine/world/spatial.c PROPERTIES COMPILE_OPTIONS "-ffp-contract=off")
if(MG_PROF)
  target_compile_definitions(mg_engine PUBLIC MG_PROF=1)
endif()
string(TOUPPER "${MG_PLATFORM}" MG_PLATFORM_UPPER)
target_compile_definitions(mg_engine PUBLIC MG_PLATFORM_${MG_PLATFORM_UPPER}=1)
if(MG_GPU_DEBUG)
  target_compile_definitions(mg_engine PRIVATE MG_GPU_DEBUG=1)
endif()

set(MG_GAME_SOURCES
  game/main.c
  game/game.c
  game/scene_${MG_SCENE}.c
  game/devroom.c
  game/devroom_layout.c
  game/hospital.c
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
  game/scenery.c
  game/net_session.c
  game/friends.c
  game/lobby.c
  game/frontend.c
  game/creator.c
  game/modes.c
  game/prefs.c
  game/save.c
  game/strings.c
)

# --------------------------------------------------------------- targets
if(MG_PLATFORM STREQUAL "desktop")
  add_executable(mobilegame ${MG_GAME_SOURCES})
  target_link_libraries(mobilegame PRIVATE mg_engine)
  mg_stamp_build(mobilegame)
  target_include_directories(mobilegame PRIVATE game)
  target_compile_options(mobilegame PRIVATE ${MG_WARNINGS})

  enable_testing()
  add_executable(mg_tests tests/test_main.c tests/test_kinematic.c tests/test_replication.c tests/test_interact.c
    game/devroom_layout.c engine/net/replication.c game/interact.c engine/world/spatial.c engine/core/arena.c)
  target_include_directories(mg_tests PRIVATE game)
  target_link_libraries(mg_tests PRIVATE mg_physics)
  target_compile_options(mg_tests PRIVATE ${MG_WARNINGS})
  add_test(NAME controller COMMAND mg_tests)

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
    MACOSX_BUNDLE_BUNDLE_NAME "Mobilegame"
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
