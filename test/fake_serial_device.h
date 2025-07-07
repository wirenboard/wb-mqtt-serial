#pragma once

#include "serial_config.h"
#include "serial_device.h"

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
    void SetTransferResult(bool ok) override;

    void BlockReadFor(int addr, bool block);
    void BlockWriteFor(int addr, bool block);
    uint32_t Read2Registers(int addr);
    void SetIsConnected(bool);
    ~TFakeSerialDevice();

    PRegisterRange CreateRegisterRange() const override;

    uint16_t Registers[256]{};

    static TFakeSerialDevice* GetDevice(const std::string& slaveId);
    static void ClearDevices();
    static void Register(TSerialDeviceFactory& factory);

    void SetSessionLogEnabled(bool enabled);
    void EndSession() override;

protected:
    TRegisterValue ReadRegisterImpl(const TRegisterConfig& reg) override;
    void WriteRegisterImpl(const TRegisterConfig& reg, const TRegisterValue& value) override;
    void PrepareImpl() override;

private:
    PFakeSerialPort FakePort;
    std::map<int, std::pair<bool, bool>> Blockings;
    bool Connected;
    bool SessionLogEnabled;

    static std::list<TFakeSerialDevice*> Devices;
};

typedef std::shared_ptr<TFakeSerialDevice> PFakeSerialDevice;
