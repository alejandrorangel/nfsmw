#ifndef FENGMOUSE_H
#define FENGMOUSE_H

// PC-only frontend mouse (speed.exe 1.3). The console builds have no mouse;
// none of these names survive in the PC binary, so they are descriptive.

#ifdef EA_PLATFORM_WIN32

#include "Speed/Indep/bWare/Inc/bMath.hpp"

class FEObject;

// The PC mouse state, one global object @ 0x987F50 (0x2C bytes). Updated once
// per frame at the end of eDisplayFrame (0x6E7667); the frontend widgets read it
// directly, and FEPackageManager::Tick broadcasts FEMSG_MOUSE_CHANGED whenever
// HasChanged().
struct FEMouseState {
    int Unused;          // 0x00
    int X, Y;            // 0x04: cursor in the 640x480 frontend screen
    int PrevX, PrevY;    // 0x0C
    int DeltaX, DeltaY;  // 0x14
    int Wheel;           // 0x1C: DirectInput lZ this frame (+120 a notch away from the user)
    bool LeftDown;       // 0x20
    bool LeftPressed;    // 0x21: went down this frame
    bool LeftReleased;   // 0x22: went up this frame
    bool RightDown;      // 0x23
    bool RightPressed;   // 0x24
    bool RightReleased;  // 0x25
    bool MiddleDown;     // 0x26
    bool MiddlePressed;  // 0x27
    bool MiddleReleased; // 0x28

    // Platform: cursor position (0x6C9B60) and the whole update (0x6C9C80).
    void UpdateCursorPos();
    void Update();
    bool HasChanged() const; // 0x6C9D00
};

extern FEMouseState gFEMouseState;
// 0x91CFB9: set while a screen (the world map) draws its own cursor.
extern bool gFEMouseCursorHidden;

// Cursor relative to the centre of the 640x480 screen (FEng's object space).
void FEngGetMousePos(float &x, float &y); // 0x515960
void FEngGetMouseWheel(float &delta);     // 0x515990

// Button state and the cursor inside a rectangle (top-left, size) ...
bool FEngMouseLeftDownIn(const bVector2 &top_left, const bVector2 &size);      // 0x5159A0
bool FEngMouseLeftPressedIn(const bVector2 &top_left, const bVector2 &size);   // 0x5159F0
bool FEngMouseLeftReleasedIn(const bVector2 &top_left, const bVector2 &size);  // 0x515A40
bool FEngMouseRightDownIn(const bVector2 &top_left, const bVector2 &size);     // 0x515A90
bool FEngMouseRightPressedIn(const bVector2 &top_left, const bVector2 &size);  // 0x515AE0
bool FEngMouseRightReleasedIn(const bVector2 &top_left, const bVector2 &size); // 0x515B30
// ... or over an object.
bool FEngMouseLeftDownOver(FEObject *obj);      // 0x54E840
bool FEngMouseLeftPressedOver(FEObject *obj);   // 0x54E890
bool FEngMouseLeftReleasedOver(FEObject *obj);  // 0x54E8E0
bool FEngMouseRightDownOver(FEObject *obj);     // 0x54E930
bool FEngMouseRightPressedOver(FEObject *obj);  // 0x54E980
bool FEngMouseRightReleasedOver(FEObject *obj); // 0x54E9D0

// Draws the PC_CURSOR texture at a frontend-screen position (0x56FDB0).
void FEngDrawMouseCursor(int x, int y);

#endif

#endif
