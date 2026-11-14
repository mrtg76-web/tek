// Drives the real plugin processor with a fake host timeline: checks MIDI output, host MIDI routing
// and state save / restore. Linux only (built when TEK_BUILD_PLUGIN_TEST is ON).
#include "../plugin/PluginProcessor.h"
#include "../plugin/PluginEditor.h"
#include <cstdio>

namespace {
struct FakePlayHead : juce::AudioPlayHead {
    double ppq = 0.0, bpm = 120.0; bool playing = true;
    juce::Optional<PositionInfo> getPosition() const override {
        PositionInfo p; p.setIsPlaying(playing); p.setPpqPosition(ppq); p.setBpm(bpm); return p;
    }
};
int fails = 0;
void check(bool c, const char* what) { if (!c) { std::printf("FAIL: %s\n", what); ++fails; } }
}

int main() {
    juce::ScopedJuceInitialiser_GUI init;
    {
        TekProcessor proc;
        FakePlayHead ph;
        proc.setPlayHead(&ph);
        proc.prepareToPlay(48000.0, 512);
        {
            const juce::SpinLock::ScopedLockType sl(proc.lock);
            auto& p = proc.project();
            p.output = tek::OutputMode::Host;
            auto& tr = p.seqs[0].pat().tracks[0];
            for (int i = 0; i < 16; i += 4) { auto& s = tr.steps[(size_t) i]; s.active = true; s.numNotes = 1; s.notes[0] = 0; }
            p.seqs[4].pat().tracks[0].steps[2].active = true;
            p.seqs[4].pat().tracks[0].steps[2].numNotes = 1;
        }
        juce::AudioBuffer<float> audio(2, 512);
        int ons = 0, offs = 0, ch5 = 0;
        for (int b = 0; b < 400; ++b) { // ~4.3 s = 8.5 beats at 120 bpm
            juce::MidiBuffer midi;
            proc.processBlock(audio, midi);
            for (const auto m : midi) {
                auto msg = m.getMessage();
                if (msg.isNoteOn()) { ++ons; if (msg.getChannel() == 5) ++ch5; }
                if (msg.isNoteOff()) ++offs;
            }
            ph.ppq += 512.0 * 2.0 / 48000.0;
        }
        std::printf("note ons %d, offs %d, ch5 %d\n", ons, offs, ch5);
        check(ons == 12, "9 quarter notes from seq 1 + 3 from seq 5 over 8.5 beats");
        check(ch5 >= 2, "sequencer 5 plays on MIDI channel 5");
        check(audio.getMagnitude(0, 512) == 0.0f, "audio output is silent");

        // stop -> all notes released
        ph.playing = false;
        juce::MidiBuffer midi;
        proc.processBlock(audio, midi);

        juce::MemoryBlock state;
        proc.getStateInformation(state);
        TekProcessor proc2;
        proc2.setStateInformation(state.getData(), (int) state.getSize());
        juce::MemoryBlock state2;
        proc2.getStateInformation(state2);
        check(state == state2, "state round trip");
        check(proc2.project().seqs[0].pat().tracks[0].steps[4].active, "restored step");

        // ---- touch controller: page, websocket handshake, input and frames
        {
            check(WebControl::acceptKey("dGhlIHNhbXBsZSBub25jZQ==") == "s3pPLMBiTxaQ9kYGzzhZRbK+xOo=", "websocket accept key (RFC 6455 example)");
            auto* w = proc.webControl();
            check(w != nullptr && w->port() >= 8765, "web server listening");
            if (w != nullptr && w->port() > 0) {
                juce::StreamingSocket http;
                check(http.connect("127.0.0.1", w->port(), 2000), "http connect");
                juce::String get = "GET / HTTP/1.1\r\nHost: x\r\n\r\n";
                http.write(get.toRawUTF8(), (int) get.getNumBytesAsUTF8());
                juce::MemoryBlock got;
                char buf[4096];
                for (int i = 0; i < 200; ++i) { if (http.waitUntilReady(true, 50) > 0) { int n = http.read(buf, sizeof buf, false); if (n <= 0) break; got.append(buf, (size_t) n); } }
                check(got.toString().contains("<title>TEK</title>"), "page served");

                juce::StreamingSocket ws;
                check(ws.connect("127.0.0.1", w->port(), 2000), "ws connect");
                juce::String hs = "GET /ws HTTP/1.1\r\nHost: x\r\nUpgrade: websocket\r\nConnection: Upgrade\r\nSec-WebSocket-Key: dGhlIHNhbXBsZSBub25jZQ==\r\nSec-WebSocket-Version: 13\r\n\r\n";
                ws.write(hs.toRawUTF8(), (int) hs.getNumBytesAsUTF8());
                juce::String resp;
                for (int i = 0; i < 4000 && !resp.contains("\r\n\r\n"); ++i) if (ws.waitUntilReady(true, 50) > 0) { char ch; if (ws.read(&ch, 1, true) == 1) resp += juce::String::charToString(ch); }
                if (!resp.contains("s3pPLM")) std::printf("resp=[%s]\n", resp.toRawUTF8());
                check(resp.contains("101") && resp.contains("s3pPLMBiTxaQ9kYGzzhZRbK+xOo="), "websocket upgrade");
                auto sendMsg = [&](const juce::String& m) {
                    std::vector<uint8_t> f = {0x81, (uint8_t) (0x80 | m.length()), 1, 2, 3, 4};
                    for (int i = 0; i < m.length(); ++i) f.push_back((uint8_t) (m[i] ^ f[2 + (i % 4)]));
                    ws.write(f.data(), (int) f.size());
                };
                {
                    const juce::SpinLock::ScopedLockType sl(proc.lock);
                    proc.project().seqs[0].pat().tracks[0].steps[9].clear();
                }
                sendMsg("pd 9 7");
                juce::Thread::sleep(60);
                sendMsg("pu 9 7");
                bool frameSeen = false;
                for (int i = 0; i < 40 && !frameSeen; ++i) {
                    if (ws.waitUntilReady(true, 50) > 0) {
                        uint8_t h[4];
                        if (ws.read(h, 2, true) != 2) break;
                        int n = h[1] & 0x7f;
                        if (n == 126) { ws.read(h + 2, 2, true); n = h[2] << 8 | h[3]; }
                        std::vector<char> pl((size_t) n);
                        ws.read(pl.data(), n, true);
                        frameSeen = n > 0 && pl[0] == 'F';
                    }
                }
                check(frameSeen, "frame received");
                juce::Thread::sleep(100);
                const juce::SpinLock::ScopedLockType sl(proc.lock);
                check(proc.project().seqs[0].pat().tracks[0].steps[9].active, "tap from the touch controller placed a step");
            }
        }

        if (int secs = juce::SystemStats::getEnvironmentVariable("TEK_SERVE", "0").getIntValue(); secs > 0) {
            // keep serving the touch page (with the transport running) for a browser test
            {
                const juce::SpinLock::ScopedLockType sl(proc.lock);
                auto& tr = proc.project().seqs[0].pat().tracks[0];
                for (int i : {2, 5, 7, 11, 13}) { tr.steps[(size_t) i].active = true; tr.steps[(size_t) i].numNotes = 1; tr.steps[(size_t) i].notes[0] = (int8_t) (i % 6); }
            }
            if (juce::SystemStats::getEnvironmentVariable("TEK_SERVE_KEYS", "").isNotEmpty()) {
                const juce::SpinLock::ScopedLockType sl(proc.lock);
                proc.project().seqs[0].scale = 2; // minor
                proc.controller().button(tek::Btn::Arp, true, 1000); proc.controller().button(tek::Btn::Arp, false, 1010);
                proc.controller().encoderTurn(0, 1, 1020);
                proc.controller().padDown(0, 6, 1030); proc.controller().padDown(3, 6, 1030); proc.controller().padDown(7, 6, 1030);
            }
            if (juce::SystemStats::getEnvironmentVariable("TEK_SERVE_MOD", "").isNotEmpty()) {
                const juce::SpinLock::ScopedLockType sl(proc.lock);
                auto& c = proc.controller();
                auto& ln = proc.project().seqs[0].pat().lanes[0];
                ln.muted = false;
                for (int i = 0; i < 16; ++i) ln.val[(size_t) i] = (int8_t) (i < 8 ? i * 16 : (15 - i) * 16);
                ln.val[12] = -60; ln.min = -127;
                c.button(tek::Btn::Mod, true, 1000); c.button(tek::Btn::Mod, false, 1010);
            }
            if (juce::SystemStats::getEnvironmentVariable("TEK_SERVE_PERF", "").isNotEmpty()) {
                const juce::SpinLock::ScopedLockType sl(proc.lock);
                auto& c = proc.controller();
                proc.project().seqs[1].mode = tek::Mode::Multi;
                proc.project().seqs[3].mode = tek::Mode::Matr;
                c.button(tek::Btn::Perf, true, 1000); c.button(tek::Btn::Perf, false, 1010);
                c.padDown(11, 0, 1020); c.padUp(11, 0, 1030);
                c.button(tek::Btn::Shift, true, 1040); c.padDown(6, 0, 1050); c.padUp(6, 0, 1060); c.button(tek::Btn::Shift, false, 1070);
                c.padDown(2, 1, 1080); c.padUp(2, 1, 1090);
                c.padDown(0, 5, 1100); c.padUp(0, 5, 1110);
            }
            ph.playing = true;
            juce::AudioBuffer<float> a2(2, 512);
            const double end = juce::Time::getMillisecondCounterHiRes() + secs * 1000.0;
            std::printf("serving on %s\n", proc.webUrl().toRawUTF8());
            while (juce::Time::getMillisecondCounterHiRes() < end) {
                juce::MidiBuffer mb; proc.processBlock(a2, mb); ph.ppq += 512.0 * 2.0 / 48000.0;
                juce::Thread::sleep(10);
            }
        }

        if (juce::SystemStats::getEnvironmentVariable("TEK_SNAPSHOT", "").isNotEmpty()) {
            // render the editor to a PNG for a visual check
            {
                const juce::SpinLock::ScopedLockType sl(proc.lock);
                auto& tr = proc.project().seqs[0].pat().tracks[0];
                tr.steps[6].active = true; tr.steps[6].numNotes = 1; tr.steps[6].notes[0] = 3; tr.steps[6].tie = 2;
                tr.steps[10].active = true; tr.steps[10].numNotes = 1; tr.steps[10].notes[0] = 5;
                if (juce::SystemStats::getEnvironmentVariable("TEK_SNAP_SAGA", "").isNotEmpty()) {
                    auto& q = proc.project().seqs[0];
                    q.mode = tek::Mode::Saga; q.scale = 1;
                    auto& t0 = q.pat().tracks[0];
                    for (auto& st : t0.steps) st = tek::Step();
                    t0.end = 23;
                    int durs[] = {1, 3, 2, 1, 4, 1, 2, 6};
                    for (int i = 0; i < 8; ++i) { auto& st = t0.steps[(size_t) (i * 2)]; st.active = true; st.numNotes = 1; st.notes[0] = (int8_t) i; st.dur = (uint8_t) durs[i]; }
                    proc.controller().padDown(4, 0, 0);
                }
                if (juce::SystemStats::getEnvironmentVariable("TEK_SNAP_DOMI", "").isNotEmpty()) {
                    auto& q = proc.project().seqs[0];
                    q.mode = tek::Mode::Domino; q.scale = 1;
                    auto& nt = q.pat().tracks[0]; auto& tt = q.pat().tracks[1];
                    nt.end = 11; tt.end = 31;
                    for (int i : {0, 2, 3, 5, 8, 9}) { nt.steps[(size_t) i].active = true; nt.steps[(size_t) i].numNotes = 1; nt.steps[(size_t) i].notes[0] = (int8_t) (i % 5); }
                    for (int i : {0, 3, 6, 8, 11, 14, 16, 19, 24, 27}) { tt.steps[(size_t) i].active = true; tt.steps[(size_t) i].numNotes = 1; }
                    tt.steps[8].tie = 2; tt.steps[9].active = false; tt.steps[10].active = false;
                    proc.controller().padDown(2, 0, 0);
                }
                if (juce::SystemStats::getEnvironmentVariable("TEK_SNAP_MATR", "").isNotEmpty()) {
                    auto& q = proc.project().seqs[0];
                    q.mode = tek::Mode::Matr;
                    auto& m = q.pat().mat;
                    for (int i : {0, 3, 6, 10, 13}) m.tracks[0].lanes[tek::kLaneTrig].val[(size_t) i] = 1;
                    m.tracks[1].lanes[tek::kLaneNote].val = {0, 3, -2, 0, 5, 0, -4, 7, 0, 0, 2, 0, 0, -1, 0, 4};
                    m.tracks[1].lanes[tek::kLaneNote].end = 11;
                    proc.controller().padDown(5, 4, 0); proc.controller().padUp(5, 4, 1);
                    m.tracks[2].lanes[tek::kLaneVel].val = {127, 40, 90, 20, 127, 60, 100, 75, 30, 75, 75, 110, 75, 75, 50, 75};
                    proc.controller().padDown(8, 5, 0); proc.controller().padUp(8, 5, 1);
                    m.tracks[3].muted = true;
                }
                if (juce::SystemStats::getEnvironmentVariable("TEK_SNAP_STOCH", "").isNotEmpty()) {
                    auto& q = proc.project().seqs[0];
                    q.mode = tek::Mode::Stoch; q.scale = 1;
                    auto& sc = q.pat().stoch;
                    sc.noteLevel = {8, 0, 3, 0, 6, 2, 0, 7, 0, 4, 1, 0};
                    sc.octUp = 2; sc.octDown = 1; sc.divRow = 5; sc.pitchRand = 4; sc.rhythmRand = 0;
                }
                if (juce::SystemStats::getEnvironmentVariable("TEK_SNAP_CHORD", "").isNotEmpty()) {
                    proc.project().seqs[0].mode = tek::Mode::Chord;
                    proc.project().seqs[0].scale = 1;
                    tr.steps[6].tie = 0; tr.steps[6].chordType = 1;
                    proc.controller().padDown(6, 4, 0);
                }
            }
            std::unique_ptr<TekEditor> ed(static_cast<TekEditor*>(proc.createEditor()));
            ed->setSize(1300, 500);
            ed->refreshFrame();
            auto img = ed->createComponentSnapshot(ed->getLocalBounds(), false, 1.0f);
            juce::File out(juce::SystemStats::getEnvironmentVariable("TEK_SNAPSHOT", ""));
            out.deleteFile();
            juce::FileOutputStream os(out);
            juce::PNGImageFormat().writeImageToStream(img, os);
            std::printf("snapshot written\n");
        }
    }
    std::printf(fails == 0 ? "PLUGIN TEST PASSED\n" : "PLUGIN TEST FAILED\n");
    return fails == 0 ? 0 : 1;
}
