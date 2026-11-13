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
}

TekProcessor::~TekProcessor() {
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

    midiIn.clear();
    for (const auto meta : midi) {
        auto m = meta.getMessage();
        if (m.getRawDataSize() >= 3) {
            auto* d = m.getRawData();
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
        if (toPort) portBuffer.addEvent(m, e.sampleOffset);
    }
    if (toPort && !portBuffer.isEmpty())
        virtualOut->sendBlockOfMessages(portBuffer, juce::Time::getMillisecondCounterHiRes(), sampleRate);
}

juce::AudioProcessorEditor* TekProcessor::createEditor() { return new TekEditor(*this); }

void TekProcessor::getStateInformation(juce::MemoryBlock& dest) {
    std::string s;
    {
        const juce::SpinLock::ScopedLockType sl(lock);
        s = tek::serialize(*proj);
    }
    dest.replaceAll(s.data(), s.size());
}

void TekProcessor::setStateInformation(const void* data, int size) {
    std::string s(static_cast<const char*>(data), (size_t) std::max(0, size));
    auto fresh = std::make_unique<tek::Project>();
    if (!tek::deserialize(s, *fresh)) return;
    const juce::SpinLock::ScopedLockType sl(lock);
    *proj = *fresh;
    ctrl->resetUiState();
}

juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter() { return new TekProcessor(); }
