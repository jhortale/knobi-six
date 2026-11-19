// SPDX-License-Identifier: GPL-3.0-only
// KNOBI-SIX plugin: the program bank as the plugin sees it (no JUCE): the 500 programs a program change or the host's
// program list loads -- the user layer (a kc_bank_t, plan Task 6's library) over the factory layer compiled into the
// core (none in the default build: the init program, "INIT") -- read without touching the instance that plays.
//
// The seam for plan Task 6: the library owns the user layer and hands each snapshot in with Processor::setBank (which
// gives it to the playing instance, kc_bank_set, and to this view); WRITE / RESTORE FACTORY and the file are the
// library's. Until then the bank is the core's own: no user layer (setBank(nullptr)).
//
// program(n): what loading n gives, program + sequence, exactly as the playing instance would load it: read through a
// private scratch core instance (its own kc_t, never rendered for output) with the same bank, so the factory layer, the
// prog_seq pairing and the record formats are the firmware's own, not re-implemented here. Not the audio thread: it
// takes a mutex and creates the scratch instance on first use. name(n): program(n)'s name (20 characters at most).
#pragma once
#include <mutex>
#include <string>
#include "CoreC.h"

namespace knobi {

class ProgramBank {
public:
    ProgramBank() = default;
    ~ProgramBank();
    ProgramBank(const ProgramBank&) = delete;
    ProgramBank& operator=(const ProgramBank&) = delete;

    // the user layer (nullptr: none); the caller keeps it alive and unchanged while set (knobi_core.h kc_bank_set)
    bool setBank(const kc_bank_t* bank);
    const kc_bank_t* bank() const { return bank_; }

    bool program(int n, program_t* p, seq_t* s);  // false: n not 0..499 (or no memory for the scratch instance)
    std::string name(int n);

    // the firmware's packed records (what a program "is" for comparisons: the name, the values, the chord; the
    // sequence's steps): equal records = the same program
    static bool same(const program_t& a, const seq_t& as, const program_t& b, const seq_t& bs);

private:
    std::mutex m_;
    kc_t* scratch_ = nullptr;
    const kc_bank_t* bank_ = nullptr;
};

} // namespace knobi
