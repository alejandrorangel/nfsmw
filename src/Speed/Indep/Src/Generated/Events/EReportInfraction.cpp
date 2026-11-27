#include "EReportInfraction.hpp"
#include "Speed/Indep/Libs/Support/Miscellaneous/StringHash.h"
#include "Speed/Indep/Src/Frontend/Localization/Localize.hpp"
#include "Speed/Indep/Src/Generated/Messages/MMiscSound.h"
#include "Speed/Indep/Src/Interfaces/IFengHud.h"
#include "Speed/Indep/Src/Interfaces/SimEntities/IPlayer.h"
#include "Speed/Indep/bWare/Inc/bWare.hpp"

// PC: EReportInfraction 0x624770 (vtable 0x8A72F8), ~EReportInfraction 0x641A30.
// GInfractionManager::ReportInfraction queues it the first time an infraction
// is committed in a pursuit; it shows the infraction on the HUD and plays the
// infraction sound.
EReportInfraction::EReportInfraction(InfractionType pInfraction)
    : Event(0x10), //
      fInfraction(pInfraction) {}

EReportInfraction::~EReportInfraction() {
    IPlayer *player = IPlayer::First(PLAYER_LOCAL);
    if (player == nullptr) {
        return;
    }
    uint32 stringHash = 0;
    switch (fInfraction) {
        case GInfractionManager::kInfraction_Speeding:
            stringHash = 0x1F87313A;
            break;
        case GInfractionManager::kInfraction_Racing:
            stringHash = 0x6DF08AFF;
            break;
        case GInfractionManager::kInfraction_Reckless:
            stringHash = 0x21857819;
            break;
        case GInfractionManager::kInfraction_Assault:
            stringHash = 0x66432B02;
            break;
        case GInfractionManager::kInfraction_HitAndRun:
            stringHash = 0x3E1B41F3;
            break;
        case GInfractionManager::kInfraction_Damage:
            stringHash = 0x98AC2BAE;
            break;
        case GInfractionManager::kInfraction_Resist:
            stringHash = 0xC03AA693;
            break;
        case GInfractionManager::kInfraction_OffRoad:
            stringHash = 0x50F83C6C;
            break;
    }
    char infractionString[128];
    bSNPrintf(infractionString, sizeof(infractionString), "%s", GetLocalizedString(stringHash));
    IHud *hud = player->GetHud();
    IInfractions *iinfractions;
    if (hud != nullptr && hud->QueryInterface(&iinfractions)) {
        iinfractions->RequestInfraction(infractionString);
    }
    MMiscSound(fInfraction).Send(UCrc32(stringhash32("Infraction")));
}

const char *EReportInfraction::GetEventName() {
    return "EReportInfraction";
}

void EReportInfraction_MakeEvent_Callback(const void *staticData) {
    const EReportInfraction::StaticData *data = static_cast<const EReportInfraction::StaticData *>(staticData);
    new EReportInfraction(data->fInfraction);
}
