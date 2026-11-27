// The PC network layer under the LAN/online frontend (see
// Speed/Indep/Src/Online/PCOnline.hpp), over BSD sockets.
//
// - ProtoAdvt (DirtySDK service advertising, speed.exe 0x84C010..0x84D340) is
//   reimplemented on its PC wire format: 0x180-byte "gEA" UDP broadcasts on
//   port 9999 (ident[3], freq, seq, kind[32], name[32], note[192], addr[120];
//   kind "?" asks everyone to announce again), so LAN servers are found the
//   way the PC finds them.
// - server.dll (the EA lobby server the PC host loads) can't be loaded; the
//   stand-in only advertises the server like server.dll's main loop does
//   (kind "NFSMWNA", the host's name, note "<port>|<users>",
//   "TCP:~1:1024", every 3 s). TODO(port): there's no lobby server, so a
//   lobby login to it fails.
// - The lobby login (DirtySDK's lobby state machine) only does its connect
//   stage: a LAN lobby connect is a real TCP connect to the host; anything
//   else fails like a lost connection ('down'), as the EA servers
//   (pcnfs06.ea.com) are gone. The port never contacts them.

#include "Speed/Indep/Src/Online/PCOnline.hpp"

#include "Speed/Indep/Src/Frontend/Database/FEDatabase.hpp"
#include "Speed/Indep/bWare/Inc/Strings.hpp"

#include <arpa/inet.h>
#include <errno.h>
#include <fcntl.h>
#include <ifaddrs.h>
#include <net/if.h>
#include <netinet/in.h>
#include <stdio.h>
#include <string.h>
#include <sys/socket.h>
#include <time.h>
#include <unistd.h>

#include <vector>

PCOnlineSettings gPCOnlineSettings;

namespace {

uint32 TickMs() {
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return static_cast<uint32>(ts.tv_sec * 1000ull + ts.tv_nsec / 1000000ull);
}

bool IsLAN() {
    return FEDatabase->IsLANMode();
}

char gLANServerAddr[32] = "127.0.0.1";            // a127_0_0_1 0x8F42EC
const char kEALobbyServerAddr[] = "*pcnfs06.ea.com"; // cOnlineSettings::LobbyServerAddr 0x8F42C8
const uint16 kEALobbyServerPort = 30920;           // 0x8F42E8
const uint16 kLANLobbyServerPort = 9900;           // 0x8F430C (server.cfg PORT default)

// Case-insensitive ASCII compare (0x84C580).
bool AdvtStrEq(const char *a, const char *b) {
    for (;; a++, b++) {
        char ca = *a, cb = *b;
        if (ca >= 'A' && ca <= 'Z') {
            ca ^= 0x20;
        }
        if (cb >= 'A' && cb <= 'Z') {
            cb ^= 0x20;
        }
        if (ca != cb) {
            return false;
        }
        if (ca == '\0') {
            return true;
        }
    }
}

uint32 LocalAddress() {
    uint32 best = 0x7F000001;
    struct ifaddrs *list = nullptr;
    if (getifaddrs(&list) == 0) {
        for (struct ifaddrs *i = list; i != nullptr; i = i->ifa_next) {
            if (i->ifa_addr == nullptr || i->ifa_addr->sa_family != AF_INET || !(i->ifa_flags & IFF_UP) || (i->ifa_flags & IFF_LOOPBACK)) {
                continue;
            }
            best = ntohl(reinterpret_cast<struct sockaddr_in *>(i->ifa_addr)->sin_addr.s_addr);
            break;
        }
        freeifaddrs(list);
    }
    return best;
}

// --- ProtoAdvt ---

const int kAdvtPort = 9999;
const int kPacketSize = 0x180;

struct AdvtPacket {
    char Ident[3]; // "gEA"
    uint8 Freq;    // seconds between announcements
    uint8 Seq[4];  // big-endian tick of the announcement's creation
    char Kind[32];
    char Name[32];
    char Note[192];
    char Addr[120];
};
static_assert(sizeof(AdvtPacket) == kPacketSize, "ProtoAdvt packet");

struct AdvtAnnounce {
    AdvtPacket Packet;
    uint32 NextSend;
    bool Cancelled;
};

struct AdvtSeen {
    bool Valid;
    uint32 Seq;
    char Kind[32];
    char Name[32];
    char Note[192];
    char Addr[120];
    uint32 Expire;
    char HostText[16];
    uint32 Host;
    bool Local;
};

struct ProtoAdvt {
    int Socket;
    int RefCount;
    std::vector<AdvtAnnounce> Announce;
    std::vector<AdvtSeen> Seen;
};

ProtoAdvt *gAdvt = nullptr;

void AdvtSendTo(const void *data, uint32 dest) {
    struct sockaddr_in to = {};
    to.sin_len = sizeof(to);
    to.sin_family = AF_INET;
    to.sin_port = htons(kAdvtPort);
    to.sin_addr.s_addr = htonl(dest);
    sendto(gAdvt->Socket, data, kPacketSize, 0, reinterpret_cast<struct sockaddr *>(&to), sizeof(to));
}

// The PC sends to 255.255.255.255; also send to each interface's broadcast
// address so every network (and a second instance on this machine) hears it.
void AdvtBroadcast(const void *data) {
    AdvtSendTo(data, 0xFFFFFFFF);
    struct ifaddrs *list = nullptr;
    if (getifaddrs(&list) == 0) {
        for (struct ifaddrs *i = list; i != nullptr; i = i->ifa_next) {
            if (i->ifa_addr == nullptr || i->ifa_addr->sa_family != AF_INET || !(i->ifa_flags & IFF_UP) || !(i->ifa_flags & IFF_BROADCAST) ||
                i->ifa_broadaddr == nullptr) {
                continue;
            }
            AdvtSendTo(data, ntohl(reinterpret_cast<struct sockaddr_in *>(i->ifa_broadaddr)->sin_addr.s_addr));
        }
        freeifaddrs(list);
    }
}

void AdvtSendQuery() { // 0x84C5C0
    AdvtPacket q = {};
    q.Ident[0] = 'g';
    q.Ident[1] = 'E';
    q.Ident[2] = 'A';
    q.Kind[0] = '?';
    AdvtBroadcast(&q);
}

bool IsOwnAnnouncement(const AdvtPacket &p) {
    for (const AdvtAnnounce &a : gAdvt->Announce) {
        if (memcmp(a.Packet.Seq, p.Seq, 4) == 0 && AdvtStrEq(a.Packet.Kind, p.Kind) && AdvtStrEq(a.Packet.Name, p.Name)) {
            return true;
        }
    }
    return false;
}

// The ProtoAdvt idle callback (0x84C630): take in announcements, expire the
// stale ones, send ours.
void AdvtUpdate() {
    if (gAdvt == nullptr) {
        return;
    }
    for (;;) {
        AdvtPacket p;
        struct sockaddr_in from = {};
        socklen_t fromlen = sizeof(from);
        ssize_t n = recvfrom(gAdvt->Socket, &p, sizeof(p), 0, reinterpret_cast<struct sockaddr *>(&from), &fromlen);
        if (n < 1) {
            break;
        }
        if (n < kPacketSize || p.Ident[0] != 'g' || p.Ident[1] != 'E' || p.Ident[2] != 'A' || p.Kind[0] == '\0') {
            continue;
        }
        p.Kind[31] = p.Name[31] = p.Note[191] = p.Addr[119] = '\0';
        uint32 now = TickMs();
        if (p.Kind[0] == '?' && p.Kind[1] == '\0') {
            for (AdvtAnnounce &a : gAdvt->Announce) {
                if (now + 1000 < a.NextSend) {
                    a.NextSend = now + 1000;
                }
            }
            continue;
        }
        if (p.Name[0] == '\0') {
            continue;
        }
        uint32 seq = (p.Seq[0] << 24) | (p.Seq[1] << 16) | (p.Seq[2] << 8) | p.Seq[3];
        AdvtSeen *slot = nullptr;
        AdvtSeen *free_slot = nullptr;
        for (AdvtSeen &s : gAdvt->Seen) {
            if (!s.Valid) {
                if (free_slot == nullptr) {
                    free_slot = &s;
                }
                continue;
            }
            if (AdvtStrEq(s.Kind, p.Kind) && AdvtStrEq(s.Name, p.Name) && s.Seq == seq) {
                slot = &s;
                break;
            }
        }
        if (slot == nullptr) {
            if (free_slot == nullptr) {
                continue;
            }
            slot = free_slot;
            memset(slot, 0, sizeof(*slot));
            slot->Valid = true;
            slot->Seq = seq;
            bStrNCpy(slot->Kind, p.Kind, sizeof(slot->Kind) - 1);
            bStrNCpy(slot->Name, p.Name, sizeof(slot->Name) - 1);
        }
        bStrNCpy(slot->Addr, p.Addr, sizeof(slot->Addr) - 1);
        bStrNCpy(slot->Note, p.Note, sizeof(slot->Note) - 1);
        // The PC flags announcements from its own address as local; two
        // instances on one machine share the address, so only this
        // process's own announcements count as local here.
        slot->Local = IsOwnAnnouncement(p);
        slot->Expire = now + 1000 + p.Freq * 2000;
        slot->Host = ntohl(from.sin_addr.s_addr);
        bStrCpy(slot->HostText, SocketInAddrGetText(slot->Host));
    }
    uint32 now = TickMs();
    for (AdvtSeen &s : gAdvt->Seen) {
        if (s.Valid && (s.Addr[0] == '\0' || s.Expire < now)) {
            s.Valid = false;
        }
    }
    for (size_t i = 0; i < gAdvt->Announce.size();) {
        AdvtAnnounce &a = gAdvt->Announce[i];
        if (a.Cancelled) {
            a.Packet.Addr[0] = '\0'; // an empty address withdraws it
            AdvtBroadcast(&a.Packet);
            gAdvt->Announce.erase(gAdvt->Announce.begin() + i);
            continue;
        }
        if (a.NextSend < now) {
            AdvtBroadcast(&a.Packet);
            a.NextSend = now + a.Packet.Freq * 1000;
        }
        i++;
    }
}

ProtoAdvt *AdvtConstruct(int size) { // 0x84D1B0
    if (gAdvt != nullptr) {
        gAdvt->RefCount++;
        return gAdvt;
    }
    int s = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
    if (s < 0) {
        return nullptr;
    }
    int one = 1;
    setsockopt(s, SOL_SOCKET, SO_REUSEADDR, &one, sizeof(one));
    setsockopt(s, SOL_SOCKET, SO_REUSEPORT, &one, sizeof(one));
    setsockopt(s, SOL_SOCKET, SO_BROADCAST, &one, sizeof(one));
    fcntl(s, F_SETFL, fcntl(s, F_GETFL) | O_NONBLOCK);
    struct sockaddr_in addr = {};
    addr.sin_len = sizeof(addr);
    addr.sin_family = AF_INET;
    addr.sin_port = htons(kAdvtPort);
    addr.sin_addr.s_addr = htonl(INADDR_ANY);
    if (bind(s, reinterpret_cast<struct sockaddr *>(&addr), sizeof(addr)) != 0) {
        close(s);
        return nullptr;
    }
    gAdvt = new ProtoAdvt;
    gAdvt->Socket = s;
    gAdvt->RefCount = 1;
    gAdvt->Seen.resize(size < 4 ? 4 : size);
    for (AdvtSeen &e : gAdvt->Seen) {
        e.Valid = false;
    }
    AdvtSendQuery();
    return gAdvt;
}

void AdvtDestroy(ProtoAdvt *ref) { // 0x84D340
    if (ref == nullptr) {
        return;
    }
    if (ref->RefCount > 1) {
        ref->RefCount--;
        return;
    }
    for (AdvtAnnounce &a : ref->Announce) {
        a.Cancelled = true;
    }
    AdvtUpdate();
    close(ref->Socket);
    delete ref;
    gAdvt = nullptr;
}

int AdvtAnnounceService(ProtoAdvt *ref, const char *kind, const char *name, const char *note, const char *addr, int freq) { // 0x84CAF0
    if (freq == 0) {
        freq = 30;
    } else if (freq < 2) {
        freq = 2;
    } else if (freq > 250) {
        freq = 250;
    }
    for (AdvtAnnounce &a : ref->Announce) {
        if (AdvtStrEq(a.Packet.Kind, kind) && AdvtStrEq(a.Packet.Name, name)) {
            bool changed = false;
            if (!AdvtStrEq(a.Packet.Addr, addr)) {
                bStrNCpy(a.Packet.Addr, addr, sizeof(a.Packet.Addr) - 1);
                changed = true;
            }
            if (!AdvtStrEq(a.Packet.Note, note)) {
                bStrNCpy(a.Packet.Note, note, sizeof(a.Packet.Note) - 1);
                changed = true;
            }
            if (changed) {
                a.NextSend = TickMs() - 1;
            }
            return 0;
        }
    }
    AdvtAnnounce a = {};
    uint32 now = TickMs();
    a.Packet.Ident[0] = 'g';
    a.Packet.Ident[1] = 'E';
    a.Packet.Ident[2] = 'A';
    a.Packet.Freq = static_cast<uint8>(freq);
    a.Packet.Seq[0] = now >> 24;
    a.Packet.Seq[1] = now >> 16;
    a.Packet.Seq[2] = now >> 8;
    a.Packet.Seq[3] = now;
    bStrNCpy(a.Packet.Kind, kind, sizeof(a.Packet.Kind) - 1);
    bStrNCpy(a.Packet.Name, name, sizeof(a.Packet.Name) - 1);
    bStrNCpy(a.Packet.Note, note, sizeof(a.Packet.Note) - 1);
    bStrNCpy(a.Packet.Addr, addr, sizeof(a.Packet.Addr) - 1);
    a.NextSend = now + 250;
    ref->Announce.push_back(a);
    AdvtBroadcast(&ref->Announce.back().Packet);
    return 0;
}

// 0x84CDE0: one "name \t note \t service \n" line per advertised service
// ("~1" in the service becomes the advertiser's address).
int AdvtQuery(ProtoAdvt *ref, const char *kind, const char *proto, char *buf, int len, bool local) {
    if (len < 5) {
        return 0;
    }
    buf[0] = '\0';
    int count = 0;
    for (const AdvtSeen &s : ref->Seen) {
        if (!s.Valid || !AdvtStrEq(s.Kind, kind) || (!local && s.Local)) {
            continue;
        }
        const char *p = s.Addr;
        while (*p != '\0') {
            char token[256];
            int t = 0;
            while (*p != '\0' && *p != '\t' && t < 255) {
                token[t++] = *p++;
            }
            token[t] = '\0';
            if (*p == '\t') {
                p++;
            }
            if (t <= 4 || token[3] != ':') {
                continue;
            }
            token[3] = '\0';
            if (proto[0] != '\0' && strstr(proto, token) == nullptr) {
                continue;
            }
            token[3] = ':';
            char line[512];
            int l = snprintf(line, sizeof(line), "%s\t%s\t", s.Name, s.Note);
            for (const char *q = token; *q != '\0' && l < 500; q++) {
                if (q[0] == '~' && q[1] == '1') {
                    l += snprintf(line + l, sizeof(line) - l, "%s", s.HostText);
                    q++;
                } else {
                    line[l++] = *q;
                }
            }
            line[l++] = '\n';
            line[l] = '\0';
            if (len < l + 5) {
                bStrCpy(buf, "...\n");
                return count;
            }
            bStrCpy(buf, line);
            buf += l;
            len -= l;
            count++;
        }
    }
    return count;
}

uint32 AdvtLocate(ProtoAdvt *ref, const char *kind, const char *name, uint32 def) { // 0x84D0F0
    if (ref == nullptr) {
        return def;
    }
    for (const AdvtSeen &s : ref->Seen) {
        if (!s.Valid || s.Local || (kind != nullptr && kind[0] != '\0' && !AdvtStrEq(s.Kind, kind)) ||
            (name != nullptr && name[0] != '\0' && !AdvtStrEq(s.Name, name)) || s.Host == 0) {
            continue;
        }
        return s.Host;
    }
    return def;
}

// --- the LAN server stand-in (server.dll) ---

NetGameUtilRefT *gLANServerUtil = nullptr;
char gLANServerName[32];

// --- lobby login ---

struct LobbyState {
    bool Created;
    int Stage;
    int Status;
    int ErrorIndex;
    LobbyErrorT Error;
    int Socket;
    uint32 Deadline;
};
LobbyState gLobby = {};

void LobbyFail(int index) {
    if (gLobby.Socket >= 0) {
        close(gLobby.Socket);
        gLobby.Socket = -1;
    }
    gLobby.Status = 2;
    gLobby.ErrorIndex = index;
    gLobby.Error.Code = index == 9 ? -1 : index;
    gLobby.Error.Unk4 = 0;
    gLobby.Error.TextHash = 0;
    gLobby.Error.UnkC = 0;
}

void LobbyUpdate() {
    if (!gLobby.Created || gLobby.Status != 1 || gLobby.Socket < 0) {
        return;
    }
    struct sockaddr_in peer;
    socklen_t len = sizeof(peer);
    int err = 0;
    socklen_t errlen = sizeof(err);
    getsockopt(gLobby.Socket, SOL_SOCKET, SO_ERROR, &err, &errlen);
    if (err != 0) {
        LobbyFail(9); // 'down'
        return;
    }
    if (getpeername(gLobby.Socket, reinterpret_cast<struct sockaddr *>(&peer), &len) == 0) {
        // TODO(port): connected, but the lobby protocol (ProtoAries login
        // stages) isn't implemented; treat it as a dropped connection.
        LobbyFail(9);
        return;
    }
    if (TickMs() > gLobby.Deadline) {
        LobbyFail(8); // 'time'
    }
}

} // namespace

// --- settings ---

void PCOnlineSetState(int state) {
    gPCOnlineSettings.LastState = gPCOnlineSettings.State;
    gPCOnlineSettings.State = state;
    gPCOnlineSettings.StateAux = 0;
}

const char *PCOnlineGetLobbyAccountName() {
    return IsLAN() ? gPCOnlineSettings.LANUserName : gPCOnlineSettings.NetworkUserName;
}

const char *PCOnlineGetLobbyPersona() {
    return IsLAN() ? gPCOnlineSettings.LANPersona : gPCOnlineSettings.NetworkPersona;
}

const char *PCOnlineGetLobbyPassword() {
    return IsLAN() ? gPCOnlineSettings.LANPasswd : gPCOnlineSettings.NetworkPasswd;
}

void PCOnlineSetLobbyAccountName(const char *name) {
    if (name != nullptr) {
        bStrNCpy(IsLAN() ? gPCOnlineSettings.LANUserName : gPCOnlineSettings.NetworkUserName, name, 0x10);
    }
}

void PCOnlineSetLobbyPersona(const char *persona) {
    if (persona != nullptr) {
        bStrNCpy(IsLAN() ? gPCOnlineSettings.LANPersona : gPCOnlineSettings.NetworkPersona, persona, 0x10);
    }
}

void PCOnlineSetLobbyPassword(const char *pass) {
    if (pass != nullptr) {
        bStrNCpy(IsLAN() ? gPCOnlineSettings.LANPasswd : gPCOnlineSettings.NetworkPasswd, pass, 0x10);
    }
}

const char *PCOnlineGetLobbyServerAddr() {
    return IsLAN() ? gLANServerAddr : kEALobbyServerAddr;
}

uint16 PCOnlineGetLobbyServerPort() {
    return IsLAN() ? kLANLobbyServerPort : kEALobbyServerPort;
}

void PCOnlineSetLANServerAddr(const char *addr) {
    if (addr != nullptr) {
        bStrCpy(gLANServerAddr, addr);
    }
}

// 0x64A3D0: BuildRegion::getLanguage() == 0 is the North American build. The
// port runs the English (NA) data.
const char *PCOnlineGetAdvertKind() {
    return "NFSMWNA";
}

// --- NetworkCore ---

namespace {
bool gNetworkUp = false;
}

bool NetworkCoreStartup() {
    gNetworkUp = true;
    return true;
}

void NetworkCoreShutdown() {
    if (gNetworkUp) {
        LobbyReset();
        gLobby.Created = false; // 0x7A00F0 destroys the lobby
        LANServerStop();
    }
    gNetworkUp = false;
    PCOnlineSetState(0);
}

// --- NetConn ---

int NetConnStatus(uint32 selector) {
    if (selector == 'open') {
        return gNetworkUp ? 1 : 0;
    }
    if (!gNetworkUp) {
        return 0;
    }
    if (selector == 'addr') {
        return static_cast<int>(LocalAddress());
    }
    return 0;
}

void NetConnConnect() {}

void NetConnIdle() {
    AdvtUpdate();
    LobbyUpdate();
}

const char *SocketInAddrGetText(uint32 addr) {
    static char text[20];
    snprintf(text, sizeof(text), "%u.%u.%u.%u", (addr >> 24) & 0xFF, (addr >> 16) & 0xFF, (addr >> 8) & 0xFF, addr & 0xFF);
    return text;
}

// --- NetGameUtil ---

struct NetGameUtilRefT {
    ProtoAdvt *Advt; // +0xA4
};

NetGameUtilRefT *NetGameUtilCreate() {
    NetGameUtilRefT *ref = new NetGameUtilRefT;
    ref->Advt = nullptr;
    return ref;
}

void NetGameUtilDestroy(NetGameUtilRefT *ref) {
    if (ref == nullptr) {
        return;
    }
    if (ref->Advt != nullptr) {
        AdvtDestroy(ref->Advt);
    }
    delete ref;
}

int NetGameUtilQuery(NetGameUtilRefT *ref, const char *kind, char *buf, int len) {
    if (ref->Advt == nullptr) {
        ref->Advt = AdvtConstruct(16);
    }
    if (ref->Advt == nullptr) {
        return 0;
    }
    return AdvtQuery(ref->Advt, kind, "", buf, len, false);
}

uint32 NetGameUtilLocate(NetGameUtilRefT *ref, const char *kind, const char *name) {
    if (ref->Advt == nullptr) {
        ref->Advt = AdvtConstruct(16);
    }
    if (ref->Advt == nullptr) {
        return 0;
    }
    return AdvtLocate(ref->Advt, kind, name, 0);
}

// --- server.dll ---

bool LANServerStart(const char *name) {
    if (gLANServerUtil != nullptr) {
        return true;
    }
    gLANServerUtil = NetGameUtilCreate();
    gLANServerUtil->Advt = AdvtConstruct(16);
    if (gLANServerUtil->Advt == nullptr) {
        NetGameUtilDestroy(gLANServerUtil);
        gLANServerUtil = nullptr;
        return false;
    }
    bStrNCpy(gLANServerName, name != nullptr && name[0] != '\0' ? name : "Default Name", sizeof(gLANServerName) - 1);
    char note[32];
    snprintf(note, sizeof(note), "%d|%d", kLANLobbyServerPort, 0); // PORT | logged-in users
    AdvtAnnounceService(gLANServerUtil->Advt, PCOnlineGetAdvertKind(), gLANServerName, note, "TCP:~1:1024", 3);
    return true;
}

void LANServerStop() {
    if (gLANServerUtil != nullptr) {
        NetGameUtilDestroy(gLANServerUtil);
        gLANServerUtil = nullptr;
    }
}

// --- lobby login ---

int LobbyInit() {
    return 0;
}

bool LobbyCreate() {
    if (gLobby.Created) {
        gLobby.Stage = 1;
        gLobby.Status = 0;
        return true;
    }
    gLobby = LobbyState();
    gLobby.Created = true;
    gLobby.Stage = 1;
    gLobby.Status = 0;
    gLobby.Socket = -1;
    return true;
}

bool LobbyIsCreated() {
    return gLobby.Created;
}

int LobbyGetStage() {
    return gLobby.Created ? gLobby.Stage : -1;
}

int LobbyGetStageStatus() {
    LobbyUpdate();
    return gLobby.Created ? gLobby.Status : 3;
}

bool LobbyHasError() {
    LobbyUpdate();
    return gLobby.Created && gLobby.Status == 2;
}

LobbyErrorT *LobbyGetError() {
    return &gLobby.Error;
}

int LobbyGetErrorIndex() {
    return gLobby.Error.Code == -1 ? 9 : gLobby.ErrorIndex;
}

bool LobbyConnect(const char *addr, uint16 port, int timeout_ms) {
    if (!gLobby.Created || gLobby.Status != 0 || gLobby.Stage != 1) {
        return false;
    }
    gLobby.Status = 1;
    gLobby.Deadline = TickMs() + timeout_ms;
    struct in_addr in;
    // Only a LAN lobby (a numeric address) is ever connected to; the EA lobby
    // name ("*pcnfs06.ea.com") isn't looked up.
    if (!IsLAN() || addr == nullptr || inet_aton(addr, &in) == 0) {
        LobbyFail(9);
        return true;
    }
    int s = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
    if (s < 0) {
        LobbyFail(9);
        return true;
    }
    fcntl(s, F_SETFL, fcntl(s, F_GETFL) | O_NONBLOCK);
    struct sockaddr_in to = {};
    to.sin_len = sizeof(to);
    to.sin_family = AF_INET;
    to.sin_port = htons(port);
    to.sin_addr = in;
    gLobby.Socket = s;
    if (connect(s, reinterpret_cast<struct sockaddr *>(&to), sizeof(to)) != 0 && errno != EINPROGRESS) {
        LobbyFail(9);
    }
    return true;
}

void LobbyDisconnect() {
    if (gLobby.Socket >= 0) {
        close(gLobby.Socket);
        gLobby.Socket = -1;
    }
}

// Reset__9LobbyCore: back to the connect stage; the lobby object stays.
void LobbyReset() {
    LobbyDisconnect();
    bool created = gLobby.Created;
    gLobby = LobbyState();
    gLobby.Socket = -1;
    gLobby.Created = created;
    gLobby.Stage = 1;
}
