#include "ERequestEventInfoDialog.hpp"
#include "Speed/Indep/Src/Frontend/FEngInterfaces/FEngInterface.hpp"

// PC: ERequestEventInfoDialog 0x624810 (vtable 0x8A732C), ~ERequestEventInfoDialog
// 0x624830, ERequestEventInfoDialog_MakeEvent_Callback 0x630460. The HUD's
// engage button inside an event's trigger: pushes the event's info dialog
// (accept / decline) with the race activity as the screen's argument.
ERequestEventInfoDialog::ERequestEventInfoDialog(int pJoyPort, GRuntimeInstance *pRaceActivity)
    : Event(0x10), //
      fJoyPort(pJoyPort), //
      fRaceActivity(pRaceActivity) {}

ERequestEventInfoDialog::~ERequestEventInfoDialog() {
    cFEng::Get()->QueuePackagePush("EngageEventDialog.fng", reinterpret_cast<intptr_t>(fRaceActivity), 0, false);
}

const char *ERequestEventInfoDialog::GetEventName() {
    return "ERequestEventInfoDialog";
}

void ERequestEventInfoDialog_MakeEvent_Callback(const void *staticData) {
    const ERequestEventInfoDialog::StaticData *data = static_cast<const ERequestEventInfoDialog::StaticData *>(staticData);
    new ERequestEventInfoDialog(data->fJoyPort, data->fRaceActivity);
}
