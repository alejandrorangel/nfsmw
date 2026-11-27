// MemoryCardImp, the platform half of the game's MemoryCard, as the PC build of
// speed.exe 1.3 has it (0x6CBE20..0x6CC000): profiles are entries of the realmc
// file store in the user's profile folder.
//
// PC keeps them in <My Documents>\NFS Most Wanted\<profile>\<profile>; natively
// the folder is ~/Library/Application Support/NFSMW/Saves (NFSMW_SAVE_DIR
// overrides it). The files themselves are in the PC format.

#include "Speed/Indep/Src/Frontend/MemoryCard/MemoryCardImp.hpp"

#include <cerrno>
#include <cstdlib>
#include <dirent.h>
#include <string>
#include <sys/stat.h>

// 0x91C300 / 0x91C9C0 / 0x91CA00 (set by InitMemoryCard).
uint16 gSaveType0[32];
uint16 gSaveType1[32];
uint16 gSaveType2[16];
// 0x91C2B8: the content name the realmc messages show ("%1"), the profile's.
uint16 MemoryCardImp::gContentName[36];
// 0x8F3948
uint16 *MemoryCardImp::gEntryType[3] = {gSaveType0, gSaveType1, gSaveType2};

namespace {

void MakeDirs(const std::string &path) {
    for (std::size_t i = 1; i <= path.size(); i++) {
        if (i == path.size() || path[i] == '/') {
            const std::string part = path.substr(0, i);
            if (mkdir(part.c_str(), 0755) != 0 && errno != EEXIST) {
                return;
            }
        }
    }
}

// PC: SHGetFolderPath(CSIDL_PERSONAL | CSIDL_FLAG_CREATE) + "\NFS Most Wanted".
std::string ProfileFolder() {
    if (const char *dir = std::getenv("NFSMW_SAVE_DIR")) {
        if (dir[0] != '\0') {
            return dir;
        }
    }
    const char *home = std::getenv("HOME");
    return std::string(home != nullptr ? home : ".") + "/Library/Application Support/NFSMW/Saves";
}

} // namespace

// PC: 0x6CBE20. Profile entries have no name prefix on PC.
const char *MemoryCardImp::GetPrefix() {
    return "";
}

// PC: 0x6CBE30.
const char *MemoryCardImp::GetTitleId() {
    return "";
}

// PC: 0x6CBE40. The header is MemoryCard's two words (version 0x10D, size).
RealmcIface::SaveInfo *MemoryCardImp::ConstructSaveInfo(MemoryCard::SaveType type, const char *DisplayName, int aSize) {
    m_SaveReq.mSaveInfo = new ("SaveInfo", 0) RealmcIface::SaveInfo();
    m_SaveReq.mSaveInfo->Clear();
    m_SaveReq.mSaveInfo->mTypeName = reinterpret_cast<const wchar_t *>(gEntryType[type]);
    m_SaveReq.mSaveInfo->mContentName = reinterpret_cast<const wchar_t *>(gContentName);
    m_SaveReq.mSaveInfo->mHeaderSize = 8;
    m_SaveReq.mSaveInfo->mBodySize = aSize;
    return m_SaveReq.mSaveInfo;
}

// PC: 0x6CBEE0.
void MemoryCardImp::DestructSaveInfo() {
    delete m_SaveReq.mSaveInfo;
    m_SaveReq.mSaveInfo = nullptr;
}

// PC: 0x6CBF00. The PC realmc's bootup check checks nothing: here the profile
// folder is created and made the realmc root, and an entry is "found" when the
// folder has anything besides "." and "..".
void MemoryCardImp::BootupCheckDone(RealmcIface::CardStatus status, RealmcIface::BootupCheckResults *pParam) {
    const std::string folder = ProfileFolder();
    MakeDirs(folder);
    MemoryCard::GetInstance()->GetInterface()->SetRootPath(folder.c_str());
    pParam->mEntryFound = false;
    DIR *dir = opendir(folder.c_str());
    if (dir == nullptr) {
        return;
    }
    int count = 0;
    while (readdir(dir) != nullptr) {
        count++;
    }
    closedir(dir);
    if (count > 2) {
        pParam->mEntryFound = true;
    }
}

// MemoryCard::ListOldSaveFilesPC (PC: 0x518B40) looks in
// <Local AppData>\NFS Underground 2 for more than "." and ".."; natively the
// folder is NFSMW_NFSU2_SAVE_DIR, when set.
bool PortOldSaveFolderHasEntries() {
    const char *folder = std::getenv("NFSMW_NFSU2_SAVE_DIR");
    if (folder == nullptr || folder[0] == '\0') {
        return false;
    }
    DIR *dir = opendir(folder);
    if (dir == nullptr) {
        return false;
    }
    int count = 0;
    while (readdir(dir) != nullptr) {
        count++;
    }
    closedir(dir);
    return count > 2;
}
