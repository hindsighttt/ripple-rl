#include "BackendHost.h"

#include <windows.h>

#include "Dispatcher.h"
#include "Features.h"
#include "Fps.h"
#include "IpcServer.h"
#include "Overlay.h"
#include "ProxyManager.h"
#include "Quickchat.h"
#include "Stats.h"
#include "Store.h"
#include "WinUtil.h"

#include <atomic>
#include <cstdio>
#include <memory>
#include <string>
#include <thread>

namespace {

// Drive the overlays and push a throttled
// tracker.changed event to the UI.
void OnStats(const std::string& ev, const json& data, Overlays& overlays, IpcServer& server) {
    const bool clear = ev == "MatchDestroyed" || ev == "StatsDisconnected";
    if (ev != "UpdateState" && !clear)
        return;
    json game = (data.is_object() && data.contains("Game") && data["Game"].is_object()) ? data["Game"] : json::object();
    json players =
        (data.is_object() && data.contains("Players") && data["Players"].is_array()) ? data["Players"] : json::array();
    bool inMatch = !players.empty();

    if (overlays.BallVisible()) {
        json ts = features::Get("tracker.settings");
        const auto ball = game.find("Ball");
        if (ball != game.end() && ball->is_object() && ball->contains("Speed") && ball->at("Speed").is_number())
            overlays.SetBall(ball->at("Speed").get<double>(), ts.value("ballUnit", "kmh"), ts.value("ballDecimals", 2));
        else
            overlays.ClearBall();
    }
    if (overlays.RosterVisible())
        overlays.SetRoster(players);

    static double lastT = 0.0;
    static int prevIn = -1;
    double now = (double)GetTickCount64() / 1000.0;
    const bool matchChanged = static_cast<int>(inMatch) != prevIn;
    if (clear || matchChanged || now - lastT >= 0.25) {
        lastT = now;
        prevIn = inMatch ? 1 : 0;
        server.Broadcast(json{{"type", "tracker.changed"},
                              {"payload", json{{"event", ev}, {"players", players}, {"game", game}}}});
        if (matchChanged) {
            json f = features::Get("tracker");
            f["inMatch"] = inMatch;
            store::WriteJson("feature_tracker.json", f);
        }
    }
}

}  // namespace

struct BackendHost::Impl {
    ProxyManager proxy;
    Overlays overlays;
    quickchat::Engine qc;
    IpcServer server;
    std::unique_ptr<Dispatcher> dispatcher;
    std::unique_ptr<stats::Client> stats;
    std::jthread fpsWorker;
    std::thread worker;
    std::atomic<bool> run{false};
};

BackendHost::BackendHost() : p_(std::make_unique<Impl>()) {}
BackendHost::~BackendHost() { Stop(); }

bool BackendHost::Start(unsigned short ipcPort) {
    store::Init();

    json settings = store::Settings();
    if (settings.value("proxyEnabled", true) && win::GetEnvW(L"RIPPLE_RL_NO_PROXY").empty()) {
        try {
            p_->proxy.Start();
        } catch (const std::exception& e) {
            std::printf("proxy: %s\n", e.what());
        }
    }

    if (!p_->server.Start(ipcPort)) return false;

    p_->dispatcher = std::make_unique<Dispatcher>(&p_->server, &p_->proxy);
    Overlays* ov = &p_->overlays;
    auto applyOverlays = [ov]() {
        json ts = features::Get("tracker.settings");
        ov->ShowBall(ts.value("ballEnabled", false));
        ov->ShowRoster(ts.value("rosterEnabled", false));
        ov->SetRosterStyle(ts.value("overlayStyle", "compact"));
        ov->ShowFps(features::Get("fps").value("on", false));
    };
    p_->dispatcher->SetOverlayApply(applyOverlays);
    applyOverlays();

    quickchat::Engine* qc = &p_->qc;
    p_->dispatcher->SetQcReload([qc]() { qc->Reload(features::Get("qc")); });
    qc->Reload(features::Get("qc"));

    IpcServer* srv = &p_->server;
    p_->stats = std::make_unique<stats::Client>(
        [ov, srv](const std::string& ev, const json& data) { OnStats(ev, data, *ov, *srv); });
    p_->stats->Start();
    p_->fpsWorker = std::jthread([ov](std::stop_token stop) { fps::Run(stop, *ov); });

    p_->run = true;
    Dispatcher* disp = p_->dispatcher.get();
    p_->worker = std::thread([this, disp, srv]() {
        while (p_->run) {
            srv->Drain([&](const json& m) { disp->Dispatch(m); });
            Sleep(4);
        }
    });
    return true;
}

void BackendHost::Stop() {
    if (p_->fpsWorker.joinable()) {
        p_->fpsWorker.request_stop();
        p_->fpsWorker.join();
    }
    if (p_->run.exchange(false)) {
        if (p_->worker.joinable()) p_->worker.join();
    }
    if (p_->stats) {
        p_->stats->Shutdown();
        p_->stats.reset();
    }
    p_->server.Stop();
    try {
        p_->proxy.Stop();
    } catch (...) {
    }
}
