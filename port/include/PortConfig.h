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
#include <cmath>
#else
#include <stddef.h>
#include <stdint.h>
#include <math.h>
#endif
// Pull in libc declarations before game headers can #define over their names
// (e.g. Lua's ldebug.h defines a getline() macro).
#include <stdio.h>
#include <string.h>
#include <strings.h>

// MSVC CRT names used by the PC code.
#define stricmp strcasecmp
#define strnicmp strncasecmp
#define _stricmp strcasecmp
#define _strnicmp strncasecmp

#define PORT_WEAK __attribute__((weak))

#ifdef __cplusplus
#include "PortPtr32.h"
// Pointer fields in load-in-place data: 4-byte compressed pointers natively.
#define PORT_PTR32(T) PortPtr32<T>
#endif

// Float -> unsigned char/short as speed.exe does it: through a 32-bit int,
// so negative values wrap (see src/types.h).
#define PORT_FTOU(T, x) static_cast<T>(static_cast<int>(x))

static inline int isnanf(float f) {
    return __builtin_isnan(f);
}

#endif // PORT_CONFIG_H
