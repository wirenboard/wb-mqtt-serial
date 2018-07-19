#pragma once

#include "utils_performance.h"

#include <chrono>
#include <memory>

class TBinarySemaphore;

using TTimePoint            = std::chrono::steady_clock::time_point;
using PBinarySemaphore      = std::shared_ptr<TBinarySemaphore>;
