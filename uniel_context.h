#pragma once

#include "uniel.h"
#include "modbus_client.h"

namespace {
    enum {
        ADDR_TYPE_BRIGHTNESS = 0x01
    };
}

class TUnielModbusContext: public TModbusContext
{
public:
    TUnielModbusContext(const std::string& device, int timeoutMs);
    void Connect();
    void Disconnect();
    void SetDebug(bool debug);
    void SetSlave(int slave_addr);
    void ReadCoils(int addr, int nb, uint8_t *dest);
    void WriteCoil(int addr, int value);
    void ReadDisceteInputs(int addr, int nb, uint8_t *dest);
    void ReadHoldingRegisters(int addr, int nb, uint16_t *dest);
    void WriteHoldingRegisters(int addr, int nb, const uint16_t *data);
    void WriteHoldingRegister(int addr, uint16_t value);
    void ReadInputRegisters(int addr, int nb, uint16_t *dest);
    void USleep(int usec);

private:
    TUnielBus Bus;
    int SlaveAddr;
};

class TUnielModbusConnector: public TModbusConnector
{
public:
    PModbusContext CreateContext(const TModbusConnectionSettings& settings);
};
