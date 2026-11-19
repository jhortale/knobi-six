// SPDX-License-Identifier: GPL-3.0-only
#include "StateCodec.h"
#include <cstring>

namespace knobi {

static void le16(std::vector<uint8_t>& b, uint16_t v) { b.push_back((uint8_t)v), b.push_back((uint8_t)(v >> 8)); }
static void le32(std::vector<uint8_t>& b, uint32_t v)
{
    for (int i = 0; i < 4; i++) b.push_back((uint8_t)(v >> (8 * i)));
}
static uint32_t rd32(const uint8_t* p) { return (uint32_t)p[0] | (uint32_t)p[1] << 8 | (uint32_t)p[2] << 16 | (uint32_t)p[3] << 24; }

void StateCodec::header(std::vector<uint8_t>& b, uint16_t version, uint16_t minReader)
{
    b.insert(b.end(), {'K', 'N', '6', 'S'});
    le16(b, version);
    le16(b, minReader);
}

void StateCodec::put(std::vector<uint8_t>& b, const char tag[4], const void* p, uint32_t n)
{
    b.insert(b.end(), tag, tag + 4);
    le32(b, n);
    const uint8_t* q = (const uint8_t*)p;
    b.insert(b.end(), q, q + n);
}

std::vector<uint8_t> StateCodec::encode(const SetState& s)
{
    std::vector<uint8_t> b;
    header(b, kVersion, kMinReader);
    uint8_t num[4] = {(uint8_t)s.number, (uint8_t)(s.number >> 8), (uint8_t)(s.number >> 16), (uint8_t)(s.number >> 24)};
    put(b, "PNUM", num, 4);
    uint8_t flags = (uint8_t)((s.edited ? 1u : 0u) | (s.differs ? 2u : 0u));
    put(b, "FLAG", &flags, 1);
    uint8_t rec[PROGRAM_PACKED_MAX];
    program_t p = s.program;
    program_set_seq_gen(&p, 0);                   // (the sequence travels with it: no store generation)
    int n = program_pack(&p, rec, sizeof rec);
    if (n > 0) put(b, "PROG", rec, (uint32_t)n);
    uint8_t sq[SEQ_PACKED_MAX];
    int m = seq_pack(&s.seq, sq, sizeof sq);
    if (m > 0) put(b, "SEQ ", sq, (uint32_t)m);
    return b;
}

StateCodec::Result StateCodec::decode(const void* data, size_t size, SetState& out)
{
    if (size == 0 || !data) return kEmpty;
    const uint8_t* b = (const uint8_t*)data;
    if (size < 8 || std::memcmp(b, "KN6S", 4)) return kBad;
    const uint16_t minReader = (uint16_t)(b[6] | b[7] << 8);
    if (minReader > kVersion) return kTooNew;
    SetState s;
    bool haveNum = false, haveProg = false, seqOk = true;
    seq_clear(&s.seq);
    size_t o = 8;
    while (o < size) {
        if (size - o < 8) return kBad;            // (a cut record)
        const uint8_t* tag = b + o;
        const uint32_t len = rd32(b + o + 4);
        o += 8;
        if (len > size - o) return kBad;
        const uint8_t* v = b + o;
        if (!std::memcmp(tag, "PNUM", 4)) {
            if (len != 4) return kBad;
            s.number = (int32_t)rd32(v), haveNum = true;
        } else if (!std::memcmp(tag, "FLAG", 4)) {
            if (len < 1) return kBad;
            s.edited = (v[0] & 1u) != 0, s.differs = (v[0] & 2u) != 0;
        } else if (!std::memcmp(tag, "PROG", 4)) {
            if (program_unpack(&s.program, v, len)) return kBad;
            haveProg = true;
        } else if (!std::memcmp(tag, "SEQ ", 4)) {
            seqOk = seq_unpack(&s.seq, v, len) == 0;
        }                                         // (else: a later writer's record, skipped)
        o += len;
    }
    if (!haveNum || !haveProg || !seqOk || s.number < 0 || s.number >= (int32_t)KC_PROGRAMS) return kBad;
    program_set_seq_gen(&s.program, 0);
    if (kc_program_valid(&s.program, &s.seq)) return kBad;
    out = s;
    return kOk;
}

} // namespace knobi
