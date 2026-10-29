#include "ToastNotifier.h"
// Same _WIN32 guard discipline as ScreenSpace.cpp: this TU includes no
// JUCE header, so JUCE_WINDOWS would read 0 everywhere — test the
// compiler's own predefined target macro instead.
#if defined(_WIN32)
 #define TICKERBAR_TOAST_WINDOWS 1
#else
 #define TICKERBAR_TOAST_WINDOWS 0
#endif

#if TICKERBAR_TOAST_WINDOWS
#ifndef NOMINMAX
 #define NOMINMAX
#endif
 #include <windows.h>
 #include <shellapi.h>
#include <cwchar>

struct ToastNotifier::Impl {
    std::string status = "no notification sent yet";
    HWND hwnd = nullptr;
    bool iconAdded = false;
    static constexpr UINT kIconId = 0x7B1;
    NOTIFYICONDATAW makeData(DWORD flags) {
        NOTIFYICONDATAW nid{};
        nid.cbSize = sizeof nid;
        nid.hWnd = hwnd;
        nid.uID = kIconId;
        nid.uFlags = flags;
        return nid;
    }
};

namespace {
std::wstring widen(const std::string& s) {
    if (s.empty()) return L"";
    const int n = MultiByteToWideChar(CP_UTF8, 0, s.data(), (int)s.size(), nullptr, 0);
    std::wstring out((size_t)std::max(n, 0), L'\0');
    if (n > 0) MultiByteToWideChar(CP_UTF8, 0, s.data(), (int)s.size(), out.data(), n);
    return out;
}
void copyWide(wchar_t* dst, size_t cap, const std::wstring& src) {
    if (cap == 0) return;
    wcsncpy_s(dst, cap, src.c_str(), _TRUNCATE);
}
} // namespace

ToastNotifier::ToastNotifier() : impl_(new Impl) {}
ToastNotifier::~ToastNotifier() {
    if (impl_->iconAdded && impl_->hwnd) {
        auto nid = impl_->makeData(0);
        Shell_NotifyIconW(NIM_DELETE, &nid);
    }
    delete impl_;
}

bool ToastNotifier::show(void* hwndV, const std::string& title, const std::string& text) {
    HWND hwnd = static_cast<HWND>(hwndV);
    if (!hwnd || title.empty() || text.empty()) { impl_->status = "toast: no window handle or empty text"; return false; }
    if (impl_->iconAdded && impl_->hwnd != hwnd) { // window recreated: drop the stale icon
        auto old = impl_->makeData(0);
        Shell_NotifyIconW(NIM_DELETE, &old);
        impl_->iconAdded = false;
    }
    impl_->hwnd = hwnd;
    if (!impl_->iconAdded) {
        auto nid = impl_->makeData(NIF_ICON | NIF_TIP | NIF_GUID);
        nid.guidItem = { 0x7b1c0de0, 0x7b1c, 0x4b1c, { 0x9e, 0x11, 0x54, 0x22, 0x10, 0x29, 0x7b, 0x01 } };
        nid.hIcon = LoadIconW(nullptr, IDI_INFORMATION);
        copyWide(nid.szTip, ARRAYSIZE(nid.szTip), L"TickerBar price alerts");
        // NIF_GUID can fail on a first-ever registration race; retry flagless identity (hwnd+id).
        if (!Shell_NotifyIconW(NIM_ADD, &nid)) {
            nid.uFlags = NIF_ICON | NIF_TIP;
            if (!Shell_NotifyIconW(NIM_ADD, &nid)) { impl_->status = "toast: tray icon registration refused"; return false; }
        }
        impl_->iconAdded = true;
    }
    auto nid = impl_->makeData(NIF_INFO);
    nid.dwInfoFlags = NIIF_INFO;
    copyWide(nid.szInfoTitle, ARRAYSIZE(nid.szInfoTitle), widen(title));
    copyWide(nid.szInfo, ARRAYSIZE(nid.szInfo), widen(text));
    if (!Shell_NotifyIconW(NIM_MODIFY, &nid)) { impl_->status = "toast: notification refused by the shell"; return false; }
    impl_->status = "toast sent: " + text;
    return true;
}
std::string ToastNotifier::statusInfo() const { return impl_->status; }

#else
struct ToastNotifier::Impl { std::string status = "unavailable (non-Windows build)"; };
ToastNotifier::ToastNotifier() : impl_(new Impl) {}
ToastNotifier::~ToastNotifier() { delete impl_; }
bool ToastNotifier::show(void*, const std::string&, const std::string&) { return false; }
std::string ToastNotifier::statusInfo() const { return impl_->status; }
#endif
