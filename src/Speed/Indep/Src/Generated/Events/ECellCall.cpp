#include "ECellCall.hpp"
#include "EShowSMS.hpp"
#include "Speed/Indep/Src/Frontend/Database/FEDatabase.hpp"
#include "Speed/Indep/Src/Frontend/FEManager.hpp"
#include "Speed/Indep/Src/Lua/source/lua.h"

void EShowSMS_OpenInbox(int showMsg); // EShowSMS.cpp

// PC: ECellCall 0x620F80 (vtable 0x8A691C), ~ECellCall 0x62B180,
// ECellCall_MakeEvent_Callback 0x62B2F0. A voiced SMS: the PC plays it as a
// phone call (MiscSpeech::SMSCellCall) and, in the languages without voiced
// calls, marks it received and opens it in the SMS inbox instead.
// TODO(port): speech isn't ported, so every language gets the inbox.
ECellCall::ECellCall(int pID)
    : Event(0x10), //
      fID(pID) {}

ECellCall::~ECellCall() {
    if (!FEManager::IsOkayToRequestPauseSimulation(0, true, false)) {
        return;
    }
    if (fID >= 0) {
        SMSMessage *message = FEDatabase->GetCareerSettings()->GetSMSMessage(fID);
        if (message != nullptr) {
            message->Flags = 2;
        }
    }
    // The rest of the PC's inbox path is EShowSMS's.
    EShowSMS_OpenInbox(fID);
}

const char *ECellCall::GetEventName() {
    return "ECellCall";
}

void ECellCall_MakeEvent_Callback(const void *staticData) {
    new ECellCall(static_cast<const ECellCall::StaticData *>(staticData)->fID);
}

// The script binding (the PC table gEventLuaBindings): ECellCall(id).
int ECellCall_MakeEvent_LuaBinding(lua_State *luaState) {
    if (lua_gettop(luaState) == 1) {
        new ECellCall(static_cast<int>(lua_tonumber(luaState, 1)));
    }
    return 0;
}
