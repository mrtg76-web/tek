#include "WebControl.h"
#include "PluginProcessor.h"
#include "WebPage.h"

namespace {

// ---------------------------------------------------------------- SHA-1 (for the WebSocket handshake)
void sha1(const uint8_t* data, size_t len, uint8_t out[20]) {
    uint32_t h0 = 0x67452301, h1 = 0xEFCDAB89, h2 = 0x98BADCFE, h3 = 0x10325476, h4 = 0xC3D2E1F0;
    std::vector<uint8_t> m(data, data + len);
    const uint64_t bits = (uint64_t) len * 8;
    m.push_back(0x80);
    while (m.size() % 64 != 56) m.push_back(0);
    for (int i = 7; i >= 0; --i) m.push_back((uint8_t) (bits >> (i * 8)));
    auto rol = [](uint32_t x, int n) { return (x << n) | (x >> (32 - n)); };
    for (size_t off = 0; off < m.size(); off += 64) {
        uint32_t w[80];
        for (int i = 0; i < 16; ++i)
            w[i] = (uint32_t) m[off + 4 * (size_t) i] << 24 | (uint32_t) m[off + 4 * (size_t) i + 1] << 16
                 | (uint32_t) m[off + 4 * (size_t) i + 2] << 8 | (uint32_t) m[off + 4 * (size_t) i + 3];
        for (int i = 16; i < 80; ++i) w[i] = rol(w[i - 3] ^ w[i - 8] ^ w[i - 14] ^ w[i - 16], 1);
        uint32_t a = h0, b = h1, c = h2, d = h3, e = h4;
        for (int i = 0; i < 80; ++i) {
            uint32_t f, k;
            if (i < 20) { f = (b & c) | (~b & d); k = 0x5A827999; }
            else if (i < 40) { f = b ^ c ^ d; k = 0x6ED9EBA1; }
            else if (i < 60) { f = (b & c) | (b & d) | (c & d); k = 0x8F1BBCDC; }
            else { f = b ^ c ^ d; k = 0xCA62C1D6; }
            uint32_t t = rol(a, 5) + f + e + k + w[i];
            e = d; d = c; c = rol(b, 30); b = a; a = t;
        }
        h0 += a; h1 += b; h2 += c; h3 += d; h4 += e;
    }
    uint32_t hs[5] = {h0, h1, h2, h3, h4};
    for (int i = 0; i < 5; ++i) for (int j = 0; j < 4; ++j) out[i * 4 + j] = (uint8_t) (hs[i] >> (24 - 8 * j));
}

bool sendAll(juce::StreamingSocket& s, const void* data, int len) {
    auto* p = static_cast<const char*>(data);
    while (len > 0) {
        int n = s.write(p, len);
        if (n <= 0) return false;
        p += n; len -= n;
    }
    return true;
}

bool readExact(juce::StreamingSocket& s, void* data, int len, std::atomic<bool>& stop) {
    auto* p = static_cast<char*>(data);
    while (len > 0) {
        if (stop) return false;
        int ready = s.waitUntilReady(true, 200);
        if (ready < 0) return false;
        if (ready == 0) continue;
        int n = s.read(p, len, false);
        if (n <= 0) return false;
        p += n; len -= n;
    }
    return true;
}

juce::String hex6(uint32_t c) { return juce::String::toHexString((int) (c & 0xffffff)).paddedLeft('0', 6); }
juce::String clean(const std::string& s) { return juce::String(s).replaceCharacters("\r\n", "  "); }

} // namespace

// ============================================================================ client connection
class WebControl::Client : public juce::Thread {
public:
    Client(WebControl& o, juce::StreamingSocket* s) : juce::Thread("TEK web client"), owner(o), sock(s) { startThread(); }
    ~Client() override { stop = true; sock->close(); stopThread(2000); }
    bool finished() const { return done.load(); }
    bool isWebSocket() const { return ws.load(); }

private:
    void run() override {
        serve();
        sock->close();
        done = true;
    }

    void serve() {
        // ---- HTTP request header
        std::string req;
        char c;
        while (req.find("\r\n\r\n") == std::string::npos && req.size() < 16384) {
            if (!readExact(*sock, &c, 1, stop)) return;
            req.push_back(c);
        }
        juce::String r(req);
        juce::String path = r.upToFirstOccurrenceOf("\r\n", false, false).fromFirstOccurrenceOf(" ", false, false).upToFirstOccurrenceOf(" ", false, false);
        juce::String key;
        for (auto line : juce::StringArray::fromLines(r))
            if (line.startsWithIgnoreCase("Sec-WebSocket-Key:")) key = line.fromFirstOccurrenceOf(":", false, false).trim();

        if (path.startsWith("/ws") && key.isNotEmpty()) { webSocket(key); return; }

        // ---- the page
        const char* utf8 = kTekWebPage; // raw UTF-8 bytes
        const int len = (int) strlen(utf8);
        juce::String hdr = "HTTP/1.1 200 OK\r\nContent-Type: text/html; charset=utf-8\r\nCache-Control: no-store\r\nContent-Length: "
                         + juce::String(len) + "\r\nConnection: close\r\n\r\n";
        sendAll(*sock, hdr.toRawUTF8(), (int) strlen(hdr.toRawUTF8()));
        sendAll(*sock, utf8, len);
    }

    bool sendText(const juce::String& t) {
        auto* u = t.toRawUTF8();
        const size_t n = strlen(u);
        uint8_t h[10];
        int hl = 0;
        h[hl++] = 0x81;
        if (n < 126) h[hl++] = (uint8_t) n;
        else if (n < 65536) { h[hl++] = 126; h[hl++] = (uint8_t) (n >> 8); h[hl++] = (uint8_t) n; }
        else { h[hl++] = 127; for (int i = 7; i >= 0; --i) h[hl++] = (uint8_t) ((uint64_t) n >> (8 * i)); }
        return sendAll(*sock, h, hl) && sendAll(*sock, u, (int) n);
    }

    void webSocket(const juce::String& key) {
        juce::String resp = "HTTP/1.1 101 Switching Protocols\r\nUpgrade: websocket\r\nConnection: Upgrade\r\nSec-WebSocket-Accept: "
                          + WebControl::acceptKey(key) + "\r\n\r\n";
        if (!sendAll(*sock, resp.toRawUTF8(), (int) strlen(resp.toRawUTF8()))) return;
        ws = true;
        juce::String last;
        double lastSend = 0.0;
        tek::Frame frame;
        while (!stop && !threadShouldExit()) {
            // incoming frames
            int ready = sock->waitUntilReady(true, 10);
            if (ready < 0) return;
            if (ready > 0) {
                uint8_t h[2];
                if (!readExact(*sock, h, 2, stop)) return;
                const int op = h[0] & 0x0f;
                uint64_t n = h[1] & 0x7f;
                if (n == 126) { uint8_t e[2]; if (!readExact(*sock, e, 2, stop)) return; n = (uint64_t) e[0] << 8 | e[1]; }
                else if (n == 127) { uint8_t e[8]; if (!readExact(*sock, e, 8, stop)) return; n = 0; for (int i = 0; i < 8; ++i) n = n << 8 | e[i]; }
                if (n > 65536) return;
                uint8_t mask[4] = {0, 0, 0, 0};
                if (h[1] & 0x80) { if (!readExact(*sock, mask, 4, stop)) return; }
                std::vector<char> payload((size_t) n);
                if (n > 0 && !readExact(*sock, payload.data(), (int) n, stop)) return;
                for (size_t i = 0; i < payload.size(); ++i) payload[i] = (char) (payload[i] ^ mask[i % 4]);
                if (op == 8) { uint8_t cl[2] = {0x88, 0}; sendAll(*sock, cl, 2); return; }
                if (op == 9) { // ping -> pong
                    std::vector<uint8_t> pong = {0x8A, (uint8_t) std::min<size_t>(payload.size(), 125)};
                    pong.insert(pong.end(), payload.begin(), payload.begin() + (long) std::min<size_t>(payload.size(), 125));
                    sendAll(*sock, pong.data(), (int) pong.size());
                }
                if (op == 1) owner.handleMessage(juce::String::fromUTF8(payload.data(), (int) payload.size()));
                continue; // drain input before rendering
            }
            // outgoing frame (~30 Hz, only when it changed)
            const double now = juce::Time::getMillisecondCounterHiRes();
            if (now - lastSend < 33.0) continue;
            lastSend = now;
            {
                const juce::SpinLock::ScopedLockType sl(owner.proc.lock);
                owner.proc.controller().render(frame, now);
            }
            juce::String t = WebControl::frameToText(&frame);
            if (t != last) { if (!sendText(t)) return; last = t; }
        }
    }

    WebControl& owner;
    std::unique_ptr<juce::StreamingSocket> sock;
    std::atomic<bool> stop { false }, done { false }, ws { false };
};

// ============================================================================ server
WebControl::WebControl(TekProcessor& p) : juce::Thread("TEK web control"), proc(p) {
    for (int port = 8765; port < 8785; ++port)
        if (listener.createListener(port)) { listenPort = port; break; }
    if (listenPort > 0) startThread();
}

WebControl::~WebControl() {
    signalThreadShouldExit();
    if (listenPort > 0) { // wake the blocking accept() so the thread can exit
        juce::StreamingSocket poke;
        poke.connect("127.0.0.1", listenPort, 200);
    }
    listener.close();
    stopThread(3000);
    const juce::ScopedLock sl(clientLock);
    clients.clear();
}

void WebControl::run() {
    while (!threadShouldExit()) {
        std::unique_ptr<juce::StreamingSocket> s(listener.waitForNextConnection());
        if (threadShouldExit()) break;
        if (s == nullptr) { wait(50); continue; }
        removeFinished();
        const juce::ScopedLock sl(clientLock);
        if (clients.size() < 16) clients.add(new Client(*this, s.release()));
    }
}

void WebControl::removeFinished() {
    const juce::ScopedLock sl(clientLock);
    for (int i = clients.size(); --i >= 0;)
        if (clients[i]->finished()) clients.remove(i);
}

int WebControl::numClients() const {
    const juce::ScopedLock sl(clientLock);
    int n = 0;
    for (auto* c : clients) if (!c->finished() && c->isWebSocket()) ++n;
    return n;
}

juce::String WebControl::url() const {
    if (listenPort <= 0) return {};
    juce::String ip;
    for (auto& a : juce::IPAddress::getAllAddresses(false)) {
        auto t = a.toString();
        if (t.startsWith("127.") || t.startsWith("169.254.")) continue;
        if (ip.isEmpty() || t.startsWith("192.168.") || t.startsWith("10.")) ip = t;
    }
    if (ip.isEmpty()) ip = "127.0.0.1";
    return "http://" + ip + ":" + juce::String(listenPort);
}

juce::String WebControl::acceptKey(const juce::String& clientKey) {
    juce::String s = clientKey + "258EAFA5-E914-47DA-95CA-C5AB0DC85B11";
    uint8_t d[20];
    sha1(reinterpret_cast<const uint8_t*>(s.toRawUTF8()), strlen(s.toRawUTF8()), d);
    return juce::Base64::toBase64(d, 20);
}

juce::String WebControl::frameToText(const void* fp) {
    const auto& f = *static_cast<const tek::Frame*>(fp);
    juce::String t = "F\n";
    t << clean(f.header) << "\n" << clean(f.headerRight) << "\n" << clean(f.line1) << "\n" << clean(f.line2) << "\n";
    for (auto& l : f.labels) t << clean(l) << "\n";
    for (auto& v : f.values) t << clean(v) << "\n";
    t << clean(f.message) << "\n" << f.progress << "\n";
    juce::String p;
    p.preallocateBytes(128 * 6 + 1);
    for (auto c : f.pads) p << hex6(c);
    t << p << "\n";
    juce::String l;
    for (auto c : f.leds) l << hex6(c);
    t << l;
    return t;
}

// Messages: "pd x y" / "pu x y" pads, "bd n" / "bu n" buttons, "ed k" / "eu k" encoder press, "et k d" encoder turn
void WebControl::handleMessage(const juce::String& msg) {
    auto tok = juce::StringArray::fromTokens(msg, " ", "");
    if (tok.size() < 2) return;
    const auto& cmd = tok[0];
    const int a = tok[1].getIntValue(), b = tok.size() > 2 ? tok[2].getIntValue() : 0;
    const double now = juce::Time::getMillisecondCounterHiRes();
    const juce::SpinLock::ScopedLockType sl(proc.lock);
    auto& c = proc.controller();
    if (cmd == "pd") c.padDown(a, b, now);
    else if (cmd == "pu") c.padUp(a, b, now);
    else if (cmd == "bd" && a >= 0 && a < tek::kNumBtns) c.button((tek::Btn) a, true, now);
    else if (cmd == "bu" && a >= 0 && a < tek::kNumBtns) c.button((tek::Btn) a, false, now);
    else if (cmd == "ed") c.encoderPress(a, true, now);
    else if (cmd == "eu") c.encoderPress(a, false, now);
    else if (cmd == "et") c.encoderTurn(a, juce::jlimit(-8, 8, b), now);
}
