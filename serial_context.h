#pragma once

#include "serial_protocol.h"
#include "modbus_client.h"

namespace {
    enum {
        ADDR_TYPE_BRIGHTNESS = 0x01
    };
}

class TSerialContext: public TModbusContext
{
public:
    TSerialContext(PSerialProtocol proto);
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
    void ReadDirectRegister(int addr, uint64_t* dest, RegisterFormat format);
    void WriteDirectRegister(int addr, uint64_t value, RegisterFormat format);
    void EndPollCycle(int usec);

private:
    PSerialProtocol Proto;
    int SlaveAddr;
};

class TSerialConnector: public TModbusConnector {
public:
    PModbusContext CreateContext(const TSerialPortSettings& settings);
protected:
    virtual PSerialProtocol CreateProtocol(PAbstractSerialPort port) = 0;
};

class TUnielConnector: public TSerialConnector
{
public:
    PSerialProtocol CreateProtocol(PAbstractSerialPort port);
};

class TMilurConnector: public TSerialConnector
{
public:
    PSerialProtocol CreateProtocol(PAbstractSerialPort port);
};

class TMercury230Connector: public TSerialConnector
{
public:
    PSerialProtocol CreateProtocol(PAbstractSerialPort port);
};
