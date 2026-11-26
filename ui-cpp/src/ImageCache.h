#pragma once

// ImageCache: fetches item thumbnails (jsDelivr PNGs) on worker threads, decodes
// them with stb_image, and uploads them to DX11 textures on the UI thread. Get()
// returns a texture id or 0 while it loads; failed URLs return 0 permanently.

#include "imgui.h"

#include <atomic>
#include <condition_variable>
#include <d3d11.h>
#include <deque>
#include <mutex>
#include <string>
#include <thread>
#include <unordered_map>
#include <vector>

class ImageCache {
public:
    void Init(ID3D11Device* device, int workers = 4);
    void Shutdown();

    // Texture for url, or 0 if not ready (queues a fetch on first call).
    ImTextureID Get(const std::string& url);

    // UI thread, once per frame: upload decoded images to the GPU.
    void Poll();

private:
    enum class State { None, Loading, Ready, Failed };
    struct Entry {
        State state = State::None;
        ImTextureID tex = 0;
        ID3D11ShaderResourceView* srv = nullptr;
    };
    struct Decoded {
        std::string url;
        int w = 0, h = 0;
        std::vector<unsigned char> rgba;
        bool ok = false;
    };

    void WorkerLoop();

    ID3D11Device* device_ = nullptr;
    std::unordered_map<std::string, Entry> entries_;
    std::deque<std::string> queue_;
    std::vector<Decoded> decoded_;
    std::vector<std::thread> workers_;
    std::mutex mutex_;
    std::condition_variable cv_;
    std::atomic<bool> running_{false};
};
