#include "Speed/Indep/Src/Frontend/MenuScreens/MemCard/uiMemcard.hpp"
#include "Speed/Indep/Src/Frontend/FEngFrontend.hpp"
#include "Speed/Indep/Src/Misc/Config.h"
#include "Speed/Indep/Src/Frontend/MenuScreens/MemCard/uiMemcardBase.hpp"
#include "Speed/Indep/Src/Frontend/MenuScreens/MemCard/uiMemcardInterface.hpp"
#include "Speed/Indep/Src/Frontend/FEngInterfaces/FEngInterface.hpp"
#include "Speed/Indep/Src/Frontend/Database/FEDatabase.hpp"
#include "Speed/Indep/Src/Frontend/MemoryCard/MemoryCard.hpp"
#include "Speed/Indep/Src/Frontend/MenuScreens/Loading/FEBootFlowManager.hpp"

#ifdef EA_PLATFORM_WIN32
// PC: 0x5644F0
void UIMemcardBoot::NotificationMessage(u32 msg, FEObject *obj, u32 param1, u32 param2) {
    UIMemcardBase::NotificationMessage(msg, obj, param1, param2);
    switch (msg) {
        case FEHASH_INITCOMPLETE:
            HideAllButtons();
            MemoryCard::GetInstance()->ShowMessages(true);
            extern int IsMemcardEnabled;
            if (!IsMemcardEnabled) {
#if !defined(EA_PORT_NATIVE)
                cFEng::Get()->QueueGameMessage(0x461a18ee, GetPackageName(), 0xff);
#endif
                return;
            }
            MemoryCard::GetInstance()->BootupCheck(nullptr);
            SetScreenVisible(true, 0);
            m_bVisible = true;
            break;
        case 0x461a18ee:
            MemoryCard::GetInstance()->StartBootSequence();
            // fall through
        case 0x8867412d:
            BootFlowManager::Get()->ChangeToNextBootFlowScreen(0xff);
            MemoryCard::GetInstance()->SetMemcardScreenInitialized(false);
            break;
    }
}
#else
void UIMemcardBoot::NotificationMessage(u32 msg, FEObject *obj, u32 param1, u32 param2) {
    UIMemcardBase::NotificationMessage(msg, obj, param1, param2);
    switch (msg) {
        case FEHASH_INITCOMPLETE:
            HideAllButtons();
            MemoryCard::GetInstance()->ShowMessages(true);
            extern int IsMemcardEnabled;
            if (!IsMemcardEnabled) {
#if !defined(EA_PORT_NATIVE)
                // UIMemcardBase::InitComplete already queued this when the
                // memory card is disabled; a second one advances the boot flow
                // twice (never seen on PC/console, where it's always enabled).
                cFEng::Get()->QueueGameMessage(0x461a18ee, GetPackageName(), 0xff);
#endif
                return;
            }
            SetStringCheckingCard();
            MemoryCard::GetInstance()->BootupCheck(nullptr);
            SetScreenVisible(true, 0);
            m_bVisible = true;
            break;
        case 0x461a18ee:
            MemoryCard::GetInstance()->StartBootSequence();
            // fall through
        case 0x8867412d:
            BootFlowManager::Get()->ChangeToNextBootFlowScreen(0xff);
            MemoryCard::GetInstance()->SetMemcardScreenInitialized(false);
            break;
    }
}
#endif

eMenuSoundTriggers UIMemcardBoot::NotifySoundMessage(u32 msg, eMenuSoundTriggers maybe) {
    return maybe;
}

MenuScreen *CreateMemCardBootScreen(ScreenConstructorData *sd) {
    UIMemcardBase *pRes = new ("MemCardBootScreen", 0) UIMemcardBoot(sd);
    pRes->Setup();
    return pRes;
}

MenuScreen *CreateMemcardMainMenu(ScreenConstructorData *sd) {
    UIMemcardMain *pRes = new ("UIMemcardMain", 0) UIMemcardMain(sd);
    pRes->Setup();
    return pRes;
}

UIMemcardMain::UIMemcardMain(ScreenConstructorData *sd) : UIMemcardBase(sd) {
#ifndef EA_PLATFORM_WIN32 // PC: 0x561090 leaves the blurb alone
    FEString *mpBlurb = FEngFindString(GetPackageName(), 0x1e2640fa);
    mpBlurb->Flags &= ~0x200;
#endif
}

#ifdef EA_PLATFORM_WIN32
// PC: 0x5436F0
void UIMemcardMain::DoSelect(const char *pName) {
    bStrCpy(m_FileName, pName);
    switch (m_pChild->GetListOp()) {
        case UIMemcardList::MCLO_Load:
            MemoryCard::GetInstance()->RequestTask(MemoryCard::MO_Load, m_FileName);
            break;
        case UIMemcardList::MCLO_Delete:
            MemoryCard::GetInstance()->RequestTask(MemoryCard::MO_Delete, m_FileName);
            break;
    }
    PopChild();
}

// PC: 0x563E90. More than one profile (or any, when loading/deleting) shows
// the list.
void UIMemcardMain::ListDone() {
    MemoryCard::GetInstance()->ShowMessages(true);
    int nSize = m_Items.CountElements();
    switch (MemcardGetCurrentUIOperation()) {
        case MCO_LoadList:
        case MCO_DeleteList:
        case MCO_CarLotLoad:
            if (nSize == 0) {
                ShowOK(0xba373453, MCP_DismissMe);
                return;
            }
            break;
        case MCO_BootList:
            gMemcardSetup.mInBootFlow = true;
            if (nSize > 1) {
                break;
            }
            if (nSize == 1) {
                const char *pName = m_Items.GetHead()->m_Name + MemoryCard::GetInstance()->GetPrefixLength();
                bStrCpy(m_FileName, pName);
                MemoryCard::GetInstance()->Load(m_FileName);
                return;
            }
            if (MemoryCard::GetSpecialError() == RealmcIface::STATUS_ENTRY_ALREADY_EXISTS) {
                cFEng::Get()->QueueGameMessage(0x8867412d, GetPackageName(), 0xff);
                return;
            }
            DoSaveFlow(MCSF_PromptForCreate);
            return;
        case MCO_SaveCreate:
            if (FEDatabase->bProfileLoaded) {
                bStrCpy(m_FileName, FEDatabase->GetUserProfile(0)->GetProfileName());
                DoSaveFlow(MCSF_PromptForSave);
            } else {
                DoSaveFlow(MCSF_PromptForCreate);
            }
            return;
        case MCO_CreateNew:
            if (FEDatabase->bProfileLoaded && (gMemcardSetup.mOp & MCE_NewCareer) == 0) {
                DoSaveFlow(MCSF_PromptForDestoryCreate);
            } else {
                DoSaveFlow(MCSF_PromptForCreate);
            }
            return;
        default:
            return;
    }
    ActivateChild();
}

// PC: 0x564630
void UIMemcardMain::NotificationMessage(u32 msg, FEObject *obj, u32 param1, u32 param2) {
    UIMemcardBase::NotificationMessage(msg, obj, param1, param2);
    switch (msg) {
        case 0xa4bb7ae1:
            cFEng::Get()->QueueGameMessage(0x461a18ee, nullptr, 0xff);
            break;
        case 0x5a051729:
            ListDone();
            break;
        case 0x15457de1:
            PopChild();
            break;
        case 0x461a18ee:
            FEDatabase->DeallocBackupDB();
            MemcardExit(0x461a18ee);
            break;
        case 0x8867412d:
        case 0xdc12af2e:
            PopChild();
            if ((gMemcardSetup.mOp & MCE_ProfileOnly) != 0 && FEDatabase->GetUserProfile(0)->IsProfileNamed()) {
                cFEng::Get()->QueueGameMessage(0x461a18ee, GetPackageName(), 0xff);
                return;
            }
            if (FEDatabase->IsCareerManagerMode() && FEDatabase->bProfileLoaded && FEDatabase->GetGameplaySettings()->AutoSaveOn &&
                gMemcardSetup.GetCommand() != MCO_LoadList && msg != 0xdc12af2e) {
                MemoryCard::GetInstance()->SetAutoSaveEnabled(true);
            } else if (gMemcardSetup.GetCommand() == MCO_CreateNew && msg == 0xdc12af2e) {
                FEDatabase->GetGameplaySettings()->AutoSaveOn = false;
                ShowOK(0xb04da4ad, MCP_OK);
            } else {
                MemcardExit(msg);
            }
            if (MemoryCard::GetInstance()->IsCheckingCardForAutoSave() || MemoryCard::GetInstance()->IsAutoSaving()) {
                MemoryCard::GetInstance()->EndAutoSave();
            }
            FEDatabase->DeallocBackupDB();
            break;
        case 0xb57fdb17:
            ShowOK(0x9e85bba8, MCP_ConfirmAutoSaveEnableFailed);
            break;
        case 0xc6c6b68f:
            DoSaveFlow(MCSF_DoSave);
            break;
        case 0xfe202e3b:
            DoSaveFlow(MCSF_PromptForSave);
            break;
    }
}
#else

void UIMemcardMain::DoSelect(const char *pName) {
    bStrCpy(m_FileName, pName);
    switch (m_pChild->GetListOp()) {
        case 0:
            MemoryCard::GetInstance()->RequestTask(5, m_FileName);
            SetStringCheckingCard();
            break;
        case 1:
            MemoryCard::GetInstance()->RequestTask(6, m_FileName);
            SetStringCheckingCard();
            break;
    }
    PopChild();
}

void UIMemcardMain::ListDone() {
    bool bCreateChild = false;
    MemoryCard::GetInstance()->ShowMessages(true);

    int nSize = m_Items.CountElements();

    switch (MemcardGetCurrentUIOperation()) {
        case 0x30:
            if (nSize == 0) {
                SetupPromptNoProfileFound();
            } else {
                bCreateChild = true;
            }
            break;
        case 0x10:
        case 0x70:
            if (nSize != 0) {
                bCreateChild = true;
            } else {
                SetupPromptNoProfileFound();
            }
            break;
        case 0x20:
            gMemcardSetup.mInBootFlow = true;
            if (nSize > 1) {
                bCreateChild = true;
            } else if (nSize == 1) {
                const char *pName = m_Items.GetHead()->m_Name;
                pName += MemoryCard::GetInstance()->GetPrefixLength();
                bStrCpy(m_FileName, pName);
                MemoryCard::GetInstance()->Load(m_FileName);
            } else {
                MemoryCard::GetInstance()->BootupCheck(nullptr);
            }
            break;
        case 0xf0:
            if (nSize > 1) {
                bCreateChild = true;
            } else if (nSize != 1) {
                DoSaveFlow(2);
            } else {
                const char *pName = m_Items.GetHead()->m_Name;
                pName += MemoryCard::GetInstance()->GetPrefixLength();
                bStrCpy(m_FileName, pName);
                MemoryCard::GetInstance()->Load(m_FileName);
            }
            break;
        case 0x40:
            if (FEDatabase->bProfileLoaded) {
                bStrCpy(const_cast<char *>(m_FileName), FEDatabase->GetUserProfile(0)->GetProfileName());
                DoSaveFlow(4);
            } else {
                DoSaveFlow(2);
            }
            break;
        case 0x60:
            if (FEDatabase->bProfileLoaded && (gMemcardSetup.mOp & 0x80000) == 0) {
                DoSaveFlow(1);
            } else {
                DoSaveFlow(2);
            }
            break;
    }

    if (bCreateChild) {
        ActivateChild();
    }
}

// UNSOLVED
void UIMemcardMain::NotificationMessage(u32 msg, FEObject *obj, u32 param1, u32 param2) {
    UIMemcardBase::NotificationMessage(msg, obj, param1, param2);
    switch (msg) {
        case 0x5a051729:
            cFEng::Get()->QueuePackageMessage(FEHashUpper("HIDE LOADER"), GetPackageName(), nullptr);
            ListDone();
            break;
        case 0xa4bb7ae1:
            cFEng::Get()->QueueGameMessage(0x461a18ee, nullptr, 0xff);
            goto hide_loader;
        case 0xfe202e3b:
            DoSaveFlow(MCSF_PromptForSave);
            break;
        case 0x461a18ee:
            if (MemoryCard::GetInstance()->InBootSequence()) {
                PopChild();
            }
            FEDatabase->DeallocBackupDB();
            MemcardExit(0x461a18ee);
            goto hide_loader;
        case 0xa643dee3:
            if (MemoryCard::GetInstance()->IsAutoLoading()) {
                return;
            }
            cFEng::Get()->QueueGameMessage(0x461a18ee, GetPackageName(), 0xff);
            break;
        case 0x15457de1:
            PopChild();
            break;
        case 0xc6c6b68f:
            DoSaveFlow(8);
            break;
        case 0x8867412d:
        case 0xdc12af2e:
            PopChild();
            if ((gMemcardSetup.mOp & MCE_ProfileOnly) != 0 && FEDatabase->GetUserProfile(0)->IsProfileNamed()) {
                cFEng::Get()->QueueGameMessage(0x461a18ee, GetPackageName(), 0xff);
                goto hide_loader;
            }
            if (FEDatabase->IsCareerManagerMode() && FEDatabase->bProfileLoaded && FEDatabase->GetGameplaySettings()->AutoSaveOn &&
                (gMemcardSetup.GetCommand()) != MCO_LoadList && msg != 0xdc12af2e) {
                MemoryCard::GetInstance()->SetAutoSaveEnabled(true);
            } else {
                if ((gMemcardSetup.GetCommand()) == MCO_CreateNew && msg == 0xdc12af2e) {
                    FEDatabase->GetGameplaySettings()->AutoSaveOn = false;
                    ShowOK(0xb04da4ad, MCP_OK);
                } else {
                    MemcardExit(msg);
                }
            }
            if (MemoryCard::GetInstance()->IsCheckingCardForAutoSave() || MemoryCard::GetInstance()->IsAutoSaving()) {
                MemoryCard::GetInstance()->EndAutoSave();
            }
            FEDatabase->DeallocBackupDB();
            goto hide_loader;
        case 0xb57fdb17:
            SetupPromptAutoSaveEnableFailedNoCard();
            break;
        case 0x8d0cc9f9:
            PopChild();
            SetStringCheckingCard();
            MemoryCard::GetInstance()->BootupCheck(nullptr);
            break;
        case FEMSG_SCREEN_TICK:
            if (!m_ExpectingInput) {
                return;
            }

            if (!FEngIsScriptSet(GetPackageName(), FEHashUpper("LOADER"), FEHashUpper("APPEAR"))) {
                return;
            }
            goto hide_loader;

            break;
    }
    return;

hide_loader:
    cFEng::Get()->QueuePackageMessage(FEHashUpper("HIDE LOADER"), GetPackageName(), nullptr);
    return;
}
#endif
