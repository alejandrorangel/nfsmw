// Native implementation of EA's RealMemcard ("realmc") library as the PC build
// of speed.exe 1.3 has it: a file-based "memory card" in a directory (the
// game's profile folder, Documents\NFS Most Wanted on PC), one subdirectory per
// entry holding one file of the same name:
//
//   <root>/<entry>/<entry>:
//     0x00 u32 magic 0x4D433032 ('20CM')
//     0x04 u32 total file size (0x1C + header size + body size)
//     0x08 u32 header size
//     0x0C u32 body size
//     0x10 u32 header signature   (_CalcSignature of the header data)
//     0x14 u32 body signature     (_CalcSignature of the body data)
//     0x18 u32 signature of bytes 0x00..0x17
//     0x1C header data, then body data
//
// The PC realmc (0x7F3B00..0x7F7700) runs a TaskManager of queued tasks whose
// steps run from MemcardInterface::Update, with the library's own messages
// (overwrite/delete prompts, "Save complete." ...) shown through the game's
// IGameInterface::ShowMessage and answered with MessageDone. This keeps those
// steps and callbacks; the file I/O is POSIX instead of the MSVC CRT.
//
// Also here: RealCore's LOCALE_* (the realmc message strings, memcard/LOCALE_*.loc).

#include "realmemcard/memcard_interface.h"

#include <algorithm>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <ctime>
#include <deque>
#include <dirent.h>
#include <fcntl.h>
#include <fnmatch.h>
#include <string>
#include <sys/mount.h>
#include <sys/stat.h>
#include <strings.h>
#include <unistd.h>
#include <vector>

// ---------------------------------------------------------------------------
// RealCore LOCALE (PC: LOCALE_create 0x7F0FD8, LOCALE_setstate 0x7F0EF2,
// LOCALE_getstrA 0x7F0F48). The .loc data: a LOCH chunk (+0x8 flags, bit 0 = has
// an id index; +0xC u16 language count; +0xE u16 current language; +0x10 per
// language offset of its LOCL chunk), then an optional LOCI chunk (+0x8 count,
// +0x10 sorted {u16 id, u16 index}) and the LOCL chunks (+0xC count, +0x10
// per string offset of a UTF-16 string, relative to the chunk).
// ---------------------------------------------------------------------------
enum LOCALESTATE {
    LOCALE_LANGUAGEID = 0,
    LOCALE_LANGUAGECOUNT = 1,
    LOCALE_STRINGCOUNT = 2,
    LOCALE_HASINDEX = 3,
};

static uint32_t LocaleU32(const void *data, int offset) {
    uint32_t v;
    std::memcpy(&v, static_cast<const uint8_t *>(data) + offset, sizeof(v));
    return v;
}

static uint16_t LocaleU16(const void *data, int offset) {
    uint16_t v;
    std::memcpy(&v, static_cast<const uint8_t *>(data) + offset, sizeof(v));
    return v;
}

void LOCALE_create(void *data, int param) {}

void LOCALE_setstate(void *data, LOCALESTATE state, int param) {
    if (state == LOCALE_LANGUAGEID) {
        uint16_t lang = static_cast<uint16_t>(param);
        std::memcpy(static_cast<uint8_t *>(data) + 0xE, &lang, sizeof(lang));
    }
}

const char *LOCALE_getstrA(void *data, int strID) {
    if (data == nullptr) {
        return nullptr;
    }
    const uint8_t *loc = static_cast<const uint8_t *>(data);
    int index = strID;
    if ((LocaleU32(loc, 8) & 1) != 0) {
        // sub_7F0F1B: bsearch the LOCI chunk that follows LOCH.
        const uint8_t *loci = loc + LocaleU32(loc, 4);
        const uint32_t count = LocaleU32(loci, 8);
        index = -1;
        int lo = 0;
        int hi = static_cast<int>(count) - 1;
        while (lo <= hi) {
            const int mid = (lo + hi) / 2;
            const int id = LocaleU16(loci, 0x10 + mid * 4);
            if (id == strID) {
                index = LocaleU16(loci, 0x10 + mid * 4 + 2);
                break;
            }
            if (id < strID) {
                lo = mid + 1;
            } else {
                hi = mid - 1;
            }
        }
    }
    const uint8_t *locl = loc + LocaleU32(loc, 0x10 + LocaleU16(loc, 0xE) * 4);
    if (index >= 0 && index < static_cast<int>(LocaleU32(locl, 0xC))) {
        return reinterpret_cast<const char *>(locl + LocaleU32(locl, 0x10 + index * 4));
    }
    return nullptr;
}

// ---------------------------------------------------------------------------
// Small RealmcIface structs.
// ---------------------------------------------------------------------------
namespace RealmcIface {

void BootupCheckResults::Clear() {
    mFirstGoodCard = CARD_UNKNOWN;
    mNumBlocksNeeded = 0;
    mEntryFound = false;
}

CardInfo::CardInfo() {
    Clear();
}

void CardInfo::Clear() {
    mCardId = CARD_UNKNOWN;
    mStatus = STATUS_UNKNOWN;
    mFreeSpace = 0;
    mFreeFiles = 0;
    mTotalSpace = 0;
    mFreeSpaceOverLimit = false;
    mTotalSpaceOverLimit = false;
}

EntryInfo::EntryInfo() {
    mTime.mCreated = 0;
    mTime.mLastModified = 0;
    mTime.mLastAccessed = 0;
    Clear();
}

// 0x7F3E78
void EntryInfo::Clear() {
    mName = nullptr;
    mStatus = STATUS_INSUFFICIENT_SPACE;
    mEntryBlocks = 0;
    mUserDataSize = 0;
    mTime.mCreated = 0;
    mTime.mLastModified = 0;
    mTime.mLastAccessed = 0;
    std::memset(mCompanyCode, 0, sizeof(mCompanyCode));
    std::memset(mGameCode, 0, sizeof(mGameCode));
}

Ps2SaveInfo::Ps2SaveInfo() {
    Clear();
}

void Ps2SaveInfo::Clear() {
    std::memset(this, 0, sizeof(*this));
}

XboxSaveInfo::XboxSaveInfo() {
    Clear();
}

void XboxSaveInfo::Clear() {
    mImageData = nullptr;
    mImageDataSize = 0;
}

GcSaveInfo::GcSaveInfo() {
    Clear();
}

void GcSaveInfo::Clear() {
    std::memset(this, 0, sizeof(*this));
}

SaveInfo::SaveInfo() {
    Clear();
}

// 0x7F3EC3
void SaveInfo::Clear() {
    mPs2Info.Clear();
    mXboxInfo.Clear();
    mGcInfo.Clear();
    mHeaderSize = 0;
    mBodySize = 0;
    mTypeName = nullptr;
    mContentName = nullptr;
}

SaveReq::SaveReq() {
    Clear();
}

void SaveReq::Clear() {
    mNumSaves = 0;
    mSaveInfo = nullptr;
}

// 0x7F3D0B
void BootupCheckParams::Clear() {
    mEntryNamePattern = nullptr;
    mNumSaveTypes = 0;
    mSaveReqs = nullptr;
    mValidCardIds = 0xFFFFFFFF;
}

void TitleInfo::Clear() {
    mTitleType = TITLE_DEFAULT;
    std::memset(&mTitleId, 0, sizeof(mTitleId));
    mNameType = NAME_ENTRY;
    mDataFormat = FORMAT_LAYER2;
}

void TitleInfo::Init(TitleType titleType, TitleId titleId, NameType nameType, DataFormat dataFormat) {
    mTitleType = titleType;
#ifdef EA_PLATFORM_GAMECUBE
    mTitleId = titleId;
#else
    std::memcpy(&mTitleId, titleId, sizeof(mTitleId));
#endif
    mNameType = nameType;
    mDataFormat = dataFormat;
}

} // namespace RealmcIface

// ---------------------------------------------------------------------------
// The interface implementation.
// ---------------------------------------------------------------------------
namespace {

using namespace RealmcIface;

const uint32_t kFileMagic = 0x4D433032;
const int kFileHeaderSize = 0x1C;

// 0x908248: the MSB-first CRC-32 table (polynomial 0x04C11DB7).
uint32_t gCrcTable[256];

void InitCrcTable() {
    if (gCrcTable[1] != 0) {
        return;
    }
    for (uint32_t i = 0; i < 256; i++) {
        uint32_t c = i << 24;
        for (int b = 0; b < 8; b++) {
            c = (c & 0x80000000) ? (c << 1) ^ 0x04C11DB7 : (c << 1);
        }
        gCrcTable[i] = c;
    }
}

// 0x7F73E1 (_CalcSignature): a CRC-32 seeded with the complement of the first
// big-endian word.
uint32_t CalcSignature(const void *data, int size) {
    if (data == nullptr || size < 4) {
        return 0;
    }
    InitCrcTable();
    const uint8_t *p = static_cast<const uint8_t *>(data);
    uint32_t crc = ~((uint32_t(p[0]) << 24) | (uint32_t(p[1]) << 16) | (uint32_t(p[2]) << 8) | uint32_t(p[3]));
    for (int i = 4; i < size; i++) {
        crc = ((crc << 8) | p[i]) ^ gCrcTable[crc >> 24];
    }
    return ~crc;
}

struct FileHeader {
    uint32_t mMagic;
    uint32_t mTotalSize;
    uint32_t mHeaderSize;
    uint32_t mBodySize;
    uint32_t mHeaderSig;
    uint32_t mBodySig;
    uint32_t mSig;
};
static_assert(sizeof(FileHeader) == kFileHeaderSize, "realmc file header");

// 0x7F4213: a time_t as seconds since 2000-01-01 local time.
uint32_t ConvertTime(time_t t) {
    if (t < 0) {
        return 0;
    }
    struct tm lt;
    localtime_r(&t, &lt);
    uint32_t years = static_cast<uint32_t>(lt.tm_year - 100);
    uint32_t leaps = years >> 2;
    if ((years & 3) == 0 && lt.tm_mon < 2) {
        leaps--;
    }
    return (leaps + 1) * 86400 + ((lt.tm_hour + (years * 365 + lt.tm_yday) * 24) * 60 + lt.tm_min) * 60 + lt.tm_sec;
}

// The _findfirst/_findnext iterator (0x7F4137 / 0x7F4275), over the entries of
// a directory that match a wildcard. Windows lists NTFS directories in name
// order, so the names are sorted (case-insensitively) to list profiles alike.
struct FindIterator {
    bool mStarted = false;
    std::vector<std::string> mNames;
    std::size_t mNext = 0;
    std::string mDirPath;
    std::string mName;
    CardStatus mStatus = STATUS_FILE_NOT_FOUND;
    EntryInfo mInfo;
    bool mIsDir = false;

    // Returns false (mStatus = STATUS_FILE_NOT_FOUND) when there are no more.
    bool Next(const char *path) {
        if (!mStarted) {
            mStarted = true;
            std::string p(path);
            std::size_t slash = p.find_last_of('/');
            mDirPath = slash == std::string::npos ? "." : p.substr(0, slash);
            const std::string pattern = slash == std::string::npos ? p : p.substr(slash + 1);
            if (DIR *dir = opendir(mDirPath.c_str())) {
                while (struct dirent *de = readdir(dir)) {
                    if (fnmatch(pattern.c_str(), de->d_name, FNM_CASEFOLD) == 0) {
                        mNames.push_back(de->d_name);
                    }
                }
                closedir(dir);
            }
            std::sort(mNames.begin(), mNames.end(), [](const std::string &a, const std::string &b) { return strcasecmp(a.c_str(), b.c_str()) < 0; });
        }
        while (mNext < mNames.size()) {
            mName = mNames[mNext++];
            struct stat st;
            const std::string full = mDirPath + "/" + mName;
            if (stat(full.c_str(), &st) != 0) {
                continue;
            }
            mIsDir = S_ISDIR(st.st_mode);
            mStatus = STATUS_OK;
            mInfo.Clear();
            mInfo.mName = const_cast<char *>(mName.c_str());
            const long long size = mIsDir ? 0 : static_cast<long long>(st.st_size);
            if (size == 0) {
                mInfo.mStatus = STATUS_INSUFFICIENT_SPACE;
            } else {
                mInfo.mStatus = size < kFileHeaderSize ? STATUS_INSUFFICIENT_SPACE : STATUS_OK;
                mInfo.mEntryBlocks = static_cast<uint32_t>(size);
                mInfo.mUserDataSize = size < kFileHeaderSize ? 0 : static_cast<uint32_t>(size - kFileHeaderSize);
            }
            mInfo.mTime.mCreated = ConvertTime(st.st_birthtimespec.tv_sec);
            mInfo.mTime.mLastModified = ConvertTime(st.st_mtimespec.tv_sec);
            mInfo.mTime.mLastAccessed = ConvertTime(st.st_atimespec.tv_sec);
            return true;
        }
        mStatus = STATUS_FILE_NOT_FOUND;
        return false;
    }
};

// A library message (the PC's 0x68-byte message object, 0x7F5A37 / 0x7F5A71).
struct LibMessage {
    int mId = 0;
    const uint16_t *mText = nullptr;
    uint32_t mNumOptions = 0;
    int mOptionIds[MAX_MESSAGE_OPTIONS] = {};
    const uint16_t *mOptions[MAX_MESSAGE_OPTIONS] = {};
};

// A queued request (the TaskManager's task entries).
struct Request {
    MemcardTask mOp = TASK_NONE;
    std::string mName;
    std::string mName2;
    char *mHeader = nullptr;
    char *mBody = nullptr;
    const uint16_t *mContent = nullptr;
    SaveInfo mSaveInfo;
    TitleInfo mTitleInfo;
    bool mHasTitleInfo = false;
    int mState = 0;
    CardId mCardId = CARD_UNKNOWN;
};

} // namespace

namespace RealmcIface {

class MemcardInterfaceImpl {
  public:
    MemcardInterfaceImpl(Realmc::SystemInterface *iSystem, IGameInterface *iGame, GameInfo *gameInfo)
        : mGame(iGame) {
        if (iSystem != nullptr) {
            mSystem = *iSystem;
        }
        mHideMask = 0;
        mForceShowMask = 0;
    }

    // ---- requests (the TaskManager entry points) -------------------------
    void BootupCheck() {
        Request r;
        r.mOp = TASK_BOOTUPCHECK;
        Submit(r);
    }
    void CheckCard(CardId cardId) {
        if (mOp != TASK_NONE) {
            // 0x7F706E: busy, report the last known card state.
            mGame->CardChecked(&mLastCardInfo);
            return;
        }
        Request r;
        r.mOp = TASK_CHECKCARD;
        r.mCardId = cardId;
        Submit(r);
    }
    void Save(const char *entryName, const char *header, const char *body, const SaveInfo *saveInfo, const TitleInfo *titleInfo) {
        Request r;
        r.mOp = TASK_SAVE;
        r.mName = entryName != nullptr ? entryName : "";
        r.mHeader = const_cast<char *>(header);
        r.mBody = const_cast<char *>(body);
        if (saveInfo != nullptr) {
            r.mSaveInfo = *saveInfo;
        }
        if (titleInfo != nullptr) {
            r.mTitleInfo = *titleInfo;
            r.mHasTitleInfo = true;
        }
        Submit(r);
    }
    void Load(const char *entryName, char *header, char *body, const uint16_t *contentName, const TitleInfo *titleInfo) {
        Request r;
        r.mOp = TASK_LOAD;
        r.mName = entryName != nullptr ? entryName : "";
        r.mHeader = header;
        r.mBody = body;
        r.mContent = contentName;
        if (titleInfo != nullptr) {
            r.mTitleInfo = *titleInfo;
            r.mHasTitleInfo = true;
        }
        Submit(r);
    }
    void Delete(const char *entryName, const uint16_t *contentName) {
        Request r;
        r.mOp = TASK_DELETE;
        r.mName = entryName != nullptr ? entryName : "";
        r.mContent = contentName;
        Submit(r);
    }
    void FindEntries(const char *pattern, const TitleInfo *titleInfo) {
        Request r;
        r.mOp = TASK_FINDENTRIES;
        r.mName = pattern != nullptr ? pattern : "";
        if (titleInfo != nullptr) {
            r.mTitleInfo = *titleInfo;
            r.mHasTitleInfo = true;
        }
        Submit(r);
    }
    void SetAutosave(AutosaveState state) {
        Request r;
        r.mOp = TASK_SETAUTOSAVE;
        r.mState = state;
        Submit(r);
    }
    // 0x7F70B5
    void SetMonitor(MonitorState state) {
        mMonitorState = state;
        if (mOp == TASK_NONE) {
            if (state != MONITOR_OFF) {
                mOp = TASK_MONITOR;
                mMonitorReported = false;
                StartCheckCard();
                return;
            }
        } else {
            if (mOp != TASK_MONITOR) {
                return;
            }
            if (state == MONITOR_OFF) {
                ClearTask();
                mOp = TASK_NONE;
            }
        }
        mGame->SetMonitorDone(mLastCardInfo.mStatus, state);
        StartQueued();
    }
    // 0x7F5BD2
    void SetMessage(MessageState state, uint32_t message) {
        if (state == MESSAGE_HIDE) {
            mHideMask |= message;
        } else if (state == MESSAGE_FORCE) {
            mForceShowMask |= message;
            mHideMask &= ~message;
            return;
        } else {
            mHideMask &= ~message;
        }
        mForceShowMask &= ~message;
    }
    // 0x7F5AE8
    void MessageDone(MessageChoices choice) {
        mChoice = choice;
    }
    bool IsResettable() const {
        return mResettable;
    }
    void SetRootPath(const char *path) {
        mRootPath = path != nullptr ? path : "";
        while (mRootPath.size() > 1 && mRootPath.back() == '/') {
            mRootPath.pop_back();
        }
    }
    uint32_t CalcSaveSize(const SaveInfo *saveInfo) const {
        return saveInfo->mBodySize + kFileHeaderSize + saveInfo->mHeaderSize;
    }

    // 0x7F5B1D
    MemcardTask Update(uint32_t elapsedTime) {
        if (mTask == TASK_NONE) {
            return TASK_NONE;
        }
        switch (mTask) {
            case TASK_LOAD:
                ProcessLoad();
                break;
            case TASK_CHECKCARD:
                ProcessCheckCard();
                break;
            case TASK_BOOTUPCHECK:
                ProcessBootupCheck();
                break;
            case TASK_SAVECHECK:
            case TASK_SAVE:
                ProcessSave();
                break;
            case TASK_DELETE:
                ProcessDelete();
                break;
            case TASK_FINDENTRIES:
                ProcessFindEntries();
                break;
            case TASK_SETAUTOSAVE:
                ProcessSetAutosave();
                break;
            default:
                break;
        }
        if (mMonitorState == MONITOR_ON && mTask == TASK_CHECKCARD) {
            return TASK_MONITOR;
        }
        return mTask;
    }

  private:
    // ---- the TaskManager ---------------------------------------------------
    void Submit(const Request &r) {
        if (mOp == TASK_NONE || mOp == TASK_MONITOR) {
            // Natively the monitor only reports once (a PC hard disk never
            // changes), so requests made under it run right away.
            if (mOp == TASK_MONITOR) {
                ClearTask();
                mMonitorSuspended = true;
            }
            Start(r);
        } else {
            mQueue.push_back(r);
        }
    }

    // 0x7F6455 (_StartTask)
    void Start(const Request &r) {
        mOp = r.mOp;
        mResult = RESULT_UNKNOWN;
        mStatus = STATUS_UNKNOWN;
        switch (r.mOp) {
            case TASK_BOOTUPCHECK:
                mTask = TASK_BOOTUPCHECK;
                break;
            case TASK_CHECKCARD:
                StartCheckCard();
                break;
            case TASK_SAVE:
                if (!r.mHasTitleInfo || r.mTitleInfo.mDataFormat == FORMAT_LAYER2) {
                    StartSave(r);
                } else {
                    Complete(RESULT_FAILED, STATUS_FAILED);
                }
                break;
            case TASK_LOAD:
                StartLoad(r);
                break;
            case TASK_DELETE:
                StartDelete(r);
                break;
            case TASK_FINDENTRIES:
                StartFindEntries(r);
                break;
            case TASK_SETAUTOSAVE:
                // 0x7F482B
                if (r.mState == AUTOSAVE_ENABLE) {
                    mAutosave = true;
                    mTask = TASK_SETAUTOSAVE;
                } else {
                    mAutosave = false;
                    mCompletedAutosaveState = AUTOSAVE_DISABLE;
                    Complete(RESULT_SUCCESS, STATUS_OK);
                }
                break;
            default:
                mOp = TASK_NONE;
                break;
        }
    }

    void StartQueued() {
        if (mOp != TASK_NONE) {
            return;
        }
        if (mMonitorSuspended) {
            mMonitorSuspended = false;
            if (mMonitorState != MONITOR_OFF) {
                mOp = TASK_MONITOR;
                mMonitorReported = true;
                StartCheckCard();
                return;
            }
        }
        if (!mQueue.empty()) {
            Request r = mQueue.front();
            mQueue.pop_front();
            Start(r);
        }
    }

    // 0x7F66A8 (CompleteTask): report the finished task to the game.
    void Complete(TaskResult result, CardStatus status) {
        const MemcardTask op = mOp;
        mTask = TASK_NONE;
        mState = 0;
        mOp = TASK_NONE;
        switch (op) {
            case TASK_BOOTUPCHECK: {
                BootupCheckResults res;
                res.Clear();
                res.mFirstGoodCard = PORT5_SLOT1;
                mGame->BootupCheckDone(status, res);
                break;
            }
            case TASK_CHECKCARD:
                if (result == RESULT_RETRY) {
                    mGame->Retry(status);
                } else {
                    mGame->CardChecked(&mLastCardInfo);
                }
                break;
            case TASK_MONITOR:
                if (result != RESULT_RETRY) {
                    mOp = TASK_MONITOR;
                    if (!mMonitorReported) {
                        mMonitorReported = true;
                        mGame->SetMonitorDone(status != STATUS_CARD_CHANGED ? status : STATUS_OK, mMonitorState);
                    }
                    // Natively the monitor stops polling here; the PC re-runs the
                    // card check every Update and reports CardChanged on a change.
                }
                break;
            case TASK_SAVECHECK:
                mGame->SaveCheckDone(result, status);
                break;
            case TASK_SAVE:
                if (result == RESULT_SUCCESS) {
                    mGame->SaveDone(mEntryName);
                } else if (result == RESULT_RETRY) {
                    mGame->Retry(status);
                } else {
                    mGame->Failed(result, status);
                }
                break;
            case TASK_LOAD:
                if (result == RESULT_SUCCESS) {
                    mGame->LoadDone(mEntryName);
                } else if (result == RESULT_RETRY) {
                    mGame->Retry(status);
                } else {
                    mGame->Failed(result, status);
                }
                break;
            case TASK_DELETE:
                if (result == RESULT_SUCCESS) {
                    mGame->DeleteDone(mEntryName);
                } else if (result == RESULT_RETRY) {
                    mGame->Retry(status);
                } else {
                    mGame->Failed(result, status);
                }
                break;
            case TASK_FINDENTRIES:
                if (result == RESULT_SUCCESS) {
                    mGame->FindEntriesDone(status);
                } else if (result == RESULT_RETRY) {
                    mGame->Retry(status);
                } else {
                    mGame->Failed(result, status);
                }
                break;
            case TASK_SETAUTOSAVE:
                if (result == RESULT_RETRY) {
                    mGame->Retry(status);
                } else {
                    mGame->SetAutosaveDone(result, status, mCompletedAutosaveState);
                }
                break;
            default:
                break;
        }
        StartQueued();
    }

    // 0x7F5CF1
    void ClearTask() {
        mTask = TASK_NONE;
        mState = 0;
    }

    // ---- messages ----------------------------------------------------------
    // 0x7F5E04: the library string strID, with %1.. replaced by the arguments
    // (all strings here); '"' is dropped.
    const uint16_t *FormatString(int strID, const uint16_t *arg) {
        if (mSystem.mGetStrCallback == nullptr) {
            return nullptr;
        }
        const uint16_t *src = reinterpret_cast<const uint16_t *>(mSystem.mGetStrCallback(strID));
        if (src == nullptr) {
            return nullptr;
        }
        uint16_t *dst = mStringBuffers[mNextStringBuffer];
        mNextStringBuffer = (mNextStringBuffer + 1) % kNumStringBuffers;
        int n = 0;
        bool percent = false;
        for (; *src != 0 && n < kStringBufferSize - 1; src++) {
            if (percent) {
                percent = false;
                if (*src == '1' && arg != nullptr) {
                    for (const uint16_t *a = arg; *a != 0 && n < kStringBufferSize - 1; a++) {
                        dst[n++] = *a;
                    }
                }
            } else if (*src == '%') {
                percent = true;
            } else if (*src != '"') {
                dst[n++] = *src;
            }
        }
        dst[n] = 0;
        return dst;
    }

    // 0x7F5A37 / 0x7F5A71 (+0x7F41B3 for the options, one id per byte).
    LibMessage MakeMessage(int id, const uint16_t *arg, uint32_t options) {
        LibMessage m;
        m.mId = id;
        m.mText = FormatString(id, arg);
        for (; options != 0 && m.mNumOptions < MAX_MESSAGE_OPTIONS; options >>= 8) {
            m.mOptionIds[m.mNumOptions] = options & 0xFF;
            m.mOptions[m.mNumOptions] = FormatString(options & 0xFF, nullptr);
            m.mNumOptions++;
        }
        return m;
    }

    // 0x7F4B97: show the message unless the game hid it, in which case it is
    // answered right away (the prompts with "yes", the rest with "OK").
    void PostMessage(const LibMessage &m) {
        uint32_t hidden = mHideMask;
        if (mAutosave) {
            hidden |= ID_SAVE_READY | ID_SAVE_COMPLETE;
        }
        if (mOp == TASK_BOOTUPCHECK) {
            hidden |= ID_LOAD_COMPLETE;
        }
        if (mForceShowMask != 0) {
            hidden &= ~mForceShowMask;
        }
        uint32_t bit = 0;
        MessageChoices autoChoice = CHOICE_NONE;
        switch (m.mId) {
            case 7: bit = ID_SAVE_READY; autoChoice = CHOICE_OPTION2; break;
            case 8: bit = ID_SAVE_COMPLETE; autoChoice = CHOICE_OPTION1; break;
            case 9: bit = ID_SAVE_FAILED; autoChoice = CHOICE_OPTION1; break;
            case 10: bit = ID_LOAD_COMPLETE; autoChoice = CHOICE_OPTION1; break;
            case 11: bit = ID_LOAD_FAILED; autoChoice = CHOICE_OPTION1; break;
            case 12: bit = ID_DELETE_READY; autoChoice = CHOICE_OPTION2; break;
            case 13: bit = ID_DELETE_COMPLETE; autoChoice = CHOICE_OPTION1; break;
            case 14: bit = ID_DELETE_FAILED; autoChoice = CHOICE_OPTION1; break;
            default: break;
        }
        if (bit != 0 && (hidden & bit) != 0) {
            mChoice = autoChoice;
            return;
        }
        // 0x7F5DA9
        mChoice = CHOICE_NONE;
        ClearMessage();
        const wchar_t *options[MAX_MESSAGE_OPTIONS];
        for (uint32_t i = 0; i < m.mNumOptions; i++) {
            options[i] = reinterpret_cast<const wchar_t *>(m.mOptions[i]);
        }
        static const uint16_t kEmpty[1] = {0};
        mGame->ShowMessage(reinterpret_cast<const wchar_t *>(m.mText != nullptr ? m.mText : kEmpty), m.mNumOptions, options);
        mMessageShown = true;
    }

    // 0x7F5C07
    void ClearMessage() {
        if (mMessageShown) {
            mGame->ClearMessage();
            mMessageShown = false;
        }
    }

    std::string EntryPath(const std::string &a) const {
        return mRootPath + "/" + a;
    }
    std::string EntryPath(const std::string &a, const std::string &b) const {
        return mRootPath + "/" + a + "/" + b;
    }

    // ---- check card --------------------------------------------------------
    // 0x7F4821
    void StartCheckCard() {
        mTask = TASK_CHECKCARD;
    }
    // 0x7F48A4: the "card" is the disk the profiles are on.
    void ProcessCheckCard() {
        CardInfo info;
        struct statfs fs;
        if (statfs(mRootPath.empty() ? "." : mRootPath.c_str(), &fs) == 0) {
            info.mStatus = STATUS_OK;
            info.mFreeSpace = static_cast<uint32_t>(fs.f_bavail > 0xFFFFFFFFull ? 0xFFFFFFFFull : fs.f_bavail);
            info.mTotalSpace = static_cast<uint32_t>(fs.f_blocks > 0xFFFFFFFFull ? 0xFFFFFFFFull : fs.f_blocks);
        } else {
            info.mStatus = STATUS_ACCESS_DENIED;
        }
        mLastCardInfo = info;
        mResult = RESULT_SUCCESS;
        mStatus = STATUS_OK;
        mTask = TASK_NONE;
        ClearMessage();
        Complete(RESULT_SUCCESS, info.mStatus);
    }

    // ---- bootup check ------------------------------------------------------
    // 0x7F43FE / 0x7F4869: the PC library checks nothing; the game looks for
    // profiles itself (MemoryCardImp::BootupCheckDone).
    void ProcessBootupCheck() {
        mResult = RESULT_SUCCESS;
        mStatus = STATUS_OK;
        mTask = TASK_NONE;
        ClearMessage();
        Complete(RESULT_SUCCESS, STATUS_OK);
    }

    // ---- set autosave ------------------------------------------------------
    // 0x7F4B65
    void ProcessSetAutosave() {
        mAutosave = true;
        mResult = RESULT_SUCCESS;
        mStatus = STATUS_OK;
        mTask = TASK_NONE;
        mCompletedAutosaveState = AUTOSAVE_ENABLE;
        Complete(RESULT_SUCCESS, STATUS_OK);
    }

    // ---- save --------------------------------------------------------------
    // 0x7F4E57
    void StartSave(const Request &r) {
        std::snprintf(mEntryName, sizeof(mEntryName), "%s", r.mName.c_str());
        mContent = r.mSaveInfo.mContentName != nullptr ? reinterpret_cast<const uint16_t *>(r.mSaveInfo.mContentName) : nullptr;
        mHeaderData = r.mHeader;
        mBodyData = r.mBody;
        const uint32_t headerSize = r.mSaveInfo.mHeaderSize;
        const uint32_t bodySize = r.mSaveInfo.mBodySize;
        mFileHeader.mMagic = kFileMagic;
        mFileHeader.mTotalSize = headerSize + kFileHeaderSize + bodySize;
        mFileHeader.mHeaderSize = headerSize;
        mFileHeader.mBodySize = bodySize;
        mFileHeader.mHeaderSig = CalcSignature(mHeaderData, static_cast<int>(headerSize));
        mFileHeader.mBodySig = CalcSignature(mBodyData, static_cast<int>(bodySize));
        mFileHeader.mSig = 0;
        mFileHeader.mSig = CalcSignature(&mFileHeader, 0x18);
        mStatus = STATUS_UNKNOWN;
        mResult = RESULT_UNKNOWN;
        mDoWrite = true;
        mTask = TASK_SAVE;
        mState = 9;
    }

    // 0x7F4F11
    void ProcessSave() {
        switch (mState) {
            case 9: {
                // An existing entry asks before it is overwritten.
                FindIterator it;
                bool exists = false;
                const std::string path = EntryPath(mEntryName);
                while (it.Next(path.c_str())) {
                    if (it.mIsDir) {
                        exists = true;
                        break;
                    }
                }
                if (exists) {
                    PostMessage(MakeMessage(7, nullptr, 0x304));
                    mState = 10;
                    return;
                }
                mState = 11;
                return;
            }
            case 10:
                if (mChoice == CHOICE_OPTION1) {
                    mStatus = STATUS_OK;
                    mResult = RESULT_CANCELLED;
                    mState = 14;
                } else if (mChoice == CHOICE_OPTION2) {
                    mState = 11;
                }
                return;
            case 11:
                if (!mDoWrite) {
                    mResult = RESULT_SUCCESS;
                    mStatus = STATUS_OK;
                    mState = 14;
                    return;
                }
                WriteEntry();
                mState = 12;
                return;
            case 12:
                mChoice = CHOICE_NONE;
                if (mResult == RESULT_SUCCESS) {
                    PostMessage(MakeMessage(8, nullptr, 5));
                } else if (mResult == RESULT_FAILED) {
                    PostMessage(MakeMessage(9, mContent, 5));
                }
                mState = 13;
                return;
            case 13:
                if (mChoice == CHOICE_OPTION1) {
                    mState = 14;
                }
                return;
            case 14:
                mTask = TASK_NONE;
                mState = 0;
                mContent = nullptr;
                ClearMessage();
                Complete(mResult, mStatus);
                return;
        }
    }

    void WriteEntry() {
        const std::string dir = EntryPath(mEntryName);
        mkdir(dir.c_str(), 0755);
        const std::string path = EntryPath(mEntryName, mEntryName);
        mResettable = false;
        const int fd = open(path.c_str(), O_CREAT | O_TRUNC | O_WRONLY, 0600);
        if (fd == -1) {
            mResult = RESULT_FAILED;
            mStatus = STATUS_ACCESS_DENIED;
        } else {
            bool ok = write(fd, &mFileHeader, kFileHeaderSize) == kFileHeaderSize;
            if (ok && mFileHeader.mHeaderSize != 0 && mHeaderData != nullptr) {
                ok = write(fd, mHeaderData, mFileHeader.mHeaderSize) == static_cast<ssize_t>(mFileHeader.mHeaderSize);
            }
            if (ok && mFileHeader.mBodySize != 0 && mBodyData != nullptr) {
                ok = write(fd, mBodyData, mFileHeader.mBodySize) == static_cast<ssize_t>(mFileHeader.mBodySize);
            }
            close(fd);
            // The PC reports success here even when a write came up short;
            // natively a failed write is reported.
            mResult = ok ? RESULT_SUCCESS : RESULT_FAILED;
            mStatus = ok ? STATUS_OK : STATUS_ACCESS_DENIED;
        }
        mResettable = true;
    }

    // ---- load --------------------------------------------------------------
    // 0x7F445F / 0x7F451D
    void StartLoad(const Request &r) {
        mRawFormat = r.mHasTitleInfo && r.mTitleInfo.mDataFormat == FORMAT_RAW;
        mNameType = r.mHasTitleInfo ? r.mTitleInfo.mNameType : NAME_ENTRY;
        if (mNameType == NAME_ENTRY) {
            std::snprintf(mDirName, sizeof(mDirName), "%s", r.mName.c_str());
            std::snprintf(mEntryName, sizeof(mEntryName), "%s", r.mName.c_str());
        } else {
            // 0x7F437C: "dir/file".
            const std::size_t slash = r.mName.find_last_of("/\\");
            std::snprintf(mDirName, sizeof(mDirName), "%s", r.mName.substr(0, slash).c_str());
            std::snprintf(mEntryName, sizeof(mEntryName), "%s", slash == std::string::npos ? "" : r.mName.substr(slash + 1).c_str());
        }
        mContent = r.mContent;
        mHeaderData = r.mHeader;
        mBodyData = r.mBody;
        std::memset(&mFileHeader, 0, sizeof(mFileHeader));
        mStatus = STATUS_UNKNOWN;
        mResult = RESULT_UNKNOWN;
        mTask = TASK_LOAD;
        mState = 15;
    }

    // 0x7F5255
    void ProcessLoad() {
        switch (mState) {
            case 15:
                ReadEntry();
                mState = 16;
                return;
            case 16:
                mChoice = CHOICE_NONE;
                if (mResult == RESULT_SUCCESS) {
                    PostMessage(MakeMessage(10, nullptr, 5));
                } else if (mResult == RESULT_FAILED) {
                    PostMessage(MakeMessage(11, mContent, 5));
                }
                mState = 17;
                return;
            case 17:
                if (mChoice == CHOICE_OPTION1) {
                    mState = 18;
                }
                return;
            case 18:
                mTask = TASK_NONE;
                mState = 0;
                mContent = nullptr;
                Complete(mResult, mStatus);
                return;
        }
    }

    bool HeaderValid(long long fileLength) {
        return mFileHeader.mTotalSize == fileLength && mFileHeader.mMagic == kFileMagic && mFileHeader.mSig == CalcSignature(&mFileHeader, 0x18);
    }

    void ReadEntry() {
        mResult = RESULT_UNKNOWN;
        const std::string path = EntryPath(mDirName, mEntryName);
        const int fd = open(path.c_str(), O_RDONLY);
        if (fd == -1) {
            mStatus = STATUS_FILE_NOT_FOUND;
            mResult = RESULT_FAILED;
            return;
        }
        struct stat st;
        const long long length = fstat(fd, &st) == 0 ? static_cast<long long>(st.st_size) : -1;
        if (length == -1) {
            mResult = RESULT_FAILED;
            mStatus = STATUS_ACCESS_DENIED;
        }
        TaskStatus ready = TASK_CONTINUE;
        if (!mRawFormat) {
            if (mResult != RESULT_FAILED) {
                if (read(fd, &mFileHeader, kFileHeaderSize) != kFileHeaderSize) {
                    mStatus = STATUS_ACCESS_DENIED;
                    mResult = RESULT_FAILED;
                } else if (!HeaderValid(length)) {
                    mStatus = STATUS_INSUFFICIENT_SPACE;
                    mResult = RESULT_FAILED;
                }
            }
            if (mHeaderData == nullptr && mBodyData == nullptr) {
                ready = mGame->LoadReady(mEntryName, mFileHeader.mHeaderSize, mFileHeader.mBodySize, mHeaderData, mBodyData);
            }
            if (ready == TASK_CANCEL) {
                mResult = RESULT_FAILED;
                mStatus = STATUS_INSUFFICIENT_SPACE;
            }
            if (mResult != RESULT_FAILED) {
                if (mFileHeader.mHeaderSize != 0 && mHeaderData != nullptr &&
                    read(fd, mHeaderData, mFileHeader.mHeaderSize) != static_cast<ssize_t>(mFileHeader.mHeaderSize)) {
                    mResult = RESULT_FAILED;
                    mStatus = STATUS_ACCESS_DENIED;
                }
                if (mResult != RESULT_FAILED && mFileHeader.mBodySize != 0 && mBodyData != nullptr) {
                    if (mHeaderData == nullptr &&
                        lseek(fd, mFileHeader.mHeaderSize, SEEK_CUR) != static_cast<off_t>(mFileHeader.mHeaderSize + kFileHeaderSize)) {
                        mResult = RESULT_FAILED;
                        mStatus = STATUS_ACCESS_DENIED;
                    }
                    if (mResult != RESULT_FAILED &&
                        read(fd, mBodyData, mFileHeader.mBodySize) != static_cast<ssize_t>(mFileHeader.mBodySize)) {
                        mResult = RESULT_FAILED;
                        mStatus = STATUS_ACCESS_DENIED;
                    }
                }
            }
            close(fd);
        } else {
            if (mBodyData == nullptr) {
                ready = mGame->LoadReady(mEntryName, 0, static_cast<uint32_t>(length), mHeaderData, mBodyData);
            }
            if (ready == TASK_CANCEL) {
                mResult = RESULT_FAILED;
                mStatus = STATUS_INSUFFICIENT_SPACE;
            }
            if (mResult != RESULT_FAILED && read(fd, mBodyData, static_cast<size_t>(length)) != length) {
                mStatus = STATUS_ACCESS_DENIED;
                mResult = RESULT_FAILED;
            }
            close(fd);
        }
        if (mResult == RESULT_FAILED) {
            return;
        }
        if (!mRawFormat) {
            if (HeaderValid(length)) {
                // The PC checks the header and body signatures here but then
                // reports success either way (the profile has its own hash).
                mResult = RESULT_SUCCESS;
                mStatus = STATUS_OK;
                return;
            }
        } else if (mGame->CheckLoadedData(mBodyData) == DATA_OK) {
            mResult = RESULT_SUCCESS;
            mStatus = STATUS_OK;
            return;
        }
        mResult = RESULT_FAILED;
        mStatus = STATUS_INSUFFICIENT_SPACE;
    }

    // ---- delete ------------------------------------------------------------
    // 0x7F45FE
    void StartDelete(const Request &r) {
        mFoundAny = false;
        std::snprintf(mEntryName, sizeof(mEntryName), "%s", r.mName.c_str());
        mStatus = STATUS_UNKNOWN;
        mResult = RESULT_UNKNOWN;
        mTask = TASK_DELETE;
        mState = 19;
        mContent = r.mContent;
    }

    // 0x7F56BC
    void ProcessDelete() {
        switch (mState) {
            case 19:
                PostMessage(MakeMessage(12, mContent, 0x304));
                mState = 20;
                return;
            case 20:
                if (mChoice == CHOICE_OPTION1) {
                    mStatus = STATUS_OK;
                    mResult = RESULT_CANCELLED;
                    mState = 24;
                } else if (mChoice == CHOICE_OPTION2) {
                    mState = 21;
                }
                return;
            case 21: {
                bool deleted = false;
                FindIterator it;
                const std::string pattern = EntryPath(mEntryName, "*");
                if (!it.Next(pattern.c_str())) {
                    mStatus = STATUS_FILE_NOT_FOUND;
                } else {
                    do {
                        if (!it.mIsDir) {
                            const std::string file = EntryPath(mEntryName, it.mName);
                            if (remove(file.c_str()) == 0) {
                                deleted = true;
                            } else {
                                mResult = RESULT_FAILED;
                                mStatus = STATUS_ACCESS_DENIED;
                            }
                        }
                    } while (mResult != RESULT_FAILED && it.Next(pattern.c_str()));
                    if (mResult != RESULT_FAILED) {
                        if (rmdir(EntryPath(mEntryName).c_str()) == 0) {
                            deleted = true;
                        } else {
                            mResult = RESULT_FAILED;
                            mStatus = STATUS_ACCESS_DENIED;
                        }
                    }
                }
                if ((mResult == RESULT_FAILED || !deleted) && (mStatus != STATUS_FILE_NOT_FOUND || !deleted)) {
                    mResult = RESULT_FAILED;
                    mStatus = STATUS_FILE_NOT_FOUND;
                } else {
                    mResult = RESULT_SUCCESS;
                    mStatus = STATUS_OK;
                }
                mState = 22;
                return;
            }
            case 22:
                mChoice = CHOICE_NONE;
                if (mResult == RESULT_SUCCESS) {
                    PostMessage(MakeMessage(13, nullptr, 5));
                } else if (mResult == RESULT_FAILED) {
                    PostMessage(MakeMessage(14, mContent, 5));
                }
                mState = 23;
                return;
            case 23:
                if (mChoice == CHOICE_OPTION1) {
                    mState = 24;
                }
                return;
            case 24:
                mTask = TASK_NONE;
                mState = 0;
                mContent = nullptr;
                ClearMessage();
                Complete(mResult, mStatus);
                return;
        }
    }

    // ---- find entries ------------------------------------------------------
    // 0x7F46E4 / 0x7F4768
    void StartFindEntries(const Request &r) {
        mNameType = r.mHasTitleInfo ? r.mTitleInfo.mNameType : NAME_ENTRY;
        if (mNameType == NAME_ENTRY) {
            std::snprintf(mDirName, sizeof(mDirName), "%s", r.mName.c_str());
            std::snprintf(mEntryName, sizeof(mEntryName), "%s", r.mName.c_str());
        } else {
            const std::size_t slash = r.mName.find_last_of("/\\");
            std::snprintf(mDirName, sizeof(mDirName), "%s", r.mName.substr(0, slash).c_str());
            std::snprintf(mEntryName, sizeof(mEntryName), "%s", r.mName.c_str());
        }
        mGame->ClearEntries();
        mStatus = STATUS_UNKNOWN;
        mFoundAny = false;
        mFoundFile = false;
        mResult = RESULT_UNKNOWN;
        mTask = TASK_FINDENTRIES;
    }

    // 0x7F492D: every subdirectory matching the pattern is an entry; its info
    // is that of the file of the same name inside it (or a cleared one, which
    // the game shows as corrupt, when there is none).
    void ProcessFindEntries() {
        FindIterator dirs;
        const std::string pattern = EntryPath(mDirName);
        while (dirs.Next(pattern.c_str())) {
            if (!dirs.mIsDir || dirs.mName == "." || dirs.mName == "..") {
                continue;
            }
            mFoundFile = false;
            mFoundAny = true;
            std::snprintf(mFoundDir, sizeof(mFoundDir), "%s", dirs.mName.c_str());
            if (mNameType == NAME_ENTRY) {
                std::snprintf(mEntryName, sizeof(mEntryName), "%s", mFoundDir);
            }
            EntryInfo info;
            FindIterator file;
            const std::string path = EntryPath(mFoundDir, mEntryName);
            if (file.Next(path.c_str()) && !file.mIsDir) {
                info = file.mInfo;
                std::snprintf(mFoundFileName, sizeof(mFoundFileName), "%s", file.mName.c_str());
                mFoundFile = true;
            }
            if (mNameType == NAME_ENTRY) {
                info.mName = mFoundDir;
            } else {
                if (!mFoundFile) {
                    continue;
                }
                std::snprintf(mFoundPath, sizeof(mFoundPath), "%s/%s", mFoundDir, mFoundFileName);
                info.mName = mFoundPath;
            }
            mGame->FoundEntry(&info);
        }
        mResult = RESULT_SUCCESS;
        const bool found = mNameType == NAME_ENTRY ? mFoundAny : mFoundFile;
        mTask = TASK_NONE;
        mStatus = found ? STATUS_OK : STATUS_FILE_NOT_FOUND;
        Complete(RESULT_SUCCESS, mStatus);
    }

    static const int kNumStringBuffers = 5;
    static const int kStringBufferSize = 0x400;

    Realmc::SystemInterface mSystem;
    IGameInterface *mGame;
    std::string mRootPath;
    std::deque<Request> mQueue;
    MemcardTask mOp = TASK_NONE;   // the TaskManager's current operation
    MemcardTask mTask = TASK_NONE; // the implementation's current task (+0x6C)
    int mState = 0;                // its step (+0x70)
    MessageChoices mChoice = CHOICE_NONE;
    bool mMessageShown = false;
    uint32_t mHideMask;
    uint32_t mForceShowMask;
    bool mResettable = true;
    bool mAutosave = false;
    AutosaveState mCompletedAutosaveState = AUTOSAVE_DISABLE;
    MonitorState mMonitorState = MONITOR_OFF;
    bool mMonitorReported = false;
    bool mMonitorSuspended = false;
    CardInfo mLastCardInfo;
    TaskResult mResult = RESULT_UNKNOWN;
    CardStatus mStatus = STATUS_UNKNOWN;
    NameType mNameType = NAME_ENTRY;
    bool mRawFormat = false;
    bool mDoWrite = false;
    bool mFoundAny = false;
    bool mFoundFile = false;
    char mEntryName[MAX_FILENAME_LENGTH] = {};
    char mDirName[MAX_FILENAME_LENGTH] = {};
    char mFoundDir[256] = {};
    char mFoundFileName[256] = {};
    char mFoundPath[512] = {};
    const uint16_t *mContent = nullptr;
    FileHeader mFileHeader = {};
    char *mHeaderData = nullptr;
    char *mBodyData = nullptr;
    uint16_t mStringBuffers[kNumStringBuffers][kStringBufferSize] = {};
    int mNextStringBuffer = 0;
};

// ---------------------------------------------------------------------------
// MemcardInterface (thin forwarders, as on PC).
// ---------------------------------------------------------------------------
static MemcardInterface *gInstance = nullptr;

MemcardInterface *MemcardInterface::CreateInstance(Realmc::SystemInterface *iSystem, IGameInterface *iGame, GameInfo *gameInfo) {
    if (gInstance == nullptr) {
        gInstance = new MemcardInterface(iSystem, iGame, gameInfo);
    }
    return gInstance;
}

MemcardInterface::MemcardInterface(Realmc::SystemInterface *iSystem, IGameInterface *iGame, GameInfo *gameInfo)
    : mImpl(new MemcardInterfaceImpl(iSystem, iGame, gameInfo)) {}

MemcardInterface::~MemcardInterface() {
    delete mImpl;
}

void MemcardInterface::Release() {
    if (gInstance == this) {
        gInstance = nullptr;
    }
    delete this;
}

const char *MemcardInterface::GetFilterForAllEntries() {
    return "*";
}

void MemcardInterface::BootupCheck(const BootupCheckParams *params, uint32_t nEntries, const char **entryNames, wchar_t *content) {
    mImpl->BootupCheck();
}

void MemcardInterface::BootupCheck(const BootupCheckParams *params, uint32_t nEntries, const AutoloadEntry *autoloadEntries) {
    mImpl->BootupCheck();
}

void MemcardInterface::SaveCheck(const char *entryName, const SaveInfo *saveInfo, const TitleInfo *titleInfo) {}

void MemcardInterface::Save(const char *entryName, const char *header, const char *body, const SaveInfo *saveInfo, const TitleInfo *titleInfo) {
    mImpl->Save(entryName, header, body, saveInfo, titleInfo);
}

void MemcardInterface::Save(const char *entryName, const char *header, const char *body, const SaveInfo *saveInfo) {
    mImpl->Save(entryName, header, body, saveInfo, nullptr);
}

void MemcardInterface::Load(const char *entryName, char *header, char *body, const wchar_t *contentName, const TitleInfo *titleInfo,
                            const wchar_t *typeName) {
    mImpl->Load(entryName, header, body, reinterpret_cast<const uint16_t *>(contentName), titleInfo);
}

void MemcardInterface::Load(const char *entryName, char *header, char *body, const wchar_t *contentName, const TitleInfo *titleInfo) {
    mImpl->Load(entryName, header, body, reinterpret_cast<const uint16_t *>(contentName), titleInfo);
}

void MemcardInterface::Delete(const char *entryName, const unsigned short *contentName) {
    mImpl->Delete(entryName, contentName);
}

void MemcardInterface::DeleteMultiple(uint32_t nEntryNames, const char **entryNames, const wchar_t *contentName) {
    for (uint32_t i = 0; i < nEntryNames; i++) {
        mImpl->Delete(entryNames[i], reinterpret_cast<const uint16_t *>(contentName));
    }
}

void MemcardInterface::FindEntries(const char *entryNamePattern, const TitleInfo *titleInfo) {
    // 0x7F3C41: only an alternate-title or raw-format TitleInfo is passed on.
    if (titleInfo != nullptr && titleInfo->mDataFormat != FORMAT_RAW && titleInfo->mTitleType != TITLE_ALTERNATE) {
        titleInfo = nullptr;
    }
    mImpl->FindEntries(entryNamePattern, titleInfo);
}

void MemcardInterface::MessageDone(MessageChoices choice) {
    mImpl->MessageDone(choice);
}

void MemcardInterface::CheckCard(CardId cardId) {
    mImpl->CheckCard(cardId);
}

void MemcardInterface::SetActiveCard(CardId cardId) {}

void MemcardInterface::SetAutosave(AutosaveState state, uint32_t nSaveReqs, SaveReq **saveReqs, const char *entryName, CardId cardId) {
    mImpl->SetAutosave(state);
}

void MemcardInterface::SetMonitor(MonitorState state) {
    mImpl->SetMonitor(state);
}

void MemcardInterface::SetMessage(MessageState state, uint32_t message) {
    mImpl->SetMessage(state, message);
}

MemcardTask MemcardInterface::Update(uint32_t elapsedTime) {
    return mImpl->Update(elapsedTime);
}

uint32_t MemcardInterface::CalcSaveSize(const SaveInfo *saveInfo) {
    return mImpl->CalcSaveSize(saveInfo);
}

void MemcardInterface::SetMaxCardNameLength(uint32_t maxLength) {}

bool MemcardInterface::IsResettable() {
    return mImpl->IsResettable();
}

void MemcardInterface::SetRootPath(const char *path) {
    mImpl->SetRootPath(path);
}

} // namespace RealmcIface
