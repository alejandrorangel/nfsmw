cmake_minimum_required(VERSION 3.24)

# Native macOS (Apple Silicon) + Metal port of NFS: Most Wanted.
# This build lives beside the matching decomp build (configure.py/ninja) and
# never touches it. Configure from the repo root with:
#   cmake -S port -B build/port -G Ninja
project(nfsmw_port LANGUAGES C CXX OBJC OBJCXX)

set(CMAKE_OSX_ARCHITECTURES arm64 CACHE STRING "" FORCE)
set(CMAKE_OSX_DEPLOYMENT_TARGET 14.0 CACHE STRING "")
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)
if(NOT CMAKE_BUILD_TYPE)
    set(CMAKE_BUILD_TYPE Debug CACHE STRING "" FORCE) # the port is in bring-up; keep it debuggable
endif()

get_filename_component(NFSMW_ROOT "${CMAKE_CURRENT_SOURCE_DIR}/.." ABSOLUTE)
set(NFSMW_SRC "${NFSMW_ROOT}/src")

option(NFSMW_PORT_LINK_GAME "Link decompiled game code (+ generated stubs) into the app" ON)

# ---------------------------------------------------------------------------
# Game code (decompiled, platform independent). Same unity source lists the
# matching build uses for SPEED_EXE_1_3.
# ---------------------------------------------------------------------------
set(NFSMW_GAME_UNITS
    zAI zAnim zAttribSys zBWare zCamera zComms zDebug zDynamics zEagl4Anim zFeOverlay
    zEAXSound zEAXSound2 zEcstasy zFe zFe2 zFEng zFoundation zGameModes
    zGameplay zLua zMain zMisc zMiscSmall zMission zPhysics zPhysicsBehaviors
    zPlatform zRender zSim zSpeech zTrack zWorld zWorld2
)

set(NFSMW_GAME_INCLUDES
    "${CMAKE_CURRENT_SOURCE_DIR}/include/shims"
    "${CMAKE_CURRENT_SOURCE_DIR}/include"
    "${NFSMW_SRC}"
    "${NFSMW_SRC}/Packages"
    "${NFSMW_SRC}/Speed/Indep/Libs/allocator/1.5.0"
    "${NFSMW_SRC}/Speed/Indep/Libs/csis/dev/include"
    "${NFSMW_SRC}/Packages/eathread/1.1.0/include"
    "${NFSMW_SRC}/Speed/Indep/Libs/snd/9/include"
    "${NFSMW_SRC}/Speed/Indep/Libs/spch/dev/include"
    "${NFSMW_SRC}/Speed/Indep/Libs/path/5.01.04/include"
    "${NFSMW_SRC}/Speed/Indep/Libs/realcore/6.24.00/include/common"
    "${NFSMW_SRC}/Speed/Indep/Libs/endian/0.5.2/include"
    "${NFSMW_SRC}/Speed/Indep/Libs/realgraph/6.09.01/include/common"
    "${NFSMW_SRC}/Speed/Indep/Libs/rcmp/4.00.11/include"
    "${NFSMW_SRC}/Packages/realmemcard/3.04.01-layer2/include/common"
)

set(NFSMW_GAME_DEFINES
    LUA_NUMBER=float
    DEFAULT_ALLOCATOR=0
    BUILD_VERSION=4
    VERSION_SPEED_EXE_1_3
)

set(NFSMW_GAME_FLAGS
    -include "${CMAKE_CURRENT_SOURCE_DIR}/include/PortConfig.h"
    -fms-extensions          # __int16/__int64, MS-isms in decompiled code
    -fdeclspec               # __declspec(align(n))
    -fno-strict-aliasing     # original code type-puns freely
    -fwrapv
    -w                       # warnings off for now; the report tracks errors
    -Wno-address-of-temporary # MSVC-ism: &T(...) passed to a function
    -ferror-limit=0
)

set(NFSMW_GAME_SOURCES "")
foreach(unit IN LISTS NFSMW_GAME_UNITS)
    list(APPEND NFSMW_GAME_SOURCES "${NFSMW_SRC}/Speed/Indep/SourceLists/${unit}.cpp")
endforeach()

add_library(nfsmw_game OBJECT ${NFSMW_GAME_SOURCES})
set_target_properties(nfsmw_game PROPERTIES CXX_STANDARD 14 CXX_EXTENSIONS ON)
target_include_directories(nfsmw_game PRIVATE ${NFSMW_GAME_INCLUDES})
target_compile_definitions(nfsmw_game PRIVATE ${NFSMW_GAME_DEFINES})
target_compile_options(nfsmw_game PRIVATE ${NFSMW_GAME_FLAGS})
# Decomp placeholders like `int f() {}` silently return garbage. In Debug,
# trap them with file:line instead.
option(NFSMW_PORT_CHECK_MISSING_RETURN "Report decomp functions that fall off the end without returning" ON)
if(NFSMW_PORT_CHECK_MISSING_RETURN)
    target_compile_options(nfsmw_game PRIVATE $<$<CONFIG:Debug>:-fsanitize=return>)
endif()

# ---------------------------------------------------------------------------
# Native platform layer: replaces EA SDK / Win32 pieces the game links against.
# Same includes as the game, but with warnings on since this code is ours.
# ---------------------------------------------------------------------------
add_library(nfsmw_platform OBJECT
    src/Platform/RealCoreNative.cpp
    src/Platform/TexturePackNative.cpp
    src/Platform/TexturePlatMetal.cpp
    src/Platform/FERenderMetal.cpp
    src/Platform/eMathNative.cpp
    src/Platform/PortPtr32.cpp
    src/Platform/LanguageNative.cpp
    src/Platform/RealFontNative.cpp
    src/Platform/SolidNative.cpp
    src/Platform/SceneNative.cpp
    src/Platform/ViewsNative.cpp
    src/Platform/EnvMapNative.cpp
    src/Platform/VisualTreatmentNative.cpp
    src/Platform/GraphicsOptionsNative.cpp
    src/Game/SelectCarCameraMover.cpp
    src/Game/CameraDriveNative.cpp
    src/Game/ICENative.cpp
    src/Game/USpline.cpp
    src/Game/StringToNumberNative.cpp
    src/Game/AttribDefaultsNative.cpp
    src/Game/UComNative.cpp
    src/Game/GInfractionManagerNative.cpp
    src/Audio/EAAudioFormat.cpp
    src/Audio/AudioMixer.cpp
    src/Audio/SpchNative.cpp
    src/Audio/CsisNative.cpp
    src/Audio/SndNative.cpp
    src/Game/EventRegistryNative.cpp
    src/Platform/InputNative.cpp
    src/Platform/InputBindingsNative.cpp
    src/Data/RecoveredData.cpp # generated by tools/gen_data.py
    src/Data/RecoveredObjects.cpp
)
set_target_properties(nfsmw_platform PROPERTIES CXX_STANDARD 17)
target_include_directories(nfsmw_platform PRIVATE ${NFSMW_GAME_INCLUDES})
target_compile_definitions(nfsmw_platform PRIVATE ${NFSMW_GAME_DEFINES})
target_compile_options(nfsmw_platform PRIVATE
    -include "${CMAKE_CURRENT_SOURCE_DIR}/include/PortConfig.h"
    -fms-extensions -fdeclspec -Wall -Wno-unused-parameter -Wno-mismatched-tags
)

# ---------------------------------------------------------------------------
# macOS platform layer + app bundle.
# ---------------------------------------------------------------------------
add_executable(nfsmw MACOSX_BUNDLE
    src/Mac/main.mm
    src/Mac/MetalRenderer.mm
    src/Mac/InputMac.mm
    src/Mac/InputPadMac.mm
    src/Mac/AudioOutputMac.mm
)
set_target_properties(nfsmw PROPERTIES
    OUTPUT_NAME "NFSMW"
    MACOSX_BUNDLE_GUI_IDENTIFIER "com.alejandrorangel.nfsmw"
    MACOSX_BUNDLE_BUNDLE_NAME "NFSMW"
)
target_compile_options(nfsmw PRIVATE -fobjc-arc)
if(NFSMW_PORT_CHECK_MISSING_RETURN)
    target_link_options(nfsmw PRIVATE $<$<CONFIG:Debug>:-fsanitize=return>)
endif()
target_link_libraries(nfsmw PRIVATE
    "-framework Cocoa"
    "-framework Metal"
    "-framework MetalKit"
    "-framework QuartzCore"
    "-framework GameController"
    "-framework CoreFoundation"
    "-framework AudioToolbox"
)
if(NFSMW_PORT_LINK_GAME)
    # Generated by port/tools/gen_stubs.py: weak stand-ins for every symbol
    # the decompiled code references but doesn't define yet.
    target_sources(nfsmw PRIVATE
        $<TARGET_OBJECTS:nfsmw_game>
        $<TARGET_OBJECTS:nfsmw_platform>
        src/Stubs/PortStubs.cpp
        src/Stubs/GeneratedStubs.S
    )
    enable_language(ASM)
endif()
