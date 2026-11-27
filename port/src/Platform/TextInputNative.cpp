// Typed characters for the frontend's text fields. On PC, MainLoop 0x663D30
// ends by pumping the window messages (0x6DF8E0 -> 0x6C21A0); TranslateMessage
// turns key presses into WM_CHAR, and the window proc 0x6DB6C0 passes each one
// to KeyboardEditString::NotifyChar (via 0x5A95F0).

#include "Speed/Indep/Src/Frontend/MenuScreens/Common/feKeyboardInput.hpp"

#include "../Mac/InputBridge.h"
#include "Benchmark.h"

void PortDispatchCharMessages() {
    int chars[64];
    int count;
    // NFSMW_BENCHMARK: the scenario's typing instead of the keyboard's.
    while ((count = PortBenchmarkActive() ? PortBenchmarkTakeChars(chars, 64) : PortInputTakeChars(chars, 64)) > 0) {
        for (int i = 0; i < count; i++) {
            gKeyboardManager.NotifyChar(chars[i]);
        }
    }
}
