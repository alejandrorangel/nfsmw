#include "EReportMilestoneAtStake.hpp"
#include "Speed/Indep/Src/Frontend/Database/FEDatabase.hpp"
#include "Speed/Indep/Src/Frontend/Localization/Localize.hpp"
#include "Speed/Indep/Src/Frontend/MenuScreens/InGame/FEPkg_PostRace.hpp"
#include "Speed/Indep/Src/Gameplay/GMilestone.h"
#include "Speed/Indep/Src/Interfaces/IFengHud.h"
#include "Speed/Indep/Src/Interfaces/SimEntities/IPlayer.h"
#include "Speed/Indep/bWare/Inc/bWare.hpp"

// PC: EReportMilestoneAtStake 0x641C20 (vtable 0x8A7DC4), ~EReportMilestoneAtStake
// 0x6247A0 (trivial), EReportMilestoneAtStake_MakeEvent_Callback 0x641D40. The
// constructor does the work: a milestone's goal reached in a pursuit, the HUD
// says so ("<milestone at stake>\n<milestone>") and the pursuit results get it.
EReportMilestoneAtStake::EReportMilestoneAtStake(GMilestone *pMilestonePtr)
    : Event(0x10), //
      fMilestonePtr(pMilestonePtr) {
    IPlayer *player = IPlayer::First(PLAYER_ALL);
    IHud *hud = player != nullptr ? player->GetHud() : nullptr;
    IGenericMessage *imessage;
    if (hud != nullptr && hud->QueryInterface(&imessage)) {
        const char *milestone = GetTranslatedString(FEDatabase->GetMilestoneHeaderHash(fMilestonePtr->GetLocalizationTag()));
        const char *atStake = GetTranslatedString(static_cast<int>(0xFB197367));
        char text[64];
        bSNPrintf(text, sizeof(text), "%s\n%s", atStake, milestone);
        imessage->RequestGenericMessage(text, false, 0x8AB83EDB, 0, 0, GenericMessage_Priority_2);
    }
    PostRacePursuitScreen::GetPursuitData().AddMilestone(fMilestonePtr);
}

EReportMilestoneAtStake::~EReportMilestoneAtStake() {}

const char *EReportMilestoneAtStake::GetEventName() {
    return "EReportMilestoneAtStake";
}

void EReportMilestoneAtStake_MakeEvent_Callback(const void *staticData) {
    new EReportMilestoneAtStake(static_cast<const EReportMilestoneAtStake::StaticData *>(staticData)->fMilestonePtr);
}
