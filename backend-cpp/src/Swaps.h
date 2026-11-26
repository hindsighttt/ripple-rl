#pragma once
// Native port of ripple/swaps.py: apply/revert item swaps on the game's
// CookedPCConsole packages and track them in swaps.json.
#include <nlohmann/json.hpp>

using json = nlohmann::json;

namespace swaps {
json ApplySwap(const json& p);   // apply a target<-source swap, record it
json PaintItem(const json& p);   // best-effort paint marker on active rows
json Revert(long long index);    // revert one active swap
json RevertAll();                // revert every active swap
json ListActive();               // {swaps,count,otherInstall,copyable}
json CopyToOther();              // stub, matches Python
json CurrentSwaps();             // the raw swaps.json array (for presets)
}  // namespace swaps
