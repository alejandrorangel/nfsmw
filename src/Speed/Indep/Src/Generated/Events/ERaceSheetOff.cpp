#include "ERaceSheetOff.hpp"
#include "Speed/Indep/Src/Frontend/FEngInterfaces/FEngInterface.hpp"

// PC: ERaceSheetOff 0x6242F0 (vtable 0x8A7250), ~ERaceSheetOff 0x624300,
// ERaceSheetOff_MakeEvent_Callback 0x62FF80. Closes the in-game rap sheet.
ERaceSheetOff::ERaceSheetOff()
    : Event(0x10) {}

ERaceSheetOff::~ERaceSheetOff() {
    cFEng::Get()->QueuePackagePop(0);
}

const char *ERaceSheetOff::GetEventName() {
    return "ERaceSheetOff";
}

void ERaceSheetOff_MakeEvent_Callback(const void *staticData) {
    new ERaceSheetOff();
}
