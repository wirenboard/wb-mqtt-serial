#include "port.h"
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

std::chrono::milliseconds TPort::GetSendTime(double bytesNumber)
{
    return std::chrono::milliseconds::zero();
}

void TPort::SetSerialPortByteFormat(const TSerialPortByteFormat* params)
{}

TPortOpenCloseLogic::TPortOpenCloseLogic(const TPortOpenCloseLogic::TSettings& settings)
    : Settings(settings)
{}

void TPortOpenCloseLogic::OpenIfAllowed(PPort port)
{
    if (port->IsOpen()) {
        return;
    }

    auto currentTime = std::chrono::steady_clock::now();
    if (NextOpenTryTime > currentTime) {
        return;
    }

    try {
        port->Open();
    } catch (...) {
        NextOpenTryTime = currentTime + Settings.ReopenTimeout;
        throw;
    }
    LastSuccessfulCycle = std::chrono::steady_clock::now();
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

    auto currentTime = std::chrono::steady_clock::now();
    if (!allPreviousDataExchangeWasFailed) {
        LastSuccessfulCycle = currentTime;
        RemainingFailCycles = Settings.ConnectionMaxFailCycles;
    }

    if (RemainingFailCycles > 0) {
        --RemainingFailCycles;
    }

    if ((currentTime - LastSuccessfulCycle > Settings.MaxFailTime) && RemainingFailCycles == 0) {
        Warn.Log() << port->GetDescription() <<  ": closed due to repetetive errors";
        port->Close();
    }
}
