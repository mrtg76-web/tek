#pragma once
#include <juce_gui_basics/juce_gui_basics.h>
#include "PluginProcessor.h"

// Draws the instrument panel and forwards mouse / keyboard input to the Controller.
// Everything is laid out in a fixed 1300 x 500 design space and scaled to the window size.
class TekEditor : public juce::AudioProcessorEditor, private juce::Timer {
public:
    explicit TekEditor(TekProcessor&);
    ~TekEditor() override;

    void paint(juce::Graphics&) override;
    void refreshFrame(); // pulls a fresh Frame from the controller (also used by the timer)
    void resized() override;

    void mouseDown(const juce::MouseEvent&) override;
    void mouseDrag(const juce::MouseEvent&) override;
    void mouseUp(const juce::MouseEvent&) override;
    void mouseWheelMove(const juce::MouseEvent&, const juce::MouseWheelDetails&) override;
    bool keyPressed(const juce::KeyPress&) override;
    bool keyStateChanged(bool isKeyDown) override;
    void modifierKeysChanged(const juce::ModifierKeys&) override;

private:
    enum class HitKind { None, Pad, Button, Encoder };
    struct Hit { HitKind kind = HitKind::None; int a = 0, b = 0; };

    void timerCallback() override;
    void openMidiFile(int seq);
    Hit hitAt(juce::Point<float> designPos) const;
    juce::Point<float> toDesign(juce::Point<float> p) const;
    juce::Rectangle<float> buttonRect(tek::Btn b) const;
    juce::Rectangle<float> padRect(int x, int y) const;
    juce::Rectangle<float> encoderRect(int i) const;
    juce::Rectangle<float> displayRect() const { return {24, 24, 372, 128}; }
    void drawDisplay(juce::Graphics&, juce::Rectangle<float>);
    void drawButton(juce::Graphics&, tek::Btn);
    double now() const { return juce::Time::getMillisecondCounterHiRes(); }

    template <typename F> void withLock(F&& f) {
        const juce::SpinLock::ScopedLockType sl(proc.lock);
        f(proc.controller());
    }

    TekProcessor& proc;
    std::unique_ptr<juce::FileChooser> chooser;
    tek::Frame frame;
    float scale = 1.0f;

    // mouse state
    Hit pressed;                       // what the left button went down on
    std::vector<std::pair<int,int>> dragPads; // pads pressed during the current drag
    float encDragAccum = 0.0f;
    float lastDragY = 0.0f;
    std::array<bool, tek::kNumBtns> latchedBtn {};
    std::vector<std::pair<int,int>> latchedPads;

    // keyboard state
    std::array<bool, tek::kNumBtns> keyHeld {};
    bool shiftKey = false;

    static constexpr float kW = 1300.0f, kH = 500.0f;
    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(TekEditor)
};
