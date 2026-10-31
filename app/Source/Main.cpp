#include <juce_gui_basics/juce_gui_basics.h>
#include <juce_gui_extra/juce_gui_extra.h>
#include "MainComponent.h"
#include <tickerbar/WindowGeom.hpp> // v0.3.0: startup position clamp (never restore off-screen)
#if JUCE_WINDOWS
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>
namespace {
// v0.4.0 (Jackson's ask): a REAL backdrop blur for the bar window —
// Windows acrylic (blur + a dark plum tint) behind the window's
// translucent pixels, via the window-composition attribute. Loaded
// dynamically so nothing new links; falls back to plain
// blur-behind, and to nothing at all where unsupported (the painted
// backgrounds are unchanged either way, so this is pure gain).
struct AccentPolicy { int accentState; int accentFlags; unsigned int gradientColor; int animationId; };
struct CompositionAttribData { int attribute; void* data; size_t sizeOfData; };
void enableBackdropBlur(void* hwnd) {
    if (hwnd == nullptr) return;
    using SetWindowCompositionAttributeFn = BOOL(WINAPI*)(HWND, CompositionAttribData*);
    auto* user32 = GetModuleHandleW(L"user32.dll");
    auto fn = user32 != nullptr ? (SetWindowCompositionAttributeFn)GetProcAddress(user32, "SetWindowCompositionAttribute") : nullptr;
    if (fn == nullptr) return;
    AccentPolicy accent {};
    accent.accentState = 4;                    // ACCENT_ENABLE_ACRYLICBLURBEHIND
    accent.accentFlags = 2;
    accent.gradientColor = 0xC4140d1cu;        // ABGR: dark plum tint, ~77% opaque
    CompositionAttribData data {};
    data.attribute = 19;                       // WCA_ACCENT_POLICY
    data.data = &accent;
    data.sizeOfData = sizeof(accent);
    if (!fn((HWND)hwnd, &data)) {
        accent.accentState = 3;                // ACCENT_ENABLE_BLURBEHIND
        fn((HWND)hwnd, &data);
    }
}
} // namespace
#endif
class MainWindow : public juce::DocumentWindow {
public:
    explicit MainWindow(const juce::String& name) : juce::DocumentWindow(name, juce::Colour(0x00000000), 0) {
        setUsingNativeTitleBar(false);
        setTitleBarHeight(0);
        // Per-pixel transparent top level (MeterBar v0.13 pattern): the
        // window background is fully clear, so wherever the content
        // paints with alpha (the Background Opacity setting) the
        // desktop shows through. v0.4.0: the desktop behind those
        // pixels is now really blurred (acrylic, enableBackdropBlur
        // below) instead of showing through sharp.
        setOpaque(false);
        auto* mc = new MainComponent();
        content = mc;
        setContentOwned(mc, true);
        setResizable(true, false);
        setResizeLimits(160, 48, 7680, 2160); // v0.4.0: free resize — Jackson's ask; overflow scrolls (marquee)
        // v0.3.0: clamp the restored rect into a visible work area —
        // a bar must never restore off-screen (SessionBar's field bug,
        // fixed family-wide; the pure rule is core WindowGeom).
        {
            std::vector<tickerbar::IntRect> areas;
            for (auto& d : juce::Desktop::getInstance().getDisplays().displays) {
                auto ua = d.userArea;
                areas.push_back({ ua.getX(), ua.getY(), ua.getWidth(), ua.getHeight() });
            }
            auto clamped = tickerbar::clampRectToWorkAreas(
                { mc->state.windowX, mc->state.windowY, mc->state.windowW, mc->state.windowH }, areas);
            setBounds(clamped.x, clamped.y, clamped.w, clamped.h);
        }
        setAlwaysOnTop(mc->state.pinned);
        setVisible(true);
#if JUCE_WINDOWS
        if (auto* peer = getPeer()) enableBackdropBlur(peer->getNativeHandle()); // v0.4.0 acrylic backdrop
#endif
    }
    void closeButtonPressed() override { juce::JUCEApplication::getInstance()->systemRequestedQuit(); }
    // v0.2.0: the reserve machine is EVENT-driven — the window's own
    // moved/resized notifications are its geometry feed (these fire
    // for native drags and border resizes too).
    void moved() override { if (content != nullptr) content->notifyWindowGeometryChanged(); }
    void resized() override {
        juce::DocumentWindow::resized();
        if (content != nullptr) content->notifyWindowGeometryChanged();
    }
private:
    MainComponent* content = nullptr; // owned by the window (setContentOwned) — never delete here
};
class TickerBarApp : public juce::JUCEApplication {
public:
    const juce::String getApplicationName() override { return "TickerBar"; }
    const juce::String getApplicationVersion() override { return "0.4.0"; }
    bool moreThanOneInstanceAllowed() override { return false; }
    void initialise(const juce::String&) override {
        // JUCE free-license attribution splash (MeterBar v0.15 pattern):
        // small, brief — "Made with JUCE" for ~1.5 s, click dismisses.
        juce::Image splashImg(juce::Image::RGB, 440, 160, true);
        {
            juce::Graphics g(splashImg);
            g.fillAll(juce::Colour(0xff17141f));
            g.setColour(juce::Colour(0xffe04c89)); g.setFont(juce::Font(30.f, juce::Font::bold));
            g.drawText("TickerBar", splashImg.getBounds().withTrimmedBottom(52), juce::Justification::centred);
            g.setColour(juce::Colour(0xfff6eff3)); g.setFont(15.f);
            g.drawText("Made with JUCE", splashImg.getBounds().withTrimmedTop(88), juce::Justification::centred);
        }
        // SplashScreen deletes ITSELF after the delay — never own it.
        splash = new juce::SplashScreen("TickerBar", splashImg, true);
        splash->deleteAfterDelay(juce::RelativeTime::milliseconds(1500), true);
        mainWindow = std::make_unique<MainWindow>("TickerBar");
    }
    void shutdown() override { mainWindow.reset(); }
    void systemRequestedQuit() override { quit(); }
private:
    std::unique_ptr<MainWindow> mainWindow;
    juce::SplashScreen* splash = nullptr; // self-deleting — never delete via this pointer
};
START_JUCE_APPLICATION(TickerBarApp)
