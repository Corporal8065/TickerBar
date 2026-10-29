#pragma once
#include <string>
// Windows toast-style alert notifications (v0.2.0, item f).
// Implementation note: a proper WinRT toast needs a registered AppUserModelID
// (an installed/shortcut-registered app identity); an unpackaged test-kit exe
// does not have one. This notifier therefore uses the supported unpackaged
// route — a Shell_NotifyIcon balloon, which Windows 10/11 renders in the
// notification area as a toast-style banner and keeps in Action Center's
// notification history for the tray icon. The tray icon exists only after
// the first notification and is removed on destruction. Non-Windows builds
// compile the stub: show() returns false and statusInfo() says unavailable.
class ToastNotifier {
public:
    ToastNotifier();
    ~ToastNotifier();
    // hwnd = the bar's top-level native handle (void*). Returns true when
    // the notification was handed to the shell.
    bool show(void* hwnd, const std::string& title, const std::string& text);
    std::string statusInfo() const;
private:
    struct Impl; Impl* impl_ = nullptr;
};
