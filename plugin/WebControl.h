#pragma once
// Touch controller server: serves a single web page (for iPad / any browser on the local network) and
// talks to it over a WebSocket. The page sends pad / button / encoder events, the server streams the
// Controller's Frame (display text, pad and LED colours) back whenever it changes.
#include <juce_core/juce_core.h>
#include <functional>

class TekProcessor;

class WebControl : private juce::Thread {
public:
    explicit WebControl(TekProcessor& p);
    ~WebControl() override;

    int port() const { return listenPort; }
    juce::String url() const;            // http://<lan-ip>:<port> (empty when not listening)
    int numClients() const;

    // exposed for tests
    static juce::String acceptKey(const juce::String& clientKey);
    static juce::String frameToText(const void* frame);
    void handleMessage(const juce::String& msg);

private:
    class Client;
    void run() override;
    void removeFinished();

    TekProcessor& proc;
    juce::StreamingSocket listener;
    int listenPort = 0;
    juce::CriticalSection clientLock;
    juce::OwnedArray<Client> clients;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(WebControl)
};
