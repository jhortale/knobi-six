// SPDX-License-Identifier: GPL-3.0-only
#include "Library.h"
#include <cerrno>
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <ctime>
#include <dirent.h>
#include <fcntl.h>
#include <filesystem>
#include <signal.h>
#include <sys/file.h>
#include <sys/stat.h>
#include <unistd.h>

namespace knobi {

namespace {

constexpr char kMagic[4] = {'K', 'N', '6', 'L'};
constexpr size_t kHeader = 12, kTrailer = 4;

uint32_t crc32(const uint8_t* d, size_t n)
{
    uint32_t c = 0xFFFFFFFFu;
    for (size_t i = 0; i < n; i++) {
        c ^= d[i];
        for (int k = 0; k < 8; k++) c = (c >> 1) ^ (0xEDB88320u & (0u - (c & 1u)));
    }
    return ~c;
}
void put16(std::vector<uint8_t>& b, uint32_t v) { b.push_back((uint8_t)v), b.push_back((uint8_t)(v >> 8)); }
void put32(std::vector<uint8_t>& b, uint32_t v) { put16(b, v & 0xFFFFu), put16(b, v >> 16); }
uint32_t get16(const uint8_t* p) { return (uint32_t)p[0] | (uint32_t)p[1] << 8; }
uint32_t get32(const uint8_t* p) { return get16(p) | get16(p + 2) << 16; }
void record(std::vector<uint8_t>& b, const char* tag, const std::vector<uint8_t>& body)
{
    b.insert(b.end(), tag, tag + 4);
    put32(b, (uint32_t)body.size());
    b.insert(b.end(), body.begin(), body.end());
}
bool isKept(int g)
{
    for (int k : Library::kGlobals)
        if (k == g) return true;
    return false;
}

std::mutex gDefaultLock;
std::string gDefault;                             // (setDefaultPath; empty: ~/Music/KNOBI-SIX/library.knobi)
std::mutex gSharedLock;
std::map<std::string, std::weak_ptr<Library>> gShared;

bool readAll(const std::string& path, std::vector<uint8_t>& out, bool& exists)
{
    out.clear();
    exists = false;
    const int fd = ::open(path.c_str(), O_RDONLY | O_CLOEXEC);
    if (fd < 0) return errno == ENOENT;           // (no file: an empty library)
    exists = true;
    uint8_t buf[65536];
    for (;;) {
        const ssize_t r = ::read(fd, buf, sizeof buf);
        if (r < 0 && errno == EINTR) continue;
        if (r < 0) { ::close(fd); return false; }
        if (r == 0) break;
        out.insert(out.end(), buf, buf + r);
    }
    ::close(fd);
    return true;
}
bool writeAll(int fd, const std::vector<uint8_t>& b)
{
    size_t done = 0;
    while (done < b.size()) {
        const ssize_t w = ::write(fd, b.data() + done, b.size() - done);
        if (w < 0 && errno == EINTR) continue;
        if (w <= 0) return false;
        done += (size_t)w;
    }
    return true;
}

} // namespace

// ---- the format
std::vector<uint8_t> Library::encode(const Data& d)
{
    std::vector<uint8_t> body;
    if (!d.globals.empty()) {
        std::vector<uint8_t> g;
        for (const auto& [id, v] : d.globals) g.push_back(id), g.push_back((uint8_t)v);
        record(body, "GLOB", g);
    }
    for (uint32_t n = 0; n < KC_PROGRAMS; n++) {
        if (d.prog[n].empty()) continue;
        std::vector<uint8_t> r;
        put16(r, n);
        put16(r, (uint32_t)d.prog[n].size());
        r.insert(r.end(), d.prog[n].begin(), d.prog[n].end());
        put16(r, (uint32_t)d.seq[n].size());
        r.insert(r.end(), d.seq[n].begin(), d.seq[n].end());
        record(body, "SLOT", r);
    }
    body.insert(body.end(), d.unknown.begin(), d.unknown.end());
    std::vector<uint8_t> b(kMagic, kMagic + 4);
    put16(b, kVersion);
    put16(b, 1);                                  // minReader: every version-1 reader reads this
    put32(b, (uint32_t)body.size());
    b.insert(b.end(), body.begin(), body.end());
    put32(b, crc32(b.data(), b.size()));
    return b;
}

Library::Decode Library::decode(const uint8_t* b, size_t n, Data& d)
{
    d = Data();
    if (!b || n < kHeader + kTrailer || std::memcmp(b, kMagic, 4) != 0) return kDecBad;
    const uint32_t minReader = get16(b + 6), len = get32(b + 8);
    if ((uint64_t)kHeader + len + kTrailer != n) return kDecBad;   // (cut, or something appended)
    if (get32(b + kHeader + len) != crc32(b, kHeader + len)) return kDecBad;
    if (minReader > kVersion) return kDecTooNew;
    const uint8_t* p = b + kHeader;
    const uint8_t* end = p + len;
    while (p < end) {
        if (end - p < 8) return kDecBad;
        const uint32_t rl = get32(p + 4);
        if ((uint64_t)(end - p - 8) < rl) return kDecBad;
        const uint8_t* r = p + 8;
        if (std::memcmp(p, "SLOT", 4) == 0) {
            if (rl < 6) return kDecBad;
            const uint32_t num = get16(r), pl = get16(r + 2);
            if (4u + pl + 2u > rl) return kDecBad;
            const uint32_t sl = get16(r + 4 + pl);
            if (4u + pl + 2u + sl != rl) return kDecBad;
            if (num < KC_PROGRAMS && pl > 0) {    // (a number past the bank, or no program record: not a slot)
                d.prog[num].assign(r + 4, r + 4 + pl);
                d.seq[num].assign(r + 6 + pl, r + 6 + pl + sl);
            }
        } else if (std::memcmp(p, "GLOB", 4) == 0) {
            if (rl % 2) return kDecBad;
            for (uint32_t i = 0; i < rl; i += 2) d.globals[r[i]] = (int8_t)r[i + 1];
        } else
            d.unknown.insert(d.unknown.end(), p, r + rl);   // (a later version's: kept as it is)
        p = r + rl;
    }
    return kDecOk;
}

void Library::makeSnapshot(const Data& d, LibrarySnapshot& s)
{
    std::memset(&s.bank, 0, sizeof s.bank);
    s.badSlots = 0;
    for (uint32_t n = 0; n < KC_PROGRAMS; n++) {
        if (d.prog[n].empty()) continue;
        program_t& p = s.bank.program[n];
        seq_t& q = s.bank.seq[n];
        // the firmware's own readers (a record it cannot read, or would not store: kc_program_valid), so the bank is
        // never refused by kc_bank_set for one slot: that slot is the factory program, counted
        const bool ok = program_unpack(&p, d.prog[n].data(), (uint32_t)d.prog[n].size()) == 0 &&
                        seq_unpack(&q, d.seq[n].data(), (uint32_t)d.seq[n].size()) == 0 && kc_program_valid(&p, &q) == 0;
        if (ok) {
            program_set_seq_gen(&p, 0);
            s.bank.used[n] = 1;
        } else {
            program_init(&p), seq_clear(&q);
            s.badSlots++;
        }
    }
    for (int g = 0; g < G_COUNT; g++) s.globals[g] = GLOB_DEF[g];
    for (const auto& [id, v] : d.globals)
        if (id < G_COUNT && isKept(id) && v >= GLOB_MIN[id] && v <= GLOB_MAX[id]) s.globals[id] = v;
}

// ---- paths, the shared instance
std::string Library::defaultPath()
{
    {
        std::lock_guard<std::mutex> g(gDefaultLock);
        if (!gDefault.empty()) return gDefault;
    }
    const char* home = std::getenv("HOME");
    return std::string(home && *home ? home : "") + "/Music/KNOBI-SIX/library.knobi";
}

void Library::setDefaultPath(const std::string& path)
{
    std::lock_guard<std::mutex> g(gDefaultLock);
    gDefault = path;
}

std::shared_ptr<Library> Library::shared(const std::string& path)
{
    std::lock_guard<std::mutex> g(gSharedLock);
    if (auto l = gShared[path].lock()) return l;
    auto l = std::make_shared<Library>(path);
    gShared[path] = l;
    return l;
}

// ---- the instance
bool Library::Sig::operator==(const Sig& o) const
{
    return exists == o.exists && ino == o.ino && size == o.size && mtimeNs == o.mtimeNs;
}

Library::Sig Library::statSig() const
{
    Sig s;
    struct stat st;
    if (::stat(path_.c_str(), &st) != 0) return s;   // (no allocation: the watcher's poll)
    s.exists = true, s.ino = (uint64_t)st.st_ino, s.size = (uint64_t)st.st_size;
    s.mtimeNs = (int64_t)st.st_mtimespec.tv_sec * 1000000000 + st.st_mtimespec.tv_nsec;
    return s;
}

Library::Library(std::string path, int pollMs) : path_(std::move(path)), pollMs_(pollMs)
{
    auto s = std::make_shared<LibrarySnapshot>();
    makeSnapshot(Data(), *s);
    s->generation = ++generation_;
    snap_ = s;
    reload();
    if (pollMs_ > 0) watcher_ = std::thread([this] { watch(); });
}

Library::~Library()
{
    stop_.store(true);
    if (watcher_.joinable()) watcher_.join();
}

std::shared_ptr<const LibrarySnapshot> Library::snapshot() const
{
    std::lock_guard<std::mutex> g(snapLock_);
    return snap_;
}

std::thread::id Library::lastReloadThread() const
{
    std::lock_guard<std::mutex> g(threadLock_);
    return reloadThread_;
}

void Library::watch()
{
    // the poll: a stat (no allocation, no lock) every pollMs, slept in short slices so the destructor's join is quick
    const auto slice = std::chrono::milliseconds(pollMs_ < 20 ? pollMs_ : 20);
    int waited = 0;
    while (!stop_.load()) {
        std::this_thread::sleep_for(slice);
        waited += (int)slice.count();
        if (waited < pollMs_) continue;
        waited = 0;
        reload();
    }
}

bool Library::reload()
{
    std::lock_guard<std::mutex> g(readLock_);
    const Sig now = statSig();
    if (now == sig_) return false;                // (unchanged since read or written here; at first: no file)
    std::vector<uint8_t> bytes;
    bool exists = false;
    if (!readAll(path_, bytes, exists)) return false;   // (could not read it now: the next poll tries again)
    const Sig after = statSig();
    if (!(after == now)) return false;            // (changed while read: read again at the next poll)
    {
        std::lock_guard<std::mutex> t(threadLock_);
        reloadThread_ = std::this_thread::get_id();
    }
    sig_ = now;
    if (exists == bytesExist_ && bytes == bytes_) return false;   // (the same content: nothing new)
    bytes_ = bytes, bytesExist_ = exists;
    auto s = std::make_shared<LibrarySnapshot>();
    Data d;
    const Decode dec = exists ? decode(bytes.data(), bytes.size(), d) : kDecOk;
    if (dec != kDecOk) d = Data();               // unreadable: the factory layer, the file left as it is
    makeSnapshot(d, *s);
    s->unreadable = dec != kDecOk;
    publish(std::move(s));
    return true;
}

void Library::publish(std::shared_ptr<LibrarySnapshot> s)   // (readLock_ held: snapshots in the file's order)
{
    s->generation = ++generation_;
    std::shared_ptr<const LibrarySnapshot> c = std::move(s);
    {
        std::lock_guard<std::mutex> g(snapLock_);
        snap_ = c;
    }
    std::lock_guard<std::mutex> g(listenLock_);
    for (auto& [id, l] : listeners_) l(c);
}

int Library::addListener(Listener l)
{
    std::lock_guard<std::mutex> g(listenLock_);
    const int id = nextListener_++;
    listeners_[id] = std::move(l);
    return id;
}

void Library::removeListener(int id)
{
    std::lock_guard<std::mutex> g(listenLock_);
    listeners_.erase(id);
}

// ---- writing: read-modify-write under the lock file, temp + fsync + rename
Library::Result Library::mutate(const std::function<bool(Data&)>& change)
{
    namespace fs = std::filesystem;
    std::lock_guard<std::mutex> w(writeLock_);
    std::lock_guard<std::mutex> r(readLock_);
    const fs::path file(path_), dir = file.parent_path();
    std::error_code ec;
    if (!dir.empty()) fs::create_directories(dir, ec);   // (the first WRITE makes ~/Music/KNOBI-SIX)
    const std::string lockPath = path_ + ".lock";
    const int lfd = ::open(lockPath.c_str(), O_RDWR | O_CREAT | O_CLOEXEC, 0644);
    if (lfd < 0) return kIoError;
    while (::flock(lfd, LOCK_EX) != 0)
        if (errno != EINTR) { ::close(lfd); return kIoError; }
    struct Unlock { int fd; ~Unlock() { ::flock(fd, LOCK_UN), ::close(fd); } } unlock{lfd};

    // the file as it is now (another writer's change included), under the lock
    std::vector<uint8_t> bytes;
    bool exists = false;
    if (!readAll(path_, bytes, exists)) return kIoError;
    Data d;
    if (exists && decode(bytes.data(), bytes.size(), d) != kDecOk) {
        // unreadable: kept as library.knobi.bad-<time> before it is replaced (a hard link: the same bytes, atomically;
        // else a copy)
        char stamp[32];
        const std::time_t t = std::time(nullptr);
        std::tm tm{};
        localtime_r(&t, &tm);
        std::strftime(stamp, sizeof stamp, "%Y%m%d-%H%M%S", &tm);
        std::string backup = path_ + ".bad-" + stamp;
        for (int i = 2; fs::exists(backup, ec); i++) backup = path_ + ".bad-" + stamp + "-" + std::to_string(i);
        if (::link(path_.c_str(), backup.c_str()) != 0) {
            fs::copy_file(file, backup, ec);
            if (ec) return kIoError;              // (no backup: the bad file is not replaced)
        }
        d = Data();
    }
    if (!change(d)) return kInvalid;
    const std::vector<uint8_t> out = encode(d);

    // stale temp files of writers that died (library_atomic): theirs, never a live process's
    const std::string tmpBase = file.filename().string() + ".tmp-";
    for (auto& e : fs::directory_iterator(dir.empty() ? fs::path(".") : dir, ec)) {
        const std::string f = e.path().filename().string();
        if (f.compare(0, tmpBase.size(), tmpBase) != 0) continue;
        const long pid = std::strtol(f.c_str() + tmpBase.size(), nullptr, 10);
        if (pid > 0 && pid != (long)::getpid() && ::kill((pid_t)pid, 0) != 0 && errno == ESRCH) fs::remove(e.path(), ec);
    }
    static std::atomic<unsigned> counter{0};
    const std::string tmp = path_ + ".tmp-" + std::to_string((long)::getpid()) + "-" + std::to_string(++counter);
    const int fd = ::open(tmp.c_str(), O_WRONLY | O_CREAT | O_EXCL | O_CLOEXEC, 0644);
    if (fd < 0) return kIoError;
    struct stat st;
    const bool ok = writeAll(fd, out) && ::fsync(fd) == 0 && ::fstat(fd, &st) == 0;
    ::close(fd);
    if (!ok) { ::unlink(tmp.c_str()); return kIoError; }
    if (beforeRename) beforeRename();
    if (::rename(tmp.c_str(), path_.c_str()) != 0) { ::unlink(tmp.c_str()); return kIoError; }
    if (!dir.empty()) {                           // (the rename itself on disk)
        const int dfd = ::open(dir.c_str(), O_RDONLY | O_CLOEXEC);
        if (dfd >= 0) ::fsync(dfd), ::close(dfd);
    }
    // this library's snapshot at once (its watcher then sees its own file: the same identity, not read again)
    sig_.exists = true, sig_.ino = (uint64_t)st.st_ino, sig_.size = (uint64_t)st.st_size;
    sig_.mtimeNs = (int64_t)st.st_mtimespec.tv_sec * 1000000000 + st.st_mtimespec.tv_nsec;
    bytes_ = out, bytesExist_ = true;
    auto s = std::make_shared<LibrarySnapshot>();
    makeSnapshot(d, *s);
    s->unreadable = false;
    publish(std::move(s));
    return kOk;
}

Library::Result Library::write(int n, const program_t& p, const seq_t& s)
{
    if (n < 0 || n >= (int)KC_PROGRAMS || kc_program_valid(&p, &s) != 0) return kInvalid;
    program_t q = p;
    program_set_seq_gen(&q, 0);                   // (the sequence's store generation is not the program's)
    uint8_t pb[PROGRAM_PACKED_MAX], sb[SEQ_PACKED_MAX];
    const int np = program_pack(&q, pb, sizeof pb), ns = seq_pack(&s, sb, sizeof sb);
    if (np <= 0 || ns <= 0) return kInvalid;
    return mutate([&](Data& d) {
        d.prog[n].assign(pb, pb + np);
        d.seq[n].assign(sb, sb + ns);
        return true;
    });
}

Library::Result Library::clear(int n)
{
    if (n < 0 || n >= (int)KC_PROGRAMS) return kInvalid;
    return mutate([&](Data& d) {
        d.prog[n].clear();
        d.seq[n].clear();
        return true;
    });
}

Library::Result Library::setGlobal(int g, int v)
{
    if (g < 0 || g >= G_COUNT || !isKept(g) || v < GLOB_MIN[g] || v > GLOB_MAX[g]) return kInvalid;
    return mutate([&](Data& d) {
        d.globals[(uint8_t)g] = (int8_t)v;
        return true;
    });
}

} // namespace knobi
