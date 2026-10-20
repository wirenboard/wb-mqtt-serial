#include "modbus_simulator_port.h"

#include "crc16.h"
#include "serial_exc.h"

namespace
{
    const uint8_t READ_HOLDING = 3;
    const uint8_t READ_INPUT = 4;
    const uint8_t WRITE_SINGLE = 6;
    const uint8_t WRITE_MULTIPLE = 16;
    const uint8_t ILLEGAL_FUNCTION = 1;
    const uint8_t ILLEGAL_DATA_ADDRESS = 2;

    uint16_t GetWord(const std::vector<uint8_t>& data, size_t offset)
    {
        return (data.at(offset) << 8) | data.at(offset + 1);
    }

    void AppendWord(std::vector<uint8_t>& data, uint16_t value)
    {
        data.push_back(value >> 8);
        data.push_back(value & 0xFF);
    }
}

TModbusSimulatorPort::TModbusSimulatorPort(const std::string& path): Path(path)
{}

void TModbusSimulatorPort::AddDevice(uint8_t slaveId)
{
    std::lock_guard<std::mutex> lock(Mutex);
    Devices.insert(slaveId);
}

void TModbusSimulatorPort::SetRegister(uint8_t slaveId, uint16_t address, uint16_t value)
{
    std::lock_guard<std::mutex> lock(Mutex);
    Registers[{slaveId, address}] = value;
}

void TModbusSimulatorPort::SetString(uint8_t slaveId, uint16_t address, const std::string& value)
{
    for (size_t i = 0; i < value.size(); ++i) {
        SetRegister(slaveId, address + i, static_cast<uint8_t>(value[i]));
    }
}

void TModbusSimulatorPort::SetUnsupported(uint8_t slaveId, uint16_t address)
{
    std::lock_guard<std::mutex> lock(Mutex);
    Unsupported.insert({slaveId, address});
}

void TModbusSimulatorPort::SetSilentFrom(uint8_t slaveId, uint16_t address)
{
    std::lock_guard<std::mutex> lock(Mutex);
    SilentFrom[slaveId] = address;
}

void TModbusSimulatorPort::SetAllowOpen(bool allowOpen)
{
    std::lock_guard<std::mutex> lock(Mutex);
    AllowOpen = allowOpen;
}

std::vector<TModbusSimulatorPort::TWrite> TModbusSimulatorPort::GetWrites()
{
    std::lock_guard<std::mutex> lock(Mutex);
    return Writes;
}

void TModbusSimulatorPort::ClearWrites()
{
    std::lock_guard<std::mutex> lock(Mutex);
    Writes.clear();
}

size_t TModbusSimulatorPort::GetCloseCount()
{
    std::lock_guard<std::mutex> lock(Mutex);
    return CloseCount;
}

std::vector<int> TModbusSimulatorPort::GetAppliedBaudRates()
{
    std::lock_guard<std::mutex> lock(Mutex);
    return AppliedBaudRates;
}

void TModbusSimulatorPort::ApplySerialPortSettings(const TSerialPortConnectionSettings& settings)
{
    std::lock_guard<std::mutex> lock(Mutex);
    AppliedBaudRates.push_back(settings.BaudRate);
}

void TModbusSimulatorPort::Open()
{
    std::lock_guard<std::mutex> lock(Mutex);
    if (!AllowOpen) {
        throw TSerialDeviceException(Path + ", can't open serial port");
    }
    PortIsOpen = true;
}

void TModbusSimulatorPort::Close()
{
    std::lock_guard<std::mutex> lock(Mutex);
    PortIsOpen = false;
    ++CloseCount;
}

bool TModbusSimulatorPort::IsOpen() const
{
    std::lock_guard<std::mutex> lock(Mutex);
    return PortIsOpen;
}

void TModbusSimulatorPort::CheckPortOpen() const
{
    if (!IsOpen()) {
        throw TSerialDeviceException(Path + " is not open");
    }
}

void TModbusSimulatorPort::WriteBytes(const uint8_t* buf, int count)
{
    CheckPortOpen();
    std::lock_guard<std::mutex> lock(Mutex);
    Response.clear();
    if (count >= 4) {
        MakeResponse(std::vector<uint8_t>(buf, buf + count - 2));
    }
}

void TModbusSimulatorPort::MakeResponse(const std::vector<uint8_t>& request)
{
    auto slaveId = request[0];
    if (!Devices.count(slaveId)) {
        return;
    }
    auto function = request[1];
    if (function != READ_HOLDING && function != READ_INPUT && function != WRITE_SINGLE && function != WRITE_MULTIPLE) {
        SetResponse({slaveId, static_cast<uint8_t>(function | 0x80), ILLEGAL_FUNCTION});
        return;
    }
    auto address = GetWord(request, 2);
    uint16_t count = (function == WRITE_SINGLE) ? 1 : GetWord(request, 4);
    auto silent = SilentFrom.find(slaveId);
    for (uint16_t i = 0; i < count; ++i) {
        if (silent != SilentFrom.end() && address + i >= silent->second) {
            return;
        }
        if (Unsupported.count({slaveId, static_cast<uint16_t>(address + i)})) {
            SetResponse({slaveId, static_cast<uint8_t>(function | 0x80), ILLEGAL_DATA_ADDRESS});
            return;
        }
    }
    if (function == WRITE_SINGLE) {
        auto value = GetWord(request, 4);
        Registers[{slaveId, address}] = value;
        Writes.emplace_back(slaveId, address, value);
        SetResponse(request);
        return;
    }
    if (function == WRITE_MULTIPLE) {
        for (uint16_t i = 0; i < count; ++i) {
            auto value = GetWord(request, 7 + 2 * i);
            Registers[{slaveId, static_cast<uint16_t>(address + i)}] = value;
            Writes.emplace_back(slaveId, address + i, value);
        }
        SetResponse(std::vector<uint8_t>(request.begin(), request.begin() + 6));
        return;
    }
    std::vector<uint8_t> response{slaveId, function, static_cast<uint8_t>(count * 2)};
    for (uint16_t i = 0; i < count; ++i) {
        auto it = Registers.find({slaveId, static_cast<uint16_t>(address + i)});
        AppendWord(response, it == Registers.end() ? 0 : it->second);
    }
    SetResponse(response);
}

void TModbusSimulatorPort::SetResponse(std::vector<uint8_t> response)
{
    AppendWord(response, CRC16::CalculateCRC16(response.data(), response.size()));
    Response = response;
}

uint8_t TModbusSimulatorPort::ReadByte(const std::chrono::microseconds& timeout)
{
    std::lock_guard<std::mutex> lock(Mutex);
    if (Response.empty()) {
        throw TResponseTimeoutException();
    }
    auto res = Response.front();
    Response.erase(Response.begin());
    return res;
}

TReadFrameResult TModbusSimulatorPort::ReadFrame(uint8_t* buf,
                                                 size_t count,
                                                 const std::chrono::microseconds& responseTimeout,
                                                 const std::chrono::microseconds& frameTimeout,
                                                 TFrameCompletePred frameComplete)
{
    CheckPortOpen();
    std::lock_guard<std::mutex> lock(Mutex);
    if (Response.empty()) {
        throw TResponseTimeoutException();
    }
    TReadFrameResult res;
    res.Count = std::min(count, Response.size());
    std::copy(Response.begin(), Response.begin() + res.Count, buf);
    Response.clear();
    return res;
}

void TModbusSimulatorPort::SkipNoise()
{
    std::lock_guard<std::mutex> lock(Mutex);
    Response.clear();
}

void TModbusSimulatorPort::SleepSinceLastInteraction(const std::chrono::microseconds& us)
{}

std::chrono::microseconds TModbusSimulatorPort::GetSendTimeBytes(double bytesNumber) const
{
    return std::chrono::microseconds(static_cast<int64_t>(bytesNumber));
}

std::chrono::microseconds TModbusSimulatorPort::GetSendTimeBits(size_t bitsNumber) const
{
    return std::chrono::microseconds(bitsNumber / 8);
}

std::string TModbusSimulatorPort::GetDescription(bool verbose) const
{
    return Path;
}
