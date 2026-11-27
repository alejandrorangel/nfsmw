#ifndef UIONLINEPC_HPP
#define UIONLINEPC_HPP

#ifdef EA_PRAGMA_ONCE_SUPPORTED
#pragma once
#endif

#include "Speed/Indep/Src/Frontend/MenuScreens/Common/FEMenuScreen.hpp"
#include "Speed/Indep/Src/Frontend/MenuScreens/Common/feScrollerina.hpp"
#include "Speed/Indep/Src/Misc/Timer.hpp"
#include "Speed/Indep/bWare/Inc/bList.hpp"

// speed.exe 1.3's LAN and online login frontend (the screens of the PC screen
// factory table 0x8F4708..0x8F4858 that are reachable without a lobby server).

struct NetGameUtilRefT;

// The online login flow (OnlineLoginScreenManager, vtable 0x89A1E8): the
// screens before the online main menu, each with the functions that pick the
// next/previous screen.
class OnlineLoginScreenManager {
  public:
    typedef const char *(*ScreenFunc)(OnlineLoginScreenManager *mgr);
    struct ScreenEntry : public bTNode<ScreenEntry> {
        const char *Name;      // +0x8
        ScreenFunc NextFunc;   // +0xC
        ScreenFunc PrevFunc;   // +0x10
    };

    OnlineLoginScreenManager(const char *from_pkg, const char *exit_pkg, const char *done_pkg); // 0x5496A0
    virtual ~OnlineLoginScreenManager();

    static void EnterFront(const char *pkg_name); // 0x5538E0
    static void CommitSuicide();                  // 0x512660
    static OnlineLoginScreenManager *Get() {      // 0x512650
        return pInstance;
    }

    void ChangeToNextScreen();                   // 0x533560
    void ChangeToPrevScreen();                   // 0x5335D0
    void ChangeToScreen(const char *pkg_name);   // 0x533530
    ScreenEntry *FindScreen(const char *pkg_name); // 0x520260

    static OnlineLoginScreenManager *pInstance;

    int SelectedOption;      // +0x4: the OLEALogin choice (-1 none, 0 create, 1 use existing, 2 use <name>)
    bool LoginFlag8;         // +0x8
    bool LoginFlag9;         // +0x9
    bool CreatingAccount;    // +0xA
    bool FlagB;              // +0xB
    bool ShowRegistration;   // +0xC
    bool FlagD;              // +0xD
    bool FlagE;              // +0xE
    bool SkipToRegistration; // +0xF
    bool FlagUpdateDialog;   // +0x10
    bool FlagRetryLogin;     // +0x11
    char AccountName[0x14];  // +0x13
    char Password[0x14];     // +0x27
    bTList<ScreenEntry> Screens; // +0x150
    ScreenEntry *Current;        // +0x158
    const char *ExitPackage;     // +0x15C: "MainMenu.fng"
    const char *DonePackage;     // +0x160: "OL_MAIN.fng"
};

// 0x533460
void ExitOnlineMode(const char *pkg_name);

// UI_OLISPConnect.fng (CreateOLConnectingScreen 0x570910, ctor 0x533650,
// vtable 0x89B294): network startup and the lobby login stages.
class UIOnlineLogin : public MenuScreen {
  public:
    UIOnlineLogin(ScreenConstructorData *sd);
    ~UIOnlineLogin() override;
    void NotificationMessage(u32 msg, FEObject *pobj, u32 param1, u32 param2) override; // 0x55BD10
    void DoLobbyLoginProcessing(); // 0x553980

  private:
    int mLoginState;     // +0x2C: 1 connecting to the buddy server, 2 done
    int mDialog;         // +0x30
    bool mFlag34;        // +0x34
    bool mLeaving;       // +0x35
    int mUnk38;          // +0x38
    bool mErrorShown;    // +0x3C
    bool mFlag3D;        // +0x3D
    int mUnk40;          // +0x40
};

// OLEALogin.fng (CreateUIOLEALoginScreen 0x55BF10, ctor 0x553F70, vtable
// 0x89DB78): create / use an EA account.
class UIOLEALogin : public MenuScreen {
  public:
    UIOLEALogin(ScreenConstructorData *sd);
    ~UIOLEALogin() override; // 0x533B40
    void NotificationMessage(u32 msg, FEObject *pobj, u32 param1, u32 param2) override; // 0x533FA0

  private:
    void Setup();         // 0x549A80
    void SetupSlots();    // 0x533BB0
    void SetupOptions();  // 0x533DB0

    int mDialog;          // +0x2C
    bool mFlag30;         // +0x30
    bool mExitOnline;     // +0x31
    Scrollerina mOptions; // +0x34
};

// A LAN server found by ProtoAdvt (0x24 bytes).
struct LANServerInfo : public bTNode<LANServerInfo> {
    char Name[16];  // +0x8
    uint32 Addr;    // +0x18
    uint16 Port;    // +0x1C
    int NumUsers;   // +0x20
};

// UI_PC_LAN_ServerSelect.fng (CreateUIPCLanServerSelect 0x562A30, ctor
// 0x560470, vtable 0x89E398): the LAN servers on the network.
class UIPCLanServerSelect : public MenuScreen {
  public:
    UIPCLanServerSelect(ScreenConstructorData *sd);
    ~UIPCLanServerSelect() override; // 0x542170
    void NotificationMessage(u32 msg, FEObject *pobj, u32 param1, u32 param2) override; // 0x560640

  private:
    void SetupSlots();   // 0x558B20
    void RefreshList();  // 0x558DF0
    void PopulateList(); // 0x54E560
    void AddServer(LANServerInfo *info); // 0x54E490

    Scrollerina mServers;          // +0x2C
    Timer mRefreshTimer;           // +0xE0
    NetGameUtilRefT *mGameUtil;    // +0xE4
    char mQueryBuf[0x800];         // +0xE8
    Timer mDialogTimer;            // +0x8E8
    int mDialog;                   // +0x8EC
    bTList<LANServerInfo> mList;   // +0x8F0
    int mNumServers;               // +0x8FC
    char mKind[16];                // +0x900
};

// UI_PC_LAN.fng (CreateUIPCLan 0x559030, ctor 0x54E6D0, vtable 0x89E9EC):
// the player name, and starting the LAN server when hosting.
class UIPCLan : public MenuScreen {
  public:
    UIPCLan(ScreenConstructorData *sd);
    ~UIPCLan() override {}
    void NotificationMessage(u32 msg, FEObject *pobj, u32 param1, u32 param2) override; // 0x559090

  private:
    void Setup(); // 0x542260

    int mDialog;       // +0x30
    Timer mStartTimer; // +0x34
    bool mStarted;     // +0x38
};

MenuScreen *CreateOLConnectingScreen(ScreenConstructorData *sd);
MenuScreen *CreateUIOLEALoginScreen(ScreenConstructorData *sd);
MenuScreen *CreateUIPCLanServerSelect(ScreenConstructorData *sd);
MenuScreen *CreateUIPCLan(ScreenConstructorData *sd);

#endif
