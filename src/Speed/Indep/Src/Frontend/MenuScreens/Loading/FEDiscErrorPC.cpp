#include "Speed/Indep/Src/Frontend/MenuScreens/Loading/FEDiscErrorPC.hpp"

#include "Speed/Indep/Src/Ecstasy/Texture.hpp"
#include "Speed/Indep/Src/FEng/FEObject.h"
#include "Speed/Indep/Src/Frontend/FEPackageData.hpp"
#include "Speed/Indep/Src/Frontend/FEPackageManager.hpp"
#include "Speed/Indep/Src/Frontend/FEngHashes/FEHash_FeBonusCards.hpp"
#include "Speed/Indep/Src/Frontend/FEngInterfaces/FEngInterfaceFEObjects.hpp"
#include "Speed/Indep/Src/Frontend/MenuScreens/Loading/FEBootFlowManager.hpp"
#include "Speed/Indep/bWare/Inc/Strings.hpp"

extern int ExitTheGameFlag;

#if defined(EA_PORT_NATIVE)
// The PC looks for bin.dat on the CD-ROM drives (sub_6C2620, sub_6C25A0);
// the port has no disc to check.
bool PCCheckGameDisc() {
    return true;
}
#endif

namespace {

const uint32 kObjMarketingBlurb = 0x505E82BF;

// sub_5981A0
void MarketingBlurbLoaded(void *) {
    MenuScreen *screen = FEPackageManager::Get()->FindScreen("LS_Demo_Marketing.fng");
    if (screen != nullptr) {
        FEngSetVisible(FEngFindObject(screen->GetPackageName(), kObjMarketingBlurb));
    }
}

} // namespace

// sub_5981E0
DiscErrorPCScreen::DiscErrorPCScreen(ScreenConstructorData *sd) : MenuScreen(sd), Unknown2C(0) {
    uint32 texture = bStringHash("DEMO_DISC_MARKETING_BLURB");
    eLoadStreamingTexture(&texture, 1, MarketingBlurbLoaded, nullptr, 0);
    FEngSetInvisible(FEngFindObject(GetPackageName(), kObjMarketingBlurb));
}

// sub_57E630: "retry" goes on once the disc is found; the other button quits.
void DiscErrorPCScreen::NotificationMessage(u32 msg, FEObject *pobj, u32 param1, u32 param2) {
    if (msg == __BUTTON_PRESSED__ && pobj != nullptr) {
        if (pobj->NameHash == 0x4F876B6F) {
            if (PCCheckGameDisc()) {
                BootFlowManager::Get()->ChangeToNextBootFlowScreen(0xFF);
            }
        } else {
            ExitTheGameFlag = 1;
        }
    }
}
