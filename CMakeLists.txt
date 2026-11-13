cmake_minimum_required(VERSION 3.22)
# macOS: the oldest system the app runs on, Akhenaten's default (its CI: 10.15 for x86_64, 11.7 for arm64); set
# before project() as CMake asks
set(CMAKE_OSX_DEPLOYMENT_TARGET "10.15" CACHE STRING "The oldest macOS the program runs on")
project(tutankhamun C CXX)

# Tutankhamun: tutankhamun, the game of Pharaoh + Cleopatra in C. The game (src/game), the platform layer
# (src/platform) and the program around them, SDL2 and SDL2_mixer fetched and linked statically, Akhenaten's Bink
# decoder for the videos and parson for the JSON configs. Windows (MSVC, Win32 or x64), Linux (GCC or Clang, 64-bit;
# the AppImage: packaging/linux), macOS (Apple Clang; the app bundle Tutankhamun.app for one architecture,
# -DCMAKE_OSX_ARCHITECTURES=arm64 or x86_64, as Akhenaten builds; the disk image: packaging/macos) and Android (the
# NDK through the Gradle project in android/: the game is libmain.so beside a shared SDL2 and SDL2_mixer) and the
# browser (Emscripten: emcmake cmake; tutankhamun.html, .js and .wasm with Emscripten's SDL2 ports).
if(NOT CMAKE_BUILD_TYPE AND NOT CMAKE_CONFIGURATION_TYPES)
    set(CMAKE_BUILD_TYPE Release)
endif()

if(MSVC)
    set(CMAKE_C_STANDARD 99)
else()
    set(CMAKE_C_STANDARD 11)
endif()
if(ANDROID)
    set(CMAKE_POSITION_INDEPENDENT_CODE ON)
endif()
if(MSVC)
    set(CMAKE_MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>")
    add_compile_options(/W4 /wd4100)
    add_compile_definitions(_CRT_SECURE_NO_WARNINGS WIN32_LEAN_AND_MEAN)
    set(TUTANKHAMUN_THIRD_PARTY_WARNINGS /W1)
else()
    add_compile_options(-Wall -Wextra -Wno-unused-parameter -Wno-sign-compare -Wno-missing-field-initializers -Wno-missing-braces -fno-strict-aliasing)
    set(TUTANKHAMUN_THIRD_PARTY_WARNINGS -w)
endif()

file(GLOB_RECURSE GAME_SOURCES CONFIGURE_DEPENDS src/game/*.c)

# The platform: SDL2 window, input and SDL_mixer audio; static SDL2 + SDL2_mixer built once per build folder
# (cmake/TutankhamunBuildSDL.cmake; -DTUTANKHAMUN_SDL_LIBS_DIR=<dir> shares the built libraries between build folders),
# shared ones on Android (cmake/TutankhamunAndroidSDL.cmake).
add_library(tutankhamun_platform STATIC EXCLUDE_FROM_ALL
    src/platform/screen.c
    src/platform/sound_device.c
)
target_include_directories(tutankhamun_platform PUBLIC src PRIVATE src/game)

if(EMSCRIPTEN)
    # the browser: SDL2 and SDL2_mixer are Emscripten's own ports (as in Julius), WAV and MP3 as on the other systems
    set(TUTANKHAMUN_EMSCRIPTEN_SDL "SHELL:-sUSE_SDL=2" "SHELL:-sUSE_SDL_MIXER=2" "SHELL:-sSDL2_MIXER_FORMATS=[\"mp3\"]")
    target_compile_options(tutankhamun_platform PUBLIC ${TUTANKHAMUN_EMSCRIPTEN_SDL})
    target_link_options(tutankhamun_platform PUBLIC ${TUTANKHAMUN_EMSCRIPTEN_SDL})
elseif(ANDROID)
    include(${CMAKE_SOURCE_DIR}/cmake/TutankhamunAndroidSDL.cmake)
    if(NOT TUTANKHAMUN_SDL_READY)
        message(FATAL_ERROR "SDL2 / SDL2_mixer did not become ready")
    endif()
    target_link_libraries(tutankhamun_platform PUBLIC SDL2_mixer::SDL2_mixer SDL2::SDL2)
    find_library(ANDROID_LOG_LIBRARY log)
    find_library(ANDROID_SYSTEM_LIBRARY android)
    target_link_libraries(tutankhamun_platform PUBLIC ${ANDROID_LOG_LIBRARY} ${ANDROID_SYSTEM_LIBRARY} ${CMAKE_DL_LIBS} m)
else()
    include(${CMAKE_SOURCE_DIR}/cmake/TutankhamunBuildSDL.cmake)
    if(NOT TUTANKHAMUN_SDL_READY)
        message(FATAL_ERROR "SDL2 / SDL2_mixer did not become ready")
    endif()
    target_include_directories(tutankhamun_platform PUBLIC
        ${SDL2_INCLUDE_DIR}
        ${SDL2_INCLUDE_ADD_DIR}
        ${SDL2_MIXER_INCLUDE_DIR}
    )
    target_link_libraries(tutankhamun_platform PUBLIC
        ${SDL2_MIXER_LIBRARY}
        ${SDL2_LIBRARY}
    )
    if(SDL2_MAIN_LIBRARY)
        target_link_libraries(tutankhamun_platform PUBLIC ${SDL2_MAIN_LIBRARY})
    endif()
    if(WIN32)
        target_link_libraries(tutankhamun_platform PUBLIC winmm version imm32 setupapi)
    elseif(APPLE)
        # the frameworks of the static SDL2: Akhenaten's list (CMakeLists.txt, "Apple specific"), with the others of
        # SDL2's sdl2.pc (AVFoundation, Foundation, QuartzCore, iconv)
        target_link_libraries(tutankhamun_platform PUBLIC
            "-framework Cocoa" "-framework Carbon" "-framework IOKit" "-framework CoreFoundation"
            "-framework ForceFeedback" "-framework Metal" "-framework MetalKit" "-framework CoreVideo"
            "-framework CoreAudio" "-framework AudioToolbox" "-framework CoreHaptics" "-framework GameController"
            "-framework AVFoundation" "-framework Foundation" "-framework QuartzCore" iconv m)
    else()
        find_package(Threads REQUIRED)
        target_link_libraries(tutankhamun_platform PUBLIC Threads::Threads ${CMAKE_DL_LIBS} m)
    endif()
endif()

# The videos: Bink 1 on Akhenaten's decoder (C++, src/third_party/akhenaten_bink, AGPL-3.0).
add_library(akhenaten_bink STATIC EXCLUDE_FROM_ALL
    src/third_party/akhenaten_bink/bink.cpp
    src/third_party/akhenaten_bink/bink_c.cpp
)
set_target_properties(akhenaten_bink PROPERTIES CXX_STANDARD 17 CXX_STANDARD_REQUIRED ON POSITION_INDEPENDENT_CODE ON)
target_include_directories(akhenaten_bink PRIVATE src/third_party/akhenaten_bink)
target_compile_options(akhenaten_bink PRIVATE ${TUTANKHAMUN_THIRD_PARTY_WARNINGS})
target_link_libraries(akhenaten_bink PRIVATE tutankhamun_platform)

# The gameplay configs: parson 1.5.3 (C, MIT, src/third_party/parson).
add_library(parson STATIC src/third_party/parson/parson.c)
set_target_properties(parson PROPERTIES POSITION_INDEPENDENT_CODE ON)
target_compile_options(parson PRIVATE ${TUTANKHAMUN_THIRD_PARTY_WARNINGS})

if(WIN32)
    set(TUTANKHAMUN_SYSTEM_SOURCES res/tutankhamun.rc src/app/port.c src/platform/platform_win32.c)
elseif(ANDROID)
    set(TUTANKHAMUN_SYSTEM_SOURCES src/app/port_posix.c src/platform/platform_posix.c src/platform/platform_android.c)
else()
    set(TUTANKHAMUN_SYSTEM_SOURCES src/app/port_posix.c src/platform/platform_posix.c)
endif()
# Discord's presence talks to the Discord client over a local pipe: a browser has none
if(EMSCRIPTEN)
    set(TUTANKHAMUN_DISCORD_SOURCE src/platform/discord_rpc_none.c)
else()
    set(TUTANKHAMUN_DISCORD_SOURCE src/platform/discord_rpc.c)
endif()

set(TUTANKHAMUN_SOURCES
    ${TUTANKHAMUN_SYSTEM_SOURCES}
    src/app/app_files.c
    src/app/app_icon.c
    src/app/game_dir.c
    src/app/main.c
    src/app/media_audio.c
    src/app/media_video.c
    src/app/miles_sdl.c
    src/app/play.c
    src/app/play_cursors.c
    src/app/startup.c
    src/app/video_bink.c
    src/platform/cursor.c
    ${TUTANKHAMUN_DISCORD_SOURCE}
    src/platform/log.c
    src/platform/platform_display.c
    src/platform/platform_file.c
    src/platform/platform_screen.c
    src/platform/http.c
    src/platform/report.c
    src/platform/updater.c
    src/platform/version.c
)

# a call that does not match its prototype, or a call without one, is an error (the game and the program)
if(MSVC)
    set(TUTANKHAMUN_STRICT_CALLS /we4113 /we4028 /we4047 /we4024 /we4133 /we4020 /we4029 /we4021 /we4013)
else()
    set(TUTANKHAMUN_STRICT_CALLS -Werror=implicit-function-declaration -Werror=int-conversion -Werror=incompatible-pointer-types
        -Werror=return-type -Werror=pointer-to-int-cast -Werror=int-to-pointer-cast)
endif()

# The game (src/game) as a library of its own, linked into the program: it calls the platform layer, parson and the
# functions the program defines for it (the play mode's messages, the log, the files).
add_library(tutankhamun_game STATIC ${GAME_SOURCES})
target_include_directories(tutankhamun_game PUBLIC src src/game)
target_compile_options(tutankhamun_game PRIVATE ${TUTANKHAMUN_STRICT_CALLS})
target_link_libraries(tutankhamun_game PUBLIC parson tutankhamun_platform)

# The build's version (src/platform/version.c, the front menu): 0.1.<the number of commits>, the commit and the day,
# read at configure time as in Akhenaten; the CI checks out the whole history for the count.
find_package(Git QUIET)
set(TUTANKHAMUN_BUILD_NUMBER 0)
set(TUTANKHAMUN_COMMIT unknown)
if(GIT_FOUND)
    execute_process(COMMAND ${GIT_EXECUTABLE} rev-list --count HEAD WORKING_DIRECTORY "${CMAKE_SOURCE_DIR}"
        OUTPUT_VARIABLE git_count OUTPUT_STRIP_TRAILING_WHITESPACE ERROR_QUIET RESULT_VARIABLE git_result)
    if(git_result EQUAL 0 AND git_count MATCHES "^[0-9]+$")
        set(TUTANKHAMUN_BUILD_NUMBER ${git_count})
    endif()
    execute_process(COMMAND ${GIT_EXECUTABLE} rev-parse --short=7 HEAD WORKING_DIRECTORY "${CMAKE_SOURCE_DIR}"
        OUTPUT_VARIABLE git_commit OUTPUT_STRIP_TRAILING_WHITESPACE ERROR_QUIET RESULT_VARIABLE git_result)
    if(git_result EQUAL 0 AND git_commit)
        set(TUTANKHAMUN_COMMIT ${git_commit})
    endif()
endif()
string(TIMESTAMP TUTANKHAMUN_BUILD_DATE "%Y-%m-%d" UTC)
message(STATUS "Tutankhamun 0.1.${TUTANKHAMUN_BUILD_NUMBER} (${TUTANKHAMUN_COMMIT}) ${TUTANKHAMUN_BUILD_DATE}")
set_source_files_properties(src/platform/version.c PROPERTIES COMPILE_DEFINITIONS
    "TUTANKHAMUN_BUILD_NUMBER=${TUTANKHAMUN_BUILD_NUMBER};TUTANKHAMUN_COMMIT=\"${TUTANKHAMUN_COMMIT}\";TUTANKHAMUN_BUILD_DATE=\"${TUTANKHAMUN_BUILD_DATE}\"")

# The program's own update (src/platform/updater.c): on by default in the CI's builds only, whose files the release
# "nightly" holds, so a build of one's own is never replaced by them.
if(DEFINED ENV{GITHUB_ACTIONS})
    set(updater_default ON)
else()
    set(updater_default OFF)
endif()
option(TUTANKHAMUN_UPDATER "The program updates itself from the release nightly (Windows)" ${updater_default})
if(TUTANKHAMUN_UPDATER AND WIN32)
    set_source_files_properties(src/platform/updater.c PROPERTIES COMPILE_DEFINITIONS TUTANKHAMUN_UPDATER)
endif()
if(ANDROID)
    # SDLActivity loads the game as libmain.so and calls its main (TutankhamunActivity.getMainFunction)
    add_library(tutankhamun SHARED ${TUTANKHAMUN_SOURCES})
    set_target_properties(tutankhamun PROPERTIES OUTPUT_NAME main)
elseif(APPLE)
    # The app bundle Tutankhamun.app: Contents/MacOS/Tutankhamun, the icon and the configs in Contents/Resources
    # (packaging/macos: the Info.plist template, the disk image).
    set(TUTANKHAMUN_ICNS "${CMAKE_BINARY_DIR}/tutankhamun.icns")
    find_program(SIPS_PROGRAM sips)
    find_program(ICONUTIL_PROGRAM iconutil)
    if(SIPS_PROGRAM AND ICONUTIL_PROGRAM AND NOT EXISTS "${TUTANKHAMUN_ICNS}")
        set(iconset "${CMAKE_BINARY_DIR}/tutankhamun.iconset")
        file(MAKE_DIRECTORY "${iconset}")
        foreach(icon 16:icon_16x16 32:icon_16x16@2x 32:icon_32x32 64:icon_32x32@2x 128:icon_128x128
                     256:icon_128x128@2x 256:icon_256x256)
            string(REPLACE ":" ";" icon "${icon}")
            list(GET icon 0 icon_size)
            list(GET icon 1 icon_name)
            execute_process(COMMAND ${SIPS_PROGRAM} -z ${icon_size} ${icon_size} "${CMAKE_SOURCE_DIR}/res/tutankhamun_256.png"
                --out "${iconset}/${icon_name}.png" OUTPUT_QUIET ERROR_QUIET)
        endforeach()
        execute_process(COMMAND ${ICONUTIL_PROGRAM} -c icns "${iconset}" -o "${TUTANKHAMUN_ICNS}")
        file(REMOVE_RECURSE "${iconset}")
    endif()
    set(TUTANKHAMUN_BUNDLE_FILES "")
    if(EXISTS "${TUTANKHAMUN_ICNS}")
        set(TUTANKHAMUN_BUNDLE_FILES "${TUTANKHAMUN_ICNS}")
        set_source_files_properties("${TUTANKHAMUN_ICNS}" PROPERTIES MACOSX_PACKAGE_LOCATION Resources)
    endif()
    add_executable(tutankhamun MACOSX_BUNDLE ${TUTANKHAMUN_SOURCES} ${TUTANKHAMUN_BUNDLE_FILES})
    set_target_properties(tutankhamun PROPERTIES
        OUTPUT_NAME Tutankhamun
        MACOSX_BUNDLE_INFO_PLIST "${CMAKE_SOURCE_DIR}/packaging/macos/Info.plist.in"
        MACOSX_BUNDLE_BUNDLE_NAME Tutankhamun
        MACOSX_BUNDLE_GUI_IDENTIFIER com.github.dalerank.tutankhamun
        MACOSX_BUNDLE_ICON_FILE tutankhamun.icns
        MACOSX_BUNDLE_BUNDLE_VERSION 0.1.${TUTANKHAMUN_BUILD_NUMBER}
        MACOSX_BUNDLE_SHORT_VERSION_STRING 0.1.${TUTANKHAMUN_BUILD_NUMBER})
else()
    add_executable(tutankhamun ${TUTANKHAMUN_SOURCES})
endif()
target_include_directories(tutankhamun PRIVATE src src/game)
target_compile_options(tutankhamun PRIVATE ${TUTANKHAMUN_STRICT_CALLS})
if(MSVC)
    target_link_options(tutankhamun PRIVATE /DYNAMICBASE /INCREMENTAL:NO)
    target_link_libraries(tutankhamun PRIVATE winmm winhttp)
elseif(EMSCRIPTEN)
    # The browser build (res/web/shell.html: the game folder the user picks is copied into IndexedDB): tutankhamun.html,
    # .js and .wasm. The game's own loops (the frames, the message box, the videos) stay as they are: ASYNCIFY lets
    # them give the browser its turn (play.c waits for the next animation frame, SDL_Delay sleeps). main is called by
    # the page with the game folder; the gameplay configs are built in at /app/config (app_files.c).
    set_target_properties(tutankhamun PROPERTIES SUFFIX ".html")
    target_link_options(tutankhamun PRIVATE
        "SHELL:-sASYNCIFY=1"
        "SHELL:-sASYNCIFY_STACK_SIZE=1048576"
        "SHELL:-sSTACK_SIZE=8388608"
        "SHELL:-sINITIAL_MEMORY=268435456"
        "SHELL:-sALLOW_MEMORY_GROWTH=1"
        "SHELL:-sFORCE_FILESYSTEM=1"
        "SHELL:-sINVOKE_RUN=0"
        "SHELL:-sEXIT_RUNTIME=1"
        "SHELL:-sEXPORTED_RUNTIME_METHODS=[\"callMain\",\"FS\"]"
        -lidbfs.js
        "SHELL:--shell-file ${CMAKE_SOURCE_DIR}/res/web/shell.html"
        "SHELL:--embed-file ${CMAKE_SOURCE_DIR}/config@/app/config")
    set_property(TARGET tutankhamun APPEND PROPERTY LINK_DEPENDS "${CMAKE_SOURCE_DIR}/res/web/shell.html")
elseif(NOT ANDROID AND NOT APPLE)
    # the C++ run time of the Bink decoder is linked in: the program needs only the system's C library
    target_link_options(tutankhamun PRIVATE -static-libstdc++ -static-libgcc -rdynamic)
endif()
target_compile_definitions(tutankhamun PRIVATE SDL_MAIN_HANDLED)
target_link_libraries(tutankhamun PRIVATE tutankhamun_game parson tutankhamun_platform akhenaten_bink)

if(ANDROID)
    # the configs go into the APK's assets (android/app/build.gradle) and are copied to the app's storage at start
    return()
endif()

set_target_properties(tutankhamun PROPERTIES RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/bin)

if(EMSCRIPTEN)
    # the configs are in the program (--embed-file above); bin/ holds the page to serve
    return()
endif()

# The gameplay configs (config/*.json) are copied beside the program on every build (macOS: into the bundle's
# Contents/Resources).
if(APPLE)
    set(TUTANKHAMUN_CONFIG_OUT "$<TARGET_BUNDLE_CONTENT_DIR:tutankhamun>/Resources/config")
else()
    set(TUTANKHAMUN_CONFIG_OUT "$<TARGET_FILE_DIR:tutankhamun>/config")
endif()
add_custom_target(tutankhamun_configs COMMAND ${CMAKE_COMMAND} -E copy_directory
    "${CMAKE_SOURCE_DIR}/config" "${TUTANKHAMUN_CONFIG_OUT}")
add_dependencies(tutankhamun tutankhamun_configs)

if(APPLE)
    # `cmake --install` with a prefix: Tutankhamun.app in it (packaging/macos/build_dmg.sh)
    install(TARGETS tutankhamun BUNDLE DESTINATION .)
elseif(WIN32)
    # The game folder (`cmake --install`, build.bat): tutankhamun.exe and config\ beside it.
    install(TARGETS tutankhamun RUNTIME DESTINATION .)
    install(DIRECTORY config/ DESTINATION config)
else()
    # The Linux tree (`cmake --install` with a prefix, the AppDir of the AppImage): bin/tutankhamun, the configs in
    # share/tutankhamun/config, the desktop entry, the icons and the AppStream metadata.
    include(GNUInstallDirs)
    install(TARGETS tutankhamun RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR})
    install(DIRECTORY config/ DESTINATION ${CMAKE_INSTALL_DATADIR}/tutankhamun/config)
    install(FILES packaging/linux/tutankhamun.desktop DESTINATION ${CMAKE_INSTALL_DATADIR}/applications)
    install(FILES packaging/linux/tutankhamun.metainfo.xml DESTINATION ${CMAKE_INSTALL_DATADIR}/metainfo)
    install(FILES res/tutankhamun_256.png DESTINATION ${CMAKE_INSTALL_DATADIR}/icons/hicolor/256x256/apps RENAME tutankhamun.png)
    install(FILES res/tutankhamun.svg DESTINATION ${CMAKE_INSTALL_DATADIR}/icons/hicolor/scalable/apps RENAME tutankhamun.svg)
endif()
