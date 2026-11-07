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
)
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
set(MG_WARNINGS -Wall -Wextra -Wshadow -Wno-unused-parameter -Wno-missing-field-initializers)

add_library(mg_engine STATIC
  ${MG_EMBED_C}
  engine/core/arena.c
  engine/core/assets.c
  engine/core/log.c
  engine/core/time.c
  engine/platform/app_sdl.c
  engine/platform/input_touch.c
  engine/gfx/camera.c
  engine/gfx/gpu.c
  engine/gfx/material.c
  engine/gfx/mesh.c
  engine/gfx/reflection.c
  engine/ui/text.c
)
target_include_directories(mg_engine PUBLIC engine ${CMAKE_BINARY_DIR}/generated)
target_link_libraries(mg_engine PUBLIC SDL3::SDL3-static mg_thirdparty)
target_compile_options(mg_engine PRIVATE ${MG_WARNINGS})
string(TOUPPER "${MG_PLATFORM}" MG_PLATFORM_UPPER)
target_compile_definitions(mg_engine PUBLIC MG_PLATFORM_${MG_PLATFORM_UPPER}=1)
if(MG_GPU_DEBUG)
  target_compile_definitions(mg_engine PRIVATE MG_GPU_DEBUG=1)
endif()

set(MG_GAME_SOURCES
  game/main.c
  game/game.c
  game/devroom.c
)

# --------------------------------------------------------------- targets
if(MG_PLATFORM STREQUAL "desktop")
  add_executable(mobilegame ${MG_GAME_SOURCES})
  target_link_libraries(mobilegame PRIVATE mg_engine)
  target_include_directories(mobilegame PRIVATE game)
  target_compile_options(mobilegame PRIVATE ${MG_WARNINGS})

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
    XCODE_ATTRIBUTE_TARGETED_DEVICE_FAMILY "1"
    XCODE_ATTRIBUTE_IPHONEOS_DEPLOYMENT_TARGET "17.0"
    XCODE_ATTRIBUTE_CODE_SIGN_STYLE "Automatic"
    XCODE_ATTRIBUTE_DEVELOPMENT_TEAM "${MG_IOS_TEAM_ID}"
    XCODE_ATTRIBUTE_CODE_SIGN_IDENTITY "Apple Development"
    XCODE_ATTRIBUTE_ENABLE_BITCODE "NO"
    XCODE_ATTRIBUTE_GCC_C_LANGUAGE_STANDARD "c11"
    XCODE_ATTRIBUTE_ASSETCATALOG_COMPILER_APPICON_NAME "AppIcon"
    XCODE_ATTRIBUTE_SUPPORTED_PLATFORMS "iphoneos"
  )
endif()
