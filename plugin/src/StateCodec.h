// SPDX-License-Identifier: GPL-3.0-only
// KNOBI-SIX plugin: the instance's set state (what a Live set saves for it; no JUCE). Spec "Set state": the program
// number, the full current program + sequence (the firmware's packed records), the edited flag and "differs from
// bank". The stored copy wins over the bank on load (Processor::setStateInformation).
//
// Format (all integers little-endian):
//   "KN6S"            magic
//   u16 version       the writer's format version (this build writes kVersion)
//   u16 minReader     the oldest reader version that can load it (this build writes 1): a reader older than that
//                     refuses the state (nothing changes); any other reader loads it
//   then records to the end: char tag[4], u32 length, length bytes
//     "PNUM"  4: int32 program number 0..499
//     "FLAG"  1: bit 0 edited, bit 1 differs from bank (other bits: ignored)
//     "PROG"  the program, the firmware's packed record (params.c program_pack: version, name, values, chord, CRC);
//             any record version the firmware loads (1..PROGRAM_VERSION: program_unpack, older ones at the defaults for
//             what they lack)
//     "SEQ "  the sequence, the firmware's packed record (synth/seq.h seq_pack)
//   Unknown tags (a later writer's) are skipped; a known tag appearing twice: the last one. Required: PNUM and PROG; no
//   "SEQ ": an empty sequence; no "FLAG": not edited, not differing.
// Versioning: a later build adds records (new tags; older readers skip them) and raises version; only a change an older
// reader must not misread raises minReader. A state of size 0 (the plan-Task-3 build saved nothing) is "no state".
#pragma once
#include <cstdint>
#include <vector>
#include "CoreC.h"

namespace knobi {

struct SetState {
    int32_t number = 0;
    program_t program{};
    seq_t seq{};
    bool edited = false;
    bool differs = false;
};

struct StateCodec {
    static constexpr uint16_t kVersion = 1;       // this build's writer and reader
    static constexpr uint16_t kMinReader = 1;

    static std::vector<uint8_t> encode(const SetState& s);
    enum Result { kOk, kEmpty, kBad, kTooNew };
    static Result decode(const void* data, size_t size, SetState& out);   // out changed only on kOk

    // (tests) a record of any tag, appended as the format says
    static void put(std::vector<uint8_t>& b, const char tag[4], const void* p, uint32_t n);
    static void header(std::vector<uint8_t>& b, uint16_t version, uint16_t minReader);
};

} // namespace knobi
