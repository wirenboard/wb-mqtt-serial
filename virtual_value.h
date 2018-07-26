#pragma once

#include "declarations.h"

struct TIRDeviceValueContext
{
    const TIRDeviceValueDesc & Desc;
    TIRValue &                 Value;
};

struct IVirtualValue
{
    ~IVirtualValue() = default;

    virtual TIRDeviceValueContext GetReadContext() const = 0;
    virtual TIRDeviceValueContext GetWriteContext() const = 0;

    virtual void InvalidateReadValues() = 0;
};
