#pragma once

#include "types.h"

#include <string>

class TAbstractVirtualRegister
{
public:
    virtual ~TAbstractVirtualRegister() {};

    virtual std::string GetTextValue() const = 0;
    virtual void SetTextValue(const std::string &) = 0;
    virtual EErrorState GetErrorState() const = 0;
    virtual bool GetValueIsRead() const = 0;
    virtual bool IsChanged(EPublishData) const = 0;
    virtual void ResetChanged(EPublishData) = 0;
};
