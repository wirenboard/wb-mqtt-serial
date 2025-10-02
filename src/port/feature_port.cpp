#include "feature_port.h"
#include "log.h"
#include "modbus_base.h"

#define LOG(logger) ::logger.Log() << "[port] "

TFeaturePort::TFeaturePort(PPort basePort, bool modbusTcp)
    : BasePort(basePort),
      ModbusTcp(modbusTcp),
      FastModbus(!modbusTcp),
      ModbusTcpFastModbusChecked(!modbusTcp)
{}

void TFeaturePort::Open()
{
    BasePort->Open();
    if (ModbusTcp && !ModbusTcpFastModbusChecked) {
        try {
            FastModbus = Modbus::CheckMgeModbusTcp(
                *BasePort,
                std::chrono::ceil<std::chrono::milliseconds>(CalcResponseTimeout(GetMinimalResponseTimeout())),
                DefaultFrameTimeout);
            ModbusTcpFastModbusChecked = true;
        } catch (const std::exception& e) {
            LOG(Error) << BasePort->GetDescription() << ": MGE v.3 check failed: " << e.what();
        }
    }
}

void TFeaturePort::Close()
{
    BasePort->Close();
}

void TFeaturePort::Reopen()
{
    BasePort->Reopen();
}

bool TFeaturePort::IsOpen() const
{
    return BasePort->IsOpen();
}

void TFeaturePort::CheckPortOpen() const
{
    BasePort->CheckPortOpen();
}

void TFeaturePort::WriteBytes(const uint8_t* buf, int count)
{
    BasePort->WriteBytes(buf, count);
}

uint8_t TFeaturePort::ReadByte(const std::chrono::microseconds& timeout)
{
    return BasePort->ReadByte(timeout);
}

TReadFrameResult TFeaturePort::ReadFrame(uint8_t* buf,
                                         size_t count,
                                         const std::chrono::microseconds& responseTimeout,
                                         const std::chrono::microseconds& frameTimeout,
                                         TFrameCompletePred frame_complete)
{
    return BasePort->ReadFrame(buf, count, responseTimeout, frameTimeout, frame_complete);
}

void TFeaturePort::SkipNoise()
{
    BasePort->SkipNoise();
}

void TFeaturePort::SleepSinceLastInteraction(const std::chrono::microseconds& us)
{
    BasePort->SleepSinceLastInteraction(us);
}

std::chrono::microseconds TFeaturePort::GetSendTimeBytes(double bytesNumber) const
{
    return BasePort->GetSendTimeBytes(bytesNumber);
}

std::chrono::microseconds TFeaturePort::GetSendTimeBits(size_t bitsNumber) const
{
    return BasePort->GetSendTimeBits(bitsNumber);
}

std::string TFeaturePort::GetDescription(bool verbose) const
{
    return BasePort->GetDescription(verbose);
}

void TFeaturePort::ApplySerialPortSettings(const TSerialPortConnectionSettings& settings)
{
    BasePort->ApplySerialPortSettings(settings);
}

void TFeaturePort::ResetSerialPortSettings()
{
    BasePort->ResetSerialPortSettings();
}

bool TFeaturePort::IsModbusTcp() const
{
    return ModbusTcp;
}

bool TFeaturePort::SupportsFastModbus() const
{
    return FastModbus;
}

PPort TFeaturePort::GetBasePort() const
{
    return BasePort;
}
