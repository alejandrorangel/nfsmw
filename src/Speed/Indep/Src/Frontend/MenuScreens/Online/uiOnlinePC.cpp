#include "Speed/Indep/Src/Frontend/MenuScreens/Online/uiOnlinePC.hpp"

#include "Speed/Indep/Src/FEng/FEObject.h"
#include "Speed/Indep/Src/Frontend/Database/FEDatabase.hpp"
#include "Speed/Indep/Src/Frontend/FEngFrontend.hpp"
#include "Speed/Indep/Src/Frontend/FEngHashes/FEHash_FeBonusCards.hpp"
#include "Speed/Indep/Src/Frontend/FEngHashes/ScriptHashes.hpp"
#include "Speed/Indep/Src/Frontend/FEngInterfaces/FEngInterface.hpp"
#include "Speed/Indep/Src/Frontend/FEngInterfaces/FEngInterfaceFEButtons.hpp"
#include "Speed/Indep/Src/Frontend/FEngInterfaces/FEngInterfaceFEObjects.hpp"
#include "Speed/Indep/Src/Frontend/FEngInterfaces/FEngInterfaceFEStrings.hpp"
#include "Speed/Indep/Src/Frontend/Localization/Localize.hpp"
#include "Speed/Indep/Src/Frontend/MenuScreens/Common/feDialogBox.hpp"
#include "Speed/Indep/Src/Frontend/MenuScreens/Common/feKeyboardInput.hpp"
#include "Speed/Indep/Src/Online/PCOnline.hpp"
#include "Speed/Indep/bWare/Inc/Strings.hpp"
#include "Speed/Indep/bWare/Inc/bPrintf.hpp"

#include <string.h>

extern Timer RealTimer;
extern int ExitTheGameFlag;

namespace {

const char *const kLANServerSelect = "UI_PC_LAN_ServerSelect.fng"; // g_LAN_SERVER_SELECT 0x8F3288
const char *const kLAN = "UI_PC_LAN.fng";
const char *const kMainMenu = "MainMenu.fng";              // g_MAIN_MENU 0x8F3284
const char *const kOnlineMainMenu = "OL_MAIN.fng";         // g_ONLINE_MAIN_MENU 0x8F3280
const char *const kISPConnect = "UI_OLISPConnect.fng";     // 0x8F3254
const char *const kEALogin = "OLEALogin.fng";              // 0x8F325C
const char *const kEATerms = "UI_OL_WebOffer2.fng";        // gOL_EA_TERMS
const char *const kSelectPersona = "OL_SelectPersona.fng"; // gOL_SELECT_PERSONA
const char *const kCreateUserA = "OL_Create_Account.fng";  // gOL_CREATE_USER_A
const char *const kCreateUserB = "UI_OLX_Info_Sharing.fng"; // gOL_CREATE_USER_B
const char *const kUseExisting = "OL_UseExisting.fng";     // gOL_USE_EXISTING
const char *const kRegistration = "OL_PC_REGISTRATION.fng"; // 0x8F3274

const uint32 kObjTitle = 0x42ADB44C;
const uint32 kMsgLookingDialogDone = 0x7345710F;
const uint32 kMsgEALoginExit = 0x587C018B;   // starts the screen's exit animation
const uint32 kMsgEALoginExited = 0xE1FDE1D1; // the exit animation finished
const uint32 kMsgEALoginQuitYes = 0x2C0E485A;
const uint32 kMsgLobbyErrorOk = 0xCAC20C24;  // MenuScreen::BaseNotify (PC) handles it
const uint32 kMsgOnlineExitGame = 0x94292DD0;

bool IsLANMode() {
    return FEDatabase->IsLANMode();
}

bool HasElapsed(Timer &t) {
    return t.GetSeconds() < RealTimer.GetSeconds();
}

// The lobby error dialogs (0x89A350: index, button text, button message, second
// button text, second button message).
struct LobbyErrorDialog {
    int Index;
    uint32 Button1Text;
    uint32 Button1Msg;
    uint32 Button2Text;
    uint32 Button2Msg;
};
const LobbyErrorDialog kLobbyErrorDialogs[15] = {
    {0, 0x417B2601, 0xCAC20C24, 0xFFFFFFFF, 0xFFFFFFFF},  {1, 0x417B2601, 0xCAC20C24, 0xFFFFFFFF, 0xFFFFFFFF},
    {2, 0x417B2601, 0xCAC20C24, 0xFFFFFFFF, 0xFFFFFFFF},  {3, 0x417B2601, 0xCAC20C24, 0xFFFFFFFF, 0xFFFFFFFF},
    {4, 0x417B2601, 0xCAC20C24, 0xFFFFFFFF, 0xFFFFFFFF},  {5, 0x417B2601, 0xCAC20C24, 0xFFFFFFFF, 0xFFFFFFFF},
    {6, 0x417B2601, 0xCAC20C24, 0xFFFFFFFF, 0xFFFFFFFF},  {7, 0x417B2601, 0xCAC20C24, 0xFFFFFFFF, 0xFFFFFFFF},
    {8, 0x417B2601, 0x94292DD0, 0xFFFFFFFF, 0xFFFFFFFF},  {9, 0x417B2601, 0xCAC20C24, 0xFFFFFFFF, 0xFFFFFFFF},
    {10, 0x417B2601, 0xCAC20C24, 0xFFFFFFFF, 0xFFFFFFFF}, {11, 0x417B2601, 0x406415E3, 0xFFFFFFFF, 0xFFFFFFFF},
    {12, 0x417B2601, 0x406415E3, 0xFFFFFFFF, 0xFFFFFFFF}, {13, 0x417B2601, 0x5577A88B, 0xFFFFFFFF, 0xFFFFFFFF},
    {14, 0x417B2601, 0xCAC20C24, 0xFFFFFFFF, 0xFFFFFFFF},
};

// Next/previous screen functions of the login flow (0x512710..0x5129C0).
const char *ISPConnectNext(OnlineLoginScreenManager *mgr) { // 0x512710
    if (!LobbyIsCreated()) {
        return kOnlineMainMenu;
    }
    int stage = LobbyGetStage();
    if (stage == 1) {
        return kEALogin;
    }
    if (stage == 2) {
        if (mgr->ShowRegistration) {
            // falls through to the persona select
        } else {
            if (mgr->SkipToRegistration) {
                return kRegistration;
            }
            if (mgr->SelectedOption == 0) {
                return kCreateUserA;
            }
            if (mgr->SelectedOption == 1) {
                return kUseExisting;
            }
            return kOnlineMainMenu;
        }
    } else if (stage == 3) {
        // TODO(port): PC 0x796520 fetches the terms of service text first.
        return kEATerms;
    } else if (stage != 5) {
        return nullptr;
    }
    bStrNCpy(mgr->AccountName, PCOnlineGetLobbyAccountName(), 0x10);
    bStrNCpy(mgr->Password, PCOnlineGetLobbyPassword(), 0x10);
    return kSelectPersona;
}

const char *ISPConnectPrev(OnlineLoginScreenManager *mgr) { // 0x5128B0
    if (IsLANMode()) {
        LobbyReset();
        PCOnlineSetState(0);
        return kLANServerSelect;
    }
    if (!LobbyIsCreated()) {
        return kMainMenu;
    }
    int stage = LobbyGetStage();
    if (stage == 1) {
        return kEALogin;
    }
    if (stage == 2) {
        if (mgr->SelectedOption == 1) {
            return kUseExisting;
        }
        if (mgr->SelectedOption != 0) {
            return kEALogin;
        }
    } else if (stage != 3) {
        if (stage != 5 && stage != 6) {
            return kMainMenu;
        }
        mgr->FlagD = false;
        return kSelectPersona;
    }
    return kCreateUserA;
}

const char *EALoginNext(OnlineLoginScreenManager *mgr) { // 0x512800
    if (mgr->SelectedOption == 0) {
        mgr->CreatingAccount = true;
    }
    return kISPConnect;
}
const char *EALoginPrev(OnlineLoginScreenManager *) { // 0x512960
    return kMainMenu;
}
// TODO(port): the screens after a lobby login (0x512820..0x5129C0) need a
// lobby server; they go back to the connecting screen / the EA login.
const char *ToISPConnect(OnlineLoginScreenManager *) {
    return kISPConnect;
}
const char *ToEALogin(OnlineLoginScreenManager *) {
    return kEALogin;
}

// OLEALogin's options (vtable 0x89DB74): the datum's choice id.
class EALoginOption : public ScrollerDatum {
  public:
    EALoginOption() : Id(-1) {}
    ~EALoginOption() override {}
    int Id; // +0x18
};

// A server row of the LAN server list (vtable 0x89E3A4).
class LANServerDatum : public ScrollerDatum {
  public:
    LANServerDatum() : Server(nullptr) {}
    ~LANServerDatum() override {}
    LANServerInfo *Server; // +0x18
};

FEObject *FindStringObject(const char *pkg, const char *fmt, int i) {
    char name[0x20];
    FEngSNPrintf(name, sizeof(name), fmt, i);
    return FEngFindString(pkg, FEHashUpper(name));
}

FEObject *FindObject(const char *pkg, const char *fmt, int i) {
    char name[0x20];
    FEngSNPrintf(name, sizeof(name), fmt, i);
    return FEngFindObject(pkg, FEHashUpper(name));
}

} // namespace

// --- OnlineLoginScreenManager ---

OnlineLoginScreenManager *OnlineLoginScreenManager::pInstance = nullptr;
bool NetworkUseLobbies = false;

OnlineLoginScreenManager::OnlineLoginScreenManager(const char *from_pkg, const char *exit_pkg, const char *done_pkg)
    : SelectedOption(-1), LoginFlag8(false), LoginFlag9(false), CreatingAccount(false), FlagB(false), ShowRegistration(false),
      FlagD(false), FlagE(false), SkipToRegistration(false), FlagUpdateDialog(false), FlagRetryLogin(false), Current(nullptr),
      ExitPackage(exit_pkg), DonePackage(done_pkg) {
    static const struct {
        const char *Name;
        ScreenFunc Next;
        ScreenFunc Prev;
    } kScreens[] = {
        {kISPConnect, ISPConnectNext, ISPConnectPrev}, {kEALogin, EALoginNext, EALoginPrev},
        {kEATerms, ToISPConnect, ToEALogin},          {kSelectPersona, ToISPConnect, ToEALogin},
        {kCreateUserA, ToISPConnect, ToEALogin},      {kCreateUserB, ToISPConnect, ToEALogin},
        {kUseExisting, ToISPConnect, ToEALogin},      {kRegistration, ToISPConnect, ToEALogin},
    };
    AccountName[0] = '\0';
    Password[0] = '\0';
    for (unsigned i = 0; i < sizeof(kScreens) / sizeof(kScreens[0]); i++) {
        if (FindScreen(kScreens[i].Name) == nullptr) {
            ScreenEntry *entry = new ("OnlineLoginScreen", 0) ScreenEntry;
            entry->Name = kScreens[i].Name;
            entry->NextFunc = kScreens[i].Next;
            entry->PrevFunc = kScreens[i].Prev;
            Screens.AddTail(entry);
        }
    }
    Current = FindScreen(kISPConnect);
    cFEng::Get()->QueuePackageSwitch(Current->Name, 0, 0, false);
    bStrNCpy(AccountName, PCOnlineGetLobbyAccountName(), 0x10);
    bStrNCpy(Password, PCOnlineGetLobbyPassword(), 0x10);
}

OnlineLoginScreenManager::~OnlineLoginScreenManager() {
    while (!Screens.IsEmpty()) {
        delete Screens.RemoveHead();
    }
}

void OnlineLoginScreenManager::EnterFront(const char *pkg_name) {
    NetworkUseLobbies = true;
    pInstance = new ("OnlineLoginScreenManager", 0) OnlineLoginScreenManager(pkg_name, kMainMenu, kOnlineMainMenu);
}

void OnlineLoginScreenManager::CommitSuicide() {
    if (pInstance != nullptr) {
        delete pInstance;
    }
    pInstance = nullptr;
}

OnlineLoginScreenManager::ScreenEntry *OnlineLoginScreenManager::FindScreen(const char *pkg_name) {
    for (ScreenEntry *e = Screens.GetHead(); e != Screens.EndOfList(); e = e->GetNext()) {
        if (bStrICmp(e->Name, pkg_name) == 0) {
            return e;
        }
    }
    return nullptr;
}

void OnlineLoginScreenManager::ChangeToNextScreen() {
    const char *next = Current->NextFunc(this);
    if (next != nullptr) {
        Current = FindScreen(next);
        cFEng::Get()->QueuePackageSwitch(next, 0, 0, false);
        if (bStrICmp(next, DonePackage) == 0) {
            CommitSuicide();
        }
    }
}

void OnlineLoginScreenManager::ChangeToPrevScreen() {
    const char *prev = Current->PrevFunc(this);
    Current = FindScreen(prev);
    cFEng::Get()->QueuePackageSwitch(prev, 0, 0, false);
    if (bStrICmp(prev, ExitPackage) == 0) {
        NetworkCoreShutdown();
        CommitSuicide();
    }
}

void OnlineLoginScreenManager::ChangeToScreen(const char *pkg_name) {
    Current = FindScreen(pkg_name);
    cFEng::Get()->QueuePackageSwitch(Current->Name, 0, 0, false);
}

void ExitOnlineMode(const char *pkg_name) {
    NetworkCoreShutdown();
    OnlineLoginScreenManager::CommitSuicide();
    if (pkg_name != nullptr) {
        FEPackage *control = cFEng::Get()->FindPackageWithControl();
        if (control != nullptr && bStrCmp(control->GetName(), pkg_name) != 0) {
            cFEng::Get()->QueuePopChildPackages(pkg_name);
        }
        cFEng::Get()->QueuePackageSwitch(kMainMenu, 0, 0xFF, false);
    }
}

// --- UI_OLISPConnect.fng ---

MenuScreen *CreateOLConnectingScreen(ScreenConstructorData *sd) {
    // TODO(port): modes 2/3 (FEDatabase + 0x180) build the online-error
    // screen (0x55BF70) after an in-game disconnect, which can't happen here.
    if (gPCOnlineSettings.ConnectScreenMode == 0) {
        return new ("UIOnlineLogin", 0) UIOnlineLogin(sd);
    }
    gPCOnlineSettings.ConnectScreenMode = 0;
    return nullptr;
}

UIOnlineLogin::UIOnlineLogin(ScreenConstructorData *sd)
    : MenuScreen(sd), mLoginState(0), mDialog(0), mFlag34(false), mLeaving(false), mUnk38(0), mErrorShown(false), mFlag3D(false),
      mUnk40(-1) {
    uint32 title;
    if (!LobbyIsCreated()) {
        title = 0xD5344356; // "Initializing network ..."
    } else if (!IsLANMode()) {
        title = 0x1B9C916E; // "Connecting to EA.com ..."
    } else {
        title = 0x60129941; // "Connecting..."
    }
    FEngSetLanguageHash(GetPackageName(), kObjTitle, title);
    if (gPCOnlineSettings.State == 0) {
        NetworkCoreStartup();
        NetConnConnect();
        PCOnlineSetState(1);
    }
    mUnk38 = 1;
    mFlag34 = true;
}

UIOnlineLogin::~UIOnlineLogin() {}

void UIOnlineLogin::DoLobbyLoginProcessing() {
    if (gPCOnlineSettings.State < 1 || gPCOnlineSettings.State > 1) {
        return;
    }
    OnlineLoginScreenManager *mgr = OnlineLoginScreenManager::Get();
    if (!LobbyIsCreated()) {
        LobbyInit();
        FEngSetLanguageHash(GetPackageName(), kObjTitle, IsLANMode() ? 0x60129941 : 0x1B9C916E);
        mErrorShown = false;
        LobbyCreate();
    }
    if (!LobbyHasError() && !mErrorShown) {
        if (LobbyGetStageStatus() != 0) {
            return;
        }
        int stage = LobbyGetStage();
        if (stage == 1) {
            mgr->FlagRetryLogin = false;
            if (IsLANMode()) {
                mgr->SelectedOption = 2;
                LobbyConnect(PCOnlineGetLobbyServerAddr(), PCOnlineGetLobbyServerPort(), 60000);
                return;
            }
            if (mgr->SelectedOption != -1) {
                LobbyConnect(PCOnlineGetLobbyServerAddr(), PCOnlineGetLobbyServerPort(), 60000);
                return;
            }
        } else {
            // TODO(port): stages 2..6 (login, terms, spam, persona, the buddy
            // server) follow a successful lobby connection; the port has no
            // lobby client/server yet, so the connection never gets there.
        }
        mgr->ChangeToNextScreen();
        return;
    }
    LobbyErrorT error_copy = *LobbyGetError(); // the reset below clears the native lobby's copy
    LobbyErrorT *error = &error_copy;
    if (mErrorShown) {
        return;
    }
    mErrorShown = true;
    int index = LobbyGetErrorIndex();
    if (index == 12) {
        mgr->ShowRegistration = true;
        mgr->ChangeToNextScreen();
        return;
    }
    uint32 text;
    if (index == 15) {
        // TODO(port): 'ikey' (the CD key check) isn't reachable without a lobby server.
        text = 0xC5C25C87;
        mDialog = DialogInterface::ShowTwoButtons(GetPackageName(), "", dialog_alert, 0x70E01038, 0x417B25E4, 0x80E8874F, __PAD_ACCEPT__,
                                                  __PAD_ACCEPT__, first_dialog_button1, text);
        return;
    }
    LobbyReset();
    if (!IsLANMode()) {
        if (error->Code != -1 || index != 9) {
            text = error->TextHash;
        } else {
            text = 0xCCC6B264; // "EA servers are currently experiencing problems. ..."
        }
    } else if (index == 9 || index == 8 || index == 10) {
        text = 0xA2717C2A; // "Your connection to the LAN server has been lost."
    } else {
        text = error->TextHash;
    }
    FEngSetScript(GetPackageName(), 0xED03D1D3, 0xBCBFCC87, true);
    const LobbyErrorDialog &d = kLobbyErrorDialogs[index < 15 ? index : 0];
    if (d.Button2Msg == 0xFFFFFFFF) {
        mDialog = DialogInterface::ShowOneButton(GetPackageName(), "", dialog_alert, d.Button1Text, d.Button1Msg, static_cast<uint32>(0), text);
        return;
    }
    mDialog = DialogInterface::ShowTwoButtons(GetPackageName(), "", dialog_alert, d.Button1Text, d.Button2Text, d.Button1Msg, d.Button2Msg,
                                              d.Button2Msg, first_dialog_button1, text);
}

void UIOnlineLogin::NotificationMessage(u32 msg, FEObject *pobj, u32 param1, u32 param2) {
    OnlineLoginScreenManager *mgr = OnlineLoginScreenManager::Get();
    switch (msg) {
        case __BUTTON_PRESSED__:
        case __PAD_ACCEPT__:
            if (mDialog != 0) {
                DialogInterface::DismissDialog(mDialog);
                mDialog = 0;
            }
            if (!mLeaving) {
                if (mgr->FlagD && !LobbyIsCreated()) {
                    return;
                }
                mLeaving = true;
                mgr->ChangeToPrevScreen();
            }
            break;
        case 0x5577A88B:
            mgr->ChangeToPrevScreen();
            break;
        case 0x80E8874F:
            mgr->FlagUpdateDialog = true;
            mgr->SkipToRegistration = true;
            mgr->ChangeToNextScreen();
            break;
        case kMsgOnlineExitGame:
            mLeaving = true;
            ExitOnlineMode(GetPackageName());
            break;
        case FEMSG_SCREEN_TICK:
            DoLobbyLoginProcessing();
            if (mLoginState == 2) {
                mgr->ChangeToNextScreen();
                OnlineLoginScreenManager::CommitSuicide();
            }
            break;
    }
}

// --- OLEALogin.fng ---

MenuScreen *CreateUIOLEALoginScreen(ScreenConstructorData *sd) {
    return new ("UIOLEALogin", 0) UIOLEALogin(sd);
}

UIOLEALogin::UIOLEALogin(ScreenConstructorData *sd)
    : MenuScreen(sd), mDialog(0), mFlag30(false), mExitOnline(false), mOptions(GetPackageName(), "", "", true, true, false, true) {
    // PC: the North American build hides two objects of the screen.
    if (bStrCmp(PCOnlineGetAdvertKind(), "NFSMWNA") != 0) {
        FEngSetInvisible(FEngFindObject(GetPackageName(), 0xBE3C17B7));
        FEngSetInvisible(FEngFindObject(GetPackageName(), 0xCA8CA439));
    }
    Setup();
    if (gPCOnlineSettings.State > 1) {
        LobbyReset();
        PCOnlineSetState(1);
    }
    PCOnlineSetLobbyPersona("");
    OnlineLoginScreenManager *mgr = OnlineLoginScreenManager::Get();
    mgr->LoginFlag8 = false;
    mgr->LoginFlag9 = false;
    mgr->CreatingAccount = false;
    mgr->ShowRegistration = false;
    mgr->FlagD = false;
    mgr->SkipToRegistration = false;
    mgr->FlagE = false;
    mgr->FlagUpdateDialog = false;
    mgr->FlagRetryLogin = false;
    mgr->SelectedOption = -1;
}

UIOLEALogin::~UIOLEALogin() {
    if (mDialog != 0) {
        DialogInterface::DismissDialog(mDialog);
        mDialog = 0;
    }
}

void UIOLEALogin::Setup() {
    FEngSetLanguageHash(GetPackageName(), kObjTitle, 0xC033EA98); // "ea login"
    SetupSlots();
    SetupOptions();
    mOptions.SetMouseDownMsg(__BUTTON_PRESSED__);
    mOptions.SetClickToSelectMode(true);
    if (LobbyIsCreated() && LobbyGetStage() > 1) {
        LobbyDisconnect();
    }
}

void UIOLEALogin::SetupSlots() {
    for (int i = 1; i <= 3; i++) {
        ScrollerSlot *slot = new ("ScrollerSlot", 0) ScrollerSlot();
        FEObject *text = FindStringObject(GetPackageName(), "option_%d", i);
        slot->SetBacking(FindObject(GetPackageName(), "mouse_option_%d", i));
        slot->AddData(text);
        mOptions.AddSlot(slot);
    }
}

void UIOLEALogin::SetupOptions() {
    const char *name = PCOnlineGetLobbyAccountName();
    bool has_name = name[0] != '\0';
    mOptions.DeleteScrollerData();
    for (int i = 0; i < 3; i++) {
        char text[0x40];
        EALoginOption *option = new ("EALoginOption", 0) EALoginOption;
        int id;
        if (i == 0) {
            if (has_name) {
                FEngSNPrintf(text, 0x21, GetLocalizedString(0x3D0447AA), name); // "Use %s"
                id = 2;
            } else {
                bStrCpy(text, GetLocalizedString(0xED21B47F)); // "Create New EA Account"
                id = 0;
            }
        } else if (i == 1) {
            if (has_name) {
                bStrCpy(text, GetLocalizedString(0xED21B47F));
                id = 0;
            } else {
                bStrCpy(text, GetLocalizedString(0x036E79B9)); // "Use Existing EA Account"
                id = 1;
            }
        } else if (has_name) {
            bStrCpy(text, GetLocalizedString(0x036E79B9));
            id = 1;
        } else {
            text[0] = '\0';
            id = -1;
        }
        option->Id = id;
        char obj_name[0x10];
        FEngSNPrintf(obj_name, 0x21, "option_%d", i + 1);
        FEPrintf(GetPackageName(), FEHashUpper(obj_name), "%s", text);
        option->AddData(text, 0);
        mOptions.AddData(option);
        if (option->Id == -1) {
            mOptions.Disable(option);
        } else {
            mOptions.Enable(option);
        }
    }
}

void UIOLEALogin::NotificationMessage(u32 msg, FEObject *pobj, u32 param1, u32 param2) {
    switch (msg) {
        case FEMSG_SCREEN_TICK:
            mOptions.UpdateMouse();
            break;
        case __PAD_BACK__:
            // "Are you sure you wish to disconnect and stop playing Online?"
            mDialog = DialogInterface::ShowTwoButtons(GetPackageName(), "", dialog_alert, 0x70E01038, 0x417B25E4, kMsgEALoginQuitYes,
                                                      0x34DC1BCF, first_dialog_button2, GetLocalizedString(0xAFFF7C7A));
            break;
        case __PAD_DOWN__:
            mOptions.ScrollNext();
            break;
        case __PAD_UP__:
            mOptions.ScrollPrev();
            break;
        case kMsgEALoginExited:
            if (mExitOnline) {
                ExitOnlineMode(GetPackageName());
                return;
            }
            OnlineLoginScreenManager::Get()->ChangeToNextScreen();
            break;
        case 0x34DC1BCF:
            if (mDialog != 0) {
                DialogInterface::DismissDialog(mDialog);
                mDialog = 0;
            }
            break;
        case kMsgEALoginQuitYes:
            if (mDialog != 0) {
                DialogInterface::DismissDialog(mDialog);
                mDialog = 0;
            }
            mExitOnline = true;
            cFEng::Get()->QueuePackageMessage(kMsgEALoginExit, GetPackageName(), nullptr);
            break;
        case __BUTTON_PRESSED__:
        case __PAD_ACCEPT__:
            if (OnlineLoginScreenManager::Get() != nullptr) {
                OnlineLoginScreenManager::Get()->SelectedOption = static_cast<EALoginOption *>(mOptions.GetSelectedDatum())->Id;
            }
            cFEng::Get()->QueuePackageMessage(kMsgEALoginExit, GetPackageName(), nullptr);
            break;
    }
}

// --- UI_PC_LAN_ServerSelect.fng ---

MenuScreen *CreateUIPCLanServerSelect(ScreenConstructorData *sd) {
    return new ("UIPCLanServerSelect", 0) UIPCLanServerSelect(sd);
}

UIPCLanServerSelect::UIPCLanServerSelect(ScreenConstructorData *sd)
    : MenuScreen(sd), mServers(GetPackageName(), "ScrollBar", "ScrollBar", true, false, false, true),
      mRefreshTimer(RealTimer.GetSeconds() - 1.0f), mGameUtil(nullptr), mDialogTimer(), mDialog(0), mNumServers(0) {
    bMemSet(mQueryBuf, 0, sizeof(mQueryBuf));
    SetupSlots();
    bStrCpy(mKind, PCOnlineGetAdvertKind());
    // "Looking for LAN servers ..." for up to 5 seconds (or until one shows up).
    mDialog = DialogInterface::ShowOneButton(GetPackageName(), "", dialog_alert, 0x1A294DAD, kMsgLookingDialogDone,
                                             GetTranslatedString(0x43003B5B));
    mDialogTimer = Timer(RealTimer.GetSeconds() + 5.0f);
    if (NetworkCoreStartup() && NetConnStatus('open') == 1 && (mGameUtil = NetGameUtilCreate()) != nullptr) {
        PCOnlineSetState(1);
        NetConnConnect();
    } else {
        // "Could not connect to Local Area Network"
        DialogInterface::ShowOneButton(GetPackageName(), "", dialog_alert, 0x417B2601, __PAD_BACK__, GetTranslatedString(0xA392D977));
    }
    OnlineLoginScreenManager::CommitSuicide();
}

UIPCLanServerSelect::~UIPCLanServerSelect() {
    mServers.DeleteScrollerData();
    if (mDialog != 0) {
        DialogInterface::DismissDialog(mDialog);
    }
    if (mGameUtil != nullptr) {
        NetGameUtilDestroy(mGameUtil);
    }
    if (gPCOnlineSettings.State == 0) {
        NetworkCoreShutdown();
    }
    while (!mList.IsEmpty()) {
        delete mList.RemoveHead();
    }
}

void UIPCLanServerSelect::SetupSlots() {
    for (int i = 1; i <= 15; i++) {
        ScrollerSlot *slot = new ("ScrollerSlot", 0) ScrollerSlot();
        slot->AddData(FindStringObject(GetPackageName(), "scroll_text_%d", i));
        slot->AddData(FindStringObject(GetPackageName(), "scroll_racers_%d", i));
        slot->SetBacking(FindObject(GetPackageName(), "mouse_option_%d", i));
        mServers.AddSlot(slot);
    }
    PopulateList();
}

// Each ProtoAdvt line: name \t "port|users" \t address \n.
void UIPCLanServerSelect::RefreshList() {
    char buf[0x800];
    bMemSet(buf, 0, sizeof(buf));
    int count = NetGameUtilQuery(mGameUtil, mKind, buf, sizeof(buf));
    if (count == mNumServers && bMemCmp(mQueryBuf, buf, sizeof(buf)) == 0) {
        return;
    }
    bMemCpy(mQueryBuf, buf, sizeof(buf));
    mNumServers = count;
    while (!mList.IsEmpty()) {
        delete mList.RemoveHead();
    }
    const char *p = buf;
    for (int i = 0; i < mNumServers; i++) {
        const char *end = p;
        while (*end != '\t') {
            end++;
        }
        LANServerInfo *info = new ("LANServerInfo", 0) LANServerInfo;
        info->Addr = 0;
        info->Port = 0;
        info->NumUsers = 0;
        int len = static_cast<int>(end - p);
        if (len > 0xB) {
            len = 0xC;
        }
        bStrNCpy(info->Name, p, len);
        info->Name[len] = '\0';
        p = end + 1;
        end = p;
        while (*end != '\t') {
            end++;
        }
        char note[0x10];
        bMemSet(note, 0, sizeof(note));
        len = static_cast<int>(end - p);
        bStrNCpy(note, p, len);
        note[len] = '\0';
        char *bar = strchr(note, '|');
        *bar = '\0';
        info->Port = static_cast<uint16>(bStrToLong(note));
        info->NumUsers = bStrToLong(bar + 1);
        info->Addr = NetGameUtilLocate(mGameUtil, mKind, info->Name);
        if (info->Addr == 0) {
            delete info;
        } else {
            mList.AddTail(info);
        }
        while (*end != '\n') {
            end++;
        }
        p = end + 1;
    }
    PopulateList();
    if (mDialog != 0) {
        DialogInterface::DismissDialog(mDialog);
        mDialog = 0;
        mDialogTimer = Timer();
    }
}

void UIPCLanServerSelect::AddServer(LANServerInfo *info) {
    LANServerDatum *datum = new ("LANServerDatum", 0) LANServerDatum;
    datum->Server = info;
    datum->AddData(info->Name, 0);
    char users[4] = {};
    bSPrintf(users, "%d", info->NumUsers);
    datum->AddData(users, 0);
    mServers.AddData(datum);
    mServers.Enable(datum);
}

void UIPCLanServerSelect::PopulateList() {
    char selected[0x40];
    selected[0] = '\0';
    if (mNumServers != 0) {
        const char *sel = mServers.GetActiveSelection();
        if (sel != nullptr) {
            bStrCpy(selected, sel);
        }
        if (mServers.GetSelectedSlot() != nullptr) {
            mServers.GetSelectedSlot()->SetScript(0x7AB5521A);
        }
    }
    mServers.DeleteScrollerData();
    int num = 0;
    for (LANServerInfo *info = mList.GetHead(); info != mList.EndOfList(); info = info->GetNext()) {
        if (mServers.FindDatum(info->Name) == nullptr) {
            AddServer(info);
            num++;
        }
    }
    for (int i = num; i < 15; i++) {
        LANServerDatum *datum = new ("LANServerDatum", 0) LANServerDatum;
        datum->AddData("", 0);
        datum->AddData("", 0);
        mServers.AddData(datum);
        mServers.Disable(datum);
    }
    if (num == 0) {
        if (mServers.GetSelectedSlot() != nullptr) {
            mServers.GetSelectedSlot()->SetScript(0x7AB5521A);
        }
    } else {
        ScrollerDatum *datum = mServers.FindDatum(selected);
        int index = datum != nullptr ? mServers.GetDatumIndex(datum) - 1 : 0;
        mServers.SetSelectedDatum(index);
        if (mServers.GetSelectedSlot() != nullptr) {
            mServers.GetSelectedSlot()->SetScript(0x249DB7B7);
        }
    }
    mServers.Update(true);
}

void UIPCLanServerSelect::NotificationMessage(u32 msg, FEObject *pobj, u32 param1, u32 param2) {
    switch (msg) {
        case kMsgLookingDialogDone:
            mDialogTimer = Timer();
            mDialog = 0;
            break;
        case __PAD_ACCEPT__:
        case __BUTTON_PRESSED__:
            if (!cFEng::Get()->IsPackagePushed(kLAN)) {
                gPCOnlineSettings.LANHosting = false;
                const char *sel = mServers.GetActiveSelection();
                LANServerDatum *datum = static_cast<LANServerDatum *>(sel != nullptr ? mServers.FindDatum(sel) : nullptr);
                if (datum != nullptr && datum->IsEnabled()) {
                    PCOnlineSetLANServerAddr(SocketInAddrGetText(datum->Server->Addr));
                    cFEng::Get()->QueuePackagePush(kLAN, 0, 0, false);
                }
            }
            break;
        case FEHASH_INITCOMPLETE:
            if (mNumServers != 0) {
                FEngSetCurrentButton(GetPackageName(), FEHashUpper("scroll_text_1"));
            }
            break;
        case __PAD_UP__:
            if (mNumServers != 0) {
                mServers.ScrollPrev();
            }
            break;
        case __PAD_BUTTON1__:
            // create a LAN server
            gPCOnlineSettings.LANHosting = true;
            cFEng::Get()->QueuePackagePush(kLAN, 0, 0, false);
            break;
        case __PAD_BACK__:
            PCOnlineSetState(0);
            cFEng::Get()->QueuePackageSwitch(kMainMenu, 0, 0, false);
            break;
        case __PAD_DOWN__:
            if (mNumServers != 0) {
                mServers.ScrollNext();
            }
            break;
        case FEMSG_SCREEN_TICK:
            mServers.UpdateMouse();
            if (mGameUtil == nullptr) {
                break;
            }
            NetConnIdle();
            if (mDialogTimer.IsSet() || HasElapsed(mRefreshTimer)) {
                mRefreshTimer = Timer(RealTimer.GetSeconds() + 3.0f);
                RefreshList();
            }
            if (mDialog != 0 && mDialogTimer.IsSet() && HasElapsed(mDialogTimer)) {
                DialogInterface::DismissDialog(mDialog);
                mDialog = 0;
                mDialogTimer = Timer();
            }
            break;
    }
}

// --- UI_PC_LAN.fng ---

MenuScreen *CreateUIPCLan(ScreenConstructorData *sd) {
    return new ("UIPCLan", 0) UIPCLan(sd);
}

UIPCLan::UIPCLan(ScreenConstructorData *sd) : MenuScreen(sd), mDialog(0), mStartTimer(), mStarted(false) {
    Setup();
}

void UIPCLan::Setup() {
    FEObject *obj = FEngFindObject(GetPackageName(), 0x6042A783);
    if (obj != nullptr) {
        FEScript *script = obj->FindScript(0x1CA7C0);
        if (script != nullptr) {
            obj->SetScript(script, false);
            script->CurTime = 0;
        }
    }
    const char *title = GetLocalizedString(0x22729437); // "Enter name:"
    FEngBeginTextInput(0x5B9D88B9, 6, GetLocalizedString(0x818F0385), title, 0xC);
    FEPrintf(GetPackageName(), kObjTitle, GetTranslatedString(0x22729437));
    PCOnlineSetLobbyAccountName(GetTranslatedString(0xD9DC2F12)); // "PLAYER"
    PCOnlineSetLobbyPersona(GetTranslatedString(0xD9DC2F12));
    PCOnlineSetLobbyPassword(GetTranslatedString(0xD9DC2F12));
}

void UIPCLan::NotificationMessage(u32 msg, FEObject *pobj, u32 param1, u32 param2) {
    switch (msg) {
        case FEMSG_DECLINE_EDITED_TEXT:
        case __PAD_BACK__:
            cFEng::Get()->QueuePackagePop(1);
            break;
        case FEMSG_SCREEN_TICK:
            if (!mStarted && mStartTimer.IsSet() && HasElapsed(mStartTimer)) {
                mStarted = true;
                DialogInterface::DismissDialog(mDialog);
                mDialog = 0;
                PCOnlineSetLANServerAddr(SocketInAddrGetText(NetConnStatus('addr')));
                mStartTimer = Timer();
                cFEng::Get()->QueuePackagePop(1);
                OnlineLoginScreenManager::EnterFront(GetPackageName());
            }
            break;
        case FEMSG_ACCEPT_EDITED_TEXT: {
            const char *name = FEngGetEditedString();
            PCOnlineSetLobbyAccountName(name);
            PCOnlineSetLobbyPersona(name);
            PCOnlineSetLobbyPassword(name);
            if (!gPCOnlineSettings.LANHosting) {
                cFEng::Get()->QueuePackagePop(1);
                OnlineLoginScreenManager::EnterFront(GetPackageName());
                return;
            }
            if (LANServerStart(PCOnlineGetLobbyAccountName())) {
                mStartTimer = Timer(RealTimer.GetSeconds() + 5.0f);
                mDialog = DialogInterface::ShowMessage(GetPackageName(), "", dialog_alert, static_cast<uint32>(0xB13D6A9B)); // "Starting LAN Server ..."
                return;
            }
            // "There was a problem trying to start the LAN server. ..."
            DialogInterface::ShowOneButton(GetPackageName(), "", dialog_alert, 0x417B2601, __PAD_BACK__, static_cast<uint32>(0xA8CD6FD3));
            break;
        }
    }
}
