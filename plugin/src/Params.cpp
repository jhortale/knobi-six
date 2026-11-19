// SPDX-License-Identifier: GPL-3.0-only
#include "Params.h"
#include <cmath>

namespace knobi {

namespace {
thread_local bool tlFromCore = false;           // syncToHost's own setValue on this thread: not a host change

class IntParam final : public juce::AudioParameterInt {
public:
    IntParam(Params& o, uint16_t id, const kc_param_info_t& in)
        : juce::AudioParameterInt(juce::ParameterID{Params::paramID(id), 1}, in.name, (int)in.min, (int)in.max,
                                  (int)in.def),
          o_(o), id_(id) {}
    void valueChanged(int v) override { o_.hostChanged(id_, v); }
private:
    Params& o_;
    uint16_t id_;
};

class BoolParam final : public juce::AudioParameterBool {
public:
    BoolParam(Params& o, uint16_t id, const kc_param_info_t& in)
        : juce::AudioParameterBool(juce::ParameterID{Params::paramID(id), 1}, in.name, in.def != 0), o_(o), id_(id) {}
    void valueChanged(bool v) override { o_.hostChanged(id_, v ? 1 : 0); }
private:
    Params& o_;
    uint16_t id_;
};

class ChoiceParam final : public juce::AudioParameterChoice {
public:
    ChoiceParam(Params& o, uint16_t id, const kc_param_info_t& in, const juce::StringArray& labels)
        : juce::AudioParameterChoice(juce::ParameterID{Params::paramID(id), 1}, in.name, labels, (int)(in.def - in.min)),
          o_(o), id_(id), min_(in.min) {}
    void valueChanged(int index) override { o_.hostChanged(id_, min_ + index); }
private:
    Params& o_;
    uint16_t id_;
    int32_t min_;
};
} // namespace

Params::Params(juce::AudioProcessor& owner)
{
    index_.fill(-1);
    for (auto& a : hostVal_) a.store(kNone);
    for (auto& a : coreVal_) a.store(kNone);
    for (auto& a : notified_) a.store(kNone);
    for (auto& a : hostDirty_) a.store(0);
    for (auto& a : toHost_) a.store(0);
    for (uint16_t id = 0; id < kLimit; id++) {
        kc_param_info_t in;
        if (kc_param_info(id, &in)) continue;
        std::unique_ptr<juce::RangedAudioParameter> p;
        if (in.kind == KC_KIND_SWITCH)
            p = std::make_unique<BoolParam>(*this, id, in);
        else if (in.kind == KC_KIND_SELECT && in.labels) {
            juce::StringArray labels;
            for (int32_t v = in.min; v <= in.max; v++) labels.add(in.labels[v - in.min]);
            p = std::make_unique<ChoiceParam>(*this, id, in, labels);
        } else
            p = std::make_unique<IntParam>(*this, id, in);
        index_[id] = (int)entries_.size();
        entries_.push_back({id, in, p.get()});
        hostVal_[id].store(in.def), coreVal_[id].store(in.def);
        owner.addParameter(p.release());
    }
    n_ = (int)entries_.size();
}

int32_t Params::toCore(const kc_param_info_t& in, float x)
{
    x = x < 0.0f ? 0.0f : x > 1.0f ? 1.0f : x;
    if (in.kind == KC_KIND_SWITCH) return x >= 0.5f ? 1 : 0;
    return in.min + (int32_t)std::lround((double)x * (double)(in.max - in.min));
}

float Params::toNormalised(const kc_param_info_t& in, int32_t v)
{
    if (v < in.min) v = in.min;
    if (v > in.max) v = in.max;
    if (in.max == in.min) return 0.0f;
    return (float)((double)(v - in.min) / (double)(in.max - in.min));
}

int32_t Params::hostValue(uint16_t id) const
{
    const Entry* e = byId(id);
    return e ? toCore(e->info, e->param->getValue()) : 0;
}

void Params::hostChanged(uint16_t id, int32_t v)
{
    if (tlFromCore || id >= kLimit || index_[id] < 0) return;
    hostVal_[id].store(v, std::memory_order_release);
    hostDirty_[id / 64].fetch_or(1ull << (id % 64), std::memory_order_acq_rel);
}

void Params::coreChanged(uint16_t id, int32_t v)
{
    if (id >= kLimit || index_[id] < 0) return;
    coreVal_[id].store(v, std::memory_order_release);
    toHost_[id / 64].fetch_or(1ull << (id % 64), std::memory_order_acq_rel);
}

int Params::syncToHost()
{
    int told = 0;
    const juce::ScopedValueSetter<bool> guard(tlFromCore, true);
    for (int w = 0; w < 2; w++) {
        uint64_t m = toHost_[w].exchange(0, std::memory_order_acq_rel);
        while (m) {
            const int b = __builtin_ctzll(m);
            m &= m - 1;
            const uint16_t id = (uint16_t)(w * 64 + b);
            const Entry* e = byId(id);
            const int32_t v = coreVal_[id].load(std::memory_order_acquire);
            if (!e || v == kNone) continue;
            notified_[id].store(v, std::memory_order_release);
            if (toCore(e->info, e->param->getValue()) == v) continue;   // (the host has it already)
            e->param->setValueNotifyingHost(toNormalised(e->info, v));     // (no begin / endChangeGesture)
            told++;
        }
    }
    return told;
}

} // namespace knobi
