#pragma once
// Native port of ripple/bridge.py's command dispatch. Holds a table of
// {command -> handler}. Commands not yet ported raise "Not implemented yet: X"
// exactly as the Python Bridge did, so the UI degrades identically while the
// port is in progress.
#include <nlohmann/json.hpp>

#include <functional>
#include <string>
#include <unordered_map>

using json = nlohmann::json;

class IpcServer;
class ProxyManager;

class Dispatcher {
public:
    Dispatcher(IpcServer* server, ProxyManager* proxy);
    void Dispatch(const json& msg);

    // Called after overlay.set / fps.set / tracker.set (mirrors app.apply_overlays).
    void SetOverlayApply(std::function<void()> f) { overlayApply_ = std::move(f); }
    // Called after misc.qc.* changes (mirrors app.reload_qc).
    void SetQcReload(std::function<void()> f) { qcReload_ = std::move(f); }

private:
    using Handler = std::function<json(const json&)>;
    void Register();
    void Emit(const std::string& type, const json& payload);
    json CustomApply(const json& p, const std::string& kindOverride);  // custom.apply (+ball event)

    IpcServer* server_;
    ProxyManager* proxy_;
    std::function<void()> overlayApply_;
    std::function<void()> qcReload_;
    std::unordered_map<std::string, Handler> handlers_;
};
