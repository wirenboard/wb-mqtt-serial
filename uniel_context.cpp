#include <unistd.h>

#include "uniel_context.h"

TUnielModbusContext::TUnielModbusContext(const std::string& device, int timeout_ms):
    Bus(device, timeout_ms), SlaveAddr(1) {}

void TUnielModbusContext::Connect()
{
    try {
        if (!Bus.IsOpen())
            Bus.Open();
    } catch (const TUnielBusException& e) {
        throw TModbusException(e.what());
    }
}

void TUnielModbusContext::Disconnect()
{
    if (Bus.IsOpen())
        Bus.Close();
}

void TUnielModbusContext::SetDebug(bool)
{
    // TBD: set debug
}

void TUnielModbusContext::SetSlave(int slave_addr)
{
    SlaveAddr = slave_addr;
}

void TUnielModbusContext::ReadCoils(int addr, int nb, uint8_t *dest)
{
    try {
        Connect();
        for (int i = 0; i < nb; ++i)
            *dest++ = Bus.ReadRegister(SlaveAddr, addr + i) == 0 ? 0 : 1;
    } catch (const TUnielBusTransientErrorException& e) {
        throw TModbusException(e.what());
    } catch (const TUnielBusException& e) {
        Disconnect();
        throw TModbusException(e.what());
    }
}

void TUnielModbusContext::WriteCoil(int addr, int value)
{
    try {
        Connect();
        Bus.WriteRegister(SlaveAddr, addr, value ? 0xff : 0);
    } catch (const TUnielBusTransientErrorException& e) {
        throw TModbusException(e.what());
    } catch (const TUnielBusException& e) {
        Disconnect();
        throw TModbusException(e.what());
    }
}

void TUnielModbusContext::ReadDisceteInputs(int addr, int nb, uint8_t *dest)
{
    ReadCoils(addr, nb, dest);
}

void TUnielModbusContext::ReadHoldingRegisters(int addr, int nb, uint16_t *dest)
{
    try {
        Connect();
        for (int i = 0; i < nb; ++i)
            *dest++ = Bus.ReadRegister(SlaveAddr, addr + i);
    } catch (const TUnielBusTransientErrorException& e) {
        throw TModbusException(e.what());
    } catch (const TUnielBusException& e) {
        Disconnect();
        throw TModbusException(e.what());
    }
}

void TUnielModbusContext::WriteHoldingRegisters(int addr, int nb, const uint16_t *data)
{
    try {
        Connect();
        for (int i = 0; i < nb; ++i)
            Bus.WriteRegister(SlaveAddr, addr, *data++);
    } catch (const TUnielBusTransientErrorException& e) {
        throw TModbusException(e.what());
    } catch (const TUnielBusException& e) {
        Disconnect();
        throw TModbusException(e.what());
    }
}

void TUnielModbusContext::ReadInputRegisters(int addr, int nb, uint16_t *dest)
{
    ReadHoldingRegisters(addr, nb, dest);
}

void TUnielModbusContext::USleep(int usec)
{
    usleep(usec);
}

PModbusContext TUnielModbusConnector::CreateContext(const TModbusConnectionSettings& settings)
{
    int timeout = settings.ResponseTimeoutMs ? settings.ResponseTimeoutMs : TUnielBus::DefaultTimeoutMs;
    return PModbusContext(new TUnielModbusContext(settings.Device, timeout));
}

// TBD: debug
// TBD: brightness values (0x100 + channel as holding register index)
// TBD: port settings (do we need them?)
