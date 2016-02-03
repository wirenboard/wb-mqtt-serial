#pragma once

#include <vector>
#include <unordered_map>
#include <string>
#include <memory>
#include <exception>
#include <stdint.h>

#include "portsettings.h"
#include "register.h"
#include "serial_exc.h"
#include "serial_config.h"
#include "serial_port.h"

class TSerialDevice: public std::enable_shared_from_this<TSerialDevice> {
public:
    TSerialDevice(PAbstractSerialPort port);
    TSerialDevice(const TSerialDevice&) = delete;
    TSerialDevice& operator=(const TSerialDevice&) = delete;
    virtual ~TSerialDevice();

    virtual uint64_t ReadRegister(PRegister reg) = 0;
    virtual void WriteRegister(PRegister reg, uint64_t value) = 0;
    virtual void EndPollCycle();

protected:
    PAbstractSerialPort Port() { return SerialPort; }

private:
    PAbstractSerialPort SerialPort;
};

typedef std::shared_ptr<TSerialDevice> PSerialDevice;

typedef PSerialDevice (*TSerialDeviceMaker)(PDeviceConfig device_config,
                                                PAbstractSerialPort port);

struct TSerialProtocolEntry {
    TSerialProtocolEntry(TSerialDeviceMaker maker, PRegisterTypeMap register_types):
        Maker(maker), RegisterTypes(register_types) {}
    TSerialDeviceMaker Maker;
    PRegisterTypeMap RegisterTypes;
};

class TSerialDeviceFactory {
public:
    TSerialDeviceFactory() = delete;
    static void RegisterProtocol(const std::string& name, TSerialDeviceMaker maker,
                                 const TRegisterTypes& register_types);
    static PRegisterTypeMap GetRegisterTypes(PDeviceConfig device_config);
    static PSerialDevice CreateDevice(PDeviceConfig device_config, PAbstractSerialPort port);

private:
    static const TSerialProtocolEntry& GetProtocolEntry(PDeviceConfig device_config);
    static std::unordered_map<std::string, TSerialProtocolEntry> *Protocols;
};

template<class Dev>
class TSerialDeviceRegistrator {
public:
    TSerialDeviceRegistrator(const std::string& name, const TRegisterTypes& register_types)
    {
        TSerialDeviceFactory::RegisterProtocol(
            name, [](PDeviceConfig device_config, PAbstractSerialPort port) {
                return PSerialDevice(new Dev(device_config, port));
            }, register_types);
    }
};

#define REGISTER_PROTOCOL(name, cls, regTypes) \
    TSerialDeviceRegistrator<cls> reg__##cls(name, regTypes)
