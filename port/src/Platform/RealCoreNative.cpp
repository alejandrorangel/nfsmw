// Native implementation of the parts of EA's RealCore the game links against:
// RealSystem::Mutex, THREAD_*, and the RealFile/FILESYS_* file system.
//
// The original PC build implemented these over Win32 (CRITICAL_SECTION,
// CreateThread, overlapped file I/O). Here they sit on pthreads and POSIX I/O,
// with asynchronous FILESYS ops serviced by a worker thread so callbacks keep
// the original off-thread semantics.

#include "Speed/Indep/Libs/realcore/6.24.00/include/common/realcore/file/driver.h"
// The game includes filesys_cc.h (C++ linkage), not filesys.h (extern "C").
#include "Speed/Indep/Libs/realcore/6.24.00/include/common/realcore/file/filesys_cc.h"
#include "Speed/Indep/Libs/realcore/6.24.00/include/common/realcore/system.h"

#include <cerrno>
#include <condition_variable>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <deque>
#include <dirent.h>
#include <fcntl.h>
#include <mutex>
#include <pthread.h>
#include <string>
#include <sys/stat.h>
#include <thread>
#include <unistd.h>
#include <unordered_map>

static bool PortTraceFiles() {
    static const bool on = getenv("NFSMW_TRACE_FILES") != nullptr;
    return on;
}

// ---------------------------------------------------------------------------
// RealSystem::Mutex - 28 bytes of opaque storage; we keep a pointer to a heap
// recursive mutex in it, created lazily because the game locks zero-initialised
// global mutexes without calling Create() first.
// ---------------------------------------------------------------------------
static_assert(sizeof(RealSystem::Mutex) >= sizeof(void *), "Mutex storage too small");

static std::mutex gMutexCreateLock;

static std::recursive_mutex *MutexImpl(RealSystem::Mutex *m, bool create) {
    std::recursive_mutex *impl;
    std::memcpy(&impl, m->mBuf, sizeof(impl));
    if (impl == nullptr && create) {
        std::lock_guard<std::mutex> guard(gMutexCreateLock);
        std::memcpy(&impl, m->mBuf, sizeof(impl));
        if (impl == nullptr) {
            impl = new std::recursive_mutex();
            std::memcpy(m->mBuf, &impl, sizeof(impl));
        }
    }
    return impl;
}

RealSystem::Mutex::Mutex() {
    std::memset(mBuf, 0, sizeof(mBuf));
}

void RealSystem::Mutex::Create() {
    MutexImpl(this, true);
}

void RealSystem::Mutex::Destroy() {
    std::recursive_mutex *impl = MutexImpl(this, false);
    delete impl;
    std::memset(mBuf, 0, sizeof(mBuf));
}

void RealSystem::Mutex::Lock() {
    MutexImpl(this, true)->lock();
}

void RealSystem::Mutex::Unlock() {
    MutexImpl(this, true)->unlock();
}

// ---------------------------------------------------------------------------
// THREAD_*
// ---------------------------------------------------------------------------
struct NativeThread {
    pthread_t handle;
    bool started;
};
static_assert(sizeof(THREAD) >= sizeof(NativeThread), "THREAD storage too small");

static NativeThread *Thread(THREAD *t) {
    return reinterpret_cast<NativeThread *>(t);
}

struct ThreadStart {
    int (*func)(void *);
    void *param;
};

static void *ThreadTrampoline(void *arg) {
    ThreadStart start = *static_cast<ThreadStart *>(arg);
    delete static_cast<ThreadStart *>(arg);
    start.func(start.param);
    return nullptr;
}

void THREAD_create(THREAD *thread, int (*func)(void *), void *param, void *stack, int stackSize, int priority) {
    pthread_attr_t attr;
    pthread_attr_init(&attr);
    // The supplied stack was sized for 32-bit code; give native threads room.
    size_t size = stackSize > 0 ? static_cast<size_t>(stackSize) * 4 : 0;
    if (size < 1024 * 1024) {
        size = 1024 * 1024;
    }
    pthread_attr_setstacksize(&attr, size);
    NativeThread *t = Thread(thread);
    t->started = pthread_create(&t->handle, &attr, ThreadTrampoline, new ThreadStart{func, param}) == 0;
    pthread_attr_destroy(&attr);
}

void THREAD_waitexit(THREAD *thread, int status) {
    NativeThread *t = Thread(thread);
    if (t->started) {
        pthread_join(t->handle, nullptr);
        t->started = false;
    }
}

void THREAD_destroy(THREAD *thread) {
    NativeThread *t = Thread(thread);
    if (t->started) {
        pthread_detach(t->handle);
        t->started = false;
    }
}

bool THREAD_iscurrent(THREAD *thread) {
    NativeThread *t = Thread(thread);
    return t->started && pthread_equal(t->handle, pthread_self());
}

void THREAD_setpriority(THREAD *thread, int priority) {}

void THREAD_yield(int dur) {
    if (dur <= 0) {
        sched_yield();
    } else {
        usleep(static_cast<useconds_t>(dur) * 1000);
    }
}

void THREAD_sleep(int ticks) {
    usleep(static_cast<useconds_t>(ticks > 0 ? ticks : 1) * 1000);
}

// ---------------------------------------------------------------------------
// Path resolution. Game paths look like "GLOBAL\\GLOBALB.BUN", sometimes with
// a device prefix ("discu:", "cdrom0:"). They're resolved relative to the
// working directory (the game install), case-insensitively.
// ---------------------------------------------------------------------------
static std::mutex gPathLock;
static std::unordered_map<std::string, std::string> gPathCache;

static std::string NormalisePath(const char *name) {
    std::string p(name ? name : "");
    size_t colon = p.find(':');
    if (colon != std::string::npos && colon > 1) { // device prefix, not a drive letter
        p.erase(0, colon + 1);
    }
    for (char &c : p) {
        if (c == '\\') {
            c = '/';
        }
    }
    while (!p.empty() && p[0] == '/') {
        p.erase(0, 1);
    }
    return p;
}

// Returns the on-disk path, matching each component case-insensitively.
static std::string ResolvePath(const char *name) {
    std::string norm = NormalisePath(name);
    std::lock_guard<std::mutex> guard(gPathLock);
    auto it = gPathCache.find(norm);
    if (it != gPathCache.end()) {
        return it->second;
    }

    std::string resolved;
    size_t start = 0;
    while (start <= norm.size()) {
        size_t slash = norm.find('/', start);
        std::string part = norm.substr(start, slash == std::string::npos ? std::string::npos : slash - start);
        start = slash == std::string::npos ? norm.size() + 1 : slash + 1;
        if (part.empty() || part == ".") {
            continue;
        }
        std::string dir = resolved.empty() ? "." : resolved;
        std::string match = part;
        struct stat st;
        if (stat((dir + "/" + part).c_str(), &st) != 0) {
            if (DIR *d = opendir(dir.c_str())) {
                while (dirent *e = readdir(d)) {
                    if (strcasecmp(e->d_name, part.c_str()) == 0) {
                        match = e->d_name;
                        break;
                    }
                }
                closedir(d);
            }
        }
        resolved = resolved.empty() ? match : resolved + "/" + match;
    }
    gPathCache[norm] = resolved;
    return resolved;
}

// For platform code that reads game files itself (the movie player).
std::string PortResolveDataPath(const char *name) {
    return ResolvePath(name);
}

static int OpenFlagsFor(unsigned int modeflags) {
    // RealFile mode flags as used by bFile (bFile.hpp): 0x1 read, 0x2 write,
    // 0x4 truncate. BOPEN_FLAG_WRITE is 0x6, BOPEN_FLAG_APPEND is 0x2. bFile
    // always writes at explicit offsets, so append doesn't use O_APPEND.
    if (modeflags & 2) {
        return O_RDWR | O_CREAT | ((modeflags & 4) ? O_TRUNC : 0);
    }
    return O_RDONLY;
}

// FILEHANDLE 0 means failure, so handles are fd + 1.
static intptr_t HandleFromFd(int fd) {
    return fd < 0 ? 0 : static_cast<intptr_t>(fd) + 1;
}

static int FdFromHandle(intptr_t h) {
    return static_cast<int>(h) - 1;
}

static intptr_t OpenNow(const char *name, unsigned int modeflags) {
    std::string path = ResolvePath(name);
    int fd = open(path.c_str(), OpenFlagsFor(modeflags), 0644);
    if (PortTraceFiles() || fd < 0) {
        std::fprintf(stderr, "[file] open %s -> %s%s\n", name, path.c_str(), fd < 0 ? " (FAILED)" : "");
    }
    return HandleFromFd(fd);
}

bool PortBenchmarkActive(); // BenchmarkNative.cpp

// ---------------------------------------------------------------------------
// Asynchronous ops. Every op runs on one worker thread, in submission order
// (the original serviced the disc the same way). An op stays alive until
// FILESYS_completeop retires it.
// ---------------------------------------------------------------------------
namespace {

enum OpKind { OP_OPEN, OP_READ, OP_CLOSE, OP_SIZE };

struct Op {
    OpKind kind;
    std::string name;
    unsigned int modeflags = 0;
    intptr_t handle = 0;
    int offset = 0;
    void *buffer = nullptr;
    int bytes = 0;
    void *userdata = nullptr;

    bool done = false;
    int status = 0; // 1 = success
    int64_t result = 0;
    FILESYS_CALLBACK *callback = nullptr;
};

std::mutex gOpLock;
std::condition_variable gOpDone;
std::condition_variable gOpQueued;
std::unordered_map<int, Op *> gOps;
std::deque<int> gQueue;
int gNextOp = 1;
std::once_flag gWorkerOnce;

void RunOp(Op &op) {
    switch (op.kind) {
    case OP_OPEN:
        op.result = OpenNow(op.name.c_str(), op.modeflags);
        op.status = op.result != 0 ? 1 : 0;
        break;
    case OP_READ: {
        ssize_t n = pread(FdFromHandle(op.handle), op.buffer, op.bytes, op.offset);
        op.result = n < 0 ? 0 : n;
        op.status = n >= 0 ? 1 : 0;
        break;
    }
    case OP_CLOSE:
        op.status = close(FdFromHandle(op.handle)) == 0 ? 1 : 0;
        op.result = op.status;
        break;
    case OP_SIZE: {
        struct stat st;
        op.status = fstat(FdFromHandle(op.handle), &st) == 0 ? 1 : 0;
        op.result = op.status ? st.st_size : 0;
        break;
    }
    }
}

void Worker() {
    pthread_setname_np("NFSMW file I/O");
    for (;;) {
        int id;
        Op *op;
        {
            std::unique_lock<std::mutex> lock(gOpLock);
            gOpQueued.wait(lock, [] { return !gQueue.empty(); });
            id = gQueue.front();
            gQueue.pop_front();
            op = gOps[id];
        }
        RunOp(*op);
        FILESYS_CALLBACK *cb;
        {
            std::lock_guard<std::mutex> lock(gOpLock);
            op->done = true;
            cb = op->callback;
        }
        gOpDone.notify_all();
        if (cb) {
            cb(id, op->status, op->userdata);
        }
    }
}

FILEOP Submit(Op *op) {
    if (::PortBenchmarkActive()) {
        // NFSMW_BENCHMARK: every op is done by the time it's asked for, so
        // what's loaded by a given frame doesn't depend on the disk's timing.
        RunOp(*op);
        op->done = true;
        std::lock_guard<std::mutex> lock(gOpLock);
        int id = gNextOp++;
        gOps[id] = op;
        return id;
    }
    std::call_once(gWorkerOnce, [] { std::thread(Worker).detach(); });
    std::lock_guard<std::mutex> lock(gOpLock);
    int id = gNextOp++;
    gOps[id] = op;
    gQueue.push_back(id);
    gOpQueued.notify_one();
    return id;
}

} // namespace

FILEOP FILESYS_open(const char *name, unsigned int modeflags, int priority, void *userdata) {
    Op *op = new Op;
    op->kind = OP_OPEN;
    op->name = name ? name : "";
    op->modeflags = modeflags;
    op->userdata = userdata;
    return Submit(op);
}

FILEOP FILESYS_read(intptr_t filehandle, int offset, void *buffer, int bytes, int priority, void *userdata) {
    Op *op = new Op;
    op->kind = OP_READ;
    op->handle = filehandle;
    op->offset = offset;
    op->buffer = buffer;
    op->bytes = bytes;
    op->userdata = userdata;
    return Submit(op);
}

FILEOP FILESYS_close(intptr_t filehandle, int timeout, void *userdata) {
    Op *op = new Op;
    op->kind = OP_CLOSE;
    op->handle = filehandle;
    op->userdata = userdata;
    return Submit(op);
}

int FILESYS_size(intptr_t filehandle, int priority, void *userdata) {
    Op *op = new Op;
    op->kind = OP_SIZE;
    op->handle = filehandle;
    op->userdata = userdata;
    return Submit(op);
}

int FILESYS_waitop(FILEOP ophandle) {
    std::unique_lock<std::mutex> lock(gOpLock);
    auto it = gOps.find(ophandle);
    if (it == gOps.end()) {
        return 0;
    }
    Op *op = it->second;
    gOpDone.wait(lock, [op] { return op->done; });
    return op->status;
}

int64_t FILESYS_completeop64(FILEOP ophandle) {
    FILESYS_waitop(ophandle);
    std::lock_guard<std::mutex> lock(gOpLock);
    auto it = gOps.find(ophandle);
    if (it == gOps.end()) {
        return 0;
    }
    int64_t result = it->second->result;
    delete it->second;
    gOps.erase(it);
    return result;
}

int FILESYS_completeop(FILEOP ophandle) {
    return static_cast<int>(FILESYS_completeop64(ophandle));
}

void FILESYS_callbackop(FILEOP ophandle, FILESYS_CALLBACK func) {
    Op *op;
    bool run_now;
    {
        std::lock_guard<std::mutex> lock(gOpLock);
        auto it = gOps.find(ophandle);
        if (it == gOps.end()) {
            return;
        }
        op = it->second;
        op->callback = func;
        run_now = op->done; // finished before the callback was attached
    }
    if (run_now) {
        func(ophandle, op->status, op->userdata);
    }
}

void FILESYS_priorityop(FILEOP ophandle, int priority) {}

int FILESYS_opensync(const char *name, unsigned int modeflags, int priority) {
    return static_cast<int>(OpenNow(name, modeflags));
}

bool FILESYS_closesync(intptr_t filehandle, int priority) {
    return close(FdFromHandle(filehandle)) == 0;
}

int FILESYS_writesync(intptr_t filehandle, int offset, void *buffer, int bytes, int priority) {
    ssize_t n = pwrite(FdFromHandle(filehandle), buffer, bytes, offset);
    return n < 0 ? 0 : static_cast<int>(n);
}

bool FILESYS_existssync(const char *name, int priority) {
    struct stat st;
    return stat(ResolvePath(name).c_str(), &st) == 0;
}

// ---------------------------------------------------------------------------
// RealFile device registry. The PC build registered virtual devices (e.g. the
// "disculator" disc emulator); natively everything is a plain file.
// ---------------------------------------------------------------------------
RealFile::DeviceDriver::DeviceDriver(const char *name) {
    std::memset(mDeviceName, 0, sizeof(mDeviceName));
    std::strncpy(mDeviceName, name ? name : "", sizeof(mDeviceName) - 1);
}

void RealFile::AddDevice(DeviceDriver *device) {}

void RealFile::AddSearchLocation(const char *location, bool recursive) {}

bool RealFile::GetInfoFastByName(const char *name, const unsigned int modeflags, uint64_t &location, uint64_t &size) {
    struct stat st;
    std::string path = ResolvePath(name);
    if (stat(path.c_str(), &st) != 0) {
        if (PortTraceFiles()) {
            std::fprintf(stderr, "[file] info %s -> %s (missing)\n", name, path.c_str());
        }
        return false;
    }
    location = 0;
    size = static_cast<uint64_t>(st.st_size);
    return true;
}
