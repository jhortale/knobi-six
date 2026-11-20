// SPDX-License-Identifier: GPL-3.0-only
#include "ProgramBank.h"
#include <cstring>

namespace knobi {

ProgramBank::~ProgramBank()
{
    if (scratch_) kc_destroy(scratch_);
}

bool ProgramBank::setBank(const kc_bank_t* bank)
{
    std::lock_guard<std::mutex> g(m_);
    if (scratch_ && kc_bank_set(scratch_, bank)) return false;   // (refused: a used entry the firmware would not store)
    if (!scratch_ && bank) {                      // (validated the same way without an instance yet)
        for (uint32_t i = 0; i < KC_PROGRAMS; i++)
            if (bank->used[i] && kc_program_valid(&bank->program[i], &bank->seq[i])) return false;
    }
    bank_ = bank;
    return true;
}

bool ProgramBank::program(int n, program_t* p, seq_t* s)
{
    if (n < 0 || n >= (int)KC_PROGRAMS) return false;
    std::lock_guard<std::mutex> g(m_);
    return read(bank_, n, p, s);
}

bool ProgramBank::factory(int n, program_t* p, seq_t* s)
{
    if (n < 0 || n >= (int)KC_PROGRAMS) return false;
    std::lock_guard<std::mutex> g(m_);
    return read(nullptr, n, p, s);
}

bool ProgramBank::read(const kc_bank_t* bank, int n, program_t* p, seq_t* s)
{
    if (!scratch_) {
        scratch_ = kc_create();
        if (!scratch_) return false;
    }
    kc_bank_set(scratch_, bank);                  // (every read sets the bank it reads: an old one is never read again)
    // a load is taken by the engine at its next block (engine.c eng_program: one at a time): a frame before (a load
    // still pending is done) and one after (this one is), then the edit buffer is program n as a program change gives it
    int16_t lr[2];
    kc_render_exact(scratch_, lr, 1);
    kc_program_select(scratch_, n);
    kc_render_exact(scratch_, lr, 1);
    int32_t num = -1;
    kc_program_get(scratch_, &num, p, s, nullptr);
    return num == n && !kc_program_pending(scratch_);
}

std::string ProgramBank::name(int n)
{
    program_t p;
    seq_t s;
    if (!program(n, &p, &s)) return {};
    size_t len = 0;
    while (len < PROGRAM_NAME_LEN && p.name[len]) len++;   // (a full 20-character name has no terminator)
    std::string r(p.name, len);
    while (!r.empty() && r.back() == ' ') r.pop_back();
    return r;
}

bool ProgramBank::same(const program_t& a, const seq_t& as, const program_t& b, const seq_t& bs)
{
    uint8_t x[PROGRAM_PACKED_MAX], y[PROGRAM_PACKED_MAX];
    program_t a0 = a, b0 = b;                     // (the sequence's store generation is not the program: as kc_program_get)
    program_set_seq_gen(&a0, 0);
    program_set_seq_gen(&b0, 0);
    int nx = program_pack(&a0, x, sizeof x), ny = program_pack(&b0, y, sizeof y);
    if (nx < 0 || nx != ny || std::memcmp(x, y, (size_t)nx)) return false;
    uint8_t u[SEQ_PACKED_MAX], v[SEQ_PACKED_MAX];
    int nu = seq_pack(&as, u, sizeof u), nv = seq_pack(&bs, v, sizeof v);
    return nu >= 0 && nu == nv && std::memcmp(u, v, (size_t)nu) == 0;
}

} // namespace knobi
