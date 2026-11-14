#pragma once
#include <juce_audio_processors/juce_audio_processors.h>
#include <juce_audio_devices/juce_audio_devices.h>
#include "Controller.h"
#include "WebControl.h"

class TekProcessor : public juce::AudioProcessor, private juce::Timer {
public:
    TekProcessor();
    ~TekProcessor() override;

    void prepareToPlay(double sampleRate, int samplesPerBlock) override;
    void releaseResources() override {}
    bool isBusesLayoutSupported(const BusesLayout& layouts) const override;
    using AudioProcessor::processBlock;
    void processBlock(juce::AudioBuffer<float>&, juce::MidiBuffer&) override;

    juce::AudioProcessorEditor* createEditor() override;
    bool hasEditor() const override { return true; }

    const juce::String getName() const override { return "TEK"; }
    bool acceptsMidi() const override { return true; }
    bool producesMidi() const override { return true; }
    bool isMidiEffect() const override { return false; }
    double getTailLengthSeconds() const override { return 0.0; }

    int getNumPrograms() override { return 1; }
    int getCurrentProgram() override { return 0; }
    void setCurrentProgram(int) override {}
    const juce::String getProgramName(int) override { return {}; }
    void changeProgramName(int, const juce::String&) override {}

    void getStateInformation(juce::MemoryBlock& destData) override;
    void setStateInformation(const void* data, int sizeInBytes) override;

    // Shared with the editor. Hold `lock` while touching project / engine / controller.
    juce::SpinLock lock;
    tek::Project& project() { return *proj; }
    tek::Engine& engine() { return *eng; }
    tek::Controller& controller() { return *ctrl; }
    bool hasVirtualPort() const { return virtualOut != nullptr; }
    juce::String webUrl() const { return web ? web->url() : juce::String(); }
    int webClients() const { return web ? web->numClients() : 0; }
    WebControl* webControl() { return web.get(); }

private:
    std::unique_ptr<tek::Project> proj;
    std::unique_ptr<tek::Engine> eng;
    std::unique_ptr<tek::Controller> ctrl;
    std::unique_ptr<juce::MidiOutput> virtualOut;
    std::unique_ptr<WebControl> web;
    std::vector<tek::MidiIn> midiIn;
    std::vector<tek::MidiOut> midiOut;
    juce::MidiBuffer portBuffer;
    // Echo filter: events we sent to the "TEK Out" port can come straight back in when TEK's own track
    // listens to "All Ins". Those are ignored so they don't re-trigger (which made notes hang).
    struct Echo { uint8_t st, d1, d2; double ms; };
    std::array<Echo, 1024> echoes {};
    int echoHead = 0;
    bool isEcho(uint8_t st, uint8_t d1, uint8_t d2, double nowMs);
    double sampleRate = 44100.0;
    // file jobs (message thread)
    void timerCallback() override;
    void runJob(const tek::Controller::Job& j);
    void openMidiFile(int seq);
    void applyProject(const std::string& text, const juce::String& okMsg);
    void message(const juce::String& m);
    static juce::File tekDir(const juce::String& sub);
    static juce::File templateFile(int slot);
    std::unique_ptr<juce::FileChooser> chooser;
    std::string lastSaved;   // state at the last Ableton save / load
    int templateScan = 0;
    double portClockMs = -1.0; // steady timestamp clock for the virtual port

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(TekProcessor)
};
