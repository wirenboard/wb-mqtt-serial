#pragma once

#include "declarations.h"

struct TIRValue
{
    virtual ~TIRValue() = default;

    virtual std::string GetTextValue(const TRegisterConfig &) const = 0;
    virtual void SetTextValue(const TRegisterConfig &, const std::string &) = 0;

    virtual void Assign(const TIRValue & rhs) = 0;

    virtual uint8_t GetByte(TByteIndex iByte) const = 0;
    virtual void SetByte(uint8_t byte, TByteIndex iByte) = 0;

    virtual std::string DescribeShort() const { return "<no desc>"; }

    void ResetChanged();
    bool IsChanged() const;

    static PIRValue Make(const TRegisterConfig &);

protected:
    bool Changed = false;
};
