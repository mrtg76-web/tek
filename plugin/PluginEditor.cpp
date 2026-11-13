#include "PluginEditor.h"

using tek::Btn;

namespace {
const Btn kLayout[6][5] = {
    {Btn::Back, Btn::Perf, Btn::Gen, Btn::Mod, Btn::Flow},
    {Btn::Arranger, Btn::Keyboard, Btn::Arp, Btn::Lfo, Btn::Random},
    {Btn::Step, Btn::Page, Btn::Track, Btn::Load, Btn::Save},
    {Btn::Copy, Btn::Paste, Btn::Init, Btn::End, Btn::Undo},
    {Btn::P16, Btn::P32, Btn::P48, Btn::P64, Btn::Mute},
    {Btn::Shift, Btn::Rec, Btn::Stop, Btn::Play, Btn::Count},
};
constexpr float kGridX = 482.0f, kGridY = 52.0f, kPitch = 50.0f, kPad = 44.0f;
constexpr float kSeqX = 420.0f;

juce::Colour col(uint32_t rgb) { return juce::Colour((juce::uint8) (rgb >> 16), (juce::uint8) (rgb >> 8), (juce::uint8) rgb); }
juce::Font mono(float h, bool bold = false) {
    return juce::Font(juce::FontOptions(juce::Font::getDefaultMonospacedFontName(), h, bold ? juce::Font::bold : juce::Font::plain));
}
juce::Font sans(float h, bool bold = false) { return juce::Font(juce::FontOptions(h, bold ? juce::Font::bold : juce::Font::plain)); }

struct KeyMap { int key; Btn btn; };
const KeyMap kKeys[] = {
    {'1', Btn::Seq1}, {'2', Btn::Seq2}, {'3', Btn::Seq3}, {'4', Btn::Seq4},
    {'5', Btn::Seq5}, {'6', Btn::Seq6}, {'7', Btn::Seq7}, {'8', Btn::Seq8},
    {'m', Btn::Mute}, {'c', Btn::Copy}, {'v', Btn::Paste}, {'i', Btn::Init}, {'o', Btn::End},
    {'t', Btn::Track}, {'p', Btn::Page}, {'z', Btn::Undo}, {'b', Btn::Back}, {'l', Btn::Load},
    {'x', Btn::Flow}, {'k', Btn::Step},
};
bool keyDown(int k) {
    return juce::KeyPress::isKeyCurrentlyDown(k) || juce::KeyPress::isKeyCurrentlyDown((int) juce::CharacterFunctions::toUpperCase((juce::juce_wchar) k));
}
} // namespace

TekEditor::TekEditor(TekProcessor& p) : AudioProcessorEditor(&p), proc(p) {
    setWantsKeyboardFocus(true);
    setResizable(true, true);
    setResizeLimits(650, 250, 2600, 1000);
    if (auto* c = getConstrainer()) c->setFixedAspectRatio(kW / kH);
    setSize(1300, 500);
    startTimerHz(30);
}

TekEditor::~TekEditor() { stopTimer(); }

void TekEditor::resized() { scale = (float) getWidth() / kW; }

juce::Point<float> TekEditor::toDesign(juce::Point<float> p) const { return p / scale; }

juce::Rectangle<float> TekEditor::padRect(int x, int y) const { return {kGridX + x * kPitch, kGridY + y * kPitch, kPad, kPad}; }

juce::Rectangle<float> TekEditor::encoderRect(int i) const { return {40.0f + i * 93.0f, 162.0f, 62.0f, 62.0f}; }

juce::Rectangle<float> TekEditor::buttonRect(Btn b) const {
    if (b >= Btn::Seq1 && b <= Btn::Seq8) {
        int r = (int) b - (int) Btn::Seq1;
        return {kSeqX, kGridY + r * kPitch, kPad, kPad};
    }
    for (int r = 0; r < 6; ++r)
        for (int c = 0; c < 5; ++c)
            if (kLayout[r][c] == b) return {24.0f + c * 75.0f, 238.0f + r * 41.0f, 71.0f, 36.0f};
    return {};
}

TekEditor::Hit TekEditor::hitAt(juce::Point<float> p) const {
    for (int y = 0; y < 8; ++y)
        for (int x = 0; x < 16; ++x)
            if (padRect(x, y).expanded(3).contains(p)) return {HitKind::Pad, x, y};
    for (int i = 0; i < 4; ++i)
        if (encoderRect(i).contains(p)) return {HitKind::Encoder, i, 0};
    for (int b = 0; b < tek::kNumBtns; ++b)
        if (buttonRect((Btn) b).contains(p)) return {HitKind::Button, b, 0};
    return {};
}

// ---------------------------------------------------------------------------- mouse
void TekEditor::mouseDown(const juce::MouseEvent& e) {
    grabKeyboardFocus();
    auto p = toDesign(e.position);
    Hit h = hitAt(p);
    const double t = now();
    if (e.mods.isPopupMenu()) { // right click / ctrl-click: latch a "held" pad or button
        if (h.kind == HitKind::Pad) {
            auto it = std::find(latchedPads.begin(), latchedPads.end(), std::make_pair(h.a, h.b));
            withLock([&](tek::Controller& c) {
                if (it != latchedPads.end()) { c.padUp(h.a, h.b, t + 10000.0); latchedPads.erase(it); }
                else { c.padDown(h.a, h.b, t); latchedPads.push_back({h.a, h.b}); }
            });
        } else if (h.kind == HitKind::Button) {
            auto& l = latchedBtn[(size_t) h.a];
            l = !l;
            withLock([&](tek::Controller& c) { c.button((Btn) h.a, l, t); });
        }
        pressed = {};
        return;
    }
    pressed = h;
    switch (h.kind) {
        case HitKind::Pad:
            dragPads = {{h.a, h.b}};
            withLock([&](tek::Controller& c) { c.padDown(h.a, h.b, t); });
            break;
        case HitKind::Button:
            if (latchedBtn[(size_t) h.a]) { latchedBtn[(size_t) h.a] = false; withLock([&](tek::Controller& c) { c.button((Btn) h.a, false, t); }); pressed = {}; break; }
            withLock([&](tek::Controller& c) { c.button((Btn) h.a, true, t); });
            break;
        case HitKind::Encoder:
            encDragAccum = 0.0f; lastDragY = p.y;
            withLock([&](tek::Controller& c) { c.encoderPress(h.a, true, t); });
            break;
        default: break;
    }
}

void TekEditor::mouseDrag(const juce::MouseEvent& e) {
    auto p = toDesign(e.position);
    const double t = now();
    if (pressed.kind == HitKind::Encoder) {
        encDragAccum += lastDragY - p.y;
        lastDragY = p.y;
        const float step = e.mods.isAltDown() ? 30.0f : 8.0f;
        while (std::abs(encDragAccum) >= step) {
            int d = encDragAccum > 0 ? 1 : -1;
            encDragAccum -= d * step;
            withLock([&](tek::Controller& c) { c.encoderTurn(pressed.a, d, t); });
        }
    } else if (pressed.kind == HitKind::Pad) {
        Hit h = hitAt(p);
        if (h.kind == HitKind::Pad && h.b == dragPads.front().second &&
            std::find(dragPads.begin(), dragPads.end(), std::make_pair(h.a, h.b)) == dragPads.end()) {
            dragPads.push_back({h.a, h.b});
            withLock([&](tek::Controller& c) { c.padDown(h.a, h.b, t); });
        }
    }
}

void TekEditor::mouseUp(const juce::MouseEvent&) {
    const double t = now();
    switch (pressed.kind) {
        case HitKind::Pad:
            withLock([&](tek::Controller& c) { for (auto it = dragPads.rbegin(); it != dragPads.rend(); ++it) c.padUp(it->first, it->second, t); });
            dragPads.clear();
            break;
        case HitKind::Button: withLock([&](tek::Controller& c) { c.button((Btn) pressed.a, false, t); }); break;
        case HitKind::Encoder: withLock([&](tek::Controller& c) { c.encoderPress(pressed.a, false, t); }); break;
        default: break;
    }
    pressed = {};
}

void TekEditor::mouseWheelMove(const juce::MouseEvent& e, const juce::MouseWheelDetails& w) {
    Hit h = hitAt(toDesign(e.position));
    if (h.kind != HitKind::Encoder) return;
    float dy = w.deltaY != 0.0f ? w.deltaY : -w.deltaX;
    if (dy == 0.0f) return;
    int d = dy > 0 ? 1 : -1;
    withLock([&](tek::Controller& c) { c.encoderTurn(h.a, d, now()); });
}

// ---------------------------------------------------------------------------- keyboard
bool TekEditor::keyPressed(const juce::KeyPress& k) {
    static const int up[4] = {'q', 'w', 'e', 'r'}, dn[4] = {'a', 's', 'd', 'f'};
    int c = (int) juce::CharacterFunctions::toLowerCase(k.getTextCharacter());
    if (c == 0) c = (int) juce::CharacterFunctions::toLowerCase((juce::juce_wchar) k.getKeyCode());
    for (int i = 0; i < 4; ++i) {
        if (c == up[i] || c == dn[i]) {
            int d = c == up[i] ? 1 : -1;
            withLock([&](tek::Controller& ctl) { ctl.encoderTurn(i, d, now()); });
            return true;
        }
    }
    for (auto& m : kKeys) if (m.key == c) return true;
    return false;
}

bool TekEditor::keyStateChanged(bool) {
    bool handled = false;
    const double t = now();
    for (auto& m : kKeys) {
        bool down = keyDown(m.key);
        auto& h = keyHeld[(size_t) m.btn];
        if (down != h) {
            h = down;
            handled = true;
            withLock([&](tek::Controller& c) { c.button(m.btn, down, t); });
        }
    }
    return handled;
}

void TekEditor::modifierKeysChanged(const juce::ModifierKeys& mods) {
    bool s = mods.isShiftDown();
    if (s != shiftKey) {
        shiftKey = s;
        withLock([&](tek::Controller& c) { c.button(Btn::Shift, s, now()); });
    }
}

void TekEditor::refreshFrame() { withLock([&](tek::Controller& c) { c.render(frame, now()); }); }

void TekEditor::timerCallback() {
    bool wantMidi = false;
    int target = 0;
    withLock([&](tek::Controller& c) { wantMidi = c.takeMidiLoadRequest(); target = c.midiLoadTarget(); });
    if (wantMidi) openMidiFile(target);
    refreshFrame();
    repaint();
}

// MIDI player mode: pick a .mid file and import its note on/off data (other events are ignored)
void TekEditor::openMidiFile(int seq) {
    chooser = std::make_unique<juce::FileChooser>("Choose a MIDI file", juce::File(), "*.mid;*.midi");
    chooser->launchAsync(juce::FileBrowserComponent::openMode | juce::FileBrowserComponent::canSelectFiles,
        [this, seq](const juce::FileChooser& fc) {
            auto file = fc.getResult();
            if (!file.existsAsFile()) return;
            juce::FileInputStream in(file);
            juce::MidiFile mf;
            if (!in.openedOk() || !mf.readFrom(in)) {
                withLock([&](tek::Controller& c) { c.showMessage("COULD NOT READ MIDI FILE", now()); });
                return;
            }
            int tpq = mf.getTimeFormat();
            if (tpq <= 0) { // SMPTE time format: convert through seconds at 120 bpm
                mf.convertTimestampTicksToSeconds();
                tpq = -1;
            }
            std::vector<tek::ClipNote> notes;
            for (int t = 0; t < mf.getNumTracks(); ++t) {
                juce::MidiMessageSequence seqm(*mf.getTrack(t));
                seqm.updateMatchedPairs();
                for (int i = 0; i < seqm.getNumEvents(); ++i) {
                    auto* ev = seqm.getEventPointer(i);
                    if (!ev->message.isNoteOn()) continue;
                    double on = ev->message.getTimeStamp();
                    double off = ev->noteOffObject != nullptr ? ev->noteOffObject->message.getTimeStamp() : on + (tpq > 0 ? tpq / 4.0 : 0.125);
                    if (tpq < 0) { on *= 2.0 * tek::kClipPpqn; off *= 2.0 * tek::kClipPpqn; } // seconds -> 96 ppqn at 120 bpm
                    tek::ClipNote n;
                    n.tick = (uint32_t) std::max(0.0, on);
                    n.len = (uint32_t) std::max(1.0, off - on);
                    n.note = (uint8_t) ev->message.getNoteNumber();
                    n.vel = (uint8_t) juce::jlimit(1, 127, (int) ev->message.getVelocity());
                    notes.push_back(n);
                }
            }
            auto clip = tek::makeClip(std::move(notes), tpq > 0 ? tpq : tek::kClipPpqn, file.getFileNameWithoutExtension().toStdString());
            withLock([&](tek::Controller& c) {
                if (clip.notes.empty()) c.showMessage("NO NOTES IN FILE", now());
                else c.setClip(seq, std::move(clip), now());
            });
        });
}

// ---------------------------------------------------------------------------- painting
void TekEditor::drawDisplay(juce::Graphics& g, juce::Rectangle<float> r) {
    g.setColour(juce::Colour(0xff050708));
    g.fillRoundedRectangle(r, 6.0f);
    g.setColour(juce::Colour(0xff2a3035));
    g.drawRoundedRectangle(r, 6.0f, 1.5f);
    auto in = r.reduced(10.0f, 7.0f);
    const auto ink = juce::Colour(0xffcfefff), dimInk = juce::Colour(0xff6f8f9f);

    g.setFont(mono(12.5f));
    g.setColour(ink);
    g.drawText(frame.header, in.withHeight(15), juce::Justification::left);
    g.setFont(mono(15.0f, true));
    g.drawText(frame.headerRight, in.withHeight(15), juce::Justification::right);

    // bar progress (16ths)
    auto bar = in.withY(in.getY() + 18).withHeight(4.0f);
    float segW = bar.getWidth() / 16.0f;
    for (int i = 0; i < 16; ++i) {
        g.setColour(i <= frame.progress ? ink.withAlpha(i % 4 == 0 ? 0.9f : 0.55f) : juce::Colour(0xff1a2226));
        g.fillRect(bar.getX() + i * segW + 1, bar.getY(), segW - 2, bar.getHeight());
    }

    g.setFont(mono(14.5f, true));
    g.setColour(ink);
    g.drawText(frame.line1, in.withY(in.getY() + 27).withHeight(18), juce::Justification::left);
    g.setFont(mono(12.5f));
    g.setColour(dimInk.brighter(0.3f));
    g.drawText(frame.line2, in.withY(in.getY() + 46).withHeight(16), juce::Justification::left);

    float colW = in.getWidth() / 4.0f;
    for (int i = 0; i < 4; ++i) {
        auto cell = juce::Rectangle<float>(in.getX() + i * colW, in.getBottom() - 38, colW, 38);
        g.setColour(ink);
        g.setFont(mono(14.0f, true));
        g.drawText(frame.values[(size_t) i], cell.withHeight(20), juce::Justification::centred);
        g.setColour(dimInk);
        g.setFont(mono(11.0f));
        g.drawText(frame.labels[(size_t) i], cell.withTrimmedTop(21), juce::Justification::centred);
    }

    if (!frame.message.empty()) {
        auto box = r.reduced(26.0f, 40.0f);
        g.setColour(ink);
        g.fillRoundedRectangle(box, 4.0f);
        g.setColour(juce::Colours::black);
        g.setFont(mono(14.0f, true));
        g.drawFittedText(frame.message, box.toNearestInt().reduced(6, 2), juce::Justification::centred, 2);
    }
}

void TekEditor::drawButton(juce::Graphics& g, Btn b) {
    auto r = buttonRect(b);
    if (r.isEmpty()) return;
    uint32_t led = frame.leds[(size_t) b];
    bool isSeq = b >= Btn::Seq1 && b <= Btn::Seq8;
    auto base = juce::Colour(0xff2b2d30);
    if (b == Btn::Shift) base = juce::Colour(0xff1f6e66);
    g.setColour(base);
    g.fillRoundedRectangle(r, 5.0f);
    if (led) {
        auto c = col(led);
        if (isSeq) { g.setColour(c.withAlpha(0.9f)); g.fillRoundedRectangle(r.reduced(2.0f), 4.0f); }
        else { g.setColour(c.withAlpha(0.85f)); g.fillRoundedRectangle(r.withHeight(4.0f).reduced(6.0f, 0.0f).translated(0, 3), 2.0f); }
    }
    if (latchedBtn[(size_t) b]) { g.setColour(juce::Colour(0xff30d0c0)); g.drawRoundedRectangle(r.reduced(1.0f), 5.0f, 2.0f); }
    g.setColour(isSeq && led && col(led).getPerceivedBrightness() > 0.6f ? juce::Colours::black : juce::Colours::white);
    if (isSeq) {
        g.setFont(sans(17.0f, true));
        g.drawText(tek::btnLabel(b), r, juce::Justification::centred);
        return;
    }
    g.setFont(sans(11.5f, true));
    g.drawText(tek::btnLabel(b), r.withTrimmedTop(7).withHeight(14), juce::Justification::centred);
    g.setColour(juce::Colour(0xff46d6c6));
    g.setFont(sans(9.5f));
    g.drawText(tek::btnSubLabel(b), r.withTrimmedTop(21).withHeight(12), juce::Justification::centred);
}

void TekEditor::paint(juce::Graphics& g) {
    g.fillAll(juce::Colour(0xff121314));
    g.addTransform(juce::AffineTransform::scale(scale));

    // panel
    g.setColour(juce::Colour(0xff1b1c1e));
    g.fillRoundedRectangle(8, 8, kW - 16, kH - 16, 14.0f);

    drawDisplay(g, displayRect());

    // encoders
    for (int i = 0; i < 4; ++i) {
        auto r = encoderRect(i);
        g.setColour(juce::Colour(0xff0d0d0e));
        g.fillEllipse(r);
        g.setColour(juce::Colour(0xff3a3c40));
        g.drawEllipse(r.reduced(2.0f), 2.0f);
        g.setColour(juce::Colour(0xff2a2c2f));
        g.fillEllipse(r.reduced(9.0f));
        g.setColour(juce::Colours::white);
        g.setFont(sans(15.0f, true));
        g.drawText(juce::String(i + 1), r, juce::Justification::centred);
    }

    for (int b = 0; b < tek::kNumBtns; ++b) drawButton(g, (Btn) b);

    // pads
    for (int y = 0; y < 8; ++y)
        for (int x = 0; x < 16; ++x) {
            auto r = padRect(x, y);
            uint32_t c = frame.pads[(size_t) (y * 16 + x)];
            g.setColour(juce::Colour(0xff26282b));
            g.fillRoundedRectangle(r, 5.0f);
            if (c) {
                auto cc = col(c);
                g.setColour(cc.withAlpha(0.25f));
                g.fillRoundedRectangle(r.expanded(2.0f), 6.0f);
                g.setColour(cc);
                g.fillRoundedRectangle(r.reduced(2.0f), 4.0f);
            }
            if (std::find(latchedPads.begin(), latchedPads.end(), std::make_pair(x, y)) != latchedPads.end()) {
                g.setColour(juce::Colours::white);
                g.drawRoundedRectangle(r, 5.0f, 2.0f);
            }
            if (x % 4 == 0 && y == 7) {
                g.setColour(juce::Colour(0xff5a5c60));
                g.fillRect(r.getX(), r.getBottom() + 4, 2.0f, 4.0f);
            }
        }

    // status / help line
    g.setFont(sans(11.0f));
    g.setColour(juce::Colour(0xff8a8e94));
    juce::String port = proc.hasVirtualPort() ? "MIDI port 'TEK Out' active" : "no virtual port (use HOST output)";
    juce::String web = proc.webUrl();
    juce::String touch = web.isEmpty() ? "touch controller off" : "iPad: " + web + (proc.webClients() > 0 ? "  (" + juce::String(proc.webClients()) + " connected)" : "");
    g.drawText("TEK  |  " + port + "  |  " + touch + "  |  right-click = hold  |  keys: Shift, 1-8, Q/A W/S E/D R/F knobs",
               juce::Rectangle<float>(kSeqX, kH - 40.0f, kW - kSeqX - 20.0f, 20.0f), juce::Justification::left);
}
