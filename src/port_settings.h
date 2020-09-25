#pragma once

#include <chrono>
#include <memory>
#include <string>

struct TPortSettings
{
    virtual ~TPortSettings() = default;

    virtual std::string ToString() const = 0;
};

using PPortSettings = std::shared_ptr<TPortSettings>;
