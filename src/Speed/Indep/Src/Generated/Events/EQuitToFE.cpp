#include "EQuitToFE.hpp"
#include "EFadeScreenOn.hpp"
#include "Speed/Indep/Src/Frontend/Database/FEDatabase.hpp"
#include "Speed/Indep/Src/Frontend/Database/RaceDB.hpp"
#include "Speed/Indep/Src/Frontend/FEManager.hpp"
#include "Speed/Indep/Src/Frontend/FEngInterfaces/FEngInterface.hpp"
#include "Speed/Indep/Src/Frontend/MenuScreens/Career/FEGameWonScreen.hpp"
#include "Speed/Indep/Src/Frontend/MenuScreens/Common/FEAnyMovieScreen.hpp"
#include "Speed/Indep/Src/Frontend/MenuScreens/InGame/FEPkg_Chyron.hpp"
#include "Speed/Indep/Src/Gameplay/GManager.h"
#include "Speed/Indep/Src/Gameplay/GRaceDatabase.h"
#include "Speed/Indep/Src/Gameplay/GRaceStatus.h"
#include "Speed/Indep/Src/Misc/GameFlow.hpp"
#include "Speed/Indep/bWare/Inc/bWare.hpp"

extern int iCurrentViewBin;

// PC: EQuitToFE 0x62FE40, ~EQuitToFE 0x623F80, EQuitToFE_MakeEvent_Callback
// 0x62FEF0. Fades out; the destructor picks the frontend screen to come back
// to (by race context) and unloads the track back to the frontend.
EQuitToFE::EQuitToFE(eGarageType pGarageType, const char *pFirstScreenName)
    : Event(0x10), //
      fGarageType(pGarageType), //
      fFirstScreenName(pFirstScreenName) {
    new EFadeScreenOn(false);
}

namespace {

// PC: 0x517110 (inlined network check). Online/LAN games go back to their
// lobby; otherwise to the main menu.
// TODO(port): the online lobby return (NetworkCore) isn't ported.
void SetOnlineReturnScreen() {
    FEManager::Get()->SetFirstScreen("MainMenu.fng", 0, 0);
}

} // namespace

EQuitToFE::~EQuitToFE() {
    if (TheGameFlowManager.GetState() != GAMEFLOW_STATE_RACING) {
        return;
    }
    uint32 gameMode = FEDatabase->GetGameMode();
    FEManager::Get()->SetGarageType(fGarageType);
    if (fGarageType == GARAGETYPE_CUSTOMIZATION_SHOP) {
        FEDatabase->SetGameMode(eFE_GAME_MODE_CUSTOMIZE);
    }
    switch (GRaceStatus::Get().GetRaceContext()) {
        case GRace::kRaceContext_QuickRace:
            if ((FEDatabase->GetGameMode() & eFE_GAME_MODE_CHALLENGE) == 0) {
                FEDatabase->ResetGameMode();
                FEDatabase->SetGameMode(eFE_GAME_MODE_QUICK_RACE);
                FEManager::Get()->SetFirstScreen("MainMenu_Sub.fng", 0, 0);
            } else {
                FEManager::Get()->SetFirstScreen("MainMenu.fng", 0, 0);
            }
            break;
        case GRace::kRaceContext_Online:
            if ((gameMode & eFE_GAME_MODE_LAN) == 0) {
                FEDatabase->SetGameMode(eFE_GAME_MODE_ONLINE);
            } else {
                FEDatabase->SetGameMode(eFE_GAME_MODE_LAN);
            }
            SetOnlineReturnScreen();
            break;
        case GRace::kRaceContext_Career: {
            FEDatabase->ResetGameMode();
            FEDatabase->SetGameMode(eFE_GAME_MODE_CAREER);
            if (fFirstScreenName != nullptr &&
                (bStrICmp(fFirstScreenName, "SafeHouseRivalChallenge.fng") == 0 ||
                 bStrICmp(fFirstScreenName, FEAnyMovieScreen::GetFEngPackageName()) == 0)) {
                FEDatabase->SetGameMode(eFE_GAME_MODE_POST_RIVAL);
            } else if (fGarageType == GARAGETYPE_CAR_LOT) {
                FEDatabase->SetGameMode(eFE_GAME_MODE_CAR_LOT);
                FEManager::Get()->SetFirstScreen("Car_Select.fng", 0, 0);
            } else {
                GRaceParameters *parms = GRaceStatus::Get().GetRaceParameters();
                bool toCareerManager = false;
                if (parms == nullptr) {
                    toCareerManager = FEDatabase->GetCareerSettings()->GetCurrentBin() == 16;
                } else if (parms->GetIsDDayRace()) {
                    toCareerManager = true;
                } else if (FEDatabase->IsFinalEpicChase()) {
                    if (GRaceDatabase::Get().CheckRaceScoreFlags(parms->GetEventHash(), GRaceDatabase::kCompleted_ContextCareer)) {
                        FEDatabase->GetCareerSettings()->SetPlayerHasBeatenTheGame();
                        FEDatabase->SetGameMode(eFE_GAME_MODE_BEAT_GAME);
                        FEManager::Get()->SetGarageType(GARAGETYPE_CAREER_SAFEHOUSE);
                        FEGameWonScreen::mCurrentScreen = 0;
                        FEManager::Get()->SetFirstScreen("GameWon.fng", 0, 0);
                    } else {
                        toCareerManager = true;
                    }
                } else {
                    iCurrentViewBin = FEDatabase->GetCareerSettings()->GetCurrentBin();
                    FEManager::Get()->SetFirstScreen("SafeHouseRaceSheet.fng", 0, 0);
                }
                if (toCareerManager) {
                    FEDatabase->ClearGameMode(eFE_GAME_MODE_CAREER);
                    FEDatabase->SetGameMode(eFE_GAME_MODE_CAREER_MANAGER);
                    FEManager::Get()->SetGarageType(GARAGETYPE_MAIN_FE);
                    FEManager::Get()->SetFirstScreen("MainMenu_Sub.fng", 0, 0);
                } else if (parms == nullptr) {
                    FEManager::Get()->SetFirstScreen("MainMenu_Sub.fng", 0, 0);
                }
            }
            if (fGarageType == GARAGETYPE_CAREER_SAFEHOUSE && GRaceStatus::Get().GetRaceParameters() == nullptr) {
                GManager::Get().SetStartingFreeRoamFromSafeHouse();
            }
            break;
        }
        default:
            FEManager::Get()->SetFirstScreen("MainMenu.fng", 0, 0);
            break;
    }
    if (fFirstScreenName != nullptr) {
        FEManager::Get()->SetFirstScreen(fFirstScreenName, 0, 0);
    }
    DismissChyron();
    if (cFEng::Get()->IsPackagePushed("FadeScreen.fng")) {
        cFEng::Get()->PopNoControlPackage("FadeScreen.fng");
    }
    if (cFEng::Get()->IsPackagePushed("Pause_Main.fng")) {
        FEDatabase->NotifyExitRaceToFrontend(EXIT_RACE_FROM_PAUSE);
    }
    TheGameFlowManager.UnloadTrack();
    FEDatabase->SetPlayersJoystickPort(1, -1);
}

const char *EQuitToFE::GetEventName() {
    return "EQuitToFE";
}

void EQuitToFE_MakeEvent_Callback(const void *staticData) {
    // TODO(port): the static data's screen name is a 32-bit pointer in the
    // event data; the PC callback passes it on.
    const EQuitToFE::StaticData *data = static_cast<const EQuitToFE::StaticData *>(staticData);
    new EQuitToFE(data->fGarageType, nullptr);
}
