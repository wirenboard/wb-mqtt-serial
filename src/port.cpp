#include "port.h"

#include <cmath>

#include "log.h"

void TPort::Reopen()
{
    if (IsOpen()) {
        Close();
        Open();
    }
}

void TPort::WriteBytes(const std::vector<uint8_t>& buf)
{
    WriteBytes(&buf[0], buf.size());
}

void TPort::WriteBytes(const std::string& buf)
{
    WriteBytes(reinterpret_cast<const uint8_t*>(buf.c_str()), buf.size());
}

std::chrono::microseconds TPort::GetSendTimeBytes(double bytesNumber) const
{
    return GetSendTimeBits(bytesNumber * 8);
}

std::chrono::microseconds TPort::GetSendTimeBits(size_t bitsNumber) const
{
    // Defaults to serial port 9600
    auto us = std::ceil(bitsNumber * 1000000.0 / 9600.0);
    return std::chrono::microseconds(static_cast<std::chrono::microseconds::rep>(us));
}

void TPort::ApplySerialPortSettings(const TSerialPortConnectionSettings& settings)
{}

void TPort::ResetSerialPortSettings()
{}

void TPort::SetMinimalResponseTimeout(const std::chrono::microseconds& value)
{
    MinimalResponseTimeout = value;
}

const std::chrono::microseconds& TPort::GetMinimalResponseTimeout() const
{
    return MinimalResponseTimeout;
}

std::chrono::microseconds TPort::CalcResponseTimeout(const std::chrono::microseconds& timeoutFromCall) const
{
    auto responseTimeout = std::max(GetMinimalResponseTimeout(), timeoutFromCall);
    if (responseTimeout.count() < 0) {
        return DEFAULT_RESPONSE_TIMEOUT;
    }
    return responseTimeout;
}

TPortOpenCloseLogic::TPortOpenCloseLogic(const TPortOpenCloseLogic::TSettings& settings, util::TGetNowFn nowFn)
    : Settings(settings),
      NowFn(nowFn)
{}

void TPortOpenCloseLogic::OpenIfAllowed(PPort port)
{
    if (port->IsOpen()) {
        return;
    }

    auto currentTime = NowFn();
    if (NextOpenTryTime > currentTime) {
        return;
    }

    try {
        port->Open();
    } catch (...) {
        NextOpenTryTime = currentTime + Settings.ReopenTimeout;
        throw;
    }
    LastSuccessfulCycle = NowFn();
    RemainingFailCycles = Settings.ConnectionMaxFailCycles;
}

void TPortOpenCloseLogic::CloseIfNeeded(PPort port, bool allPreviousDataExchangeWasFailed)
{
    if (!port->IsOpen()) {
        return;
    }

    // disable reconnect functionality option
    if (Settings.MaxFailTime.count() < 0 || Settings.ConnectionMaxFailCycles < 0) {
        return;
    }

    auto currentTime = NowFn();
    if (!allPreviousDataExchangeWasFailed) {
        LastSuccessfulCycle = currentTime;
        RemainingFailCycles = Settings.ConnectionMaxFailCycles;
    }

    if (RemainingFailCycles > 0) {
        --RemainingFailCycles;
    }

    if ((currentTime - LastSuccessfulCycle > Settings.MaxFailTime) && RemainingFailCycles == 0) {
        Warn.Log() << port->GetDescription() << ": closed due to repetitive errors";
        port->Close();
    }
}
