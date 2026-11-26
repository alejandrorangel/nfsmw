// PortConfig.h - force-included (-include) into every translation unit of the
// native macOS/Metal build. Nothing here affects the matching decomp build.
//
// Strategy: the port compiles the game as the PC (EA_PLATFORM_WIN32) flavour,
// because the PC Black Edition assets are what we load, and they are
// little-endian like arm64. Port-specific behaviour is selected with
// EA_PORT_NATIVE / EA_PORT_MACOS so upstream #ifdef chains stay untouched.
#ifndef PORT_CONFIG_H
#define PORT_CONFIG_H

#define EA_PORT_NATIVE 1
#define EA_PORT_MACOS 1

#ifndef EA_PLATFORM_WIN32
#define EA_PLATFORM_WIN32 1
#endif
#ifndef EA_REGION_AMERICA
#define EA_REGION_AMERICA 1
#endif

#ifndef _USE_MATH_DEFINES
#define _USE_MATH_DEFINES 1
#endif

#ifdef __cplusplus
#include <cstddef>
#include <cstdint>
#else
#include <stddef.h>
#include <stdint.h>
#endif

#endif // PORT_CONFIG_H
