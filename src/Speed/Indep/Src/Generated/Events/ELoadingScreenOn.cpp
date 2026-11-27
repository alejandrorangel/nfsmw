#include "ELoadingScreenOn.hpp"
#include "EFadeScreenOff.hpp"
#include "Speed/Indep/Src/Ecstasy/Ecstasy.hpp"
#include "Speed/Indep/Src/Frontend/Database/FEDatabase.hpp"
#include "Speed/Indep/Src/Frontend/FEManager.hpp"
#include "Speed/Indep/Src/Frontend/FEPackageData.hpp"
#include "Speed/Indep/Src/Frontend/FEngFrontend.hpp"
#include "Speed/Indep/Src/Frontend/FEngHashes/ScriptHashes.hpp"
#include "Speed/Indep/Src/Frontend/FEngInterfaces/FEngInterface.hpp"
#include "Speed/Indep/Src/Frontend/MenuScreens/InGame/FeFadeScreen.hpp"
#include "Speed/Indep/Src/Frontend/MenuScreens/Loading/FELoadingTips.hpp"

extern const char *LoadingBootName;
extern bool bSawLoadingScreen;

// PC: ELoadingScreenOn 0x62D570, ~ELoadingScreenOn 0x62D6C0 (trivial),
// ELoadingScreenOn_MakeEvent_Callback 0x62D6F0. The constructor does the work:
// it picks the loading package for today's tip, drops loading_boot and pushes
// the loading screen, then fades the fade screen back in under it.
ELoadingScreenOn::ELoadingScreenOn(LoadingScreen::LoadingScreenTypes pLoadingType)
    : Event(0x10), //
      fLoadingType(pLoadingType) {
    intptr_t arg = pLoadingType;
    eGameTips tip = LoadingTips::WhatTipScreenShouldIUseToday(pLoadingType);
    const char *pkg_name;
    if (tip == GAME_TIP_NONE) {
        pkg_name = eIsWidescreen() ? "WS_Loading.fng" : "Loading.fng";
    } else if (tip == GAME_TIP_USE_CONTROLLER_CONFIG) {
        pkg_name = eIsWidescreen() ? "WS_Loading_Controller.fng" : "Loading_Controller.fng";
    } else {
        pkg_name = "Loading_Tips.fng";
        LoadingTips::SetDoneLoading(false);
        arg = tip;
    }
    SetLoadingScreenPackageName(pkg_name);
    if (cFEng::Get()->IsPackageInControl(LoadingBootName)) {
        cFEng::Get()->QueuePackagePop(1);
    }
    int joy_port = FEDatabase->GetPlayersJoystickPort(0);
    FEManager::Get()->AllowControllerError(false);
    if (!cFEng::Get()->IsPackagePushed(pkg_name)) {
        cFEng::Get()->QueuePackagePush(pkg_name, arg, FEngMapJoyportToJoyParam(joy_port), false);
    }
    bSawLoadingScreen = false;
    if (FadeScreen::IsFadeScreenOn()) {
        new EFadeScreenOff(FEHASH_15_IN);
    }
}

ELoadingScreenOn::~ELoadingScreenOn() {}

const char *ELoadingScreenOn::GetEventName() {
    return "ELoadingScreenOn";
}

void ELoadingScreenOn_MakeEvent_Callback(const void *staticData) {
    const ELoadingScreenOn::StaticData *data = static_cast<const ELoadingScreenOn::StaticData *>(staticData);
    new ELoadingScreenOn(data->fLoadingType);
}
