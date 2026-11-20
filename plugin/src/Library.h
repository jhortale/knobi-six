// SPDX-License-Identifier: GPL-3.0-only
// KNOBI-SIX plugin: the shared program library (plan Task 6; spec "Library file"), no JUCE. One file on this Mac,
// ~/Music/KNOBI-SIX/library.knobi, shared by every instance in every set: the bank's user layer (500 slots, each empty
// = the factory program of this build, or a full program + sequence) and the plugin's globals.
//
// The file (little-endian; versioned like the set state, StateCodec.h):
//   "KN6L"  u16 version (1)  u16 minReader (1: a reader older than this refuses the file)  u32 body length B
//   body: records  tag[4] u32 len  bytes[len]   (unknown tags kept as they are on a WRITE: a later writer's data
//         survives an older one's writes)
//     "SLOT"  u16 number (0..499)  u16 plen  program record[plen]  u16 slen  sequence record[slen]
//             the firmware's packed records (program_pack with the sequence generation 0, seq_pack), as the keyboard
//             stores them; a slot with no SLOT record is empty (factory); the last SLOT of a number wins
//     "GLOB"  n x (u8 id, i8 value): firmware globals (params/globals.h G_*) the plugin keeps: kGlobals; ids it does
//             not know or values out of GLOB_MIN..GLOB_MAX are kept in the file and not used
//   u32 CRC-32 (IEEE, reflected 0xEDB88320) of everything before it
// Readers: a bad magic, a cut file, a body length or CRC that does not match, minReader > 1: "unreadable" (the
// factory layer, notice unreadable(); the file is left exactly as it is until a WRITE, which first keeps it as
// library.knobi.bad-<YYYYMMDD-HHMMSS>). A slot whose records the firmware would not store (kc_program_valid: a broken
// or newer record): that slot is the factory program, counted in badSlots(); its bytes stay in the file.
//
// Writing (WRITE, RESTORE FACTORY + WRITE, a global): read-modify-write under an exclusive flock(2) on
// library.knobi.lock beside it (processes and instances alike): the file is read again under the lock, the one change
// made, the whole file written to a temp file in the same directory (library.knobi.tmp-<pid>-<n>), fsync'd and renamed
// over library.knobi (atomic: a reader sees the old file or the new one, never a part; a crash before the rename
// leaves the old file and a stale temp file, removed by the next WRITE once its process is gone). So two instances
// writing different slots at once both land; two writing the same slot: the later lock holder's wins (last write wins).
// The directory is created on the first write. Nothing writes the file but write / clear / setGlobal.
//
// Reloading: a watcher thread polls the file's identity (inode, size, modification time) every pollMs; a change
// (another instance's or another process's write) is read on that thread (never the audio thread) into a new
// immutable snapshot, handed to the listeners. This Library's own writes publish their snapshot at once.
//
// Snapshots are immutable and shared (std::shared_ptr<const LibrarySnapshot>): a holder keeps one alive as long as
// it may be read (Processor: the bank release protocol, Processor.h).
#pragma once
#include <atomic>
#include <cstdint>
#include <functional>
#include <map>
#include <memory>
#include <mutex>
#include <string>
#include <thread>
#include <vector>
#include "CoreC.h"

namespace knobi {

struct LibrarySnapshot {
    kc_bank_t bank;                               // the user layer: used[n] 0 = the factory program (kc_bank_set)
    int8_t globals[G_COUNT];                      // kGlobals from the file, the others (and any missing) the defaults
    bool unreadable = false;                      // the file exists but cannot be read: the factory layer
    int badSlots = 0;                             // slots the firmware would not store: the factory program instead
    uint64_t generation = 0;                      // this Library's count of snapshots made (1 = the first)
};

class Library {
public:
    // the globals the library keeps (spec: master tune, transpose, velocity / aftertouch curves, stereo / mono); MIDI
    // channel is omni in the plugin, not kept
    static constexpr int kGlobals[] = {G_TUNE, G_TRANSPOSE, G_VEL_CURVE, G_AT_CURVE, G_STEREO};
    static constexpr uint16_t kVersion = 1;
    enum Result { kOk, kInvalid, kIoError };      // write / clear / setGlobal
    enum Decode { kDecOk, kDecBad, kDecTooNew };

    // loads path now (on this thread) and starts the watcher (pollMs > 0; 0: no watcher, reload() by hand)
    explicit Library(std::string path, int pollMs = 250);
    ~Library();
    Library(const Library&) = delete;
    Library& operator=(const Library&) = delete;

    // ~/Music/KNOBI-SIX/library.knobi, unless setDefaultPath (the tests: a temporary directory, never the real one)
    static std::string defaultPath();
    static void setDefaultPath(const std::string& path);
    // the one Library of this process for path (every plugin instance shares it: one watcher, one snapshot); alive
    // while any holder keeps it
    static std::shared_ptr<Library> shared(const std::string& path);

    const std::string& path() const { return path_; }
    std::shared_ptr<const LibrarySnapshot> snapshot() const;

    Result write(int n, const program_t& p, const seq_t& s);   // slot n = p + s (kInvalid: n or the records)
    Result clear(int n);                          // slot n empty: the factory program
    Result setGlobal(int g, int v);               // g one of kGlobals, GLOB_MIN..GLOB_MAX
    bool reload();                                // read the file if it changed (the watcher's call); true: a new snapshot

    // listeners get each new snapshot (the watcher's thread or the writing thread; never the audio thread), not under
    // any lock of the library's but its listener lock (removeListener waits for a call in progress)
    using Listener = std::function<void(const std::shared_ptr<const LibrarySnapshot>&)>;
    int addListener(Listener l);
    void removeListener(int id);

    // the file format (exposed for the tests)
    struct Data {                                 // the file's content, raw records kept as read
        std::vector<uint8_t> prog[KC_PROGRAMS], seq[KC_PROGRAMS];   // prog empty: the slot is empty
        std::map<uint8_t, int8_t> globals;
        std::vector<uint8_t> unknown;             // records with tags this version does not know, as read
    };
    static std::vector<uint8_t> encode(const Data& d);
    static Decode decode(const uint8_t* b, size_t n, Data& d);
    static void makeSnapshot(const Data& d, LibrarySnapshot& s);   // (generation not set)

    // tests: called between the temp file's fsync and the rename (library_atomic kills the process there)
    static inline void (*beforeRename)() = nullptr;
    std::thread::id lastReloadThread() const;     // the thread that read the file last (reload_off_audio_thread)

private:
    struct Sig { bool exists = false; uint64_t ino = 0, size = 0; int64_t mtimeNs = 0; bool operator==(const Sig&) const; };
    Sig statSig() const;
    Result mutate(const std::function<bool(Data&)>& change);
    void publish(std::shared_ptr<LibrarySnapshot> s);
    void watch();

    const std::string path_;
    const int pollMs_;
    mutable std::mutex snapLock_;
    std::shared_ptr<const LibrarySnapshot> snap_;
    std::mutex writeLock_, readLock_;             // (this process's writers; reload vs reload)
    Sig sig_;                                     // (readLock_) the file as last read or written here
    std::vector<uint8_t> bytes_;                  // (readLock_) its bytes
    bool bytesExist_ = false;                     // (readLock_) it existed
    std::atomic<uint64_t> generation_{0};
    std::mutex listenLock_;
    std::map<int, Listener> listeners_;
    int nextListener_ = 1;
    std::atomic<bool> stop_{false};
    std::thread watcher_;
    mutable std::mutex threadLock_;
    std::thread::id reloadThread_;
};

} // namespace knobi
