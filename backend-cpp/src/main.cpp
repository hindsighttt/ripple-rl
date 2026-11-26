// ripple_backend.exe: runs the native backend as a standalone console process.
// (The same backend is hosted in-process by ripple_rl.exe via BackendHost, so the
// single-exe build needs no separate backend process.)
#include <windows.h>

#include "BackendHost.h"

#include <atomic>
#include <cstdio>

namespace {
std::atomic<bool> g_run{true};

BOOL WINAPI CtrlHandler(DWORD) {
    g_run = false;  // let main() run BackendHost::Stop() for a clean proxy teardown
    return TRUE;
}
}  // namespace

int main() {
    BackendHost host;
    SetConsoleCtrlHandler(CtrlHandler, TRUE);
    if (!host.Start()) {
        std::puts("IPC: failed to bind 127.0.0.1:49327 (another instance running?)");
        return 1;
    }
    std::puts("Ripple RL backend (native): IPC listening on 127.0.0.1:49327");
    while (g_run) Sleep(100);
    host.Stop();
    return 0;
}
