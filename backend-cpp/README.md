# Native Ripple RL backend

The Windows desktop app links the C++ backend as `ripple_core` and runs it in
the same process. The ImGui UI exchanges line-delimited JSON with the backend
over loopback. The standalone backend exposes the same commands for testing.

| Component | Purpose |
| --- | --- |
| `BackendHost` and `IpcServer` | Start backend services and accept UI requests. |
| `Dispatcher` and `Store` | Route commands and persist settings. |
| `Garage` and `PackagePatches` | Verify game files, invoke the offline package writer, install patches, and restore originals. |
| `ProxyManager` | Starts the optional local proxy when enabled. |

## Build and test

From an x64 Visual Studio developer prompt with CMake and Ninja on `PATH`, run
the following from the repository root.

```cmd
cmake -S backend-cpp -B backend-cpp/build -G Ninja -DCMAKE_BUILD_TYPE=Release
cmake --build backend-cpp/build
set RIPPLE_RL_NO_PROXY=1
backend-cpp\build\bin\ripple_backend.exe
```

This starts the standalone backend on `127.0.0.1:49327` with automatic proxy
startup disabled. The desktop app uses the same port, so close it before running
the standalone backend. Use [the packaging guide](../PACKAGING.md) to bundle the
offline patch runtime.
