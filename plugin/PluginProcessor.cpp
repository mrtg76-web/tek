#include "PluginProcessor.h"
#include "PluginEditor.h"

TekProcessor::TekProcessor()
    : AudioProcessor(BusesProperties().withOutput("Output", juce::AudioChannelSet::stereo(), true)) {
    proj = std::make_unique<tek::Project>();
    eng = std::make_unique<tek::Engine>(*proj);
    ctrl = std::make_unique<tek::Controller>(*proj, *eng);
    web = std::make_unique<WebControl>(*this); // touch controller (iPad) on the local network
    midiIn.reserve(512);
    midiOut.reserve(4096);
    portBuffer.ensureSize(4096 * 3);
   #if JUCE_MAC || JUCE_LINUX
    // A virtual MIDI source that Ableton can select per track with a channel filter.
    virtualOut = juce::MidiOutput::createNewDevice("TEK Out");
    if (virtualOut) virtualOut->startBackgroundThread();
   #endif
    startTimer(100);
}

TekProcessor::~TekProcessor() {
    stopTimer();
    web.reset();
    if (virtualOut) {
        virtualOut->stopBackgroundThread();
        for (int ch = 1; ch <= 16; ++ch) virtualOut->sendMessageNow(juce::MidiMessage::allNotesOff(ch));
    }
}

bool TekProcessor::isBusesLayoutSupported(const BusesLayout& layouts) const {
    auto out = layouts.getMainOutputChannelSet();
    return out == juce::AudioChannelSet::stereo() || out == juce::AudioChannelSet::mono();
}

void TekProcessor::prepareToPlay(double sr, int) {
    sampleRate = sr;
    const juce::SpinLock::ScopedLockType sl(lock);
    eng->prepare(sr);
}

void TekProcessor::processBlock(juce::AudioBuffer<float>& audio, juce::MidiBuffer& midi) {
    juce::ScopedNoDenormals noDenormals;
    audio.clear();
    const int n = audio.getNumSamples();

    tek::Transport t;
    if (auto* ph = getPlayHead()) {
        if (auto pos = ph->getPosition()) {
            t.playing = pos->getIsPlaying();
            if (auto ppq = pos->getPpqPosition()) t.ppq = *ppq;
            if (auto bpm = pos->getBpm()) t.bpm = *bpm;
        }
    }

    const double nowMs = juce::Time::getMillisecondCounterHiRes();
    midiIn.clear();
    for (const auto meta : midi) {
        auto m = meta.getMessage();
        if (m.getRawDataSize() >= 3) {
            auto* d = m.getRawData();
            if (isEcho(d[0], d[1], d[2], nowMs)) continue;
            midiIn.push_back({d[0], d[1], d[2]});
        }
    }
    midi.clear();
    midiOut.clear();

    {
        const juce::SpinLock::ScopedLockType sl(lock);
        eng->process(t, n, midiIn, midiOut);
    }

    const auto mode = proj->output;
    const bool toHost = mode == tek::OutputMode::Host || mode == tek::OutputMode::Both || !virtualOut;
    const bool toPort = virtualOut && (mode == tek::OutputMode::Port || mode == tek::OutputMode::Both);
    if (toPort) portBuffer.clear();
    for (auto& e : midiOut) {
        juce::MidiMessage m(e.status, e.data1, e.data2);
        if (toHost) midi.addEvent(m, e.sampleOffset);
        if (toPort) {
            portBuffer.addEvent(m, e.sampleOffset);
            echoes[(size_t) echoHead] = {e.status, e.data1, e.data2, nowMs};
            echoHead = (echoHead + 1) % (int) echoes.size();
        }
    }
    if (toPort && !portBuffer.isEmpty())
        virtualOut->sendBlockOfMessages(portBuffer, juce::Time::getMillisecondCounterHiRes(), sampleRate);
}

bool TekProcessor::isEcho(uint8_t st, uint8_t d1, uint8_t d2, double nowMs) {
    // note off may arrive as note on with velocity 0
    auto norm = [](uint8_t& s, uint8_t& v) { if ((s & 0xF0) == 0x90 && v == 0) { s = (uint8_t) (0x80 | (s & 15)); } if ((s & 0xF0) == 0x80) v = 0; };
    norm(st, d2);
    for (auto& e : echoes) {
        if (e.ms <= 0.0 || nowMs - e.ms > 500.0) continue;
        uint8_t es = e.st, ed2 = e.d2;
        norm(es, ed2);
        if (es == st && e.d1 == d1 && ed2 == d2) { e.ms = 0.0; return true; }
    }
    return false;
}

juce::AudioProcessorEditor* TekProcessor::createEditor() { return new TekEditor(*this); }

void TekProcessor::getStateInformation(juce::MemoryBlock& dest) {
    std::string s;
    {
        const juce::SpinLock::ScopedLockType sl(lock);
        s = tek::serialize(*proj);
    }
    lastSaved = s; // "reload" goes back to this state
    dest.replaceAll(s.data(), s.size());
}

void TekProcessor::setStateInformation(const void* data, int size) {
    std::string s(static_cast<const char*>(data), (size_t) std::max(0, size));
    auto fresh = std::make_unique<tek::Project>();
    if (!tek::deserialize(s, *fresh)) return;
    lastSaved = s;
    const juce::SpinLock::ScopedLockType sl(lock);
    *proj = *fresh;
    ctrl->resetUiState();
}

// MIDI player mode: pick a .mid file and import its note on/off data (other events are ignored)
void TekProcessor::openMidiFile(int seq) {
    chooser = std::make_unique<juce::FileChooser>("Choose a MIDI file", juce::File(), "*.mid;*.midi");
    chooser->launchAsync(juce::FileBrowserComponent::openMode | juce::FileBrowserComponent::canSelectFiles,
        [this, seq](const juce::FileChooser& fc) {
            auto file = fc.getResult();
            if (!file.existsAsFile()) return;
            juce::FileInputStream in(file);
            juce::MidiFile mf;
            if (!in.openedOk() || !mf.readFrom(in)) {
                { const juce::SpinLock::ScopedLockType sl(lock); auto& c = *ctrl; c.showMessage("COULD NOT READ MIDI FILE", juce::Time::getMillisecondCounterHiRes()); }
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
            { const juce::SpinLock::ScopedLockType sl(lock); auto& c = *ctrl;
                if (clip.notes.empty()) c.showMessage("NO NOTES IN FILE", juce::Time::getMillisecondCounterHiRes());
                else c.setClip(seq, std::move(clip), juce::Time::getMillisecondCounterHiRes());
            }
        });
}


// ============================================================================ host file jobs (message thread)
// Templates, project files and reloads run here, off the audio lock; only the final copy takes the lock.
static double nowMs() { return juce::Time::getMillisecondCounterHiRes(); }

juce::File TekProcessor::tekDir(const juce::String& sub) {
    auto d = juce::File::getSpecialLocation(juce::File::userDocumentsDirectory).getChildFile("TEK").getChildFile(sub);
    d.createDirectory();
    return d;
}

juce::File TekProcessor::templateFile(int slot) { return tekDir("Templates").getChildFile(juce::String::formatted("template_%02d.tek", slot + 1)); }

void TekProcessor::message(const juce::String& m) {
    const juce::SpinLock::ScopedLockType sl(lock);
    ctrl->showMessage(m.toStdString(), nowMs());
}

void TekProcessor::applyProject(const std::string& text, const juce::String& okMsg) {
    auto fresh = std::make_unique<tek::Project>();
    if (!tek::deserialize(text, *fresh)) { message("COULD NOT READ THE PROJECT"); return; }
    const juce::SpinLock::ScopedLockType sl(lock);
    const auto out = proj->output;
    *proj = *fresh;
    proj->output = out;
    eng->panic();
    ctrl->resetUiState();
    ctrl->showMessage(okMsg.toStdString(), nowMs());
}

void TekProcessor::timerCallback() {
    std::vector<tek::Controller::Job> todo;
    bool wantMidi = false; int midiSeq = 0;
    {
        const juce::SpinLock::ScopedLockType sl(lock);
        todo = ctrl->takeJobs();
        wantMidi = ctrl->takeMidiLoadRequest(); midiSeq = ctrl->midiLoadTarget();
    }
    if (wantMidi) openMidiFile(midiSeq);
    if (++templateScan % 20 == 0 || !todo.empty()) { // which template slots exist (every ~2 s)
        uint32_t mask = 0;
        for (int i = 0; i < tek::Controller::kTemplateSlots; ++i) if (templateFile(i).existsAsFile()) mask |= 1u << i;
        const juce::SpinLock::ScopedLockType sl(lock);
        ctrl->setTemplateMask(mask);
    }
    for (auto& j : todo) runJob(j);
}

void TekProcessor::runJob(const tek::Controller::Job& j) {
    using J = tek::Controller::Job;
    switch (j.type) {
        case J::ReloadPattern: {
            if (lastSaved.empty()) { message("NOTHING SAVED YET - SAVE THE SET"); return; }
            auto saved = std::make_unique<tek::Project>();
            if (!tek::deserialize(lastSaved, *saved)) return;
            const juce::SpinLock::ScopedLockType sl(lock);
            proj->seqs[(size_t) j.seq].patterns[(size_t) j.pattern] = saved->seqs[(size_t) j.seq].patterns[(size_t) j.pattern];
            ctrl->showMessage("PATTERN RELOADED", nowMs());
            return;
        }
        case J::ReloadProject:
            if (lastSaved.empty()) { message("NOTHING SAVED YET - SAVE THE SET"); return; }
            applyProject(lastSaved, "PROJECT RELOADED");
            return;
        case J::LoadProjectFile:
            chooser = std::make_unique<juce::FileChooser>("Load a TEK project", tekDir("Projects"), "*.tek");
            chooser->launchAsync(juce::FileBrowserComponent::openMode | juce::FileBrowserComponent::canSelectFiles, [this](const juce::FileChooser& fc) {
                auto f = fc.getResult();
                if (f.existsAsFile()) applyProject(f.loadFileAsString().toStdString(), "LOADED " + f.getFileNameWithoutExtension().toUpperCase());
            });
            return;
        case J::SaveProjectFile:
            chooser = std::make_unique<juce::FileChooser>("Save the TEK project", tekDir("Projects").getChildFile("project.tek"), "*.tek");
            chooser->launchAsync(juce::FileBrowserComponent::saveMode | juce::FileBrowserComponent::warnAboutOverwriting, [this](const juce::FileChooser& fc) {
                auto f = fc.getResult();
                if (f == juce::File()) return;
                if (!f.hasFileExtension("tek")) f = f.withFileExtension("tek");
                std::string text;
                { const juce::SpinLock::ScopedLockType sl(lock); text = tek::serialize(*proj); }
                message(f.replaceWithText(text) ? "SAVED " + f.getFileNameWithoutExtension().toUpperCase() : juce::String("COULD NOT SAVE"));
            });
            return;
        case J::SaveTemplate: {
            std::string text;
            { const juce::SpinLock::ScopedLockType sl(lock); text = tek::serialize(*proj, j.seq, j.pattern); }
            text.insert(text.find('\n') + 1, "TPL " + std::to_string(j.seq) + " " + std::to_string(j.pattern) + "\n");
            if (!templateFile(j.slot).replaceWithText(text)) message("COULD NOT SAVE THE TEMPLATE");
            return;
        }
        case J::LoadTemplate: {
            const std::string text = templateFile(j.slot).loadFileAsString().toStdString();
            int ts = -1, tp = -1;
            const auto at = text.find("\nTPL ");
            if (at != std::string::npos) std::sscanf(text.c_str() + at + 5, "%d %d", &ts, &tp);
            auto t = std::make_unique<tek::Project>();
            if (ts < 0 || ts >= tek::kNumSeqs || tp < 0 || tp >= tek::kNumPatterns || !tek::deserialize(text, *t)) { message("COULD NOT READ THE TEMPLATE"); return; }
            const juce::SpinLock::ScopedLockType sl(lock);
            auto& dst = proj->seqs[(size_t) j.seq];
            auto keep = std::move(dst.patterns);
            const auto cur = dst.curPattern; const bool act = dst.active, mut = dst.muted;
            const auto& src = t->seqs[(size_t) ts];
            dst = src; // settings: mode, channel, scale, arp, generators...
            dst.patterns = std::move(keep);
            dst.curPattern = cur; dst.active = act; dst.muted = mut;
            dst.patterns[(size_t) cur] = src.patterns[(size_t) tp];
            return;
        }
    }
}

juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter() { return new TekProcessor(); }
