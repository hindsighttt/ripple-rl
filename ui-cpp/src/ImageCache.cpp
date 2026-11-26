#include "ImageCache.h"

#include <windows.h>
#include <winhttp.h>

#define STB_IMAGE_IMPLEMENTATION
#define STBI_ONLY_PNG
#define STBI_ONLY_JPEG
#include "stb_image.h"

#pragma comment(lib, "winhttp.lib")

namespace {
    std::wstring Widen(const std::string& s) {
        if (s.empty())
            return std::wstring();
        int n = MultiByteToWideChar(CP_UTF8, 0, s.c_str(), (int)s.size(), nullptr, 0);
        std::wstring w(n, 0);
        MultiByteToWideChar(CP_UTF8, 0, s.c_str(), (int)s.size(), w.data(), n);
        return w;
    }

    bool HttpGet(const std::string& url, std::vector<unsigned char>& out) {
        std::wstring wurl = Widen(url);
        URL_COMPONENTS uc = {};
        uc.dwStructSize = sizeof(uc);
        wchar_t host[256] = {}, path[2048] = {};
        uc.lpszHostName = host;
        uc.dwHostNameLength = 255;
        uc.lpszUrlPath = path;
        uc.dwUrlPathLength = 2047;
        if (!WinHttpCrackUrl(wurl.c_str(), 0, 0, &uc))
            return false;

        HINTERNET session = WinHttpOpen(L"RippleRL/1.0", WINHTTP_ACCESS_TYPE_AUTOMATIC_PROXY,
                                        WINHTTP_NO_PROXY_NAME, WINHTTP_NO_PROXY_BYPASS, 0);
        if (!session)
            return false;
        DWORD redirect = WINHTTP_OPTION_REDIRECT_POLICY_ALWAYS;
        WinHttpSetOption(session, WINHTTP_OPTION_REDIRECT_POLICY, &redirect, sizeof(redirect));

        bool ok = false;
        HINTERNET conn = WinHttpConnect(session, uc.lpszHostName, uc.nPort, 0);
        if (conn) {
            DWORD flags = (uc.nScheme == INTERNET_SCHEME_HTTPS) ? WINHTTP_FLAG_SECURE : 0;
            HINTERNET req = WinHttpOpenRequest(conn, L"GET", uc.lpszUrlPath, nullptr,
                                               WINHTTP_NO_REFERER, WINHTTP_DEFAULT_ACCEPT_TYPES, flags);
            if (req) {
                if (WinHttpSendRequest(req, WINHTTP_NO_ADDITIONAL_HEADERS, 0, WINHTTP_NO_REQUEST_DATA, 0, 0, 0) &&
                    WinHttpReceiveResponse(req, nullptr)) {
                    DWORD status = 0, len = sizeof(status);
                    WinHttpQueryHeaders(req, WINHTTP_QUERY_STATUS_CODE | WINHTTP_QUERY_FLAG_NUMBER,
                                        WINHTTP_HEADER_NAME_BY_INDEX, &status, &len, WINHTTP_NO_HEADER_INDEX);
                    if (status == 200) {
                        DWORD avail = 0;
                        do {
                            avail = 0;
                            if (!WinHttpQueryDataAvailable(req, &avail))
                                break;
                            if (avail) {
                                size_t off = out.size();
                                out.resize(off + avail);
                                DWORD read = 0;
                                if (!WinHttpReadData(req, out.data() + off, avail, &read))
                                    break;
                                out.resize(off + read);
                            }
                        } while (avail > 0);
                        ok = !out.empty();
                    }
                }
                WinHttpCloseHandle(req);
            }
            WinHttpCloseHandle(conn);
        }
        WinHttpCloseHandle(session);
        return ok;
    }
}

void ImageCache::Init(ID3D11Device* device, int workers) {
    device_ = device;
    running_.store(true);
    for (int i = 0; i < workers; ++i)
        workers_.emplace_back(&ImageCache::WorkerLoop, this);
}

void ImageCache::Shutdown() {
    running_.store(false);
    cv_.notify_all();
    for (auto& t : workers_)
        if (t.joinable())
            t.join();
    workers_.clear();
    for (auto& [url, e] : entries_)
        if (e.srv)
            e.srv->Release();
    entries_.clear();
}

ImTextureID ImageCache::Get(const std::string& url) {
    std::lock_guard<std::mutex> lock(mutex_);
    auto& e = entries_[url];
    if (e.state == State::None) {
        e.state = State::Loading;
        queue_.push_back(url);
        cv_.notify_one();
        return 0;
    }
    return e.state == State::Ready ? e.tex : 0;
}

void ImageCache::WorkerLoop() {
    while (running_.load()) {
        std::string url;
        {
            std::unique_lock<std::mutex> lock(mutex_);
            cv_.wait(lock, [&] { return !running_.load() || !queue_.empty(); });
            if (!running_.load())
                return;
            url = queue_.front();
            queue_.pop_front();
        }
        std::vector<unsigned char> bytes;
        Decoded d;
        d.url = url;
        if (HttpGet(url, bytes)) {
            int w = 0, h = 0, ch = 0;
            unsigned char* px = stbi_load_from_memory(bytes.data(), (int)bytes.size(), &w, &h, &ch, 4);
            if (px) {
                d.w = w;
                d.h = h;
                d.rgba.assign(px, px + (size_t)w * h * 4);
                d.ok = true;
                stbi_image_free(px);
            }
        }
        std::lock_guard<std::mutex> lock(mutex_);
        decoded_.push_back(std::move(d));
    }
}

void ImageCache::Poll() {
    std::vector<Decoded> ready;
    {
        std::lock_guard<std::mutex> lock(mutex_);
        ready.swap(decoded_);
    }
    for (auto& d : ready) {
        Entry& e = entries_[d.url];
        if (!d.ok) {
            e.state = State::Failed;
            continue;
        }
        D3D11_TEXTURE2D_DESC td = {};
        td.Width = d.w;
        td.Height = d.h;
        td.MipLevels = 1;
        td.ArraySize = 1;
        td.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
        td.SampleDesc.Count = 1;
        td.Usage = D3D11_USAGE_DEFAULT;
        td.BindFlags = D3D11_BIND_SHADER_RESOURCE;
        D3D11_SUBRESOURCE_DATA sd = {};
        sd.pSysMem = d.rgba.data();
        sd.SysMemPitch = d.w * 4;
        ID3D11Texture2D* tex = nullptr;
        if (FAILED(device_->CreateTexture2D(&td, &sd, &tex)) || !tex) {
            e.state = State::Failed;
            continue;
        }
        D3D11_SHADER_RESOURCE_VIEW_DESC vd = {};
        vd.Format = td.Format;
        vd.ViewDimension = D3D11_SRV_DIMENSION_TEXTURE2D;
        vd.Texture2D.MipLevels = 1;
        ID3D11ShaderResourceView* srv = nullptr;
        device_->CreateShaderResourceView(tex, &vd, &srv);
        tex->Release();
        if (!srv) {
            e.state = State::Failed;
            continue;
        }
        e.srv = srv;
        e.tex = (ImTextureID)(intptr_t)srv;
        e.state = State::Ready;
    }
}
