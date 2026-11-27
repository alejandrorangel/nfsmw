#include "EShowSMS.hpp"
#include "ESndGameState.hpp"
#include "Speed/Indep/Src/Frontend/Database/FEDatabase.hpp"
#include "Speed/Indep/Src/Frontend/FEManager.hpp"
#include "Speed/Indep/Src/Frontend/FEngFrontend.hpp"
#include "Speed/Indep/Src/Frontend/FEngInterfaces/FEngInterface.hpp"

// PC: EShowSMS 0x631420 (vtable 0x8A7B38), EShowSMS_MakeEvent_Callback
// 0x631580. The constructor does the work: when the game can pause, the SMS
// inbox (SMS_Mailboxes.fng) opens on the message (or on the list for a
// negative ID), for player 1's pad.
void EShowSMS_OpenInbox(int showMsg) {
    if (!FEManager::IsOkayToRequestPauseSimulation(0, true, false)) {
        return;
    }
    new ESndGameState(8, true);
    u32 controlMask = FEngMapJoyportToJoyParam(FEDatabase->GetPlayersJoystickPort(0));
    if (cFEng::Get()->IsPackagePushed("SMS_Mailboxes.fng")) {
        return;
    }
    intptr_t arg = 0;
    if (showMsg >= 0) {
        arg = reinterpret_cast<intptr_t>(FEDatabase->GetCareerSettings()->GetSMSMessage(showMsg));
    }
    cFEng::Get()->QueuePackagePush("SMS_Mailboxes.fng", arg, controlMask, false);
}

EShowSMS::EShowSMS(int pShowMsg)
    : Event(0x10), //
      fShowMsg(pShowMsg) {
    EShowSMS_OpenInbox(pShowMsg);
}

EShowSMS::~EShowSMS() {}

const char *EShowSMS::GetEventName() {
    return "EShowSMS";
}

void EShowSMS_MakeEvent_Callback(const void *staticData) {
    new EShowSMS(static_cast<const EShowSMS::StaticData *>(staticData)->fShowMsg);
}
