#pragma once
// Small Windows helpers shared across the native backend: UTF-8/UTF-16
// conversion, environment access, the executable directory, one-shot Winsock
// init, and running child processes (with or without capturing their output).
#include <filesystem>
#include <initializer_list>
#include <string>

namespace win {

std::wstring Widen(const std::string& s);
std::string  Narrow(const std::wstring& s);
std::string  RandomHex(size_t bytes);  // like Python secrets.token_hex(bytes)
std::wstring GetEnvW(const wchar_t* name);
std::filesystem::path ExeDir();
void EnsureWinsock();

// Run a command line and wait for it. Returns the exit code, or -1 if the
// process could not be started. `hidden` passes CREATE_NO_WINDOW.
int RunWait(const std::wstring& cmdline, bool hidden, unsigned long timeoutMs = 0xffffffffUL);

// Run a command line, capture stdout+stderr into `out`, and wait.
// Returns the exit code, or -1 if the process could not be started.
int RunCapture(const std::wstring& cmdline, std::string& out);
int RunPythonModule(const std::wstring& module, const std::filesystem::path& appDir,
                    std::initializer_list<std::filesystem::path> arguments, std::string& out);

}  // namespace win
