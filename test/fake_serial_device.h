#pragma once

#include "serial_device.h"
#include "serial_config.h"

#include <map>

class TFakeSerialPort;
using PFakeSerialPort = std::shared_ptr<TFakeSerialPort>;

class TFakeSerialDevice: public TSerialDevice, public TUInt32SlaveId
{
public:
    enum RegisterType
    {
        REG_FAKE = 123
    };

    TFakeSerialDevice(PDeviceConfig config, PPort port, PProtocol protocol);
    uint64_t ReadRegister(PRegister reg) override;
    void     WriteRegister(PRegister reg, uint64_t value) override;
    void     OnCycleEnd(bool ok) override;

    void     BlockReadFor(int addr, bool block);
    void     BlockWriteFor(int addr, bool block);
    uint32_t Read2Registers(int addr);
    void     SetIsConnected(bool);
    ~TFakeSerialDevice();

    uint16_t Registers[256]{};

    static TFakeSerialDevice* GetDevice(const std::string& slaveId);
    static void               ClearDevices();
    static void               Register(TSerialDeviceFactory& factory);

private:
    PFakeSerialPort                      FakePort;
    std::map<int, std::pair<bool, bool>> Blockings;
    bool                                 Connected;

    static std::list<TFakeSerialDevice*> Devices;
};

typedef std::shared_ptr<TFakeSerialDevice> PFakeSerialDevice;
