#pragma once

#include <stop_token>

class Overlays;

namespace fps {
void Run(std::stop_token stop, Overlays& overlays);
}
