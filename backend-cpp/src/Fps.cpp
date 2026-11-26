#include "Fps.h"

#include <windows.h>
#include <tlhelp32.h>

#include "Overlay.h"
#include "Store.h"
#include "WinUtil.h"

#include <algorithm>
#include <charconv>
#include <chrono>
#include <cmath>
#include <condition_variable>
#include <filesystem>
#include <map>
#include <mutex>
#include <sstream>
#include <string>
#include <string_view>
#include <vector>

namespace {
constexpr wchar_t kGameProcess[] = L"RocketLeague.exe";
constexpr wchar_t kPresentMon[] = L"PresentMon.exe";

DWORD GameProcessId() {
    const HANDLE snapshot = CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS, 0);
    if (snapshot == INVALID_HANDLE_VALUE)
        return 0;
    PROCESSENTRY32W entry{};
    entry.dwSize = sizeof(entry);
    DWORD pid = 0;
    for (BOOL more = Process32FirstW(snapshot, &entry); more; more = Process32NextW(snapshot, &entry)) {
        if (_wcsicmp(entry.szExeFile, kGameProcess) == 0) {
            pid = entry.th32ProcessID;
            break;
        }
    }
    CloseHandle(snapshot);
    return pid;
}

std::filesystem::path CaptureTool() {
    for (const auto& base : {win::ExeDir(), store::AppDir() / "dist"}) {
        auto exe = base / "runtime" / kPresentMon;
        std::error_code error;
        if (std::filesystem::is_regular_file(exe, error))
            return exe;
    }
    return {};
}

std::vector<std::string_view> CsvFields(std::string_view line) {
    if (line.ends_with('\r'))
        line.remove_suffix(1);
    std::vector<std::string_view> fields;
    for (auto comma = line.find(','); comma != line.npos; comma = line.find(',')) {
        fields.push_back(line.substr(0, comma));
        line.remove_prefix(comma + 1);
    }
    fields.push_back(line);
    return fields;
}

double FrameTime(const std::string& output, DWORD pid) {
    std::istringstream lines(output);
    std::string line;
    size_t address = std::string::npos;
    size_t interval = std::string::npos;
    size_t process = std::string::npos;
    const auto processId = std::to_string(pid);
    struct SwapChain { double sum = 0; int samples = 0; };
    std::map<std::string, SwapChain> chains;
    while (std::getline(lines, line)) {
        const auto fields = CsvFields(line);
        if (fields.front() == "Application") {
            address = std::find(fields.begin(), fields.end(), "SwapChainAddress") - fields.begin();
            interval = std::find(fields.begin(), fields.end(), "msBetweenPresents") - fields.begin();
            process = std::find(fields.begin(), fields.end(), "ProcessID") - fields.begin();
            if (address == fields.size() || interval == fields.size() || process == fields.size())
                return 0;
            continue;
        }
        if (fields.size() <= (std::max)({address, interval, process}) || fields[process] != processId)
            continue;
        const auto cell = fields[interval];
        double ms = 0;
        const auto parsed = std::from_chars(cell.data(), cell.data() + cell.size(), ms);
        if (parsed.ec != std::errc{} || parsed.ptr != cell.data() + cell.size() || !std::isfinite(ms) || ms <= 0)
            continue;
        auto& chain = chains[std::string(fields[address])];
        chain.sum += ms;
        ++chain.samples;
    }
    const auto busiest = std::max_element(chains.begin(), chains.end(), [](const auto& a, const auto& b) {
        return a.second.samples < b.second.samples;
    });
    return busiest == chains.end() ? 0 : busiest->second.sum / busiest->second.samples;
}
}

void fps::Run(std::stop_token stop, Overlays& overlays) {
    using namespace std::chrono_literals;
    std::mutex mutex;
    std::condition_variable_any delay;
    const std::wstring session = L"RippleFPS-" + std::to_wstring(GetCurrentProcessId());
    while (!stop.stop_requested()) {
        auto pause = 500ms;
        if (overlays.FpsVisible()) {
            const DWORD pid = GameProcessId();
            const auto tool = CaptureTool();
            if (pid == 0) {
                overlays.SetFpsText(L"Waiting for Rocket League");
            } else if (tool.empty()) {
                overlays.SetFpsText(L"PresentMon is missing");
            } else {
                std::string output;
                const auto cmd = L"\"" + tool.wstring() + L"\" --process_id " + std::to_wstring(pid) +
                                 L" --session_name " + session + L" --output_stdout --no_console_stats --v1_metrics"
                                 L" --no_track_display --no_track_gpu --no_track_input --timed 2"
                                 L" --terminate_after_timed";
                const int exitCode = win::RunCapture(cmd, output);
                if (!stop.stop_requested() && overlays.FpsVisible()) {
                    const double ms = exitCode == 0 ? FrameTime(output, pid) : 0;
                    if (ms > 0) {
                        wchar_t label[64];
                        swprintf(label, 64, L"FPS  %.0f   MS  %.1f", 1000.0 / ms, ms);
                        overlays.SetFpsText(label);
                        pause = 0ms;
                    } else if (exitCode != 0) {
                        overlays.SetFpsText(L"FPS capture unavailable");
                        pause = 2s;
                    } else {
                        overlays.SetFpsText(L"Waiting for game frames");
                    }
                }
            }
        }
        std::unique_lock lock(mutex);
        delay.wait_for(lock, stop, pause, [] { return false; });
    }
}
