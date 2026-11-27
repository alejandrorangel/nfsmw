#ifndef ONLINE_PCONLINE_H
#define ONLINE_PCONLINE_H

#ifdef EA_PRAGMA_ONCE_SUPPORTED
#pragma once
#endif

#include <types.h>

// The PC (speed.exe 1.3) network layer the LAN/online frontend screens talk
// to: EA's DirtySDK (NetConn, NetGameUtil/ProtoAdvt, the lobby login state
// machine), the game's NetworkCore/LobbyCore wrappers, the server.dll LAN
// lobby server loader and the PC cOnlineSettings fields. On PC these are
// unnamed library code; the names here are DirtySDK's/the game's, the
// addresses the PC ones. The native port implements them over BSD sockets
// (port/src/Net/PCOnlineNative.cpp).

// --- PC cOnlineSettings (FEDatabase + 0x134) and frontend database flags ---
struct PCOnlineSettings {
    int State;                // +0x68 (FEDatabase + 0x19C): 0 offline, 1 network up, 2 logged in
    int LastState;            // +0x6C
    int StateAux;             // +0x48 (cleared with every state change)
    char NetworkUserName[16]; // +0x70
    char NetworkPersona[16];  // +0x84
    char NetworkPasswd[16];   // +0x98
    char LANUserName[16];     // +0xAC
    char LANPersona[16];      // +0xC0
    char LANPasswd[16];       // +0xD4
    bool LANHosting;          // FEDatabase + 0x172: this player creates the LAN server
    int ConnectScreenMode;    // FEDatabase + 0x180: which UI_OLISPConnect screen to build
};
extern PCOnlineSettings gPCOnlineSettings;

void PCOnlineSetState(int state);           // 0x56E300
const char *PCOnlineGetLobbyAccountName();  // 0x56E320
const char *PCOnlineGetLobbyPersona();      // 0x56E340
const char *PCOnlineGetLobbyPassword();     // 0x56E360
void PCOnlineSetLobbyAccountName(const char *name); // 0x56E380
void PCOnlineSetLobbyPersona(const char *persona);  // 0x56E3C0
void PCOnlineSetLobbyPassword(const char *pass);    // 0x56E400
const char *PCOnlineGetLobbyServerAddr();   // 0x56E440: "127.0.0.1" (LAN) / "*pcnfs06.ea.com"
uint16 PCOnlineGetLobbyServerPort();        // 0x56E460: 9900 (LAN) / 30920
void PCOnlineSetLANServerAddr(const char *addr); // 0x56E480
const char *PCOnlineGetAdvertKind();        // 0x64A3D0: "NFSMWNA" / "NFSMW" by build region

// --- NetworkCore (0x799EB0 / 0x7A0F50) ---
bool NetworkCoreStartup();  // the network thread + LobbyCore
void NetworkCoreShutdown(); // also stops the LAN server and sets the online state to 0

// --- DirtySDK NetConn ---
int NetConnStatus(uint32 selector);    // 0x83CD40: 'open' (1 when the stack is up), 'addr' (own address)
void NetConnConnect();                 // 0x83CD10
void NetConnIdle();                    // 0x83FF00: runs the idle callbacks (ProtoAdvt)
const char *SocketInAddrGetText(uint32 addr); // 0x83FAF0 (static buffer)

// --- DirtySDK NetGameUtil (LAN service discovery through ProtoAdvt) ---
struct NetGameUtilRefT;
NetGameUtilRefT *NetGameUtilCreate();                                         // 0x83ED50
void NetGameUtilDestroy(NetGameUtilRefT *ref);                                // 0x83ED90
int NetGameUtilQuery(NetGameUtilRefT *ref, const char *kind, char *buf, int len); // 0x83ECF0
uint32 NetGameUtilLocate(NetGameUtilRefT *ref, const char *kind, const char *name); // 0x83ECA0

// --- server.dll (StartServer/StopServer/IsServerRunning, loaded by 0x741E20) ---
bool LANServerStart(const char *name); // 0x741E20
void LANServerStop();                  // 0x7361A0

// --- LobbyCore login (the DirtySDK lobby state machine, 0x78B350..0x792BF0) ---
// stage: 1 connect, 2 login, 3 terms of service, 4 spam, 5 persona, 6 logged in;
// status of the current stage: 0 waiting for input, 1 busy, 2 failed.
struct LobbyErrorT {
    int Code;   // -1: the connection went down
    int Unk4;
    uint32 TextHash;
    int UnkC;
};
int LobbyInit();                           // 0x7A01C0
bool LobbyCreate();                        // 0x792A00
bool LobbyIsCreated();                     // 0x78B350
int LobbyGetStage();                       // 0x78B380
int LobbyGetStageStatus();                 // 0x78B3A0
bool LobbyHasError();                      // 0x78B420
LobbyErrorT *LobbyGetError();              // 0x78B450(7)
int LobbyGetErrorIndex();                  // 0x78B490(7): index of the 4CC (none dber filt imst miss logn lock rsrv time down disc ...)
bool LobbyConnect(const char *addr, uint16 port, int timeout_ms); // 0x792BF0
void LobbyDisconnect();                    // 0x78FDD0
void LobbyReset();                         // 0x79F450

#endif
