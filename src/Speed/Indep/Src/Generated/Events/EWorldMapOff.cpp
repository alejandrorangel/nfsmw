#include "EWorldMapOff.hpp"
#include "Speed/Indep/Src/Frontend/FEngInterfaces/FEngInterface.hpp"

// PC: EWorldMapOff (inlined, vtable 0x8A7718), ~EWorldMapOff 0x625A50,
// EWorldMapOff_MakeEvent_Callback 0x632310. Closes the world map.
EWorldMapOff::EWorldMapOff()
    : Event(0x10) {}

EWorldMapOff::~EWorldMapOff() {
    if (cFEng::Get()->IsPackagePushed("WorldMapMain.fng")) {
        cFEng::Get()->QueuePackagePop(0);
    }
}

const char *EWorldMapOff::GetEventName() {
    return "EWorldMapOff";
}

void EWorldMapOff_MakeEvent_Callback(const void *staticData) {
    new EWorldMapOff();
}
