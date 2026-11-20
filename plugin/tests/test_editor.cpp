// SPDX-License-Identifier: GPL-3.0-only
// KNOBI-SIX plugin panel tests (plan Task 7): the editor made offscreen (no window, no peer), its components
// inspected and driven through the same paths the mouse takes (CTest: one test a name; no name: all):
//   panel_covers_params   every parameter the processor exposes has exactly one control on the panel (a knob for a
//                         knob, a pad for a switch, an LED list with the same labels for a select; BPM: the 7-segment
//                         display), nothing else claims a parameter; every section of the spec is on the panel
//   panel_roundtrip       for every parameter: a host change (setValueNotifyingHost) shows on its control after a poll;
//                         a user edit on the control (min, max, a middle value) is the parameter's value with one
//                         begin / end gesture, and reaches the core at the next block
//   panel_resizable       default 1280 x 760 (100 %: fits a 1440-wide screen); limits 100 .. 200 % with the aspect
//                         kept; at 150 % and 200 % every control is exactly where it was, scaled; no control
//                         overlaps another or leaves its section / the window
//   panel_browser         bank / tens / ones shown for the program in play (setCurrentProgram 237: "2" "37"), its
//                         name, "edited" after an edit, "differs from bank" after the bank's program changed; two
//                         digits load tens + ones in the bank shown; BANK + a digit the same number in that bank;
//                         < > step; the user-slot light follows a WRITE
//   panel_write           WRITE arms, the digits choose the slot without loading, WRITE again stores it (the library
//                         file has it, the instance plays it, not edited: "PROGRAM 045 STORED"); CANCEL writes nothing;
//                         kStoredOnly (a program change while the file is written), kNewer (a newer version's file)
//                         and kIoError (a library that cannot be written) shown as their messages; an unreadable library:
//                         the first WRITE asks first (nothing written until confirmed; CANCEL keeps the file as it is),
//                         confirmed: the backup kept, the slot written
//   panel_restore         RESTORE FACTORY: the factory program in the edit buffer, edited, nothing stored (the slot
//                         keeps its program); WRITE after it empties the slot (factory)
//   panel_clock           the BPM display shows P_BPM while Live is stopped; greyed with Live's tempo while it plays
//                         (followingHost); the clock divisions are a select of the 10 divisions
//   panel_sequencer       SEQ PLAY switches the sequencer (the pad lit); the state reads OFF / WAITING FOR LIVE (Live
//                         stopped) / PLAYING (Live playing); SEQ REC present and enabled (Task 8); the grid is a
//                         component of kGridW x kGridH inside the SEQUENCER section
//   panel_globals         GLOBALS opens the settings view: master tune, transpose, velocity / aftertouch curves,
//                         stereo / mono, each into the library (another processor on the file follows); the "AU
//                         recommended in Live" hint is on it
//   panel_rt              an audio thread renders (its own allocations and locks counted) while the editor polls and
//                         the user moves controls, loads programs and presses SEQ PLAY, edits the grid and records
//                         (SEQ REC, REST, TIE, BACK): no allocation, no lock on the audio thread
//   panel_idle            (fix round 1) nothing changing: 60 polls make no access() call (no snapshot, the message
//                         thread never waits for the audio thread); (Task 8) the grid: no access() call idle or while the
//                         playing step moves (an atomic), one sequence read per recorded step; a parameter edit while "edited" stays set (after
//                         RESTORE FACTORY) updates "differs from bank" within ProgramBrowser::kParamTicks polls, both
//                         ways; an unpolled switch toggles from the parameter's own value
//   panel_screenshots     the panel rendered at 100 % and 200 % to PNG (argv[2]: the directory; default a temporary
//                         one): non-empty images of the right size; the grid with a demo sequence (synthetic), its STEP
//                         editor open (panel-seqstep.png)
// The sequencer grid and the step entry (plan Task 8; ui/SeqGrid.h):
//   grid_shows            the length, each step's notes (a note, a chord, a tie, a rest, 6 notes), the steps past the
//                         end; the playing step (the core's seqStep) moves while Live plays, one at a time; none stopped
//   grid_mouse            through the mouse: a step opened, notes added from the piano (a 7th refused, its reason),
//                         removed (a lit key, X), a velocity dragged, REST, TIE (refused on step 1 and after a rest, with
//                         the reason; a tie carries the step before's notes and follows them; a rest before it unties
//                         it), LENGTH - / + / dragged (1..64), a step past the end; after every edit the core's sequence
//                         is the grid's, one the firmware stores (kc_program_valid), the program edited; WRITE stores it
//   grid_step_entry       SEQ REC (enabled now): Live's MIDI notes recorded as the firmware's recorder (ui/seq_rec.h,
//                         given the same inputs) records them (chords, REST, TIE while held = INC, BACK, notes out of
//                         range), the grid read-only meanwhile and showing the steps; TIE with nothing held ties the last
//                         step; refusals with their reasons; SEQ REC held while playing + a note: a transpose; tapped:
//                         stop and record
//   grid_playing_edits    40 grid edits while the sequence plays with Live: the core's sequence the grid's, the step
//                         playing inside it; stopped: every note-on has its note-off, the output silent
// Every library file is in a temporary directory, never ~/Music.
#include <atomic>
#include <chrono>
#include <cstdio>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <functional>
#include <map>
#include <set>
#include <thread>
#include <unistd.h>
#include "Processor.h"
#include "alloc_hook.h"
#include "ui/Editor.h"
extern "C" {
#include "ui/seq_rec.h"
#include "synth/clock.h"
}

using namespace knobi;
using namespace knobi::ui;

static int fails;
#define CHECK(c) do { if (!(c)) { std::printf("FAIL %s:%d %s\n", __FILE__, __LINE__, #c); fails++; } } while (0)

static std::string gTmp, gShots;

static std::string freshDir(const char* name)
{
    namespace fs = std::filesystem;
    fs::path d = fs::path(gTmp) / name;
    std::error_code ec;
    fs::remove_all(d, ec);
    fs::create_directories(d, ec);
    return d.string();
}
static std::vector<uint8_t> readFile(const std::string& path)
{
    std::ifstream f(path, std::ios::binary);
    return std::vector<uint8_t>((std::istreambuf_iterator<char>(f)), std::istreambuf_iterator<char>());
}
static void writeFile(const std::string& path, const std::vector<uint8_t>& b)
{
    std::ofstream f(path, std::ios::binary | std::ios::trunc);
    f.write((const char*)b.data(), (std::streamsize)b.size());
}
static int countFiles(const std::string& dir, const std::string& part)
{
    int n = 0;
    for (auto& e : std::filesystem::directory_iterator(dir)) n += e.path().filename().string().find(part) != std::string::npos;
    return n;
}
static bool waitFor(const std::function<bool()>& ok, int ms = 3000)
{
    for (int i = 0; i < ms; i++) {
        if (ok()) return true;
        std::this_thread::sleep_for(std::chrono::milliseconds(1));
    }
    return ok();
}

static uint32_t crc32(const uint8_t* d, size_t n)   // (IEEE, reflected: the library's)
{
    uint32_t c = 0xFFFFFFFFu;
    for (size_t i = 0; i < n; i++) {
        c ^= d[i];
        for (int k = 0; k < 8; k++) c = c & 1u ? (c >> 1) ^ 0xEDB88320u : c >> 1;
    }
    return ~c;
}

struct FakeHead final : juce::AudioPlayHead {
    bool playing = false;
    double bpm = 120.0, ppq = 0.0;
    juce::Optional<PositionInfo> getPosition() const override
    {
        PositionInfo i;
        i.setIsPlaying(playing);
        i.setBpm(bpm);
        i.setPpqPosition(ppq);
        return i;
    }
};

// the processor driven as a host does, block by block (the message thread here: the tests own the instance)
struct Host {
    Processor& p;
    FakeHead head;
    int block;
    juce::AudioBuffer<float> buf;
    juce::MidiBuffer midi;
    Host(Processor& pp, int b = 512) : p(pp), block(b), buf(2, b)
    {
        p.setPlayHead(&head);
        p.setPlayConfigDetails(0, 2, 44100.0, b);
        p.prepareToPlay(44100.0, b);
        midi.ensureSize(1024);
    }
    ~Host() { p.setPlayHead(nullptr); }
    void run(int blocks)
    {
        for (int i = 0; i < blocks; i++) {
            midi.clear();
            p.processBlock(buf, midi);
            if (head.playing) head.ppq += block * head.bpm / 60.0 / 44100.0;
        }
    }
};

struct GestureSpy : juce::AudioProcessorListener {
    int begins = 0, ends = 0, changes = 0;
    void audioProcessorParameterChanged(juce::AudioProcessor*, int, float) override { changes++; }
    void audioProcessorChanged(juce::AudioProcessor*, const ChangeDetails&) override {}
    void audioProcessorParameterChangeGestureBegin(juce::AudioProcessor*, int) override { begins++; }
    void audioProcessorParameterChangeGestureEnd(juce::AudioProcessor*, int) override { ends++; }
};

static void makeProgram(int seed, program_t& p, seq_t& s, const char* name)
{
    program_init(&p);
    std::memset(p.name, 0, sizeof p.name);
    std::strncpy(p.name, name, sizeof p.name);
    p.v[P_FLT_RESO] = (int16_t)(10 + seed), p.v[P_VCO1_SHAPE] = (int16_t)(seed * 3);
    seq_clear(&s);
    const uint8_t a[1] = {(uint8_t)(48 + seed % 12)}, va[1] = {100};
    seq_rec_step(&s, a, va, 1);
}

// the mouse, as JUCE delivers it to a component (offscreen: called directly)
static juce::MouseEvent mouseAt(juce::Component& c, juce::Point<int> p)
{
    return juce::MouseEvent(juce::Desktop::getInstance().getMainMouseSource(), p.toFloat(), {}, 0.0f, 0.0f, 0.0f, 0.0f,
                            0.0f, &c, &c, juce::Time(), p.toFloat(), juce::Time(), 1, false);
}
static void click(juce::Component& c, juce::Point<int> p)
{
    c.mouseDown(mouseAt(c, p));
    c.mouseUp(mouseAt(c, p));
}
static void drag(juce::Component& c, juce::Point<int> a, juce::Point<int> b)
{
    c.mouseDown(mouseAt(c, a));
    c.mouseDrag(mouseAt(c, {(a.x + b.x) / 2, (a.y + b.y) / 2}));
    c.mouseDrag(mouseAt(c, b));
    c.mouseUp(mouseAt(c, b));
}
static seq_t demoSeq();
static void loadSeq(Processor& p, Host& h, const seq_t& s, int number);

static std::unique_ptr<Editor> makeEditor(Processor& p)
{
    auto e = std::unique_ptr<Editor>(static_cast<Editor*>(p.createEditor()));
    e->poll(true);
    return e;
}

// ---------------------------------------------------------------------------------------------------------------
static void panel_covers_params()
{
    Processor p;
    auto ed = makeEditor(p);
    std::map<int, int> count;
    int wrongKind = 0, others = 0;
    std::function<void(juce::Component&)> walk = [&](juce::Component& c) {
        if (auto* k = dynamic_cast<Control*>(&c)) {
            if (k->paramId() >= 0) {
                count[k->paramId()]++;
                const auto* e = p.params().byId((uint16_t)k->paramId());
                if (!e) { others++; return; }
                auto& b = k->binding();
                bool ok = b.min == e->info.min && b.max == e->info.max;
                if (e->info.kind == KC_KIND_SWITCH) ok = ok && dynamic_cast<Switch*>(k);
                else if (e->info.kind == KC_KIND_SELECT) {
                    ok = ok && dynamic_cast<Select*>(k) && b.labels.size() == e->info.max - e->info.min + 1;
                    for (int i = 0; ok && i < b.labels.size(); i++) ok = b.labels[i] == juce::String(e->info.labels[i]);
                } else ok = ok && (k->paramId() == P_BPM ? dynamic_cast<SegValue*>(k) != nullptr : dynamic_cast<Knob*>(k) != nullptr);
                if (!ok) { std::printf("panel_covers_params: control for %d is not of its kind\n", k->paramId()); wrongKind++; }
            }
        }
        for (auto* ch : c.getChildren()) walk(*ch);
    };
    walk(*ed);
    int missing = 0, doubled = 0;
    for (int i = 0; i < p.params().size(); i++) {
        const int id = p.params().entry(i).id;
        if (count[id] == 0) { std::printf("panel_covers_params: %d (%s) has no control\n", id, p.params().entry(i).info.name); missing++; }
        if (count[id] > 1) { std::printf("panel_covers_params: %d has %d controls\n", id, count[id]); doubled++; }
    }
    // the spec's sections, all on the panel at once
    std::set<juce::String> titles;
    for (auto& s : ed->panel().sections()) titles.insert(s.first);
    int sec = 0;
    for (const char* t : {"VCO 1", "VCO 2", "MIXER", "FILTER", "ENVELOPES", "LFO", "X-MOD", "AFTERTOUCH", "MISC",
                          "EFFECTS", "ARPEGGIATOR", "CLOCK", "SEQUENCER", "PROGRAM"})
        sec += titles.count(t) ? 1 : 0;
    std::printf("panel_covers_params: %d parameters, %d controls; missing %d, doubled %d, wrong kind %d, not a "
                "parameter %d; sections %d of 14\n", p.params().size(), (int)ed->panel().controls().size(), missing,
                doubled, wrongKind, others, sec);
    CHECK(p.params().size() == 73 && missing == 0 && doubled == 0 && wrongKind == 0 && others == 0 && sec == 14);
    CHECK((int)ed->panel().controls().size() == p.params().size());
}

static void panel_roundtrip()
{
    Processor p;
    Host h(p);
    auto ed = makeEditor(p);
    GestureSpy spy;
    p.addListener(&spy);
    int hostToUi = 0, uiToHost = 0, uiToCore = 0, gestures = 0, n = 0;
    for (auto* c : ed->panel().controls()) {
        const auto* e = p.params().byId((uint16_t)c->paramId());
        const int32_t lo = e->info.min, hi = e->info.max, mid = lo + (hi - lo) / 3;
        n++;
        // host -> control
        bool ok = true;
        for (int32_t v : {hi, lo, mid}) {
            e->param->setValueNotifyingHost(Params::toNormalised(e->info, v));
            ed->poll();
            ok = ok && c->shown() == v;
        }
        hostToUi += ok;
        // control -> host -> core
        ok = true;
        bool core = true;
        for (int32_t v : {hi, lo, mid == lo ? hi : mid}) {
            const int b0 = spy.begins, e0 = spy.ends;
            c->simulateUser(v);
            ok = ok && Params::toCore(e->info, e->param->getValue()) == v && c->shown() == v;
            gestures += spy.begins == b0 + 1 && spy.ends == e0 + 1;
            h.run(1);
            core = core && kc_param_get(p.runner().core(), (uint16_t)c->paramId()) == v;
        }
        if (!ok || !core) std::printf("panel_roundtrip: %d (%s) ui->host %d ->core %d\n", c->paramId(), e->info.name, ok, core);
        uiToHost += ok, uiToCore += core;
    }
    p.removeListener(&spy);
    std::printf("panel_roundtrip: %d controls: host->control %d, control->host %d, ->core %d, one gesture each %d of %d\n",
                n, hostToUi, uiToHost, uiToCore, gestures, 3 * n);
    CHECK(n == 73 && hostToUi == n && uiToHost == n && uiToCore == n && gestures == 3 * n);
}

static void panel_resizable()
{
    Processor p;
    p.editorScale = 1.0f;
    auto ed = makeEditor(p);
    const int W = Panel::kWidth, H = Panel::kHeight;
    const bool defaultOk = ed->getWidth() == W && ed->getHeight() == H && W <= 1440 - 80 && H <= 900 - 100;
    auto* con = ed->getConstrainer();
    const bool limits = ed->isResizable() && con && con->getMinimumWidth() == W && con->getMaximumWidth() == 2 * W &&
                        con->getMinimumHeight() == H && con->getMaximumHeight() == 2 * H &&
                        std::fabs(con->getFixedAspectRatio() - (double)W / H) < 1e-9;
    std::map<Control*, juce::Rectangle<int>> base;
    for (auto* c : ed->panel().controls()) base[c] = ed->getLocalArea(c, c->getLocalBounds());
    // inside the window and its section, no two overlapping
    int outside = 0, overlaps = 0, noSection = 0;
    const auto& ctl = ed->panel().controls();
    for (size_t i = 0; i < ctl.size(); i++) {
        const auto r = ctl[i]->getBounds();
        if (!juce::Rectangle<int>(0, 0, W, H).contains(r)) outside++;
        bool inSection = false;
        for (auto& s : ed->panel().sections()) inSection = inSection || s.second.contains(r);
        if (!inSection) { std::printf("panel_resizable: %s outside its section\n", ctl[i]->getName().toRawUTF8()); noSection++; }
        for (size_t j = i + 1; j < ctl.size(); j++)
            if (r.intersects(ctl[j]->getBounds())) {
                std::printf("panel_resizable: %s overlaps %s\n", ctl[i]->getName().toRawUTF8(), ctl[j]->getName().toRawUTF8());
                overlaps++;
            }
    }
    int moved = 0;
    for (double s : {1.5, 2.0}) {
        ed->setSize((int)std::lround(W * s), (int)std::lround(H * s));
        for (auto* c : ed->panel().controls()) {
            const auto r = ed->getLocalArea(c, c->getLocalBounds());
            const auto want = base[c].toDouble() * s;
            if (std::fabs(r.getX() - want.getX()) > 1 || std::fabs(r.getY() - want.getY()) > 1 ||
                std::fabs(r.getWidth() - want.getWidth()) > 1 || std::fabs(r.getHeight() - want.getHeight()) > 1)
                moved++;
        }
    }
    const float kept = p.editorScale.load();
    // the constrainer: beyond the limits is clamped, the aspect kept
    ed->setBoundsConstrained({0, 0, W * 3, H * 3});
    const bool clampHi = ed->getWidth() == 2 * W && ed->getHeight() == 2 * H;
    ed->setBoundsConstrained({0, 0, W / 2, H / 2});
    const bool clampLo = ed->getWidth() == W && ed->getHeight() == H;
    ed->setBoundsConstrained({0, 0, (int)(W * 1.25), H * 2});
    const bool aspect = std::abs(ed->getWidth() * H - ed->getHeight() * W) <= W + H;
    // a new editor opens at the instance's last size
    ed->setSize(W * 3 / 2, H * 3 / 2);
    ed.reset();
    auto ed2 = makeEditor(p);
    const bool reopened = ed2->getWidth() == W * 3 / 2;
    std::printf("panel_resizable: default %dx%d %d; limits %d; outside %d, overlaps %d, not in a section %d; moved at "
                "150/200 %% %d; scale kept %.2f; clamped %d %d, aspect %d; reopened at 150 %% %d\n", W, H, defaultOk,
                limits, outside, overlaps, noSection, moved, kept, clampHi, clampLo, aspect, reopened);
    CHECK(defaultOk && limits && outside == 0 && overlaps == 0 && noSection == 0 && moved == 0 && kept == 2.0f &&
          clampHi && clampLo && aspect && reopened);
}

static void panel_browser()
{
    const std::string dir = freshDir("browser"), path = dir + "/library.knobi";
    Processor p;
    p.attachLibrary(std::make_shared<Library>(path, 0));
    Host h(p);
    auto ed = makeEditor(p);
    auto& b = ed->panel().browser();
    p.setCurrentProgram(237);
    ed->poll(true);
    const bool shows = b.bankText() == "2" && b.programText() == "37" && b.name() == "INIT" && !b.editedLit() &&
                       !b.differsLit() && !b.userLit();
    // an edit: "edited" (the program now differs from the bank's)
    ed->panel().controlFor(P_FLT_RESO)->simulateUser(99);
    h.run(2);
    ed->poll(true);
    const bool edited = b.editedLit() && b.differsLit();
    // tens + ones in the bank shown
    b.digit(4).press();
    const bool half = b.programText() == "4-" && p.getCurrentProgram() == 237;
    b.digit(5).press();
    ed->poll(true);
    const bool two = p.getCurrentProgram() == 245 && b.programText() == "45" && !b.editedLit();
    // BANK + a digit: the same tens / ones in that bank; a digit past bank 4 refused
    b.bankPad().press();
    const bool bankWait = b.bankText() == "-";
    b.digit(7).press();
    const bool refused = p.getCurrentProgram() == 245 && b.message().contains("BANKS 0-4");
    b.digit(0).press();
    ed->poll(true);
    const bool bank = p.getCurrentProgram() == 45 && b.bankText() == "0" && b.programText() == "45";
    b.nextPad().press(), b.nextPad().press(), b.prevPad().press();
    ed->poll(true);
    const bool steps = p.getCurrentProgram() == 46;
    b.selected();
    // the bank's program of the number in play changed (another instance's WRITE): "differs from bank"
    program_t q;
    seq_t s;
    makeProgram(3, q, s, "OTHER");
    Library(path, 0).write(46, q, s);
    p.library()->reload();
    ed->poll(true);
    const bool differs = b.differsLit() && !b.editedLit() && b.userLit() && b.name() == "INIT";
    std::printf("panel_browser: 237 shown %d; edited %d; 4 5 -> 245 %d %d; BANK 7 refused %d, BANK 0 -> 045 %d %d; "
                "> > < -> 046 %d; the bank's 46 changed: differs, user slot %d\n", shows, edited, half, two, refused,
                bankWait, bank, steps, differs);
    CHECK(shows && edited && half && two && bankWait && refused && bank && steps && differs);
}

static void panel_write()
{
    const std::string dir = freshDir("write"), path = dir + "/library.knobi";
    Processor p;
    p.attachLibrary(std::make_shared<Library>(path, 0));
    Host h(p);
    auto ed = makeEditor(p);
    auto& b = ed->panel().browser();
    p.setCurrentProgram(12);
    ed->panel().controlFor(P_FLT_RESO)->simulateUser(77);
    h.run(2);
    ed->poll(true);
    // WRITE, 4 5, WRITE: stored in 45 (nothing loaded meanwhile), the instance plays 45, not edited
    b.writePad().press();
    const bool armed = b.mode() == ProgramBrowser::Mode::WriteArmed && b.writePad().lit() && b.selected() == 12;
    b.digit(4).press(), b.digit(5).press();
    const bool noLoad = p.getCurrentProgram() == 12 && b.selected() == 45 && p.edited();
    b.writePad().press();
    ed->poll(true);
    auto snap = Library(path, 0).snapshot();
    const bool stored = snap->bank.used[45] && snap->bank.program[45].v[P_FLT_RESO] == 77 && p.getCurrentProgram() == 45 &&
                        !p.edited() && b.message() == "PROGRAM 045 STORED" && b.mode() == ProgramBrowser::Mode::Normal &&
                        b.userLit();
    // CANCEL: nothing written
    const auto before = readFile(path);
    b.writePad().press(), b.digit(9).press(), b.digit(9).press(), b.cancelPad().press();
    const bool cancelled = readFile(path) == before && b.mode() == ProgramBrowser::Mode::Normal;
    // kStoredOnly: a load arrives while the file is written
    Processor::testHooks.afterSnapshot = [](Processor& q) { q.setCurrentProgram(7); };
    b.writePad().press(), b.digit(3).press(), b.digit(0).press(), b.writePad().press();
    Processor::testHooks.afterSnapshot = nullptr;
    const bool storedOnly = b.message() == "STORED IN 030; THE INSTANCE MOVED ON TO PROGRAM 007";
    // kNewer: a newer version's library (minReader 2): refused, the file untouched
    {
        const std::string d2 = freshDir("newer"), p2 = d2 + "/library.knobi";
        Library::Data d;
        auto bytes = Library::encode(d);
        bytes[6] = 2;                              // minReader 2 (the CRC made right below)
        const uint32_t crc = crc32(bytes.data(), bytes.size() - 4);
        for (int i = 0; i < 4; i++) bytes[bytes.size() - 4 + (size_t)i] = (uint8_t)(crc >> (8 * i));
        writeFile(p2, bytes);
        p.attachLibrary(std::make_shared<Library>(p2, 0));
        ed->poll(true);
        const bool notice = ed->panel().notice().contains("NEWER");
        b.writePad().press(), b.writePad().press();
        const bool newer = b.message() == "LIBRARY FROM A NEWER VERSION - NOT WRITTEN" && !b.confirming() &&
                           readFile(p2) == bytes && p.libraryNewer();
        std::printf("panel_write: newer: notice %d, message %d\n", notice, newer);
        CHECK(notice && newer);
    }
    // kIoError: a library that cannot be written (its directory is a file)
    {
        const std::string d3 = freshDir("ioerr");
        writeFile(d3 + "/notadir", {1, 2, 3});
        p.attachLibrary(std::make_shared<Library>(d3 + "/notadir/library.knobi", 0));
        b.writePad().press(), b.writePad().press();
        const bool io = b.message() == "WRITE FAILED - NOTHING CHANGED";
        std::printf("panel_write: io error message %d (%s)\n", io, b.message().toRawUTF8());
        CHECK(io);
    }
    // an unreadable library: the first WRITE asks; CANCEL: nothing written; WRITE ANYWAY: backup kept, written
    bool asked = false, keptOnNo = false, wrote = false;
    {
        const std::string d4 = freshDir("unreadable"), p4 = d4 + "/library.knobi";
        const std::vector<uint8_t> junk = {'K', 'N', '6', 'L', 9, 9, 9, 9, 1, 2, 3};
        writeFile(p4, junk);
        p.attachLibrary(std::make_shared<Library>(p4, 0));
        ed->poll(true);
        const bool notice = ed->panel().notice().contains("UNREADABLE");
        b.writePad().press(), b.digit(1).press(), b.digit(1).press(), b.writePad().press();
        asked = b.confirming() && readFile(p4) == junk && countFiles(d4, ".bad-") == 0;
        b.confirmNo().press();
        keptOnNo = !b.confirming() && readFile(p4) == junk && countFiles(d4, ".bad-") == 0 &&
                   b.message() == "NOTHING WRITTEN";
        b.writePad().press(), b.digit(1).press(), b.digit(1).press(), b.writePad().press();
        const bool askedAgain = b.confirming();
        b.confirmYes().press();
        auto s4 = Library(p4, 0).snapshot();
        wrote = askedAgain && !s4->unreadable && s4->bank.used[11] && countFiles(d4, ".bad-") == 1 &&
                b.message().startsWith("PROGRAM 011 STORED") && b.message().contains("BAD-") && notice;
    }
    std::printf("panel_write: armed %d, no load while choosing %d, stored 045 %d (%s); cancel %d; stored only %d (%s); "
                "unreadable: asked %d, cancel keeps it %d, confirmed: backup + written %d\n", armed, noLoad, stored,
                b.message().toRawUTF8(), cancelled, storedOnly, storedOnly ? "" : "", asked, keptOnNo, wrote);
    CHECK(armed && noLoad && stored && cancelled && storedOnly && asked && keptOnNo && wrote);
}

static void panel_restore()
{
    const std::string dir = freshDir("restore"), path = dir + "/library.knobi";
    Processor p;
    p.attachLibrary(std::make_shared<Library>(path, 0));
    Host h(p);
    program_t q;
    seq_t s;
    makeProgram(5, q, s, "MINE");
    p.library()->write(20, q, s);
    p.setCurrentProgram(20);
    auto ed = makeEditor(p);
    auto& b = ed->panel().browser();
    const bool mine = b.name() == "MINE" && b.userLit();
    b.restorePad().press();
    ed->poll(true);
    const bool restored = b.name() == "INIT" && b.editedLit() && b.differsLit() && p.edited() &&
                          Library(path, 0).snapshot()->bank.used[20] && b.message().contains("FACTORY PROGRAM 020");
    b.writePad().press(), b.writePad().press();
    ed->poll(true);
    const bool emptied = !Library(path, 0).snapshot()->bank.used[20] && !b.userLit() && !b.editedLit() && !b.differsLit();
    std::printf("panel_restore: user program shown %d; restored (edited, slot kept) %d; WRITE empties the slot %d\n",
                mine, restored, emptied);
    CHECK(mine && restored && emptied);
}

static void panel_clock()
{
    Processor p;
    Host h(p);
    auto ed = makeEditor(p);
    auto& bpm = ed->panel().bpm();
    bpm.simulateUser(97);
    h.run(4);
    ed->poll();
    const bool stopped = !bpm.greyed() && bpm.displayText().trim() == "97" && kc_param_get(p.runner().core(), P_BPM) == 97;
    h.head.playing = true, h.head.bpm = 133.0;
    h.run(8);
    ed->poll();
    const bool playing = bpm.greyed() && bpm.displayText() == "133";
    h.head.playing = false;
    h.run(4);
    ed->poll();
    const bool back = !bpm.greyed() && bpm.displayText().trim() == "97";
    auto* div = dynamic_cast<Select*>(ed->panel().controlFor(P_CLK_DIV));
    const bool divs = div && div->binding().labels.size() == 10 && div->binding().labels[6] == "16TH";
    std::printf("panel_clock: stopped: P_BPM 97 shown %d; Live playing at 133: greyed, 133 shown %d; stopped again %d; "
                "divisions %d\n", stopped, playing, back, divs);
    CHECK(stopped && playing && back && divs);
}

static void panel_sequencer()
{
    Processor p;
    Host h(p);
    program_t q;
    seq_t s;
    makeProgram(2, q, s, "SEQ");
    kc_program_set(p.runner().core(), 3, &q, &s, 0);
    h.run(4);                                     // (the load taken)
    auto ed = makeEditor(p);
    auto& seq = ed->panel().sequencer();
    ed->poll();
    const bool off = seq.stateText() == "OFF" && !seq.playPad().lit();
    seq.playPad().press();
    h.run(2);
    ed->poll();
    const bool armed = seq.playPad().lit() && seq.stateText() == "WAITING FOR LIVE" && p.seqPlaying();
    if (!armed) std::printf("panel_sequencer: lit %d state %d playing %d\n", seq.playPad().lit(), p.seqState(), p.seqPlaying());
    h.head.playing = true;
    h.run(4);
    ed->poll();
    const bool playing = seq.stateText() == "PLAYING";
    h.head.playing = false;
    h.run(2);
    ed->poll();
    const bool waits = seq.stateText() == "WAITING FOR LIVE";
    seq.playPad().press();
    h.run(2);
    ed->poll();
    const bool stopped = seq.stateText() == "OFF" && !seq.playPad().lit() && !p.seqPlaying();
    const bool rec = seq.recPad().isVisible() && seq.recPad().isEnabled();
    auto& g = seq.grid();
    const bool slot = g.getWidth() == SequencerSection::kGridW && g.getHeight() == SequencerSection::kGridH &&
                      seq.getLocalBounds().contains(g.getBounds()) && g.getComponentID() == "seqGrid";
    std::printf("panel_sequencer: off %d; SEQ PLAY, Live stopped: waiting %d; Live plays: playing %d; Live stops: "
                "waiting %d; SEQ PLAY off %d; SEQ REC there, enabled %d; grid slot %dx%d %d\n", off, armed, playing,
                waits, stopped, rec, g.getWidth(), g.getHeight(), slot);
    CHECK(off && armed && playing && waits && stopped && rec && slot);
}

static void panel_globals()
{
    const std::string dir = freshDir("globals"), path = dir + "/library.knobi";
    Processor a, other;
    a.attachLibrary(std::make_shared<Library>(path, 0));
    other.attachLibrary(std::make_shared<Library>(path, 0));
    auto ed = makeEditor(a);
    auto& gv = ed->panel().globals();
    const bool hidden = !gv.isVisible();
    ed->panel().globalsPad().press();
    const bool shown = gv.isVisible();
    gv.control(G_TUNE).simulateUser(-13);
    gv.control(G_TRANSPOSE).simulateUser(5);
    gv.control(G_VEL_CURVE).simulateUser(3);
    gv.control(G_AT_CURVE).simulateUser(2);
    gv.control(G_STEREO).simulateUser(1);
    auto snap = Library(path, 0).snapshot();
    const bool inFile = snap->globals[G_TUNE] == -13 && snap->globals[G_TRANSPOSE] == 5 && snap->globals[G_VEL_CURVE] == 3 &&
                        snap->globals[G_AT_CURVE] == 2 && snap->globals[G_STEREO] == 1;
    const bool here = a.global(G_TUNE) == -13 && a.global(G_STEREO) == 1;
    other.library()->reload();
    const bool there = other.global(G_TUNE) == -13 && other.global(G_TRANSPOSE) == 5 && other.global(G_STEREO) == 1;
    const bool hint = GlobalsView::hint().contains("AU RECOMMENDED IN LIVE");
    // opened again: shows the values the library holds
    gv.closePad().press();
    const bool closed = !gv.isVisible();
    ed->panel().globalsPad().press();
    const bool reread = gv.control(G_TUNE).shown() == -13 && gv.control(G_VEL_CURVE).shown() == 3;
    std::printf("panel_globals: hidden %d, opened %d; in the file %d, this instance %d, another %d; hint %d; closed %d, "
                "reopened shows them %d\n", hidden, shown, inFile, here, there, hint, closed, reread);
    CHECK(hidden && shown && inFile && here && there && hint && closed && reread);
}

static void panel_rt()
{
    Processor p;
    Host h(p, 256);
    auto ed = makeEditor(p);
    std::atomic<bool> stop{false}, armed{false};
    std::atomic<long> allocs{0}, locks{0}, blocks{0};
    std::thread audio([&] {
        juce::AudioBuffer<float> buf(2, 256);
        juce::MidiBuffer midi;
        midi.ensureSize(256);
        for (int i = 0; i < 20; i++) p.processBlock(buf, midi);   // (warm)
        alloc_hook_arm_thread(1);
        armed = true;
        while (!stop) {
            midi.clear();
            p.processBlock(buf, midi);
            blocks++;
            std::this_thread::sleep_for(std::chrono::microseconds(300));
        }
        alloc_hook_arm_thread(0);
        allocs = alloc_hook_allocs(), locks = alloc_hook_locks();
    });
    while (!armed) std::this_thread::yield();
    const auto t0 = std::chrono::steady_clock::now();
    int i = 0;
    // (by count, not time: a slow or background-QoS machine runs the same work)
    while ((blocks < 200 || i < 300) && std::chrono::steady_clock::now() - t0 < std::chrono::seconds(60)) {
        auto& ctl = ed->panel().controls();
        ctl[(size_t)(i % (int)ctl.size())]->simulateUser(i % 3);
        ed->poll(i % 10 == 0);
        if (i % 40 == 0) ed->panel().browser().nextPad().press();
        if (i % 50 == 0) ed->panel().sequencer().playPad().press();
        auto& sq = ed->panel().sequencer();       // (plan Task 8: the grid's edits, the step entry's buttons)
        if (i % 30 == 7)
            sq.grid().edit([i](seq_t& s) {
                if (!s.len) return seqedit::setLength(s, 8);
                return seqedit::toggleNote(s, i % s.len, 48 + i % 24);
            });
        if (i % 60 == 13) sq.grid().edit([i](seq_t& s) { return seqedit::setLength(s, 1 + i % 20); });
        if (i % 90 == 21) {
            sq.recPad().press(), sq.recPad().release();
            sq.restPad().press(), sq.tiePad().press(), sq.backPad().press();
            sq.recPad().press(), sq.recPad().release();
        }
        p.syncHost();
        i++;
    }
    stop = true;
    audio.join();
    std::printf("panel_rt: %ld blocks while the panel made %d edits / polls: audio thread allocations %ld, locks %ld\n",
                blocks.load(), i, allocs.load(), locks.load());
    CHECK(blocks >= 200 && i >= 300 && allocs == 0 && locks == 0);
}

static void panel_idle()
{
    Processor p;
    Host h(p);
    p.setCurrentProgram(30);
    h.run(4);
    auto ed = makeEditor(p);
    auto& b = ed->panel().browser();
    ed->poll(true);
    const uint64_t r0 = p.access().runs(), s0 = b.snapshots();
    for (int i = 0; i < 60; i++) ed->poll();
    const uint64_t idleRuns = p.access().runs() - r0, idleSnaps = b.snapshots() - s0;
    // edited stays set (RESTORE FACTORY: forced), the edit decides "differs"
    b.restorePad().press();
    h.run(2);
    ed->poll();
    const bool start = b.editedLit() && !b.differsLit();
    auto* reso = p.params().byId(P_FLT_RESO);
    const int32_t was = p.params().hostValue(P_FLT_RESO);
    auto pollUntil = [&](bool want) {
        for (int i = 0; i <= ProgramBrowser::kParamTicks; i++) {
            ed->poll();
            if (b.differsLit() == want) return i;
        }
        return -1;
    };
    reso->param->setValueNotifyingHost(Params::toNormalised(reso->info, was + 50));
    h.run(2);
    const int on = pollUntil(true);
    const bool editedOn = b.editedLit();
    reso->param->setValueNotifyingHost(Params::toNormalised(reso->info, was));
    h.run(2);
    const int off = pollUntil(false);
    // a switch never polled toggles the parameter's value (on -> off), not from "unknown"
    auto* kb = p.params().byId(P_VCO2_KEYBOARD);
    Switch sw(std::make_unique<ParamBinding>(*kb), "KEYBOARD");
    const bool wasOn = p.params().hostValue(P_VCO2_KEYBOARD) == 1;
    sw.mouseDown(juce::MouseEvent(juce::Desktop::getInstance().getMainMouseSource(), {}, {}, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f,
                                  &sw, &sw, juce::Time(), {}, juce::Time(), 1, false));
    const bool toggled = wasOn && p.params().hostValue(P_VCO2_KEYBOARD) == 0;
    std::printf("panel_idle: 60 idle polls: %llu access() calls, %llu snapshots; restored: edited, not differing %d; an "
                "edit: differs after %d polls (edited %d), undone: not after %d polls; unpolled switch toggled %d\n",
                (unsigned long long)idleRuns, (unsigned long long)idleSnaps, start, on, editedOn, off, toggled);
    CHECK(idleRuns == 0 && idleSnaps == 0 && start && on >= 0 && editedOn && off >= 0 && toggled);
    // the grid (plan Task 8): a sequence shown and nothing changing: no access() call, no sequence read; Live playing
    // it: the playing step moves on the grid, still with no access() call (seqStep: an atomic); a recorded step read once
    auto& sec = ed->panel().sequencer();
    auto& g = sec.grid();
    loadSeq(p, h, demoSeq(), 31);
    ed->poll();
    const bool shown = g.length() == 20;
    uint64_t r1 = p.access().runs(), f1 = g.fetches();
    for (int i = 0; i < 60; i++) ed->poll();
    const uint64_t gridRuns = p.access().runs() - r1, gridFetches = g.fetches() - f1;
    sec.playPad().press();
    h.head.playing = true;
    h.run(2);
    ed->poll();
    r1 = p.access().runs(), f1 = g.fetches();
    std::set<int> steps;
    for (int i = 0; i < 60; i++) {
        h.run(2);
        ed->poll();
        for (int c = 0; c < 64; c++) if (g.cell(c).playing) steps.insert(c);
    }
    const uint64_t playRuns = p.access().runs() - r1, playFetches = g.fetches() - f1;
    h.head.playing = false;
    sec.playPad().press();
    h.run(2);
    click(sec.recPad(), sec.recPad().getLocalBounds().getCentre());
    ed->poll();
    f1 = g.fetches();
    h.midi.clear();
    h.midi.addEvent(juce::MidiMessage::noteOn(1, 60, (juce::uint8)100), 0);
    h.midi.addEvent(juce::MidiMessage::noteOff(1, 60), 100);
    p.processBlock(h.buf, h.midi);
    for (int i = 0; i < 10; i++) ed->poll();
    const uint64_t recFetches = g.fetches() - f1;
    click(sec.recPad(), sec.recPad().getLocalBounds().getCentre());
    std::printf("panel_idle: the grid (%d): 60 idle polls: %llu access() calls, %llu sequence reads; Live playing it, 60 "
                "polls: %zu steps shown, %llu access() calls, %llu reads; a recorded step: read %llu time(s)\n", shown,
                (unsigned long long)gridRuns, (unsigned long long)gridFetches, steps.size(), (unsigned long long)playRuns,
                (unsigned long long)playFetches, (unsigned long long)recFetches);
    CHECK(shown && gridRuns == 0 && gridFetches == 0 && steps.size() >= 8 && playRuns == 0 && playFetches == 0 &&
          recFetches == 1);
}

static void panel_screenshots()
{
    namespace fs = std::filesystem;
    const std::string dir = gShots.empty() ? freshDir("shots") : gShots;
    fs::create_directories(dir);
    // the library as it would be on a Mac (~/Music/KNOBI-SIX/library.knobi), in a temporary home: never the real one
    const std::string home = freshDir("home");
    setenv("HOME", home.c_str(), 1);
    Processor p;
    p.attachLibrary(std::make_shared<Library>(home + "/Music/KNOBI-SIX/library.knobi", 0));
    Host h(p);
    program_t q;
    seq_t s;
    makeProgram(4, q, s, "PANEL DEMO");
    s = demoSeq();                                // (synthetic: the grid's demo sequence)
    q.v[P_CLK_DIV] = CLK_DIV_16TH;
    p.library()->write(6, q, s);
    p.setCurrentProgram(6);
    h.run(4);
    auto ed = makeEditor(p);
    ed->panel().sequencer().playPad().press();
    ed->panel().controlFor(P_FLT_FREQ)->simulateUser(120);
    h.head.playing = true;                        // (Live playing: the step playing framed red)
    h.run(30);
    ed->poll(true);
    bool ok = true;
    for (int pct : {100, 200}) {
        ed->setSize(Panel::kWidth * pct / 100, Panel::kHeight * pct / 100);
        ed->poll(true);
        const juce::Image img = ed->createComponentSnapshot(ed->getLocalBounds(), true, 1.0f);
        const std::string file = dir + "/panel-" + std::to_string(pct) + ".png";
        fs::remove(file);
        {
            juce::FileOutputStream out{juce::File(file)};
            juce::PNGImageFormat png;
            ok = ok && out.openedOk() && png.writeImageToStream(img, out);
        }
        ok = ok && img.getWidth() == Panel::kWidth * pct / 100 && fs::file_size(file) > 20000;
        std::printf("panel_screenshots: %s %dx%d\n", file.c_str(), img.getWidth(), img.getHeight());
    }
    // the STEP editor open on step 2 (the chord), 100 %
    ed->setSize(Panel::kWidth, Panel::kHeight);
    {
        auto& g = ed->panel().sequencer().grid();
        click(g, g.cellBounds(1).getCentre());
        const juce::Image img = ed->createComponentSnapshot(ed->getLocalBounds(), true, 1.0f);
        const std::string file = dir + "/panel-seqstep.png";
        fs::remove(file);
        juce::FileOutputStream out{juce::File(file)};
        ok = ok && g.editing() && juce::PNGImageFormat().writeImageToStream(img, out);
        g.editor().closePad().press();
    }
    // the GLOBALS view and the WRITE confirmation (100 %)
    ed->panel().globalsPad().press();
    {
        const juce::Image img = ed->createComponentSnapshot(ed->getLocalBounds(), true, 1.0f);
        juce::FileOutputStream out{juce::File(dir + "/panel-globals.png")};
        out.setPosition(0), out.truncate();
        ok = ok && juce::PNGImageFormat().writeImageToStream(img, out);
    }
    CHECK(ok);
}

// ---------------------------------------------------------------------------------------------------------------
// the sequencer grid and the step entry (plan Task 8)
// the demo sequence (synthetic: the screenshots and grid_shows): 20 steps, a note, a chord, its tie, a rest, 6 notes,
// then a line
static seq_t demoSeq()
{
    seq_t s;
    seq_clear(&s);
    auto step = [&s](std::initializer_list<int> notes, int vel) {
        uint8_t n[6], v[6];
        int c = 0;
        for (int x : notes) n[c] = (uint8_t)x, v[c] = (uint8_t)juce::jlimit(1, 127, vel - 12 * c), c++;
        seq_rec_step(&s, n, v, c);
    };
    step({60}, 110);
    step({60, 64, 67}, 100);
    seq_rec_tie(&s);
    seq_rec_rest(&s);
    step({48, 55, 60, 64, 67, 71}, 120);
    for (int i = 0; i < 15; i++) {
        if (i % 4 == 3) seq_rec_rest(&s);
        else step({50 + (i * 5) % 19}, 40 + i * 5);
    }
    return s;
}
static void loadSeq(Processor& p, Host& h, const seq_t& s, int number)
{
    program_t q;
    program_init(&q);
    std::strncpy(q.name, "GRID", sizeof q.name);
    q.v[P_CLK_DIV] = CLK_DIV_16TH;
    kc_program_set(p.runner().core(), number, &q, &s, 0);
    h.run(4);
}
static bool coreSeq(Processor& p, seq_t& s) { return p.seqGet(s); }
static bool storable(const seq_t& s)
{
    program_t q;
    program_init(&q);
    return kc_program_valid(&q, &s) == 0;
}

static void grid_shows()
{
    Processor p;
    Host h(p);
    const seq_t d = demoSeq();
    loadSeq(p, h, d, 3);
    auto ed = makeEditor(p);
    auto& sec = ed->panel().sequencer();
    auto& g = sec.grid();
    ed->poll();
    const bool len = g.length() == 20;
    const auto c0 = g.cell(0), c1 = g.cell(1), c2 = g.cell(2), c3 = g.cell(3), c4 = g.cell(4), c20 = g.cell(20);
    const bool cells = c0.text == "C4" && c0.count == 1 && c1.count == 3 && c1.text == "C4 E4 G4" && c2.tie &&
                       c2.count == 3 && c3.rest && !c3.tie && c4.count == 6 && c20.past && g.cell(63).past &&
                       !c0.past && !c0.rest && !c1.tie;
    if (!cells) std::printf("grid_shows: c0 '%s' %d, c1 '%s' %d, c2 tie %d, c3 rest %d, c4 %d, c20 past %d\n",
                            c0.text.toRawUTF8(), c0.count, c1.text.toRawUTF8(), c1.count, c2.tie, c3.rest, c4.count, c20.past);
    int playingCells = 0;
    for (int i = 0; i < 64; i++) playingCells += g.cell(i).playing;
    const bool idle = playingCells == 0;
    sec.playPad().press();
    h.head.playing = true;
    std::set<int> seen;
    bool agree = true;
    for (int i = 0; i < 60; i++) {
        h.run(2);
        ed->poll();
        int at = -1, n = 0;
        for (int c = 0; c < 64; c++) if (g.cell(c).playing) at = c, n++;
        agree &= n <= 1 && at == p.seqStep() && (at < 0 || at < 20);
        if (at >= 0) seen.insert(at);
    }
    h.head.playing = false;
    h.run(2);
    ed->poll();
    playingCells = 0;
    for (int i = 0; i < 64; i++) playingCells += g.cell(i).playing;
    std::printf("grid_shows: length %d (want 20) %d; cells (note, chord, tie, rest, 6 notes, past the end) %d; none "
                "playing when stopped %d; Live playing: %zu steps shown in turn, one at a time, the core's %d; Live "
                "stopped: none %d\n", g.length(), len, cells, idle, seen.size(), agree, playingCells == 0);
    CHECK(len && cells && idle && seen.size() >= 10 && agree && playingCells == 0);
}

static void grid_mouse()
{
    Processor p;
    Host h(p);
    seq_t s0;
    seq_clear(&s0);
    const uint8_t n60[1] = {60}, n62[1] = {62}, n64[1] = {64}, v[1] = {100};
    seq_rec_step(&s0, n60, v, 1), seq_rec_step(&s0, n62, v, 1), seq_rec_rest(&s0), seq_rec_step(&s0, n64, v, 1);
    loadSeq(p, h, s0, 12);
    auto ed = makeEditor(p);
    auto& g = ed->panel().sequencer().grid();
    auto& se = g.editor();
    ed->poll();
    const bool unedited = !p.edited();
    int ops = 0;
    bool allOk = true;
    seq_t c;
    auto after = [&](const char* what) {          // the core has what the grid shows, storable, edited
        h.run(1);
        ed->poll();
        coreSeq(p, c);
        const bool ok = std::memcmp(&c, &g.sequence(), sizeof c) == 0 && storable(c) && p.edited();
        if (!ok) std::printf("grid_mouse: after %s: same %d storable %d edited %d\n", what,
                             std::memcmp(&c, &g.sequence(), sizeof c) == 0, storable(c), p.edited());
        allOk &= ok;
        ops++;
    };
    auto key = [&](int note) { click(se, se.keyBounds(note).getCentre()); };
    click(g, g.cellBounds(1).getCentre());
    const bool opened = g.editing() && se.step() == 1 && g.cell(1).selected;
    key(64), after("add E4");
    const bool added = c.step[1].count == 2 && c.step[1].notes[1] == 64;
    for (int n : {65, 67, 69, 71}) key(n), after("add");
    key(72);
    h.run(1);
    coreSeq(p, c);
    const bool max6 = c.step[1].count == 6 && g.message() == "6 NOTES AT MOST";
    key(62), after("remove D4");
    const bool removed = c.step[1].count == 5 && c.step[1].notes[0] == 64;
    // velocity: the first note's bar dragged to the top, then low
    auto bar = se.barBounds(0);
    drag(se, bar.getCentre(), {bar.getCentreX(), bar.getY()}), after("velocity up");
    const int vTop = c.step[1].vels[0];
    drag(se, {bar.getCentreX(), bar.getY()}, {bar.getCentreX(), bar.getBottom() - 10}), after("velocity down");
    const int vLow = c.step[1].vels[0];
    click(se, se.removeBounds(0).getCentre()), after("X");
    const bool xed = c.step[1].count == 4 && c.step[1].notes[0] == 65;
    se.restPad().press(), after("REST");
    const bool rest = c.step[1].count == 0 && g.cell(1).rest;
    // TIE refused: step 1, and after a rest (step 3: step 2 is a rest), the reason shown, nothing changed
    click(g, g.cellBounds(0).getCentre());
    se.tiePad().press();
    const juce::String why1 = g.message();
    click(g, g.cellBounds(3).getCentre());
    se.tiePad().press();
    const juce::String why2 = g.message();
    h.run(1);
    seq_t before = c;
    coreSeq(p, c);
    const bool refused = why1 == "NO TIE ON STEP 1" && why2 == "NO TIE AFTER A REST" && std::memcmp(&c, &before, sizeof c) == 0;
    // a tie: step 3 (index 2) after step 2 (index 1) with a note; the copy follows step 2's notes; a rest before
    // it unties it (its notes kept)
    click(g, g.cellBounds(1).getCentre()), key(60), after("C4 on step 2");
    click(g, g.cellBounds(2).getCentre()), se.tiePad().press(), after("TIE");
    const bool tied = c.step[2].tie && c.step[2].count == 1 && c.step[2].notes[0] == 60 && g.cell(2).tie;
    click(g, g.cellBounds(1).getCentre()), key(67), after("G4 on step 2");
    const bool copy = c.step[2].tie && c.step[2].count == 2 && c.step[2].notes[1] == 67;
    se.restPad().press(), after("step 2 a rest");
    const bool untied = !c.step[2].tie && c.step[2].count == 2 && c.step[1].count == 0;
    // a velocity drag on a tied step: refused (it is the step before's)
    click(g, g.cellBounds(2).getCentre()), se.tiePad().press();   // (after a rest now: refused)
    const bool tieAfterRest = g.message() == "NO TIE AFTER A REST";
    // LENGTH: + / -, the number dragged, the ends
    se.closePad().press();
    click(g.lengthUp(), g.lengthUp().getLocalBounds().getCentre()), after("+");
    const bool up = c.len == 5 && c.step[4].count == 0 && g.cell(4).rest && g.cell(5).past;
    g.lengthDown().press(), g.lengthDown().press(), g.lengthDown().press(), after("- x3");
    const bool down = c.len == 2;
    const auto lb = g.lengthBounds();
    drag(g, lb.getCentre(), {lb.getCentreX(), lb.getCentreY() - 44}), after("drag up");
    const bool dragged = c.len == 13 && g.cell(12).rest && !g.cell(12).past;
    drag(g, lb.getCentre(), {lb.getCentreX(), lb.getCentreY() + 400}), after("drag down");
    const bool one = c.len == 1;
    g.lengthDown().press();
    h.run(1);
    coreSeq(p, c);
    const bool floor1 = c.len == 1;
    for (int i = 0; i < 70; i++) g.lengthUp().press();
    h.run(1);
    coreSeq(p, c);
    const bool ceil64 = c.len == 64 && storable(c);
    click(g, g.cellBounds(63).getCentre()), key(108), after("C8 on step 64");
    g.lengthDown().press(), after("63");
    click(g, g.cellBounds(63).getCentre());
    const bool pastMsg = !g.editing() && g.message().contains("PAST THE END");
    // WRITE: the edit stored with the program (the library has the grid's sequence)
    const auto wr = p.writeProgram(12);
    auto snap = Library(Library::defaultPath(), 0).snapshot();
    const bool stored = wr == Library::kOk && snap->bank.used[12] &&
                        std::memcmp(&snap->bank.seq[12], &g.sequence(), sizeof(seq_t)) == 0 && !p.edited();
    std::printf("grid_mouse: unedited at first %d; step 2 opened %d; E4 added %d; 6 notes max (a 7th refused) %d; D4 "
                "removed %d; velocity dragged to %d then %d; X %d; REST %d; TIE refused with its reason (step 1 / after "
                "a rest) %d; tied (the copy) %d, follows step 2 %d, untied by a rest before it %d (%d); LENGTH +1 %d, "
                "-3 %d, dragged to 13 %d, to 1 %d, not below 1 %d, not above 64 %d; past the end %d; WRITE stored it %d; "
                "%d edits: the core's = the grid's, storable, edited: %d\n",
                unedited, opened, added, max6, removed, vTop, vLow, xed, rest, refused, tied, copy, untied, tieAfterRest,
                up, down, dragged, one, floor1, ceil64, pastMsg, stored, ops, allOk);
    CHECK(unedited && opened && added && max6 && removed && vTop == 127 && vLow > 0 && vLow < 30 && xed && rest &&
          refused && tied && copy && untied && tieAfterRest && up && down && dragged && one && floor1 && ceil64 &&
          pastMsg && stored && allOk);
}

// the step entry from Live's MIDI: the same inputs given to the firmware's recorder (ui/seq_rec.h) give the same
// sequence; REST / TIE / BACK; the reasons; the grid read-only while recording; SEQ REC held while playing + a note: a
// transpose, tapped: stop and record
static seq_t gRef;
static int gRefMuts;
static seq_t* gRefMut() { gRefMuts++; return &gRef; }
static void grid_step_entry()
{
    Processor p;
    Host h(p);
    seq_t old;
    seq_clear(&old);
    const uint8_t n50[1] = {50}, v[1] = {90};
    for (int i = 0; i < 5; i++) seq_rec_step(&old, n50, v, 1);
    loadSeq(p, h, old, 3);
    auto ed = makeEditor(p);
    auto& sec = ed->panel().sequencer();
    auto& g = sec.grid();
    ed->poll();
    const bool padsOff = sec.recPad().isEnabled() && !sec.restPad().isEnabled() && !sec.tiePad().isEnabled();
    click(sec.recPad(), sec.recPad().getLocalBounds().getCentre());
    ed->poll();
    const bool armed = sec.stateText() == "RECORDING" && sec.recPad().lit() && sec.restPad().isEnabled() &&
                       g.length() == 0 && g.cell(0).next && !p.edited();
    gRef = old, gRefMuts = 0;
    seqrec_t r;
    seqrec_start(&r, gRefMut);
    auto note = [&](int n, int vel) {             // a MIDI note from Live, in a block of its own
        h.midi.clear();
        h.midi.addEvent(juce::MidiMessage::noteOn(1, n, (juce::uint8)vel), 17);
        if (!vel) h.midi.clear(), h.midi.addEvent(juce::MidiMessage::noteOff(1, n), 17);
        p.processBlock(h.buf, h.midi);
        seqrec_note(&r, n, vel);
    };
    auto rest = [&] { sec.restPad().press(); if (!r.n && !r.in) seqrec_inc(&r); };   // (held: refused)
    auto tie = [&] { sec.tiePad().press(); if (r.n || r.in) seqrec_inc(&r); };      // (held: INC)
    auto back = [&] { sec.backPad().press(); seqrec_dec(&r); };
    note(60, 100), note(60, 0);
    note(62, 90), note(64, 80), note(62, 0), note(64, 0);
    ed->poll();
    const bool shownWhileRec = g.length() == 2 && g.cell(1).count == 2 && g.cell(2).next;
    rest();
    note(67, 70), tie(), tie(), note(67, 0);
    back();
    note(72, 127), note(74, 1);
    ed->poll();
    const int held = KC_REC_HELD(p.seqRecState());
    rest();
    const juce::String restWhy = g.message();
    note(72, 0), note(74, 0);
    note(5, 60), note(5, 0);                      // (below 12: not recorded)
    // the grid is the step entry's while recording: a click says so, nothing changes
    click(g, g.cellBounds(0).getCentre());
    const bool readOnly = !g.editing() && g.message().startsWith("RECORDING");
    seq_t c;
    coreSeq(p, c);
    const bool same = std::memcmp(&c, &gRef, sizeof c) == 0 && storable(c);
    // the panel's TIE with nothing held: the last step held on; after a rest refused, the reason shown
    sec.tiePad().press();
    seq_rec_tie(&gRef);
    sec.restPad().press();
    seq_rec_rest(&gRef);
    sec.tiePad().press();
    const juce::String tieWhy = g.message();
    click(sec.recPad(), sec.recPad().getLocalBounds().getCentre());   // (ends it)
    seqrec_stop(&r);
    ed->poll();
    coreSeq(p, c);
    const bool same2 = std::memcmp(&c, &gRef, sizeof c) == 0 && storable(c) && p.edited() && gRefMuts > 0;
    const bool ended = sec.stateText() == "OFF" && !sec.recPad().lit() && !sec.tiePad().isEnabled() &&
                       std::memcmp(&g.shown(), &gRef, sizeof c) == 0;
    std::printf("grid_step_entry: SEQ REC enabled, REST/TIE/BACK only while recording %d; armed: RECORDING, shown empty "
                "(the next step 1 marked), not edited yet %d; steps shown as recorded %d; %d held: REST refused '%s'; "
                "the grid read-only %d; the firmware's recorder's sequence %d (length %d); TIE with nothing held, REST, "
                "TIE refused '%s'; ended: the same %d, shown, state OFF %d\n",
                padsOff, armed, shownWhileRec, held, restWhy.toRawUTF8(), readOnly, same, c.len, tieWhy.toRawUTF8(),
                same2, ended);
    CHECK(padsOff && armed && shownWhileRec && held == 2 && restWhy.startsWith("KEYS HELD") && readOnly && same &&
          tieWhy == "NO TIE AFTER A REST" && same2 && ended);

    // transpose: SEQ REC held while the sequence plays with Live + a note: XP = the note - C4; let go: nothing
    sec.playPad().press();
    h.head.playing = true;
    h.run(8);
    sec.recPad().press();                         // (held)
    note(67, 100);
    h.run(2);
    note(67, 0);
    sec.recPad().release();
    h.run(2);
    ed->poll();
    const int xp = p.seqXp();
    const bool still = p.seqPlaying() && !(p.seqRecState() & KC_REC_ON) && sec.stateText() == "PLAYING";
    click(sec.recPad(), sec.recPad().getLocalBounds().getCentre());   // (tapped: stop, record)
    h.run(2);
    ed->poll();
    const bool tapped = !p.seqPlaying() && sec.stateText() == "RECORDING";
    click(sec.recPad(), sec.recPad().getLocalBounds().getCentre());
    h.head.playing = false;
    std::printf("grid_step_entry: SEQ REC held + G4 while playing: transpose %+d (want +7), still playing, not "
                "recording %d; tapped while playing: stopped, recording %d\n", xp, still, tapped);
    CHECK(xp == 7 && still && tapped);
}

// edits while the sequence plays with Live: every edit heard from the next step, nothing left sounding after the stop
static void grid_playing_edits()
{
    Processor p;
    Host h(p, 256);
    loadSeq(p, h, demoSeq(), 3);
    auto ed = makeEditor(p);
    auto& sec = ed->panel().sequencer();
    auto& g = sec.grid();
    ed->poll();
    sec.playPad().press();
    h.head.playing = true;
    h.run(10);
    int edits = 0, bad = 0, count[128] = {0}, unpaired = 0, logged = 0;
    kc_note_t nt[KC_LOG];
    auto take = [&] {                             // (the core's log of the sequencer's notes: drained each time)
        for (uint32_t m; (m = kc_notes(p.runner().core(), nt, KC_LOG));)
            for (uint32_t i = 0; i < m; i++, logged++) {
                count[nt[i].note] += nt[i].vel ? 1 : -1;
                if (count[nt[i].note] < 0 || count[nt[i].note] > 1) unpaired++;
            }
    };
    take();
    for (int i = 0; i < 40; i++) {
        const int st = i % juce::jmax(1, (int)g.sequence().len);
        bool ok = true;
        switch (i % 5) {
        case 0: ok = g.edit([st, i](seq_t& s) { return seqedit::toggleNote(s, st, 40 + (i * 7) % 40); }); break;
        case 1: ok = g.edit([st](seq_t& s) { return seqedit::makeRest(s, st); }); break;
        case 2: ok = g.edit([st](seq_t& s) { return seqedit::toggleTie(s, st); }); break;
        case 3: ok = g.edit([i](seq_t& s) { return seqedit::setLength(s, 1 + (i * 11) % 30); }); break;
        case 4: ok = g.edit([st](seq_t& s) { return seqedit::setVelocity(s, st, 0, 5 + st); }); break;
        }
        edits += ok;
        h.run(12 + i % 5);
        take();
        ed->poll();
        seq_t c;
        coreSeq(p, c);
        bad += !storable(c) || std::memcmp(&c, &g.sequence(), sizeof c) != 0 || p.seqStep() >= (int)c.len;
    }
    sec.playPad().press();
    h.run(4);
    take();
    int sounding = 0;
    for (int n : count) sounding += n != 0;
    h.head.playing = false;
    h.run((int)(3 * 44100 / 256));
    float peak = 0;
    for (int ch = 0; ch < 2; ch++) peak = juce::jmax(peak, h.buf.getMagnitude(ch, 0, 256));
    std::printf("grid_playing_edits: %d edits sent while playing (the core's sequence = the grid's, storable, the step "
                "inside it: %d bad); stopped: %d notes logged, %d sounding, %d unpaired; output after 3 s %.6f\n",
                edits, bad, logged, sounding, unpaired, peak);
    CHECK(edits >= 15 && logged > 30 && bad == 0 && sounding == 0 && unpaired == 0 && peak == 0.0f);
}

int main(int argc, char** argv)
{
    std::setvbuf(stdout, nullptr, _IONBF, 0);
    juce::ScopedJuceInitialiser_GUI juce;
    Processor::startHostSyncTimer = false;
    Editor::startPollTimer = false;
    gTmp = (std::filesystem::temp_directory_path() / ("knobi-six-editor-" + std::to_string((long)getpid()))).string();
    std::filesystem::create_directories(gTmp);
    Library::setDefaultPath(gTmp + "/default/library.knobi");   // (never ~/Music)
    if (argc > 2) gShots = argv[2];
    std::map<std::string, std::function<void()>> tests = {
        {"panel_covers_params", panel_covers_params}, {"panel_roundtrip", panel_roundtrip},
        {"panel_resizable", panel_resizable}, {"panel_browser", panel_browser}, {"panel_write", panel_write},
        {"panel_restore", panel_restore}, {"panel_clock", panel_clock}, {"panel_sequencer", panel_sequencer},
        {"panel_globals", panel_globals}, {"panel_rt", panel_rt}, {"panel_idle", panel_idle}, {"panel_screenshots", panel_screenshots},
        {"grid_shows", grid_shows}, {"grid_mouse", grid_mouse}, {"grid_step_entry", grid_step_entry},
        {"grid_playing_edits", grid_playing_edits},
    };
    if (argc > 1) {
        auto t = tests.find(argv[1]);
        if (t == tests.end()) { std::printf("no test %s\n", argv[1]); return 2; }
        t->second();
    } else
        for (auto& t : tests) t.second();
    std::error_code ec;
    std::filesystem::remove_all(gTmp, ec);
    if (fails) { std::printf("test_editor: %d FAILED\n", fails); return 1; }
    std::printf("test_editor: passed\n");
    return 0;
}
