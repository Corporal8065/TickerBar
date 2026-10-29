#include "ScreenSpace.h"
#include <cstdio>
// PORTED from MeterBar v0.18 — READ THIS BEFORE TOUCHING THE GUARD
// (the MeterBar v0.14 lesson, inherited on purpose): this translation
// unit deliberately includes NO JUCE header — it is driven entirely
// through void* window handles the caller passes in. JUCE_WINDOWS
// exists only inside JUCE's own headers, so a `#if JUCE_WINDOWS` guard
// here would read 0 in EVERY build and the stub branch below would ship
// on Windows — that exact bug kept WASAPI loopback out of every MeterBar
// binary from v0.10 to v0.13. The guard therefore tests the compiler's
// own predefined target macro: MSVC and MinGW both predefine _WIN32 for
// Windows targets, no header needed. The build's verification greps THIS
// file's preprocessed Windows-target output for the "appbar active"
// status string, which exists only in the real branch — proof the
// branch is compiled in, not just present in source.
#if defined(_WIN32)
 #define TICKERBAR_SCREENSPACE_WINDOWS 1
#else
 #define TICKERBAR_SCREENSPACE_WINDOWS 0
#endif

#if TICKERBAR_SCREENSPACE_WINDOWS
#ifndef NOMINMAX
 #define NOMINMAX // keep windows.h's min/max macros out of the std:: calls below
#endif
 #include <windows.h>
 #include <shellapi.h>  // SHAppBarMessage / APPBARDATA
 #include <commctrl.h>  // SetWindowSubclass (AppBar callback messages)
 #include <dwmapi.h>    // DWMWA_CLOAKED (arrange sweep skips cloaked windows)

// ---- AppBar runtime state ----------------------------------------------
// Single-instance app with exactly one dockable window, so the one live
// registration is kept at file scope (MeterBar's design): the subclass
// callback below is a plain function pointer and needs somewhere to
// stand. The ScreenSpace methods are the only writers.
namespace {
struct AppBarState {
    HWND hwnd = nullptr;
    UINT callbackMsg = WM_APP + 0x54; // private app message for ABN_* notices
    UINT edge = ABE_TOP;
    bool registered = false;
    RECT applied{}; // the rect Windows last accepted via ABM_SETPOS
};
AppBarState g_appBar;

// ABN_POSCHANGED: another bar (or the taskbar) changed the available
// space — the documented response is to re-ask and re-set our position.
LRESULT CALLBACK appBarSubclassProc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp,
                                    UINT_PTR subclassId, DWORD_PTR refData) {
    (void)subclassId; (void)refData;
    if (msg == g_appBar.callbackMsg && wp == ABN_POSCHANGED && g_appBar.registered) {
        APPBARDATA abd{}; abd.cbSize = sizeof abd; abd.hWnd = g_appBar.hwnd;
        abd.uEdge = g_appBar.edge; abd.rc = g_appBar.applied;
        SHAppBarMessage(ABM_QUERYPOS, &abd);
        SHAppBarMessage(ABM_SETPOS, &abd);
        g_appBar.applied = abd.rc;
    }
    return DefSubclassProc(hwnd, msg, wp, lp);
}
} // namespace

struct ScreenSpace::Impl {
    std::string status = "not registered";
    std::vector<SavedWindow> saved;
};

bool ScreenSpace::reserve(void* hwndV, int edge, int physX, int physY, int physW, int physH) {
    HWND hwnd = static_cast<HWND>(hwndV);
    if (!hwnd || physW <= 0 || physH <= 0) return g_appBar.registered;
    const UINT uEdge = edge == 1 ? ABE_BOTTOM : ABE_TOP;
    const RECT want{ physX, physY, physX + physW, physY + physH };
    if (g_appBar.registered && g_appBar.hwnd == hwnd && g_appBar.edge == uEdge &&
        want.left == g_appBar.applied.left && want.top == g_appBar.applied.top &&
        want.right == g_appBar.applied.right && want.bottom == g_appBar.applied.bottom)
        return true; // already exactly this bar — the UI re-sync lands here
    if (g_appBar.registered) release(); // moved / resized / other edge or monitor
    APPBARDATA abd{}; abd.cbSize = sizeof abd; abd.hWnd = hwnd;
    abd.uCallbackMessage = g_appBar.callbackMsg;
    if (!SHAppBarMessage(ABM_NEW, &abd)) {
        impl_->status = "appbar registration refused by Windows";
        return false;
    }
    g_appBar.hwnd = hwnd;
    g_appBar.edge = uEdge;
    SetWindowSubclass(hwnd, appBarSubclassProc, 1, 0);
    abd.uEdge = uEdge; abd.rc = want;
    SHAppBarMessage(ABM_QUERYPOS, &abd);
    abd.rc.left = want.left; abd.rc.right = want.right; // our span along the edge is the ask; the system may trim the thickness
    if (uEdge == ABE_TOP) abd.rc.top = want.top; else abd.rc.bottom = want.bottom;
    SHAppBarMessage(ABM_SETPOS, &abd);
    g_appBar.applied = abd.rc;
    g_appBar.registered = true;
    char buf[96];
    std::snprintf(buf, sizeof buf, "appbar active - %s strip %ld px reserved",
                  uEdge == ABE_TOP ? "top" : "bottom",
                  (long)(abd.rc.bottom - abd.rc.top));
    impl_->status = buf;
    return true;
}

void ScreenSpace::release() {
    if (!g_appBar.registered || !g_appBar.hwnd) { g_appBar.registered = false; return; }
    APPBARDATA abd{}; abd.cbSize = sizeof abd; abd.hWnd = g_appBar.hwnd;
    SHAppBarMessage(ABM_REMOVE, &abd);
    RemoveWindowSubclass(g_appBar.hwnd, appBarSubclassProc, 1);
    g_appBar.hwnd = nullptr;
    g_appBar.registered = false;
    impl_->status = "not registered";
}

bool ScreenSpace::isReserved() const { return g_appBar.registered; }

// ---- Arrange sweep ------------------------------------------------------
namespace {
struct ArrangeMove { HWND hwnd; tickerbar::IntRect from, to; };
struct ArrangeCtx {
    HMONITOR mon; HWND exclude; int strip; int edge;
    tickerbar::IntRect monRect;
    std::vector<ArrangeMove> moves;
};
BOOL CALLBACK arrangeEnumProc(HWND hwnd, LPARAM lp) {
    auto* ctx = reinterpret_cast<ArrangeCtx*>(lp);
    if (hwnd == ctx->exclude || hwnd == GetShellWindow()) return TRUE;
    // Minimised windows have no meaningful on-screen rect; MAXIMISED
    // windows are deliberately left alone — the AppBar reservation owns
    // them (Windows sizes them against the shrunken work area).
    if (!IsWindowVisible(hwnd) || IsIconic(hwnd) || IsZoomed(hwnd)) return TRUE;
    if (MonitorFromWindow(hwnd, MONITOR_DEFAULTTONEAREST) != ctx->mon) return TRUE;
    DWORD cloaked = 0; // other virtual desktops / suspended UWP ghosts
    if (SUCCEEDED(DwmGetWindowAttribute(hwnd, DWMWA_CLOAKED, &cloaked, sizeof cloaked)) && cloaked)
        return TRUE;
    if (GetWindowLongPtr(hwnd, GWL_EXSTYLE) & WS_EX_TOOLWINDOW) return TRUE;
    RECT r{};
    if (!GetWindowRect(hwnd, &r)) return TRUE;
    const tickerbar::IntRect wr{ r.left, r.top, r.right - r.left, r.bottom - r.top };
    if (wr.w <= 0 || wr.h <= 0) return TRUE;
    // Fit FIRST, move later: mutating windows mid-enumeration can disturb
    // the enumeration itself, so this pass only collects the plan.
    const tickerbar::IntRect fit = ctx->edge == 1
        ? tickerbar::arrangeFitAbove(wr, ctx->monRect, ctx->strip)
        : tickerbar::arrangeFit(wr, ctx->monRect, ctx->strip);
    if (fit != wr) ctx->moves.push_back({ hwnd, wr, fit });
    return TRUE;
}
} // namespace

int ScreenSpace::arrange(void* hwndV, int stripPhysPx, int edge) {
    HWND hwnd = static_cast<HWND>(hwndV);
    if (!hwnd || stripPhysPx <= 0) {
        impl_->status = "arrange: no strip to clear (dock the bar at a monitor edge first)";
        return 0;
    }
    const HMONITOR mon = MonitorFromWindow(hwnd, MONITOR_DEFAULTTONEAREST);
    MONITORINFO mi{}; mi.cbSize = sizeof mi;
    if (!GetMonitorInfoW(mon, &mi)) { impl_->status = "arrange: monitor lookup failed"; return 0; }
    ArrangeCtx ctx{ mon, hwnd, stripPhysPx, edge,
                    { mi.rcMonitor.left, mi.rcMonitor.top,
                      mi.rcMonitor.right - mi.rcMonitor.left,
                      mi.rcMonitor.bottom - mi.rcMonitor.top }, {} };
    EnumWindows(arrangeEnumProc, reinterpret_cast<LPARAM>(&ctx));
    impl_->saved.clear(); // each sweep snapshots fresh originals
    int moved = 0;
    for (auto& mv : ctx.moves) {
        impl_->saved.push_back({ mv.hwnd, mv.from });
        if (SetWindowPos(mv.hwnd, nullptr, mv.to.x, mv.to.y, mv.to.w, mv.to.h,
                         SWP_NOZORDER | SWP_NOACTIVATE))
            ++moved;
    }
    char buf[96];
    std::snprintf(buf, sizeof buf, "arranged %d window(s) clear of the bar", moved);
    impl_->status = buf;
    return moved;
}

int ScreenSpace::restore() {
    int n = 0;
    for (auto it = impl_->saved.rbegin(); it != impl_->saved.rend(); ++it) {
        HWND hwnd = static_cast<HWND>(it->hwnd);
        if (IsWindow(hwnd) && SetWindowPos(hwnd, nullptr, it->rect.x, it->rect.y,
                                            it->rect.w, it->rect.h,
                                            SWP_NOZORDER | SWP_NOACTIVATE))
            ++n;
    }
    impl_->saved.clear();
    char buf[96];
    std::snprintf(buf, sizeof buf, "restored %d window(s)", n);
    impl_->status = buf;
    return n;
}

std::string ScreenSpace::statusInfo() const { return impl_->status; }

#else // ---- non-Windows stub: same idiom as MeterBar's ScreenSpace ------
struct ScreenSpace::Impl {
    std::string status = "unavailable (non-Windows build)";
    std::vector<SavedWindow> saved;
};
bool ScreenSpace::reserve(void*, int, int, int, int, int) { return false; }
void ScreenSpace::release() {}
bool ScreenSpace::isReserved() const { return false; }
int ScreenSpace::arrange(void*, int, int) { return 0; }
int ScreenSpace::restore() { return 0; }
std::string ScreenSpace::statusInfo() const { return impl_->status; }
#endif

ScreenSpace::ScreenSpace() : impl_(new Impl) {}
ScreenSpace::~ScreenSpace() { release(); delete impl_; }
