#include "EEnterBin.hpp"
#include "EAwardUpgrade.hpp"
#include "Speed/Indep/Src/Frontend/Database/FEDatabase.hpp"
#include "Speed/Indep/Src/Gameplay/GManager.h"
#include "Speed/Indep/Src/Gameplay/GRaceDatabase.h"
#include "Speed/Indep/Src/Gameplay/GRaceStatus.h"
#include "Speed/Indep/Src/Gameplay/GVault.h"
#include "Speed/Indep/Src/Lua/source/lua.h"

void ClearAllNewStatus();

// PC: EEnterBin 0x62C2D0, EEnterBin_MakeEvent_Callback 0x62C470 (event ID
// 0x2AF7332B), EEnterBin_MakeEvent_LuaBinding 0x62C4C0. The constructor moves
// the career down to bin pBinIndex one bin at a time: each bin's milestones
// and speed traps are enabled and, outside gameplay, its vault is loaded and
// its baseline unlocks are awarded.
EEnterBin::EEnterBin(int pBinIndex)
    : Event(0x10), //
      fBinIndex(pBinIndex) {
    CareerSettings *career = FEDatabase->GetCareerSettings();
    for (unsigned int bin = career->GetCurrentBin() - 1u; static_cast<unsigned int>(fBinIndex) <= bin; bin--) {
        career->SetCurrentBin(static_cast<uint8>(bin));
        GManager::Get().EnableBinMilestones(bin);
        GManager::Get().EnableBinSpeedTraps(bin);
        if (GManager::Get().GetInGameplay()) {
            continue;
        }
        GRaceBin *raceBin = GRaceDatabase::Get().GetBinNumber(bin);
        if (raceBin == nullptr) {
            continue;
        }
        GVault *childVault = raceBin->GetChildVault();
        if (childVault != nullptr && !childVault->IsLoaded()) {
            childVault->LoadSyncTransient();
        }
        ClearAllNewStatus();
        for (unsigned int i = 0; i < raceBin->GetBaselineUnlockCount(); i++) {
            unsigned int unlock = raceBin->GetBaselineUnlock(i);
            if (unlock != 0) {
                new EAwardUpgrade(unlock);
            }
        }
    }
    if (GRaceStatus::Exists()) {
        GRaceStatus::Get().RefreshBinWhileInGame();
    }
}

EEnterBin::~EEnterBin() {}

const char *EEnterBin::GetEventName() {
    return "EEnterBin";
}

void EEnterBin_MakeEvent_Callback(const void *staticData) {
    const EEnterBin::StaticData *data = static_cast<const EEnterBin::StaticData *>(staticData);
    new EEnterBin(data->fBinIndex);
}

int EEnterBin_MakeEvent_LuaBinding(lua_State *luaState) {
    if (lua_gettop(luaState) == 1) {
        new EEnterBin(static_cast<int>(lua_tonumber(luaState, 1)));
    }
    return 0;
}
