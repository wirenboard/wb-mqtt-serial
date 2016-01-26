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

class TSerialProtocol: public std::enable_shared_from_this<TSerialProtocol> {
public:
    TSerialProtocol(PAbstractSerialPort port);
    TSerialProtocol(const TSerialProtocol&) = delete;
    TSerialProtocol& operator=(const TSerialProtocol&) = delete;
    virtual ~TSerialProtocol();

    virtual uint64_t ReadRegister(PRegister reg) = 0;
    virtual void WriteRegister(PRegister reg, uint64_t value) = 0;
    virtual void EndPollCycle();

protected:
    PAbstractSerialPort Port() { return SerialPort; }

private:
    PAbstractSerialPort SerialPort;
};

typedef std::shared_ptr<TSerialProtocol> PSerialProtocol;

typedef PSerialProtocol (*TSerialProtocolMaker)(PDeviceConfig device_config,
                                                PAbstractSerialPort port);

struct TSerialProtocolEntry {
    TSerialProtocolEntry(TSerialProtocolMaker maker, PRegisterTypeMap register_types):
        Maker(maker), RegisterTypes(register_types) {}
    TSerialProtocolMaker Maker;
    PRegisterTypeMap RegisterTypes;
};

class TSerialProtocolFactory {
public:
    TSerialProtocolFactory() = delete;
    static void RegisterProtocol(const std::string& name, TSerialProtocolMaker maker,
                                 const TRegisterTypes& register_types);
    static PRegisterTypeMap GetRegisterTypes(PDeviceConfig device_config);
    static PSerialProtocol CreateProtocol(PDeviceConfig device_config, PAbstractSerialPort port);

private:
    static const TSerialProtocolEntry& GetProtocolEntry(PDeviceConfig device_config);
    static std::unordered_map<std::string, TSerialProtocolEntry> *Protocols;
};

template<class Proto>
class TSerialProtocolRegistrator {
public:
    TSerialProtocolRegistrator(const std::string& name, const TRegisterTypes& register_types)
    {
        TSerialProtocolFactory::RegisterProtocol(
            name, [](PDeviceConfig device_config, PAbstractSerialPort port) {
                return PSerialProtocol(new Proto(device_config, port));
            }, register_types);
    }
};

#define REGISTER_PROTOCOL(name, cls, regTypes) \
    TSerialProtocolRegistrator<cls> reg__##cls(name, regTypes)
