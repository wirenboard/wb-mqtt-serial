#include "feature_port.h"
#include "log.h"
#include "modbus_base.h"
#include "serial_exc.h"

#define LOG(logger) ::logger.Log() << "[port] "

namespace
{
    const std::vector<uint8_t> MGE_CHECK_REQUEST =
        {0x47, 'W', 'B', '-', 'F', 'A', 'S', 'T', '-', 'M', 'O', 'D', 'B', 'U', 'S', '?'};
    const std::vector<uint8_t> MGE_CHECK_RESPONSE =
        {0x47, 'W', 'B', '-', 'F', 'A', 'S', 'T', '-', 'M', 'O', 'D', 'B', 'U', 'S', '-', 'O', 'K'};

    /**
     * @brief Checks that MGE v.3 is connected to the port.
     *        Only applicable when using Modbus TCP.
     */
    bool CheckMgeModbusTcp(TPort& port,
                           const std::chrono::milliseconds& responseTimeout,
                           const std::chrono::milliseconds& frameTimeout)
    {
        Modbus::TModbusTCPTraits traits;
        auto res =
            traits.Transaction(port, 0, MGE_CHECK_REQUEST, MGE_CHECK_RESPONSE.size(), responseTimeout, frameTimeout);
        return (res.Pdu.size() == MGE_CHECK_RESPONSE.size() &&
                std::equal(res.Pdu.begin(), res.Pdu.end(), MGE_CHECK_RESPONSE.begin()));
    }
}

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
            FastModbus = CheckMgeModbusTcp(
                *BasePort,
                std::chrono::ceil<std::chrono::milliseconds>(CalcResponseTimeout(GetMinimalResponseTimeout())),
                DefaultFrameTimeout);
            ModbusTcpFastModbusChecked = true;
            LOG(Debug) << BasePort->GetDescription() << ": MGE v.3 is " << (FastModbus ? "detected" : "not detected");
        } catch (const std::exception& e) {
            LOG(Debug) << BasePort->GetDescription() << ": MGE v.3 check failed: " << e.what();
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
