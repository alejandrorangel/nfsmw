#include "Speed/Indep/Src/Frontend/MenuScreens/MemCard/uiMemcardBase.hpp"
#include "Speed/Indep/Src/FEng/FEPackage.h"
#include "Speed/Indep/Src/FEng/FEString.h"
#include "Speed/Indep/Src/Frontend/FEngHashes/FEHash_FeBonusCards.hpp"
#include "Speed/Indep/Src/Frontend/FEngHashes/ScriptHashes.hpp"
#include "Speed/Indep/Src/Frontend/MemoryCard/MemoryCard.hpp"
#include "Speed/Indep/Src/Frontend/MenuScreens/Common/feKeyboardInput.hpp"
#include "Speed/Indep/Src/Frontend/MenuScreens/Common/feScrollerina.hpp"
#include "Speed/Indep/Src/Frontend/MenuScreens/MemCard/uiMemcardInterface.hpp"
#include "Speed/Indep/Src/Generated/LanguageHashes.hpp"
#include "types.h"
#include "Speed/Indep/Src/EAXSound/EAXSOund.hpp"
#include "Speed/Indep/Src/Frontend/Database/FEDatabase.hpp"
#include "Speed/Indep/Src/Frontend/FEngFont.hpp"
#include "Speed/Indep/Src/Frontend/FEngFrontend.hpp"
#include "Speed/Indep/Src/Frontend/FEngInterfaces/FEngInterface.hpp"
#include "Speed/Indep/Src/Frontend/FEngInterfaces/FEngInterfaceFEButtons.hpp"
#include "Speed/Indep/Src/Frontend/FEngInterfaces/FEngInterfaceFEImages.hpp"
#include "Speed/Indep/Src/Frontend/FEngInterfaces/FEngInterfaceFEObjects.hpp"
#include "Speed/Indep/Src/Frontend/MenuScreens/Loading/FEBootFlowManager.hpp"
#include "Speed/Indep/Src/Frontend/MenuScreens/Safehouse/career/uiRepSheetRivalFlow.hpp"
#include "Speed/Indep/Src/Generated/Events/EQuitToFE.hpp"
#include "Speed/Indep/Src/Misc/GameFlow.hpp"
#include "Speed/Indep/bWare/Inc/bPrintf.hpp"
#include "Speed/Indep/Src/Misc/Config.h"
#include "Speed/Indep/Src/Frontend/MenuScreens/MemCard/uiMemcard.hpp"
#include "Speed/Indep/Src/Frontend/MenuScreens/MemCard/uiMemcardBase.hpp"
#include "Speed/Indep/Src/EAXSound/CARSFX/SFXObj_Pathfinder.hpp"
#include "Speed/Indep/Src/EAXSound/EAXAudioParams.hpp"

static uint32 gButtonIDs[3] = {0xb8a7c6cc, 0xb8a7c6cd, 0xb8a7c6ce};
static uint32 gButtonTextIDs[3] = {0xf9363f30, 0xfb8b67d1, 0xfde09072};

// ===== UIMemcardKeyboard =====

// UNSOLVED
UIMemcardKeyboard::UIMemcardKeyboard(ScreenConstructorData *sd) : MenuScreen(sd) {
    const u32 FEObj_MESSAGE_BLURB = 0x1e2640fa;
    const u32 FEObj_HEADER_TEXT_02 = 0x426c7b4d;
    m_pDisplayMsg = static_cast<FEString *>(FEngFindObject(GetPackageName(), FEObj_MESSAGE_BLURB));
    m_pDisplayMsgShadow = static_cast<FEString *>(FEngFindObject(GetPackageName(), FEHashUpper("message_blurb_shadow")));
    m_pTitleMaster = static_cast<FEString *>(FEngFindObject(GetPackageName(), FEObj_HEADER_TEXT_02));
    m_pOK = static_cast<FEString *>(FEngFindObject(GetPackageName(), gButtonIDs[0]));
    m_pCancel = static_cast<FEString *>(FEngFindObject(GetPackageName(), gButtonIDs[1]));
}

// UNSOLVED
void UIMemcardKeyboard::Setup() {
    const uint32 Hahs_FADEIN = 0x5b0d9106;

    FEngSetScript(GetPackageName(), gButtonIDs[0], Hahs_FADEIN, true);
    FEngSetScript(GetPackageName(), gButtonIDs[1], Hahs_FADEIN, true);
    FEngSetVisible(GetPackageName(), gButtonIDs[0]);
    FEngSetVisible(GetPackageName(), gButtonIDs[1]);
    FEngSetCurrentButton(GetPackageName(), gButtonIDs[1]);
}

#ifdef EA_PLATFORM_WIN32
// PC: the profile name is typed straight into the PC_NAME_ENTRY panel's text
// field instead of on an on-screen keyboard.
static const uint32 FEObj_PC_NAME_ENTRY = 0x6042a783;      // "PC_NAME_ENTRY"
static const uint32 FEObj_PC_NAME_ENTRY_TEXT = 0x8f095fd5; // the edited FEString

// PC: 0x527510
void UIMemcardKeyboard::ShowKeyboard() {
    FEngSetInvisible(m_pDisplayMsg);
    FEngSetScript(GetPackageName(), FEObj_PC_NAME_ENTRY, FEHASH_SHOW, true);
    FEngBeginTextInput(FEObj_PC_NAME_ENTRY_TEXT, 6, GetLocalizedString(0x70513bd4), GetLocalizedString(0xd48d95f), 7);
    FEDatabase->mFEKeyboardSettings.Mode = 5;
}

// PC: 0x5275B0
void UIMemcardKeyboard::NotificationMessage(u32 msg, FEObject *obj, u32 param1, u32 param2) {
    if (msg == __BUTTON_PRESSED__) {
        // Clicking the OK button accepts the typed name.
        cFEng::Get()->QueueGameMessage(FEMSG_ACCEPT_EDITED_TEXT, GetPackageName(), 0xff);
        return;
    }
    if (msg == FEMSG_DECLINE_EDITED_TEXT || msg == FEMSG_ACCEPT_EDITED_TEXT) {
        FEngSetVisible(m_pDisplayMsg);
        FEngSetScript(GetPackageName(), FEObj_PC_NAME_ENTRY, FEHASH_HIDE, true);
    }
}
#else
void UIMemcardKeyboard::ShowKeyboard() {
    const u32 FEObj_DIM = FEHASH_DIM;
    const u32 FEObj_SHOW = FEHASH_SHOW;
    const u32 FEObj_PC_NAME_ENTRY = FEOBJ_EVENT_HANDLER;
    const u32 FEObjWidget = 0;

    FEngSetScript(GetPackageName(), FEObj_PC_NAME_ENTRY, FEObj_DIM, true);
    FEngBeginTextInput(0, 6, GetLocalizedString(0x70513bd4), GetLocalizedString(0xd48d95f), 7);
    FEDatabase->mFEKeyboardSettings.Mode = 5;
}

void UIMemcardKeyboard::NotificationMessage(u32 msg, FEObject *obj, u32 param1, u32 param2) {
    const u32 FEObj_UNDIM = FEHASH_UNDIM;
    const u32 FEObj_HIDE = FEHASH_HIDE;
    const u32 FEObj_PC_NAME_ENTRY = FEOBJ_EVENT_HANDLER;
    if (msg == FEMSG_DECLINE_EDITED_TEXT) {
        FEngSetScript(GetPackageName(), FEObj_PC_NAME_ENTRY, FEObj_UNDIM, true);
    }
}
#endif

#ifdef EA_PLATFORM_WIN32
// ===== UIMemcardBase (PC) =====
// The PC screens (speed.exe 1.3) have no "N_BUTTONS"/"LOADER" scripts: the
// message box has an OK/Yes button (1) and a No button (0).

// PC: 0x54F770
UIMemcardBase::UIMemcardBase(ScreenConstructorData *sd)
    : UIMemcardKeyboard(sd),       //
      mIndex(1),                   //
      m_ExpectingInput(false),     //
      m_LoadedNetConfig(0),        //
      m_nMsgOptions(0),            //
      m_bVisible(false),           //
      m_bDelayedFailed(false),     //
      m_bInButtonAnimation(false), //
      m_bAnyButtonVisible(false),  //
      m_pChild(nullptr),           //
      m_SimPausedForMemcard(false) {}

// PC: 0x54F7D0
UIMemcardBase::~UIMemcardBase() {
    m_pDisplayMsg = nullptr;
    MemoryCard::GetInstance()->FEngLinkObjects(nullptr);
    if ((gMemcardSetup.GetExtraOptions() & MCE_PostOnDestroy) != 0) {
        gMemcardSetup.Complete(gMemcardSetup.mLastMessage);
    }
}

// PC: 0x519400
void UIMemcardBase::Abort() {
    cFEng::Get()->QueueGameMessage(0x8867412d, GetPackageName(), 0xff);
}

// PC: 0x5438C0
bool UIMemcardBase::AddItem(const char *pName, const char *pDate, int size, int flag) {
    Item *pNode = new ("FEPkgMemcardFileItem", 0) Item();
    bStrNCpy(pNode->m_Name, pName, sizeof(pNode->m_Name) - 1);
    pNode->m_Name[sizeof(pNode->m_Name) - 1] = '\0';
    bStrCpy(pNode->m_Data, pDate);
    pNode->m_Size = size;
    pNode->m_Flag = static_cast<MemCardFileFlag>(flag);
    m_Items.AddTail(pNode);
    return true;
}

// PC: inlined into UIMemcardList::NotificationMessage (with no spaces).
bool UIMemcardBase::IsProfile(const char *pName) {
    return bStrLen(pName) < 8 && strchr(pName, ' ') == nullptr;
}

// PC: 0x54F970
void UIMemcardBase::EmptyFileList() {
    m_Items.DeleteAllElements();
}

// PC: 0x559D60
void UIMemcardBase::InitCompleteDoList() {
    m_Items.DeleteAllElements();
    HideAllButtons();
    m_ExpectingInput = false;
    MemoryCard::GetInstance()->RequestTask(MemoryCard::MO_List, nullptr);
}

// PC: 0x562C30
void UIMemcardBase::InitComplete() {
    if (!IsMemcardEnabled) {
        cFEng::Get()->QueueGameMessage(0x461a18ee, GetPackageName(), 0xff);
        return;
    }
    SetMessageBlurbText(const_cast<char *>(" "));
    m_pDisplayMsg->Flags |= 0x80;
    if ((gMemcardSetup.GetExtraOptions() & MCE_SendInitComplete) != 0) {
        cFEng::Get()->QueueGameMessage(0x5afe12f4, gMemcardSetup.mFromScreen, 0xff);
    }
    if ((gMemcardSetup.GetExtraOptions() & MCE_RivalFlow) != 0 ||
        ((gMemcardSetup.GetExtraOptions() & MCE_ChallengeSeries) != 0 && gMemcardSetup.GetCommand() == MCO_AutoSave)) {
        cFEng::Get()->QueuePackageMessage(FEHashUpper("MEMCARD_ON"), GetPackageName(), nullptr);
    }
    switch (gMemcardSetup.GetCommand()) {
        case MCO_LoadList:
        case MCO_CarLotLoad:
            if (FEDatabase->bProfileLoaded && (gMemcardSetup.GetExtraOptions() & MCE_Player2) == 0) {
                ShowYesNo(0x87c7577e, MCP_ConfirmDestoryLoad);
                return;
            }
            break;
        case MCO_BootList:
            if (!MemoryCard::IsCardAvailable() || !IsMemcardEnabled) {
                MemcardExit(0x8867412d);
                return;
            }
            break;
        case MCO_DeleteList:
            HideAllButtons();
            m_ExpectingInput = false;
            break;
        case MCO_SaveCreate:
        case MCO_CreateNew:
            cFEng::Get()->QueueGameMessage(0x5a051729, nullptr, 0xff);
            return;
        case MCO_Save:
            bStrCpy(m_FileName, FEDatabase->GetUserProfile(0)->GetProfileName());
            DoSaveFlow(MCSF_PromptForConfirm);
            return;
        case MCO_CheckCard:
            MemoryCard::GetInstance()->CheckCard(0);
            return;
        case MCO_AutoSaveError:
            m_SimPausedForMemcard = true;
            HandleAutoSaveError();
            return;
        case MCO_EnableAutoSave:
            if ((gMemcardSetup.mOp & MCE_Online) != 0) {
                MemoryCard::GetInstance()->SetAutoSaveEnabled(true);
                return;
            }
            HideAllButtons();
            m_ExpectingInput = false;
            ShowYesNo(0x750eb45c, MCP_EnableAutoSave);
            return;
        case MCO_AutoSave:
            if (!FEDatabase->bProfileLoaded) {
                gMemcardSetup.ClearCommand();
                gMemcardSetup.SetCommand(MCO_CreateNew);
            } else {
                if (MemoryCard::GetInstance()->ShouldDoAutoSave(false)) {
                    FEPrintf(m_pDisplayMsg, GetLocalizedString(0x50d23ebd), FEDatabase->GetUserProfile(0)->GetProfileName());
                    SetScreenVisible(true, 0);
                    HideAllButtons();
                    m_ExpectingInput = false;
                    bStrCpy(m_FileName, FEDatabase->GetUserProfile(0)->GetProfileName());
                    MemoryCard::GetInstance()->StartAutoSave(true);
                    return;
                }
                gMemcardSetup.ClearCommand();
                gMemcardSetup.SetCommand(MCO_Save);
            }
            InitComplete();
            return;
        case MCO_AutoSaveOverwrite:
            m_SimPausedForMemcard = true;
            HandleAutoSaveOverwriteMessage();
            return;
        default:
            return;
    }
    InitCompleteDoList();
}

// PC: 0x562FF0
void UIMemcardBase::ExitComplete() {
    gMemcardSetup.SendTermMessage(gMemcardSetup.mLastMessage);
    if (FEDatabase->IsCareerManagerMode() && TheGameFlowManager.IsInFrontend()) {
        FEDatabase->ResetGameMode();
        if (!FEDatabase->bProfileLoaded || (gMemcardSetup.GetCommand() == MCO_LoadList && gMemcardSetup.mLastMessage == 0x8867412d) ||
            gMemcardSetup.mPreviousPrompt == MCP_Create || gMemcardSetup.mPreviousPrompt == MCP_DismissMe ||
            gMemcardSetup.mPreviousPrompt == MCP_ConfirmDestoryCreate) {
            gMemcardSetup.ClearMethod();
            gMemcardSetup.SetMethod(MCF_Modal);
            FEDatabase->RestoreFromBackupDB();
            FEDatabase->SetGameMode(eFE_GAME_MODE_CAREER_MANAGER);
        } else if (FEDatabase->GetCareerSettings()->HasCareerStarted()) {
            FEDatabase->GetCareerSettings()->ResumeCareer();
        } else {
            FEDatabase->GetCareerSettings()->StartNewCareer(true);
        }
    }

    if ((gMemcardSetup.GetExtraOptions() & MCE_RivalFlow) != 0) {
        uiRepSheetRivalFlow::Get()->Next();
    } else if ((gMemcardSetup.GetExtraOptions() & MCE_ChallengeSeries) != 0) {
        if (TheGameFlowManager.IsInFrontend()) {
            cFEng::Get()->QueuePackagePop(1);
            if (FEDatabase->bProfileLoaded) {
                FEDatabase->ResetGameMode();
                FEDatabase->SetGameMode(eFE_GAME_MODE_CHALLENGE);
                cFEng::Get()->QueuePackageSwitch(gMemcardSetup.mToScreen, 0, 0, false);
            }
        } else {
            new EQuitToFE(GARAGETYPE_MAIN_FE, nullptr);
        }
    } else {
        switch (gMemcardSetup.GetMethod()) {
            case MCF_Modal:
                if (m_SimPausedForMemcard) {
                    m_SimPausedForMemcard = false;
                    cFEng::Get()->QueuePackagePop(cFEng::Get()->IsPackagePushed("SMS_Mailboxes.fng") ? 1 : 0);
                } else {
                    cFEng::Get()->QueuePackagePop(1);
                }
                break;
            case MCF_ModalSwitch:
                cFEng::Get()->QueuePackagePop(1);
                // fall through
            case MCF_Switch:
                cFEng::Get()->QueuePackageSwitch(gMemcardSetup.mToScreen, MemoryCard::GetInstance()->GetPlayerNum(), 0, false);
                break;
        }
    }
    m_SimPausedForMemcard = false;

    eSndAudioMode eaudiomode =
        g_pEAXSound->SetAudioModeFromMemoryCard(static_cast<eSndAudioMode>(FEDatabase->GetOptionsSettings()->TheAudioSettings.AudioMode));
    FEDatabase->GetOptionsSettings()->TheAudioSettings.AudioMode = eaudiomode;
    g_pEAXSound->UpdateVolumes(&FEDatabase->GetOptionsSettings()->TheAudioSettings, -1.0f);
    InitializeEATrax(true);

    FEPackage *pPackageWithControl = cFEng::Get()->FindPackage(gMemcardSetup.mMemScreen);
    if (pPackageWithControl != nullptr) {
        FEPackage *pParentPackage = pPackageWithControl->GetParentPackage();
        if (pParentPackage != nullptr) {
            pParentPackage->SetInputEnabled(true);
        }
    }

    gMemcardSetup.Complete(gMemcardSetup.mLastMessage);

    if (MemoryCard::GetInstance()->InBootSequence()) {
        BootFlowManager::Get()->ChangeToNextBootFlowScreen(0xff);
        MemoryCard::GetInstance()->EndBootSequence();
    }
    cFEng::Get()->QueueGameMessage(0x7e998e5e, nullptr, 0xff);
    MemoryCard::GetInstance()->SetMemcardScreenShowing(false);
    if (MemoryCard::GetInstance()->IsMonitorOn()) {
        MemoryCard::GetInstance()->SetMonitor(false);
    }
}

// PC: 0x519450
eMenuSoundTriggers UIMemcardBase::NotifySoundMessage(u32 msg, eMenuSoundTriggers maybe) {
    if (!m_bAnyButtonVisible && (msg == 0x48122792 || msg == 0x4ac5e165)) {
        return UISND_NONE;
    }
    return maybe;
}

// PC: 0x564070
void UIMemcardBase::NotificationMessage(u32 msg, FEObject *obj, u32 param1, u32 param2) {
    if (msg != __BUTTON_PRESSED__ && MemoryCard::GetInstance()->GetOp() == 0) {
        UIMemcardKeyboard::NotificationMessage(msg, obj, param1, param2);
    }
    switch (msg) {
        case FEMSG_SCREEN_TICK:
            if (m_bDelayedFailed) {
                m_bDelayedFailed = false;
                cFEng::Get()->QueueGameMessage(0x8867412d, GetPackageName(), 0xff);
            }
            break;
        case 0x3a2be557:
        case FEHASH_INITCOMPLETE:
            InitComplete();
            break;
        case __BUTTON_PRESSED__:
            m_bInButtonAnimation = false;
            gMemcardSetup.mLastController = param1;
            HandleButtonPressed(__BUTTON_PRESSED__, obj, param1, param2, false);
            break;
        case 0x54b3ac6c:
            SetScreenVisible(false, 0);
            cFEng::Get()->QueuePackagePush("MC_List.fng", 0, 0, false);
            break;
        case 0xc502df5d:
            m_bInButtonAnimation = true;
            TranslateButton(obj);
            break;
        case FEHASH_EXITCOMPLETE:
            ExitComplete();
            break;
        case 0xc9d30688:
            if (gMemcardSetup.GetCommand() == MCO_CreateNew && !FEDatabase->bProfileLoaded) {
                DoSaveFlow(MCSF_PromptForCreate);
            } else if ((gMemcardSetup.GetCommand() & MCO_CreateNew) != 0 && FEDatabase->bProfileLoaded) {
                DoSaveFlow(MCSF_PromptForDestoryCreate);
            } else {
                FEPrintf(m_pDisplayMsg, "");
                m_bDelayedFailed = true;
            }
            break;
        case 0xda5b8712:
            bStrCpy(m_FileName, FEngGetEditedString());
            FEDatabase->GetUserProfile(0)->SetProfileName(m_FileName, true);
            FEDatabase->DeallocBackupDB();
            FEDatabase->bProfileLoaded = true;
            DoSaveFlow(MCSF_PromptForSave);
            break;
        case 0xf35d144e:
            SetupPromptCorruptProfile();
            break;
    }
}

// PC: 0x563380. The Yes/OK button is button 1.
void UIMemcardBase::HandleButtonPressed(u32 msg, FEObject *obj, u32 param1, u32 param2, bool bPadBack) {
    bool bYes = obj->NameHash == gButtonIDs[1] && !bPadBack;
    int nPrompt = gMemcardSetup.GetPrompt();
    gMemcardSetup.ClearPrompt();
    HideAllButtons();

    switch (nPrompt) {
        case MCP_AutoSaveWarning:
            DoSaveFlow(MCSF_AutoSaveWarning2);
            return;
        case MCP_ConfirmDestoryCreate:
            if (!bYes) {
                FEDatabase->RestoreFromBackupDB();
                MemcardExit(0x8867412d);
                return;
            }
            FEDatabase->AllocBackupDB(true);
            if ((gMemcardSetup.GetExtraOptions() & (MCE_PromptSaveOptions | MCE_PromptSaveStable)) == 0) {
                FEDatabase->DefaultProfile();
            }
            DoSaveFlow(MCSF_AutoSaveWarning);
            return;
        case MCP_Create:
            if (bYes) {
                FEDatabase->AllocBackupDB(true);
                if ((gMemcardSetup.GetExtraOptions() & (MCE_PromptSaveOptions | MCE_PromptSaveStable)) == 0) {
                    FEDatabase->DefaultProfile();
                }
                if ((gMemcardSetup.GetExtraOptions() & MCE_NewCareer) != 0) {
                    FEDatabase->GetCareerSettings()->StartNewCareer(false);
                }
                if (gMemcardSetup.GetCommand() == MCO_BootList) {
                    gMemcardSetup.ClearCommand();
                    gMemcardSetup.SetCommand(MCO_CreateNew);
                }
                DoSaveFlow(MCSF_AutoSaveWarning);
                return;
            }
            if ((gMemcardSetup.GetExtraOptions() & MCE_NewCareer) != 0) {
                FEDatabase->RestoreFromBackupDB();
            }
            break;
        case MCP_DismissMe:
        case MCP_CorruptProfile:
            break;
        case MCP_ConfirmSave:
            if (!bYes) {
                if (gMemcardSetup.GetCommand() == MCO_CreateNew) {
                    FEDatabase->GetGameplaySettings()->AutoSaveOn = false;
                }
                cFEng::Get()->QueueGameMessage(0xdc12af2e, GetPackageName(), 0xff);
                return;
            }
            DoSaveFlow(MCSF_DisableAutoSave);
            return;
        case MCP_ConfirmDestoryLoad:
            if (bYes) {
                InitCompleteDoList();
                return;
            }
            break;
        case MCP_OK:
            cFEng::Get()->QueueGameMessage(0x461a18ee, GetPackageName(), 0xff);
            return;
        case MCP_EnableAutoSave:
            if (bYes) {
                MemoryCard::GetInstance()->SetAutoSaveEnabled(true);
                return;
            }
            FEDatabase->GetGameplaySettings()->AutoSaveOn = false;
            cFEng::Get()->QueueGameMessage(0x7e998e5e, nullptr, 0xff);
            cFEng::Get()->QueueGameMessage(0x461a18ee, nullptr, 0xff);
            return;
        case MCP_ConfirmAutoSaveEnableFailed:
            if (gMemcardSetup.GetCommand() == MCO_EnableAutoSave && (gMemcardSetup.mOp & MCE_Online) == 0) {
                gMemcardSetup.ClearMethod();
                gMemcardSetup.SetMethod(MCF_Modal);
            }
            cFEng::Get()->QueueGameMessage(0x7e998e5e, nullptr, 0xff);
            cFEng::Get()->QueueGameMessage(0x461a18ee, nullptr, 0xff);
            return;
        case MCP_AutoSaveWarning2:
            cFEng::Get()->QueuePackageMessage(0x40e73793, GetPackageName(), nullptr);
            DoSaveFlow(MCSF_PromptForKeyboard);
            return;
        case MCP_ConfirmAutoSave:
            if (bYes) {
                MemoryCard::GetInstance()->SetRetryAutoSave(true);
                FEDatabase->GetGameplaySettings()->AutoSaveOn = true;
                gMemcardSetup.ClearCommand();
                gMemcardSetup.SetCommand(MCO_AutoSave);
                InitComplete();
                return;
            }
            FEDatabase->GetGameplaySettings()->AutoSaveOn = false;
            break;
        default: {
            HideAllButtons();
            m_ExpectingInput = false;
            MemoryCardMessage *pMsg = MemoryCard::GetInstance()->GetPendingMessage();
            if (pMsg != nullptr) {
                ShowMessage(pMsg);
            }
            return;
        }
    }
    cFEng::Get()->QueueGameMessage(0x8867412d, GetPackageName(), 0xff);
}

// PC: 0x54F9A0
void UIMemcardBase::HideAllButtons() {
    m_bAnyButtonVisible = false;
    for (int i = 0; i <= 2; i++) {
        ShowButton(i, false, nullptr);
    }
    FEngSetScript(GetPackageName(), 0x07f9dca9, FEHASH_HIDE, true);
}

// PC: 0x543920
void UIMemcardBase::ShowButton(int idx, bool bShow, short *pText) {
    if (!bShow) {
        FEngDisableButton(GetPackageName(), gButtonIDs[idx]);
        FEngSetInvisible(GetPackageName(), gButtonIDs[idx]);
        FEngSetInvisible(GetPackageName(), gButtonTextIDs[idx]);
        return;
    }
    m_bAnyButtonVisible = true;
    if (pText != nullptr) {
        FEString *pObj = static_cast<FEString *>(FEngFindObject(GetPackageName(), gButtonTextIDs[idx]));
        if (pObj != nullptr) {
            FESetString(pObj, pText);
        }
    }
    FEngEnableButton(GetPackageName(), gButtonIDs[idx]);
    FEngSetVisible(GetPackageName(), gButtonIDs[idx]);
    FEngSetVisible(GetPackageName(), gButtonTextIDs[idx]);
    FEngSetScript(GetPackageName(), 0x57689fdd, FEHASH_FORWARD, true);
}

// PC: 0x543AF0
void UIMemcardBase::SetButtonText(short *b1, short *b2, short *b3) {
    int active = 0;
    if (b3 != nullptr) {
        m_nMsgOptions = 3;
        ShowButton(0, true, b1);
        ShowButton(1, true, b2);
        ShowButton(2, true, b3);
    } else if (b2 != nullptr) {
        m_nMsgOptions = 2;
        ShowButton(0, true, b1);
        ShowButton(1, true, b2);
        ShowButton(2, false, nullptr);
    } else if (b1 != nullptr) {
        m_nMsgOptions = 1;
        ShowButton(0, false, nullptr);
        ShowButton(1, true, b1);
        ShowButton(2, false, nullptr);
        active = 1;
    }
    FEngSetCurrentButton(GetPackageName(), gButtonIDs[active]);
    m_ExpectingInput = true;
    gMemcardSetup.ClearPrompt();
}

// PC: 0x54FA70
void UIMemcardBase::SetMessage(i16 *pMsg) {
    if (pMsg == nullptr) {
        SetMessageBlurbText(const_cast<char *>(""));
        HideAllButtons();
        return;
    }
    SetMessageBlurbText(pMsg);
    m_pDisplayMsg->Flags |= 2;
    FEngSetScript(GetPackageName(), 0x47ff4e7c, 0xe18da018, true);
}

// PC: 0x543BE0
void UIMemcardBase::ShowOK(uint32 language_main, uint32 flag) {
    SetMessageBlurbText(language_main);
    gMemcardSetup.SetPrompt(flag);
    ShowButton(0, false, nullptr);
    ShowButton(1, true, nullptr);
    FEngSetLanguageHash(GetPackageName(), gButtonTextIDs[1], 0x417b2601);
    FEngSetCurrentButton(GetPackageName(), gButtonIDs[1]);
    ShowButton(2, false, nullptr);
    m_ExpectingInput = true;
    SetScreenVisible(true, 1);
}

// PC: 0x543CC0
void UIMemcardBase::ShowYesNo(uint32 language_main, uint32 flag) {
    SetMessageBlurbText(language_main);
    gMemcardSetup.SetPrompt(flag);
    ShowButton(0, true, nullptr);
    FEngSetLanguageHash(GetPackageName(), gButtonTextIDs[0], 0x417b25e4);
    ShowButton(1, true, nullptr);
    FEngSetLanguageHash(GetPackageName(), gButtonTextIDs[1], 0x70e01038);
    FEngSetCurrentButton(GetPackageName(), gButtonIDs[0]);
    ShowButton(2, false, nullptr);
    m_ExpectingInput = true;
    SetScreenVisible(true, 2);
}

// PC: 0x519550 (no button count on PC).
void UIMemcardBase::SetScreenVisible(bool visible, int nButtons) {
    if (m_bVisible != visible) {
        m_bVisible = visible;
        cFEng::Get()->QueuePackageMessage(visible ? 0xc0f2ae7c : 0x4f3559b5, GetPackageName(), nullptr);
        if (visible) {
            cFEng::Get()->QueuePackageMessage(FEHashUpper("INITIALIZE_SCREEN"), GetPackageName(), nullptr);
        }
        MemoryCard::GetInstance()->SetMemcardScreenInitialized(m_bVisible);
    }
}

// PC: 0x543DB0
void UIMemcardBase::SetIcon(uint32 icon) {
    FEngSetTextureHash(GetPackageName(), 0xd4f4069, icon);
    FEngSetTextureHash(GetPackageName(), 0xfac88427, icon);
}

// PC: 0x5195D0
void UIMemcardBase::TranslateButton(FEObject *obj) {
    if (obj->Flags & 1) {
        return;
    }
    if (obj->NameHash == gButtonIDs[0]) {
        MemoryCard::GetInstance()->MessageDone(RealmcIface::CHOICE_OPTION1);
    } else if (obj->NameHash == gButtonIDs[1]) {
        MemoryCard::GetInstance()->MessageDone(m_nMsgOptions == 1 ? RealmcIface::CHOICE_OPTION1 : RealmcIface::CHOICE_OPTION2);
    } else if (obj->NameHash == gButtonIDs[2]) {
        MemoryCard::GetInstance()->MessageDone(RealmcIface::CHOICE_OPTION3);
    }
    m_ExpectingInput = false;
}

// PC: 0x543EA0
void UIMemcardBase::SetupPromptNoProfileFound() {
    ShowOK(0xba373453, MCP_DismissMe);
}

// PC: 0x543EB0
void UIMemcardBase::SetupPromptSaveConfirm() {
    uint32 fmtHash;
    if ((gMemcardSetup.GetExtraOptions() & MCE_Online) != 0) {
        fmtHash = 0x391a0aac;
    } else if ((gMemcardSetup.GetExtraOptions() & MCE_PromptSaveOptions) != 0) {
        fmtHash = 0xb0af33a5;
    } else if ((gMemcardSetup.GetExtraOptions() & MCE_PromptSaveStable) != 0) {
        fmtHash = 0xd80818f8;
    } else {
        fmtHash = 0x39b3ccba;
    }
    char *fmt = GetLocalizedString(fmtHash);
    ShowYesNo(0x39b3ccba, MCP_ConfirmSave);
    char text[512];
    bSPrintf(text, fmt, m_FileName, m_FileName);
    SetMessageBlurbText(text);
}

// PC: 0x544210
void UIMemcardBase::SetupPromptForSave() {
    ShowYesNo(0x83f4bb3e, MCP_ConfirmSave);
    char *fmt = GetLocalizedString((gMemcardSetup.GetExtraOptions() & MCE_PromptSaveStable) != 0 ? 0xd80818f8 : 0x83f4bb3e);
    char text[512];
    bSPrintf(text, fmt, m_FileName, m_FileName);
    SetMessageBlurbText(text);
}

// PC: 0x544270
void UIMemcardBase::SetupPromptCorruptProfile() {
    ShowOK(0xc5ec352d, MCP_CorruptProfile);
    char text[512];
    char *fmt = GetLocalizedString(0xc5ec352d);
    bSPrintf(text, fmt, m_FileName);
    SetMessageBlurbText(text);
}

// PC: 0x5442C0
void UIMemcardBase::SetupPromptAutoSaveEnableFailedNoCard() {
    ShowOK(0x9e85bba8, MCP_ConfirmAutoSaveEnableFailed);
}

// PC: 0x5442D0
void UIMemcardBase::Setup() {
    FEngSetLanguageHash(GetPackageName(), 0x42adb44c, 0x774e4f02);
    MemoryCard::GetInstance()->FEngLinkObjects(this);
    SetIcon(0x6948e2b3);
}

// Not on PC (the GameCube's "checking memory card" text); natively only the
// unused GameCube paths call it.
void UIMemcardBase::SetStringCheckingCard() {
    SetScreenVisible(true, 0);
    HideAllButtons();
    m_ExpectingInput = false;
}

// PC: 0x544310
void UIMemcardBase::ShowKeyboard() {
    ShowButton(1, true, nullptr);
    FEngSetLanguageHash(GetPackageName(), gButtonTextIDs[1], 0x4f876b6f);
    FEngSetCurrentButton(GetPackageName(), gButtonIDs[1]);
    ShowButton(0, false, nullptr);
    ShowButton(2, false, nullptr);
    UIMemcardKeyboard::ShowKeyboard();
}

// PC: 0x544370
void UIMemcardBase::DoSaveFlow(int flow) {
    if (flow != 0) {
        m_Flow = flow;
    } else if (!FEDatabase->GetUserProfile(0)->IsProfileNamed()) {
        m_Flow = MCSF_PromptForCreate;
    }
    switch (m_Flow) {
        case MCSF_PromptForDestoryCreate:
            ShowYesNo(0x7209349f, MCP_ConfirmDestoryCreate);
            break;
        case MCSF_PromptForCreate: {
            uint32 msg;
            if ((gMemcardSetup.GetExtraOptions() & MCE_NewCareer) != 0) {
                msg = 0xbadd522c;
            } else if ((gMemcardSetup.GetExtraOptions() & MCE_ChallengeSeries) != 0) {
                msg = 0x93c25b3d;
            } else if ((gMemcardSetup.GetExtraOptions() & MCE_Online) != 0) {
                msg = 0xf8448956;
            } else {
                msg = 0xbe97590f;
            }
            ShowYesNo(msg, MCP_Create);
            break;
        }
        case MCSF_PromptForKeyboard:
            ShowKeyboard();
            break;
        case MCSF_PromptForSave:
            SetupPromptForSave();
            break;
        case MCSF_PromptForConfirm:
            SetupPromptSaveConfirm();
            break;
        case MCSF_DoSave:
            FEDatabase->GetUserProfile(0)->SetProfileName(m_FileName, true);
            MemoryCard::GetInstance()->Save(m_FileName);
            break;
        case MCSF_PromptForExceedingLimit:
            ShowOK(0xd9783c57, MCP_DismissMe);
            break;
        case MCSF_AutoSaveWarning:
            cFEng::Get()->QueuePackageMessage(0x1c8ace, GetPackageName(), nullptr);
            ShowOK(0xb01be5dd, MCP_AutoSaveWarning);
            break;
        case MCSF_AutoSaveWarning2:
            ShowOK(0xb380362f, MCP_AutoSaveWarning2);
            break;
        case MCSF_DisableAutoSave:
            MemoryCard::GetInstance()->SetAutoSaveEnabled(false);
            break;
    }
}

// PC: 0x527740
void UIMemcardBase::SetMessageBlurbText(short *pText) {
    if (pText != nullptr && m_pDisplayMsg != nullptr) {
        FESetString(m_pDisplayMsg, pText);
    }
    if (m_pDisplayMsgShadow != nullptr && pText != nullptr) {
        FESetString(m_pDisplayMsgShadow, pText);
    }
    FindScreenSize(reinterpret_cast<const wchar_t *>(pText));
}

// PC: 0x527790
void UIMemcardBase::SetMessageBlurbText(char *pText) {
    uint16 wText[1024];
    FEPrintf(m_pDisplayMsg, pText);
    if (m_pDisplayMsgShadow != nullptr) {
        FEPrintf(m_pDisplayMsgShadow, pText);
    }
    bStrCpy(wText, pText);
    FindScreenSize(reinterpret_cast<const wchar_t *>(wText));
}

// PC: 0x5277F0
void UIMemcardBase::SetMessageBlurbText(unsigned int textHash) {
    if (m_pDisplayMsg != nullptr) {
        FEngSetLanguageHash(m_pDisplayMsg, textHash);
    }
    if (m_pDisplayMsgShadow != nullptr) {
        FEngSetLanguageHash(m_pDisplayMsgShadow, textHash);
    }
    uint16 wLocString[1024];
    bStrCpy(wLocString, GetLocalizedString(textHash));
    FindScreenSize(reinterpret_cast<const wchar_t *>(wLocString));
}

// PC: 0x519670
void UIMemcardBase::FindScreenSize(const wchar_t *msg) {
    FEngFont *font = FindFont(0x545570c6);
    const float size = static_cast<float>(msg != nullptr ? bStrLen(reinterpret_cast<const uint16 *>(msg)) : 0) * font->GetHeight();
    if (size < 2200.0f) {
        cFEng::Get()->QueuePackageMessage(0x79b0c1c7, GetPackageName(), nullptr);
    } else if (size < 4400.0f) {
        cFEng::Get()->QueuePackageMessage(0xa13adcaf, GetPackageName(), nullptr);
    } else {
        cFEng::Get()->QueuePackageMessage(0x792bc959, GetPackageName(), nullptr);
    }
}

uint32 UIMemcardBase::GetAutoSaveWarning() {
    return 0xb01be5dd;
}

uint32 UIMemcardBase::GetAutoSaveWarning2() {
    return 0xb380362f;
}

// PC: 0x559E40
void UIMemcardBase::ShowMessage(MemoryCardMessage *msg) {
    ShowMessage(reinterpret_cast<const wchar_t *>(msg->mMsg), msg->mnOptions, reinterpret_cast<const wchar_t *>(msg->mOptions[0]),
                reinterpret_cast<const wchar_t *>(msg->mOptions[1]), reinterpret_cast<const wchar_t *>(msg->mOptions[2]));
    MemoryCard::GetInstance()->ReleasePendingMessage();
}

// PC: 0x54FB00
void UIMemcardBase::ShowMessage(const wchar_t *msg, uint32_t nOptions, const wchar_t *option1, const wchar_t *option2, const wchar_t *option3) {
    PopChild();
    HideAllButtons();
    SetMessage(reinterpret_cast<i16 *>(const_cast<wchar_t *>(msg)));
    switch (nOptions) {
        case 1:
            if (option1 != nullptr) {
                m_nMsgOptions = 1;
                ShowButton(0, false, nullptr);
                ShowButton(1, true, reinterpret_cast<short *>(const_cast<wchar_t *>(option1)));
                ShowButton(2, false, nullptr);
            }
            FEngSetCurrentButton(GetPackageName(), gButtonIDs[option1 != nullptr ? 1 : 0]);
            m_ExpectingInput = true;
            gMemcardSetup.ClearPrompt();
            break;
        case 2:
            SetButtonText(reinterpret_cast<short *>(const_cast<wchar_t *>(option1)), reinterpret_cast<short *>(const_cast<wchar_t *>(option2)),
                          nullptr);
            break;
        case 3:
            SetButtonText(reinterpret_cast<short *>(const_cast<wchar_t *>(option1)), reinterpret_cast<short *>(const_cast<wchar_t *>(option2)),
                          reinterpret_cast<short *>(const_cast<wchar_t *>(option3)));
            break;
        default:
            MemoryCard::GetInstance()->SetWaitingForResponse(false);
            break;
    }
    SetScreenVisible(true, nOptions);
}

// PC: 0x5637E0
void UIMemcardBase::ActivateChild() {
    MemoryCard::GetInstance()->SetMonitor(true);
}

// PC: 0x527890
void UIMemcardBase::PopChild() {
    if (m_pChild != nullptr && cFEng::Get()->IsPackagePushed("MC_List.fng")) {
        cFEng::Get()->QueuePackagePop(1);
    }
    m_pChild = nullptr;
}

// PC: 0x559E90
void UIMemcardBase::HandleAutoSaveError() {
    MemoryCard *mc = MemoryCard::GetInstance();
    if (!mc->IsCheckingCardForAutoSave() && !mc->IsCheckingCardForOverwrite()) {
        if (gMemcardSetup.GetCommand() != MCO_AutoSave) {
            gMemcardSetup.ClearMethod();
            gMemcardSetup.SetMethod(MCF_Modal);
        }
        gMemcardSetup.ClearCommand();
        gMemcardSetup.SetCommand(MCO_Save);
    }
    bStrCpy(m_FileName, FEDatabase->GetUserProfile(0)->GetProfileName());
    if (!mc->IsCheckingCardForAutoSave() && !mc->IsCheckingCardForOverwrite() && !mc->WasCardRemovedWithAutoSaveEnabled()) {
        mc->SetRetryAutoSave(true);
        ShowMessage(mc->GetPendingMessage());
    } else {
        mc->ReleasePendingMessage();
        ShowYesNo(mc->WasCardRemovedWithAutoSaveEnabled() ? 0xa0b43e9f : 0xf1f3b4ba, MCP_ConfirmAutoSave);
        mc->SetCardRemovedWithAutoSaveEnabled(false);
    }
    mc->EndAutoSave();
}

// PC: 0x544510
void UIMemcardBase::HandleAutoSaveOverwriteMessage() {
    bStrCpy(m_FileName, FEDatabase->GetUserProfile(0)->GetProfileName());
    MemoryCard::GetInstance()->EndAutoSave();
    gMemcardSetup.ClearCommand();
    gMemcardSetup.SetCommand(MCO_Save);
    MemoryCard::GetInstance()->ShowMessages(true);
    DoSaveFlow(MCSF_DisableAutoSave);
}

// ===== UIMemcardList (PC) =====

// PC: 0x5445B0
UIMemcardList::UIMemcardList(ScreenConstructorData *sd)
    : MenuScreen(sd),                                                             //
      m_SaveGameList(GetPackageName(), "", "Scrollbar", true, true, false, false) //
{
    const uint32 FEObj_Player_Name = 0xeb406fec;

    m_Initialized = 0;
    FEPrintf(GetPackageName(), FEObj_Player_Name, FEDatabase->GetUserProfile(0)->GetProfileName());

    for (int i = 1; i < 9; i++) {
        char tempStr[32];
        ScrollerSlot *pSlot = new ("ScrollerSlot", 0) ScrollerSlot();

        FEngSNPrintf(tempStr, sizeof(tempStr), "option_name_%d", i);
        pSlot->AddData(FEngFindString(GetPackageName(), FEHashUpper(tempStr)));

        FEngSNPrintf(tempStr, sizeof(tempStr), "option_data_%d", i);
        pSlot->AddData(FEngFindString(GetPackageName(), FEHashUpper(tempStr)));

        FEngSNPrintf(tempStr, sizeof(tempStr), "option_mouse_%d", i);
        pSlot->SetBacking(FEngFindObject(GetPackageName(), FEHashUpper(tempStr)));
        pSlot->Hide();
        m_SaveGameList.AddSlot(pSlot);
    }

    m_ListOp = static_cast<int>(gMemcardSetup.GetCommand() == MCO_DeleteList);

    static uint32 sOpName[2] = {0x841c21af, 0xe85326e2};
    FEngSetLanguageHash(GetPackageName(), 0x48d4fcae, sOpName[m_ListOp]);
    FEngSetLanguageHash(GetPackageName(), 0x426c7b4d, sOpName[m_ListOp]);
    m_SaveGameList.SetClickToSelectMode(true);
    m_SaveGameList.SetMouseDownMsg(__PAD_ACCEPT__);
    m_SaveGameList.SetScrollRegion(FEngFindObject(GetPackageName(), 0xd276ba2f));
}

// PC: 0x5449D0
UIMemcardList::~UIMemcardList() {}

// PC: 0x544A30
MenuScreen *CreateMemcardListFiles(ScreenConstructorData *sd) {
    UIMemcardList *pRes = new ("UIMemcardList", 0) UIMemcardList(sd);
    static_cast<UIMemcardMain *>(MemoryCard::GetInstance()->GetScreen())->SetPopupWindow(pRes);
    return pRes;
}

// total size: 0x24
class FEMemWidget : public ScrollerDatum {
  public:
    MemCardFileFlag m_Flag;   // offset 0x18, size 0x4
    int m_Size;               // offset 0x1C, size 0x4
    UIMemcardList *m_pParent; // offset 0x20, size 0x4

    FEMemWidget() {}
    ~FEMemWidget() override {}

    // PC: 0x527370
    void Act(const char *parent_pkg, uint32 data) {
        ScrollerDatumNode *node = Strings.GetHead() != Strings.EndOfList() ? Strings.GetHead() : nullptr;
        MemoryCard::GetInstance()->GetScreen()->DoSelect(node != nullptr ? node->String : "");
    }
};

// PC: 0x563820
void UIMemcardList::NotificationMessage(u32 msg, FEObject *obj, u32 param1, u32 param2) {
    switch (msg) {
        case __PAD_DOWN__:
            gMemcardSetup.mLastController = param1;
            m_SaveGameList.ScrollNext();
            break;
        case __PAD_UP__:
            gMemcardSetup.mLastController = param1;
            m_SaveGameList.ScrollPrev();
            break;
        case FEHASH_INITCOMPLETE:
            m_SaveGameList.SetSelected(m_SaveGameList.GetFirstSlot());
            m_SaveGameList.HighlightSelected();
            m_Initialized++;
            if (MemoryCard::GetInstance()->InBootSequence()) {
                FEngSetLanguageHash(GetPackageName(), 0xb8a7c6cd, 0x1a294dad);
            }
            break;
        case __PAD_ACCEPT__:
            gMemcardSetup.mLastController = param1;
            if (!FEDatabase->IsSplitScreenMode()) {
                FEDatabase->SetPlayersJoystickPort(MemoryCard::GetInstance()->GetPlayerNum(), FEngMapJoyParamToJoyport(param1));
            }
            MemoryCard::GetInstance()->SetMonitor(false);
            break;
        case __PAD_BACK__:
            cFEng::Get()->QueueGameMessage(0x8867412d, MemoryCard::GetInstance()->GetScreen()->GetPackageName(), 0xff);
            gMemcardSetup.mLastController = param1;
            break;
        case FEMSG_MOUSE_CHANGED:
            // TODO(port): the scroller's mouse handling (0x5A5540) isn't ported.
            break;
        case FEMSG_SCREEN_TICK:
            if (m_Initialized == 0) {
                m_Initialized = 1;
                UIMemcardBase *pParent = MemoryCard::GetInstance()->GetScreen();
                for (UIMemcardBase::Item *pNode = pParent->m_Items.GetHead(); pNode != pParent->m_Items.EndOfList(); pNode = pNode->GetNext()) {
                    const char *pName = pNode->m_Name + MemoryCard::GetInstance()->GetPrefixLength();
                    if (pParent->IsProfile(pName)) {
                        AddItem(pName, pNode->m_Data, pNode->m_Size, pNode->m_Flag);
                    }
                }
                FEngSetScript("MC_List.fng", 0x47ff4e7c, 0x13c37b, true);
            }
            break;
        case 0xeb29392a:
            if (m_LastMsg == __PAD_ACCEPT__) {
                FEMemWidget *pSelected = static_cast<FEMemWidget *>(m_SaveGameList.GetSelectedDatum());
                if (pSelected != nullptr) {
                    pSelected->Act(GetPackageName(), __PAD_ACCEPT__);
                }
            }
            break;
    }
    m_LastMsg = msg;
}

// PC: 0x544A90
FEMemWidget *UIMemcardList::AddItem(const char *pName, const char *pDate, int size, int flag) {
    FEMemWidget *pDatum = new ("FEMemWidget", 0) FEMemWidget();
    pDatum->m_Flag = static_cast<MemCardFileFlag>(flag);
    pDatum->m_Size = size;
    pDatum->AddData(pName, 0);
    pDatum->AddData(pDate, 0);
    m_SaveGameList.AddData(pDatum);
    m_SaveGameList.Enable(pDatum);
    m_SaveGameList.Update(true);
    return pDatum;
}
#else
// ===== UIMemcardBase =====

UIMemcardBase::UIMemcardBase(ScreenConstructorData *sd)
    : UIMemcardKeyboard(sd),       //
      mIndex(1),                   //
      m_ExpectingInput(false),     //
      m_LoadedNetConfig(0),        //
      m_nMsgOptions(0),            //
      m_bVisible(false),           //
      m_bDelayedFailed(false),     //
      m_bInButtonAnimation(false), //
      m_pChild(nullptr),           //
      m_SimPausedForMemcard(false) {}

UIMemcardBase::~UIMemcardBase() {
    m_pDisplayMsg = nullptr;
    MemoryCard::GetInstance()->FEngLinkObjects(nullptr);
    if ((gMemcardSetup.GetExtraOptions() & MCE_PostOnDestroy) != 0) {
        gMemcardSetup.Complete(gMemcardSetup.mLastMessage);
    }
}

void UIMemcardBase::Abort() {
    cFEng::Get()->QueueGameMessage(0x8867412d, GetPackageName(), 0xff);
}

bool UIMemcardBase::AddItem(const char *pName, const char *pDate, int size, int flag) {
    Item *pNode = new ("FEPkgMemcardFileItem", 0) Item();
    bStrNCpy(pNode->m_Name, pName, sizeof(pNode->m_Name) - 1);
    pNode->m_Name[sizeof(pNode->m_Name) - 1] = '\0';
    bStrCpy(pNode->m_Data, pDate);
    pNode->m_Size = size;
    pNode->m_Flag = static_cast<MemCardFileFlag>(flag);
    m_Items.AddTail(pNode);
    return true;
}

bool UIMemcardBase::IsProfile(const char *pName) {
    return bStrLen(pName) < 8;
}

void UIMemcardBase::EmptyFileList() {
    m_Items.DeleteAllElements();
}

void UIMemcardBase::InitCompleteDoList() {
    m_Items.DeleteAllElements();
    SetStringCheckingCard();
    MemoryCard::GetInstance()->RequestTask(7, nullptr);
    cFEng::Get()->QueuePackageMessage(FEHashUpper("SHOW LOADER"), GetPackageName(), nullptr);
}

void UIMemcardBase::InitComplete() {
    if (!IsMemcardEnabled) {
        cFEng::Get()->QueueGameMessage(0x461a18ee, GetPackageName(), 0xff);
        return;
    }
    SetMessageBlurbText(const_cast<char *>(" "));
    FEngSetInvisible(GetPackageName(), FEHashUpper("Button"));
    m_pDisplayMsg->Flags |= MCO_CheckCard;
    if ((gMemcardSetup.GetExtraOptions() & MCE_SendInitComplete) != 0) {
        cFEng::Get()->QueueGameMessage(0x5afe12f4, gMemcardSetup.mFromScreen, 0xff);
    }
    if ((gMemcardSetup.GetExtraOptions() & MCE_RivalFlow) != 0 ||
        ((gMemcardSetup.GetExtraOptions() & MCE_ChallengeSeries) != 0 && gMemcardSetup.GetCommand() == MCO_AutoSave)) {
        cFEng::Get()->QueuePackageMessage(FEHashUpper("MEMCARD_ON"), GetPackageName(), nullptr);
    }
    switch (MemcardGetCurrentUIOperation()) {
        case MCO_LoadList:
        case MCO_CarLotLoad:
            if (FEDatabase->bProfileLoaded && (gMemcardSetup.GetExtraOptions() & MCE_Player2) == 0) {
                ShowYesNo(0x87c7577e, MCP_ConfirmDestoryLoad);
                return;
            }
            InitCompleteDoList();
            break;
        case MCO_BootList:
            MemcardExit(0x8867412d);
            break;
        case MCO_DeleteList:
            SetStringCheckingCard();
            InitCompleteDoList();
            break;
        case MCO_SaveCreate:
        case MCO_CreateNew:
            cFEng::Get()->QueueGameMessage(0x5a051729, nullptr, 0xff);
            break;
        case MCO_Save: {
            bStrCpy(m_FileName, FEDatabase->GetUserProfile(0)->GetProfileName());
            DoSaveFlow(6);
            break;
        }
        case MCO_CheckCard:
            MemoryCard::GetInstance()->CheckCard(0);
            break;
        case MCO_EnableAutoSave:
            if ((gMemcardSetup.mOp & MCE_Online) != 0) {
                MemoryCard::GetInstance()->SetAutoSaveEnabled(true);
                return;
            }
            SetStringCheckingCard();
            ShowYesNo(0x750eb45c, MCP_EnableAutoSave);
            break;
        case MCO_AutoSaveError:
            m_SimPausedForMemcard = true;
            HandleAutoSaveError();
            break;
        case MCO_AutoSaveOverwrite:
            m_SimPausedForMemcard = true;
            HandleAutoSaveOverwriteMessage();
            break;
        case MCO_AutoSave:
            if (FEDatabase->bProfileLoaded) {
                if (MemoryCard::GetInstance()->ShouldDoAutoSave(false)) {
                    SetScreenVisible(true, 0);
                    SetStringCheckingCard();
                    bStrCpy(m_FileName, FEDatabase->GetUserProfile(0)->GetProfileName());
                    MemoryCard::GetInstance()->StartAutoSave(true);
                    return;
                }
                gMemcardSetup.ClearCommand();
                gMemcardSetup.SetCommand(MCO_Save);
                InitComplete();
            } else {
                gMemcardSetup.ClearCommand();
                gMemcardSetup.SetCommand(MCO_CreateNew);
                InitComplete();
            }
            break;
        case MCO_AutoLoad:
            extern int IsMemcardEnabled;
            if (MemoryCard::IsCardAvailable() && IsMemcardEnabled) {
                InitCompleteDoList();
            } else {
                MemcardExit(0x8867412d);
            }
            break;
    }
}

void UIMemcardBase::ExitComplete() {
    gMemcardSetup.SendTermMessage(gMemcardSetup.mLastMessage);
    if ((FEDatabase->IsCareerManagerMode()) && TheGameFlowManager.IsInFrontend()) {
        FEDatabase->ResetGameMode();
        if (FEDatabase->bProfileLoaded && !(((gMemcardSetup.GetCommand()) == MCO_LoadList && gMemcardSetup.mLastMessage == 0x8867412d) ||
                                            gMemcardSetup.mPreviousPrompt == MCP_Create || gMemcardSetup.mPreviousPrompt == MCP_DismissMe ||
                                            gMemcardSetup.mPreviousPrompt == MCP_ConfirmDestoryCreate)) {
            if (FEDatabase->GetCareerSettings()->HasCareerStarted()) {
                FEDatabase->GetCareerSettings()->ResumeCareer();
            } else {
                FEDatabase->GetCareerSettings()->StartNewCareer(true);
            }
        } else {
            gMemcardSetup.ClearMethod();
            gMemcardSetup.SetMethod(MCF_Modal);
            FEDatabase->RestoreFromBackupDB();
            FEDatabase->SetGameMode(eFE_GAME_MODE_CAREER_MANAGER);
        }
    }

    if ((gMemcardSetup.GetExtraOptions() & MCE_RivalFlow) != 0) {
        uiRepSheetRivalFlow::Get()->Next();
    } else if ((gMemcardSetup.GetExtraOptions() & MCE_ChallengeSeries) != 0) {
        if (TheGameFlowManager.IsInFrontend()) {
            cFEng::Get()->QueuePackagePop(1);
            if (FEDatabase->bProfileLoaded) {
                FEDatabase->ResetGameMode();
                FEDatabase->SetGameMode(eFE_GAME_MODE_CHALLENGE);
                cFEng::Get()->QueuePackageSwitch(gMemcardSetup.mToScreen, 0, 0, false);
            }
        } else {
            new EQuitToFE(GARAGETYPE_MAIN_FE, nullptr);
        }
    } else {
        switch (gMemcardSetup.GetMethod()) {
            case MCF_Modal:
                if (m_SimPausedForMemcard) {
                    m_SimPausedForMemcard = false;
                    cFEng::Get()->QueuePackagePop(cFEng::Get()->IsPackagePushed("SMS_Mailboxes.fng") ? 1 : 0);
                } else {
                    cFEng::Get()->QueuePackagePop(1);
                }
                break;
            case MCF_Switch:
                cFEng::Get()->QueuePackageSwitch(gMemcardSetup.mToScreen, MemoryCard::GetInstance()->GetPlayerNum(), 0, false);
                break;
            case MCF_ModalSwitch:
                cFEng::Get()->QueuePackagePop(1);
                cFEng::Get()->QueuePackageSwitch(gMemcardSetup.mToScreen, MemoryCard::GetInstance()->GetPlayerNum(), 0, false);
                break;
        }
    }
    if (m_SimPausedForMemcard) {
        m_SimPausedForMemcard = false;
    }

    eSndAudioMode eaudiomode =
        g_pEAXSound->SetAudioModeFromMemoryCard(static_cast<eSndAudioMode>(FEDatabase->GetOptionsSettings()->TheAudioSettings.AudioMode));
    FEDatabase->GetOptionsSettings()->TheAudioSettings.AudioMode = eaudiomode;
    g_pEAXSound->UpdateVolumes(&FEDatabase->GetOptionsSettings()->TheAudioSettings, -1.0f);
    InitializeEATrax(true);

    FEPackage *pPackageWithControl = cFEng::Get()->FindPackage(gMemcardSetup.mMemScreen);
    if (pPackageWithControl != nullptr) {
        FEPackage *pParentPackage = pPackageWithControl->GetParentPackage();
        if (pParentPackage != nullptr) {
            pParentPackage->SetInputEnabled(true);
        }
    }

    gMemcardSetup.Complete(gMemcardSetup.mLastMessage);

    if (MemoryCard::GetInstance()->InBootSequence()) {
        BootFlowManager::Get()->ChangeToNextBootFlowScreen(0xff);
        MemoryCard::GetInstance()->EndBootSequence();
    }
    cFEng::Get()->QueueGameMessage(0x7e998e5e, nullptr, 0xff);
    MemoryCard::GetInstance()->SetMemcardScreenShowing(false);
    MemoryCard::GetInstance()->SetMemcardScreenExiting(false);
    if (MemoryCard::GetInstance()->IsMonitorOn()) {
        MemoryCard::GetInstance()->SetMonitor(false);
    }
}

eMenuSoundTriggers UIMemcardBase::NotifySoundMessage(u32 msg, eMenuSoundTriggers maybe) {
    if (m_bAnyButtonVisible) {
        return maybe;
    }
    if (msg == 0x48122792 || msg == 0x4ac5e165) {
        return UISND_NONE;
    }
    return MenuScreen::NotifySoundMessage(msg, maybe);
}

// UNSOLVED
void UIMemcardBase::NotificationMessage(u32 msg, FEObject *obj, u32 param1, u32 param2) {
    if (msg != __BUTTON_PRESSED__ && MemoryCard::GetInstance()->GetOp() == 0) {
        UIMemcardKeyboard::NotificationMessage(msg, obj, param1, param2);
    }

    const uint32 FEObj_Header_text_02 = 0;
    const u32 FEObj_Init = 0;
    const u32 FEObj_HIGHLIGHTCOMPLETE = 0;
    const u32 FEObj_UNHIGHLIGHTCOMPLETE = 0;

    switch (msg) {
        case FEHASH_EXITCOMPLETE:
            ExitComplete();
            break;
        case 0x3a2be557:
        case FEHASH_INITCOMPLETE:
            InitComplete();
            break;
        case 0xda5b8712:
            bStrCpy(m_FileName, FEngGetEditedString());
            FEDatabase->GetUserProfile(0)->SetProfileName(m_FileName, true);
            FEDatabase->DeallocBackupDB();
            FEDatabase->bProfileLoaded = true;
            DoSaveFlow(MCSF_PromptForSave);
            break;
        case 0xc9d30688:
            if ((gMemcardSetup.GetCommand()) == MCO_CreateNew && !FEDatabase->bProfileLoaded) {
                DoSaveFlow(MCSF_PromptForCreate);
            } else if ((gMemcardSetup.GetCommand() & MCO_CreateNew) != 0 && FEDatabase->bProfileLoaded) {
                DoSaveFlow(MCSF_PromptForDestoryCreate);
            } else {
                FEPrintf(m_pDisplayMsg, "");
                m_bDelayedFailed = true;
            }
            break;
        case FEMSG_SCREEN_TICK:
            if (m_bDelayedFailed) {
                m_bDelayedFailed = false;
                cFEng::Get()->QueueGameMessage(0x8867412d, GetPackageName(), 0xff);
            }
            break;
        case 0xc502df5d:
            m_bInButtonAnimation = true;
            TranslateButton(obj);
            break;
        case __BUTTON_PRESSED__:
            m_bInButtonAnimation = false;
            gMemcardSetup.mLastController = param1;
            HandleButtonPressed(__BUTTON_PRESSED__, obj, param1, param2, false);
            break;
        case 0xf35d144e:
            SetupPromptCorruptProfile();
            break;
        case 0x54b3ac6c:
            SetScreenVisible(false, 0);
            cFEng::Get()->QueuePackagePush("MC_List.fng", 0, 0, false);
            break;
    }
}

void UIMemcardBase::HandleButtonPressed(u32 msg, FEObject *obj, u32 param1, u32 param2, bool bPadBack) {
    bool bYes = (obj->NameHash == gButtonIDs[0]) && !bPadBack;
    int nPrompt = gMemcardSetup.GetPrompt();
    gMemcardSetup.ClearPrompt();
    HideAllButtons();

    switch (nPrompt) {
        case MCP_Create:
            if (bYes) {
                FEDatabase->AllocBackupDB(true);
                if ((gMemcardSetup.GetExtraOptions() & MCE_PromptSaveOptions) == 0) {
                    if ((gMemcardSetup.GetExtraOptions() & MCE_PromptSaveStable) == 0) {
                        FEDatabase->DefaultProfile();
                    }
                }
                if ((gMemcardSetup.GetExtraOptions() & MCE_NewCareer) != 0) {
                    FEDatabase->GetCareerSettings()->StartNewCareer(false);
                }
                if ((gMemcardSetup.GetCommand()) == MCO_BootList) {
                    gMemcardSetup.ClearCommand();
                    gMemcardSetup.SetCommand(MCO_CreateNew);
                }
                DoSaveFlow(MCSF_AutoSaveWarning);
                return;
            } else {
                if ((gMemcardSetup.GetExtraOptions() & MCE_NewCareer) != 0) {
                    FEDatabase->RestoreFromBackupDB();
                }
                if (true) {
                    cFEng::Get()->QueueGameMessage(0x8867412d, GetPackageName(), 0xff);
                } else {
                    // Unknown, stripped
                    cFEng::Get();
                    GetPackageName();
                }
            }
            break;
        case MCP_ConfirmSave:
            if (bYes) {
                DoSaveFlow(MCSF_DisableAutoSave);
            } else {
                if ((gMemcardSetup.GetCommand()) == MCO_CreateNew) {
                    FEDatabase->GetGameplaySettings()->AutoSaveOn = false;
                }
                cFEng::Get()->QueueGameMessage(0xdc12af2e, GetPackageName(), 0xff);
            }
            break;
        case 0x5000000:
            if (bYes) {
                FEDatabase->AllocBackupDB(true);
                if ((gMemcardSetup.GetExtraOptions() & MCE_PromptSaveOptions) == 0) {
                    if ((gMemcardSetup.GetExtraOptions() & MCE_PromptSaveStable) == 0) {
                        FEDatabase->DefaultProfile();
                    }
                }
                DoSaveFlow(MCSF_AutoSaveWarning);
            } else {
                MemcardExit(0x8867412d);
            }
            break;
        case MCP_ConfirmDestoryLoad:
            if (bYes) {
                InitCompleteDoList();
            } else {
                cFEng::Get()->QueueGameMessage(0x8867412d, GetPackageName(), 0xff);
            }
            break;
        case MCP_OK:
            cFEng::Get()->QueueGameMessage(0x461a18ee, GetPackageName(), 0xff);
            break;
        case MCP_AutoSaveWarning:
            DoSaveFlow(MCSF_AutoSaveWarning2);
            break;
        case MCP_AutoSaveWarning2:
            cFEng::Get()->QueuePackageMessage(0x40E73793, GetPackageName(), nullptr);
            DoSaveFlow(MCSF_PromptForKeyboard);
            break;
        case MCP_ConfirmAutoSave:
            if (bYes) {
                FEDatabase->GetGameplaySettings()->AutoSaveOn = false;
                cFEng::Get()->QueueGameMessage(0x8867412d, GetPackageName(), 0xff);
            } else {
                MemoryCard::GetInstance()->SetRetryAutoSave(true);
                FEDatabase->GetGameplaySettings()->AutoSaveOn = true;
                gMemcardSetup.ClearCommand();
                MemoryCard::GetInstance()->ShowMessages(true);
                gMemcardSetup.SetCommand(MCO_Save);
                DoSaveFlow(MCSF_DisableAutoSave);
            }
            break;
        case MCP_ConfirmAutoSaveEnableFailed:
            if ((gMemcardSetup.GetCommand()) == MCO_EnableAutoSave) {
                if ((gMemcardSetup.GetExtraOptions() & MCE_Online) == 0) {
                    gMemcardSetup.ClearMethod();
                    gMemcardSetup.SetMethod(MCF_Modal);
                }
            }
            cFEng::Get()->QueueGameMessage(0x7e998e5e, nullptr, 0xff);
            cFEng::Get()->QueueGameMessage(0x461a18ee, nullptr, 0xff);
            break;
        case MCP_EnableAutoSave:
            if (bYes) {
                MemoryCard::GetInstance()->SetAutoSaveEnabled(true);
            } else {
                FEDatabase->GetGameplaySettings()->AutoSaveOn = false;
                cFEng::Get()->QueueGameMessage(0x7e998e5e, nullptr, 0xff);
                cFEng::Get()->QueueGameMessage(0x461a18ee, nullptr, 0xff);
            }
            break;
        case MCP_DismissMe:
        case MCP_CorruptProfile:
            cFEng::Get()->QueueGameMessage(0x8867412d, GetPackageName(), 0xff);
            break;
        default:
            SetStringCheckingCard();
            MemoryCardMessage *pMsg = MemoryCard::GetInstance()->GetPendingMessage();
            if (pMsg != nullptr) {
                ShowMessage(pMsg);
            }
            if (MemoryCard::GetInstance()->GetOp() == 7) {
                cFEng::Get()->QueuePackageMessage(FEHashUpper("SHOW LOADER"), GetPackageName(), nullptr);
            }
            break;
    }
}

void UIMemcardBase::HideAllButtons() {
    m_bAnyButtonVisible = false;
    for (int i = 0; i <= 2; i++) {
        ShowButton(i, false, nullptr);
    }

    const u32 FEObj_CursorPosition = 0x07f9dca9;

    FEngSetScript(GetPackageName(), FEObj_CursorPosition, FEHASH_HIDE, true);
}

void UIMemcardBase::ShowButton(int idx, bool bShow, short *pText) {
    if (bShow) {
        m_bAnyButtonVisible = true;
        if (pText != nullptr) {
            FEString *pObj = static_cast<FEString *>(FEngFindObject(GetPackageName(), gButtonTextIDs[idx]));
            FESetString(pObj, pText);
        }
        FEngEnableButton(GetPackageName(), gButtonTextIDs[idx]);
        FEngSetVisible(GetPackageName(), gButtonTextIDs[idx]);
        FEngSetVisible(GetPackageName(), gButtonIDs[idx]);
        FEngSetScript(GetPackageName(), 0x57689fdd, FEHASH_FORWARD, true);
    } else {
        FEngDisableButton(GetPackageName(), gButtonIDs[idx]);
        FEngSetInvisible(GetPackageName(), gButtonIDs[idx]);
        FEngSetInvisible(GetPackageName(), gButtonTextIDs[idx]);
    }
}

void UIMemcardBase::SetButtonText(short *b1, short *b2, short *b3) {
    int active = 0;
    if (b3 != nullptr) {
        m_nMsgOptions = 3;
        ShowButton(0, true, b1);
        ShowButton(1, true, b2);
        ShowButton(2, true, b3);
    } else if (b2 != nullptr) {
        m_nMsgOptions = 2;
        ShowButton(0, true, b1);
        ShowButton(1, true, b2);
        ShowButton(2, false, nullptr);
    } else if (b1 != nullptr) {
        m_nMsgOptions = 1;
        ShowButton(0, true, b1);
        ShowButton(1, false, nullptr);
        ShowButton(2, false, nullptr);
    }
    FEngSetCurrentButton(GetPackageName(), gButtonIDs[active]);
    m_ExpectingInput = true;
    gMemcardSetup.ClearPrompt();
}

void UIMemcardBase::SetMessage(i16 *pMsg) {
    if (pMsg == nullptr) {
        SetMessageBlurbText(const_cast<char *>(""));
        HideAllButtons();
    } else {
        SetMessageBlurbText(pMsg);
        m_pDisplayMsg->Flags |= 2;

        const u32 FEObj_MEMCARD = 0x47ff4e7c;
        FEngSetScript(GetPackageName(), FEObj_MEMCARD, 0xe18da018, true);
    }
}

void UIMemcardBase::ShowOK(uint32 language_main, uint32 flag) {
    cFEng::Get()->QueuePackageMessage(FEHashUpper("HIDE LOADER"), GetPackageName(), nullptr);
    SetMessageBlurbText(language_main);
    gMemcardSetup.SetPrompt(flag);
    ShowButton(0, true, nullptr);
    FEngSetLanguageHash(GetPackageName(), gButtonTextIDs[0], 0x417b2601);
    FEngSetCurrentButton(GetPackageName(), gButtonIDs[0]);
    ShowButton(1, false, nullptr);
    ShowButton(2, false, nullptr);
    m_ExpectingInput = true;
    SetScreenVisible(true, 1);
}

void UIMemcardBase::ShowYesNo(uint32 language_main, uint32 flag) {
    cFEng::Get()->QueuePackageMessage(FEHashUpper("HIDE LOADER"), GetPackageName(), nullptr);
    SetMessageBlurbText(language_main);
    gMemcardSetup.SetPrompt(flag);
    ShowButton(0, true, nullptr);
    FEngSetLanguageHash(GetPackageName(), gButtonTextIDs[0], LANGUAGE_COMMON_NO);
    ShowButton(1, true, nullptr);
    FEngSetLanguageHash(GetPackageName(), gButtonTextIDs[1], LANGUAGE_COMMON_YES);
    FEngSetCurrentButton(GetPackageName(), gButtonIDs[0]);
    ShowButton(2, false, nullptr);
    m_ExpectingInput = true;
    SetScreenVisible(true, 2);
}

void UIMemcardBase::SetScreenVisible(bool visible, int nButtons) {
    if (m_bVisible != visible) {
        m_bVisible = visible;
        cFEng::Get()->QueuePackageMessage(visible ? 0xc0f2ae7c : 0x4f3559b5, GetPackageName(), nullptr);
        if (visible) {
            cFEng::Get()->QueuePackageMessage(FEHashUpper("INITIALIZE_SCREEN"), GetPackageName(), nullptr);
        }
        MemoryCard::GetInstance()->SetMemcardScreenInitialized(m_bVisible);
    }
    if (visible) {
        char script[32];
        bSPrintf(script, "%d_BUTTONS", nButtons);
        cFEng::Get()->QueuePackageMessage(FEHashUpper(script), GetPackageName(), nullptr);
    }
}

void UIMemcardBase::SetIcon(uint32 icon) {
    FEngSetTextureHash(GetPackageName(), 0xd4f4069, icon);
    FEngSetTextureHash(GetPackageName(), 0xfac88427, icon);
}

void UIMemcardBase::TranslateButton(FEObject *obj) {
    if (obj->Flags & 1) {
        return;
    }
    if (obj->NameHash == gButtonIDs[0]) {
        MemoryCard::GetInstance()->MessageDone(RealmcIface::CHOICE_OPTION1);
    } else if (obj->NameHash == gButtonIDs[1]) {
        MemoryCard::GetInstance()->MessageDone(RealmcIface::CHOICE_OPTION2);
    } else if (obj->NameHash == gButtonIDs[2]) {
        MemoryCard::GetInstance()->MessageDone(RealmcIface::CHOICE_OPTION3);
    }
    m_ExpectingInput = false;
}

void UIMemcardBase::SetupPromptNoProfileFound() {
    ShowOK(0xba373453, 0x3000000);
}

void UIMemcardBase::SetupPromptSaveConfirm() {
    char *fmt;
    if ((gMemcardSetup.GetExtraOptions() & MCE_Online) != 0) {
        fmt = GetLocalizedString(0x391a0aac);
    } else {
        uint32 fmtHash;
        if ((gMemcardSetup.GetExtraOptions() & MCE_PromptSaveOptions) != 0) {
            fmtHash = 0xb0af33a5;
        } else if ((gMemcardSetup.GetExtraOptions() & MCE_PromptSaveStable) != 0) {
            fmtHash = 0xd80818f8;
        } else {
            fmtHash = 0x39b3ccba;
        }
        fmt = GetLocalizedString(fmtHash);
    }
    ShowYesNo(0x39b3ccba, MCP_ConfirmSave);
    char text[512];
    bSPrintf(text, fmt, m_FileName, m_FileName);
    SetMessageBlurbText(text);
}

void UIMemcardBase::SetupAutoSaveConfirmPrompt() {
    gMemcardSetup.SetPrompt(MCP_ConfirmAutoSave);
    SetMessageBlurbText(GetLocalizedString(0xa0b434a2));
    FEngEnableButton(GetPackageName(), gButtonIDs[0]);
    FEngSetVisible(GetPackageName(), gButtonIDs[0]);
    FEngSetVisible(GetPackageName(), gButtonTextIDs[0]);
    FEPrintf(GetPackageName(), static_cast<int>(gButtonTextIDs[0]), GetLocalizedString(LANGUAGE_COMMON_NO));
    FEngEnableButton(GetPackageName(), gButtonIDs[1]);
    FEngSetVisible(GetPackageName(), gButtonIDs[1]);
    FEngSetVisible(GetPackageName(), gButtonTextIDs[1]);
    FEPrintf(GetPackageName(), static_cast<int>(gButtonTextIDs[1]), GetLocalizedString(0x2b07a03d));
    FEngDisableButton(GetPackageName(), gButtonIDs[2]);
    FEngSetInvisible(GetPackageName(), gButtonIDs[2]);
    FEngSetInvisible(GetPackageName(), gButtonTextIDs[2]);
    FEngSetCurrentButton(GetPackageName(), gButtonIDs[0]);
    FEngSetScript(GetPackageName(), FEHashUpper("HANDLER"), FEHashUpper("FORWARD"), true);
    SetScreenVisible(true, 2);
}

void UIMemcardBase::SetupPromptForSave() {
    ShowYesNo(0x83f4bb3e, MCP_ConfirmSave);
    char *fmt = GetLocalizedString(gMemcardSetup.GetExtraOptions() & MCE_PromptSaveStable ? 0xd80818f8 : 0x83f4bb3e);
    char text[512];
    bSPrintf(text, fmt, m_FileName, m_FileName);
    SetMessageBlurbText(text);
}

void UIMemcardBase::SetupPromptCorruptProfile() {
    ShowOK(0x821e4444, MCP_CorruptProfile);
    char text[512];
    char *fmt = GetLocalizedString(0x821e4444);
    bSPrintf(text, fmt, m_FileName);
    SetMessageBlurbText(text);
}

void UIMemcardBase::SetupPromptAutoSaveEnableFailedNoCard() {
    uint32 msg = MCP_ConfirmAutoSaveEnableFailed;
    ShowOK(0x9e85bba8, msg);
}

void UIMemcardBase::Setup() {
    const uint32 FEObj_Header_text_02 = 0x99054304;
    FEngSetLanguageHash(GetPackageName(), FEHASH_HEADERTEXT, 0x774e4dd9);
    FEngSetLanguageHash(m_pDisplayMsg, FEObj_Header_text_02);
    MemoryCard::GetInstance()->FEngLinkObjects(this);
    SetIcon(0x6948e2b3);
}

void UIMemcardBase::SetStringCheckingCard() {
    SetScreenVisible(true, 0);
    SetMessageBlurbText(0x99054304);
    cFEng::Get()->QueuePackageMessage(FEHashUpper("0_BUTTONS"), GetPackageName(), nullptr);
    HideAllButtons();
    m_ExpectingInput = false;
}

void UIMemcardBase::ShowKeyboard() {
    SetScreenVisible(false, 0);
    HideAllButtons();
    UIMemcardKeyboard::ShowKeyboard();
}

void UIMemcardBase::DoSaveFlow(int flow) {
    if (flow != 0) {
        m_Flow = flow;
    } else {
        if (!FEDatabase->GetUserProfile(0)->IsProfileNamed()) {
            m_Flow = MCSF_PromptForCreate;
        }
    }
    uint32 msg;
    switch (m_Flow) {
        case MCSF_PromptForExceedingLimit:
            ShowOK(0xd9783c57, MCP_DismissMe);
            break;
        case MCSF_PromptForDestoryCreate:
            ShowYesNo(0x7209349f, MCP_ConfirmDestoryCreate);
            break;
        case MCSF_PromptForCreate:
            if ((gMemcardSetup.GetExtraOptions() & MCE_NewCareer) != 0) {
                msg = 0xbadd522c;
            } else if ((gMemcardSetup.GetExtraOptions() & MCE_ChallengeSeries) != 0) {
                msg = 0x93c25b3d;
            } else if ((gMemcardSetup.GetExtraOptions() & MCE_Online) != 0) {
                msg = 0xf8448956;
            } else {
                msg = 0xbe97590f;
            }
            ShowYesNo(msg, MCP_Create);
            break;

        case MCSF_PromptForKeyboard:
            ShowKeyboard();
            break;
        case MCSF_PromptForConfirm:
            SetupPromptSaveConfirm();
            break;
        case MCSF_PromptForSave:
            SetupPromptForSave();
            break;
        case MCSF_DisableAutoSave:
            MemoryCard::GetInstance()->SetAutoSaveEnabled(false);
            break;
        case MCSF_DoSave:
            FEDatabase->GetUserProfile(0)->SetProfileName(m_FileName, true);
            MemoryCard::GetInstance()->Save(m_FileName);
            SetStringCheckingCard();
            break;
        case MCSF_AutoSaveWarning:
            cFEng::Get()->QueuePackageMessage(0x1c8ace, GetPackageName(), nullptr);
            msg = GetAutoSaveWarning();
            ShowOK(msg, MCP_AutoSaveWarning2);
            break;
        case MCSF_AutoSaveWarning2:
            msg = GetAutoSaveWarning2();
            ShowOK(msg, MCP_AutoSaveWarning2);
            break;
    }
}

void UIMemcardBase::SetMessageBlurbText(short *pText) {
    FESetString(m_pDisplayMsg, pText);
    if (m_pDisplayMsgShadow != nullptr) {
        FESetString(m_pDisplayMsgShadow, pText);
    }
    FindScreenSize(reinterpret_cast<const wchar_t *>(pText));
}

void UIMemcardBase::SetMessageBlurbText(char *pText) {
    int wText[1024];
    FEPrintf(m_pDisplayMsg, pText);
    if (m_pDisplayMsgShadow != nullptr) {
        FEPrintf(m_pDisplayMsgShadow, pText);
    }
    bStrCpy(reinterpret_cast<unsigned short *>(wText), pText);
    FindScreenSize(reinterpret_cast<const wchar_t *>(wText));
}

void UIMemcardBase::SetMessageBlurbText(unsigned int textHash) {
    FEngSetLanguageHash(m_pDisplayMsg, textHash);
    if (m_pDisplayMsgShadow != nullptr) {
        FEngSetLanguageHash(m_pDisplayMsgShadow, textHash);
    }
    char *locString = GetLocalizedString(textHash);
    wchar_t wLocString[1024];
    bStrCpy(reinterpret_cast<u16 *>(wLocString), locString);
    FindScreenSize(wLocString);
}

void UIMemcardBase::FindScreenSize(const wchar_t *msg) {
    FEngFont *font = FindFont(0x545570c6);
    float numLines = static_cast<float>(bStrLen(reinterpret_cast<const uint16 *>(msg))) * font->GetHeight();

    const int MAX_SIZE_SMALL = 2200.0f;
    const int MAX_SIZE_MED = 4400.0f;
    const u32 FEObj_dialogsmall = 0x79b0c1c7;
    const u32 FEObj_dialogmedium = 0xa13adcaf;
    const u32 FEObj_dialoglarge = 0x792bc959;

    if (numLines < MAX_SIZE_SMALL) {
        cFEng::Get()->QueuePackageMessage(FEObj_dialogsmall, GetPackageName(), nullptr);
    } else if (numLines < MAX_SIZE_MED) {
        cFEng::Get()->QueuePackageMessage(FEObj_dialogmedium, GetPackageName(), nullptr);
    } else {
        cFEng::Get()->QueuePackageMessage(FEObj_dialoglarge, GetPackageName(), nullptr);
    }
}

uint32 UIMemcardBase::GetAutoSaveWarning() {
    return 0xb39899c2;
}

uint32 UIMemcardBase::GetAutoSaveWarning2() {
    return 0x2386f454;
}

void UIMemcardBase::ShowMessage(MemoryCardMessage *msg) {
    ShowMessage(reinterpret_cast<const wchar_t *>(msg->mMsg), msg->mnOptions, reinterpret_cast<const wchar_t *>(msg->mOptions[0]),
                reinterpret_cast<const wchar_t *>(msg->mOptions[1]), reinterpret_cast<const wchar_t *>(msg->mOptions[2]));
    MemoryCard::GetInstance()->ReleasePendingMessage();
}

// UNSOLVED
void UIMemcardBase::ShowMessage(const wchar_t *msg, uint32_t nOptions, const wchar_t *option1, const wchar_t *option2, const wchar_t *option3) {
    PopChild();
    HideAllButtons();
    SetMessage(reinterpret_cast<i16 *>(const_cast<wchar_t *>(msg)));
    switch (nOptions) {
        case 1:
            SetButtonText(reinterpret_cast<short *>(const_cast<wchar_t *>(option1)), nullptr, nullptr);
            break;
        case 2:
            SetButtonText(reinterpret_cast<short *>(const_cast<wchar_t *>(option1)), reinterpret_cast<short *>(const_cast<wchar_t *>(option2)),
                          nullptr);
            break;
        case 3:
            SetButtonText(reinterpret_cast<short *>(const_cast<wchar_t *>(option1)), reinterpret_cast<short *>(const_cast<wchar_t *>(option2)),
                          reinterpret_cast<short *>(const_cast<wchar_t *>(option3)));
            break;
        default:
            MemoryCard::GetInstance()->SetWaitingForResponse(false);
            break;
    }
    SetScreenVisible(true, nOptions);
    cFEng::Get()->QueuePackageMessage(FEHashUpper(nOptions == 0 ? "SHOW LOADER" : "HIDE LOADER"), GetPackageName(), nullptr);
}

void UIMemcardBase::ActivateChild() {
    MemoryCard::GetInstance()->SetMonitor(true);
}

void UIMemcardBase::PopChild() {
    if (m_pChild != nullptr && cFEng::Get()->IsPackagePushed("MC_List.fng")) {
        cFEng::Get()->QueuePackagePop(1);
    }
    m_pChild = nullptr;
}

void UIMemcardBase::HandleAutoSaveError() {
    if (!MemoryCard::GetInstance()->IsCheckingCardForAutoSave() && !MemoryCard::GetInstance()->IsCheckingCardForOverwrite()) {
        if ((gMemcardSetup.GetCommand()) != MCO_AutoSave) {
            gMemcardSetup.ClearMethod();
            gMemcardSetup.SetMethod(MCF_Modal);
        }
        gMemcardSetup.ClearCommand();
        gMemcardSetup.SetCommand(MCO_Save);
    }
    bStrCpy(m_FileName, FEDatabase->GetUserProfile(0)->GetProfileName());
    if (MemoryCard::GetInstance()->IsCheckingCardForAutoSave() || MemoryCard::GetInstance()->IsCheckingCardForOverwrite() ||
        MemoryCard::GetInstance()->WasCardRemovedWithAutoSaveEnabled()) {
        MemoryCard::GetInstance()->ReleasePendingMessage();
        SetupAutoSaveConfirmPrompt();
        MemoryCard::GetInstance()->SetCardRemovedWithAutoSaveEnabled(false);
    } else {
        MemoryCard::GetInstance()->SetRetryAutoSave(true);
        ShowMessage(MemoryCard::GetInstance()->GetPendingMessage());
    }
    MemoryCard::GetInstance()->EndAutoSave();
}

void UIMemcardBase::HandleAutoSaveOverwriteMessage() {
    bStrCpy(m_FileName, FEDatabase->GetUserProfile(0)->GetProfileName());
    MemoryCard::GetInstance()->EndAutoSave();
#ifndef EA_PLATFORM_PLAYSTATION2
    FEDatabase->bAutoSaveOverwriteConfirmed = true;
#endif
    gMemcardSetup.ClearCommand();
    gMemcardSetup.SetCommand(MCO_Save);
    MemoryCard::GetInstance()->ShowMessages(true);
    DoSaveFlow(MCSF_DisableAutoSave);
}

// ===== UIMemcardList =====

UIMemcardList::UIMemcardList(ScreenConstructorData *sd)
    : MenuScreen(sd),                                                             //
      m_SaveGameList(GetPackageName(), "", "Scrollbar", true, true, false, false) //
{
    const uint32 FEObj_Header_text_02 = 0;
    const uint32 FEObj_BACK = 0;
    const uint32 FEObj_Player_Name = 0xeb406fec;
    const u32 FEObj_CREATE_BUTTON = 0;

    m_Initialized = 0;
    FEPrintf(GetPackageName(), FEObj_Player_Name, FEDatabase->GetUserProfile(0)->GetProfileName());

    for (int i = 1; i < 9; i++) {
        char tempStr[32];
        ScrollerSlot *pSlot = new ("ScrollerSlot", 0) ScrollerSlot();

        FEngSNPrintf(tempStr, sizeof(tempStr), "option_name_%d", i);
        pSlot->AddData(FEngFindString(GetPackageName(), FEHashUpper(tempStr)));

        FEngSNPrintf(tempStr, sizeof(tempStr), "option_data_%d", i);
        pSlot->AddData(FEngFindString(GetPackageName(), FEHashUpper(tempStr)));

        FEngSNPrintf(tempStr, sizeof(tempStr), "option_mouse_%d", i);
        pSlot->SetBacking(FEngFindObject(GetPackageName(), FEHashUpper(tempStr)));
        pSlot->Hide();
        m_SaveGameList.AddSlot(pSlot);
    }

    m_ListOp = static_cast<int>(gMemcardSetup.GetCommand() == MCO_DeleteList);

    static uint32 sOpName[2] = {0x841c21af, 0xe85326e2};
    FEngSetLanguageHash(GetPackageName(), 0x48d4fcae, sOpName[m_ListOp]);
    FEngSetLanguageHash(GetPackageName(), 0x426c7b4d, sOpName[m_ListOp]);
}

UIMemcardList::~UIMemcardList() {}

MenuScreen *CreateMemcardListFiles(ScreenConstructorData *sd) {
    UIMemcardList *pRes = new ("UIMemcardList", 0) UIMemcardList(sd);
    static_cast<UIMemcardMain *>(MemoryCard::GetInstance()->GetScreen())->SetPopupWindow(pRes);
    return pRes;
}

// total size: 0x24
class FEMemWidget : public ScrollerDatum {
  public:
    static const int MAX_SIZE; // size: 0x4, address: 0xFFFFFFFF
    MemCardFileFlag m_Flag;    // offset 0x18, size 0x4
    int m_Size;                // offset 0x1C, size 0x4
    UIMemcardList *m_pParent;  // offset 0x20, size 0x4

    FEMemWidget() {}
    ~FEMemWidget() override {}

    void Act(const char *parent_pkg, uint32 data) {
        MemoryCard::GetInstance()->GetScreen()->DoSelect(reinterpret_cast<ScrollerDatum *>(data)->Strings.GetNode(0)->String);
    }
    bool IsCorrupt() {}
    int GetSize() const {}
    const char *GetFileName() {}
};

void UIMemcardList::NotificationMessage(u32 msg, FEObject *obj, u32 param1, u32 param2) {
    switch (msg) {
        case FEHASH_INITCOMPLETE: {
            UIMemcardBase *pParent;
            m_SaveGameList.SetSelected(m_SaveGameList.GetFirstSlot());
            m_SaveGameList.HighlightSelected();
            MemoryCard::GetInstance()->GetScreen()->m_ExpectingInput = true;
            m_Initialized++;
            if (MemoryCard::GetInstance()->InBootSequence()) {
                FEngSetLanguageHash(GetPackageName(), 0xb8a7c6cd, 0x1a294dad);
            }
            break;
        }
        case FEMSG_SCREEN_TICK:
            if (m_Initialized != 0) {
                m_Initialized = 1;
                UIMemcardBase *pParent = MemoryCard::GetInstance()->GetScreen();
                for (UIMemcardBase::Item *pNode = pParent->m_Items.GetHead(); pNode != pParent->m_Items.EndOfList(); pNode = pNode->GetNext()) {
                    const char *pName = pNode->m_Name + MemoryCard::GetInstance()->GetPrefixLength();
                    if (pParent->IsProfile(pName)) {
                        AddItem(pName, pNode->m_Data, pNode->m_Size, pNode->m_Flag);
                    }
                }
                FEngSetScript("MC_List.fng", 0x47ff4e7c, 0x13c37b, true);
            }
            break;
        case __PAD_BACK__:
            if (MemoryCard::GetInstance()->InBootSequence()) {
                cFEng::Get()->QueueGameMessage(0x8d0cc9f9, "MC_Main_GC.fng", 0xff);
            } else {
                cFEng::Get()->QueueGameMessage(0x8867412d, MemoryCard::GetInstance()->GetScreen()->GetPackageName(), 0xff);
            }
            gMemcardSetup.mLastController = param1;
            break;
        case __PAD_UP__:
            gMemcardSetup.mLastController = param1;
            m_SaveGameList.ScrollPrev();
            break;
        case __PAD_DOWN__:
            gMemcardSetup.mLastController = param1;
            m_SaveGameList.ScrollNext();
            break;
        case __PAD_ACCEPT__:
            gMemcardSetup.mLastController = param1;
            if (!FEDatabase->IsSplitScreenMode()) {
                FEDatabase->SetPlayersJoystickPort(MemoryCard::GetInstance()->GetPlayerNum(), FEngMapJoyParamToJoyport(param1));
            }
            MemoryCard::GetInstance()->SetMonitor(false);
            break;
        case 0xeb29392a:
            if (m_LastMsg == __PAD_ACCEPT__) {
                // UNSOLVED (dwarf)
                m_pCreateNew->Act(GetPackageName(), reinterpret_cast<uint32>(m_SaveGameList.GetSelectedDatum()));
            }
            break;
    }
    m_LastMsg = msg;
}

FEMemWidget *UIMemcardList::AddItem(const char *pName, const char *pDate, int size, int flag) {
    FEMemWidget *pDatum = new ("FEMemWidget", 0) FEMemWidget();
    pDatum->m_Flag = static_cast<MemCardFileFlag>(flag);
    pDatum->m_Size = size;
    pDatum->AddData(pName, 0);
    pDatum->AddData(pDate, 0);
    m_SaveGameList.AddData(pDatum);
    m_SaveGameList.Enable(pDatum);
    m_SaveGameList.Update(true);
    return pDatum;
}
#endif
