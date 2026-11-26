#pragma once
// The whole native backend, hostable in-process. ripple_backend.exe runs it as a
// console app; ripple_rl.exe starts it on a background thread so the single
// executable needs no separate backend process (the UI's socket client then
// connects to this over loopback). Only <memory> is pulled in here so the UI
// can include it without the backend's Windows/JSON headers.
#include <memory>

class BackendHost {
public:
    BackendHost();
    ~BackendHost();

    // Start store/proxy/overlays/stats + the IPC server and dispatch loop.
    // Returns false if the IPC port could not be bound. Non-blocking.
    bool Start(unsigned short ipcPort = 49327);
    void Stop();

private:
    struct Impl;
    std::unique_ptr<Impl> p_;
};
