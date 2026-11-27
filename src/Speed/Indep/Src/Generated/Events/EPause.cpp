#include "EPause.hpp"
#include "EFadeScreenOff.hpp"
#include "Speed/Indep/Src/Frontend/Database/FEDatabase.hpp"
#include "Speed/Indep/Src/Frontend/FEManager.hpp"
#include "Speed/Indep/Src/Frontend/FEPackageData.hpp"
#include "Speed/Indep/Src/Frontend/FEngFrontend.hpp"
#include "Speed/Indep/Src/Frontend/FEngInterfaces/FEngInterface.hpp"
#include "Speed/Indep/Src/Frontend/MenuScreens/InGame/FeFadeScreen.hpp"
#include "Speed/Indep/Src/Frontend/MenuScreens/Safehouse/options/uiOptionsMain.hpp"

// PC: EPause 0x623A90, ~EPause 0x62F220, EPause_MakeEvent_Callback 0x62F360.
// Pushes the pause menu for the player (unless it's already up or pausing
// isn't allowed right now).
EPause::EPause(int pPlayerIndex, int pCalledFromPostRace, int pShowFlatPause)
    : Event(0x20), //
      fPlayerIndex(pPlayerIndex), //
      fCalledFromPostRace(pCalledFromPostRace), //
      fShowFlatPause(pShowFlatPause) {}

EPause::~EPause() {
    if (fPlayerIndex == -1) {
        fPlayerIndex = 0;
    }
    if (fCalledFromPostRace != 1 && !FEManager::IsOkayToRequestPauseSimulation(fPlayerIndex, true, false)) {
        return;
    }
    if (cFEng::Get()->IsPackagePushed("Pause_Main.fng")) {
        return;
    }
    if (FadeScreen::IsFadeScreenOn()) {
        new EFadeScreenOff(0x014035FB);
    }
    SetPlayerToEditForOptions(fPlayerIndex);
    FEDatabase->SetGameMode(eFE_GAME_MODE_OPTIONS);
    FEngSetLastButton("Pause_Main.fng", 0);
    FEDatabase->ClearGameMode(eFE_GAME_MODE_OPTIONS);
    FEngSetLastButton("Pause_Main.fng", 0);
    u32 mask = FEngMapJoyportToJoyParam(FEDatabase->GetPlayersJoystickPort(fPlayerIndex));
    if (mask == 0) {
        mask = 0xFF;
    }
    cFEng::Get()->QueuePackagePush("Pause_Main.fng", fCalledFromPostRace, mask, false);
}

const char *EPause::GetEventName() {
    return "EPause";
}

void EPause_MakeEvent_Callback(const void *staticData) {
    const EPause::StaticData *data = static_cast<const EPause::StaticData *>(staticData);
    new EPause(data->fPlayerIndex, data->fCalledFromPostRace, data->fShowFlatPause);
}
