#pragma once

#include <list>
#include <set>
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


class IProtocol;
typedef std::shared_ptr<IProtocol> PProtocol;


class TSerialDevice: public std::enable_shared_from_this<TSerialDevice> {
public:
    TSerialDevice(PDeviceConfig config, PAbstractSerialPort port, PProtocol protocol);
    TSerialDevice(const TSerialDevice&) = delete;
    TSerialDevice& operator=(const TSerialDevice&) = delete;
    virtual ~TSerialDevice();
    virtual std::list<PRegisterRange> SplitRegisterList(const std::list<PRegister> reg_list) const;

    // Prepare to access device (pauses for configured delay by default)
    virtual void Prepare();
    // Read register value
    virtual uint64_t ReadRegister(PRegister reg) = 0;
    // Write register value
    virtual void WriteRegister(PRegister reg, uint64_t value) = 0;
    // Handle end of poll cycle e.g. by resetting values caches
    virtual void EndPollCycle();
    // Read multiple registers
    virtual void ReadRegisterRange(PRegisterRange range);

protected:
    PAbstractSerialPort Port() const { return SerialPort; }
    PDeviceConfig DeviceConfig() const { return _DeviceConfig; }
    PProtocol Protocol() const { return _Protocol; }

private:
    std::chrono::milliseconds Delay;
    PAbstractSerialPort SerialPort;
    PDeviceConfig _DeviceConfig;
    PProtocol _Protocol;

};

typedef std::shared_ptr<TSerialDevice> PSerialDevice;

typedef PSerialDevice (*TSerialDeviceMaker)(PDeviceConfig device_config,
                                                PAbstractSerialPort port);

/*!
 * Protocol interface
 */
class IProtocol : public std::enable_shared_from_this<IProtocol>
{
public:
    /*! Get protocol name */
    virtual std::string GetName() const = 0;

    /*! Get map of register types */
    virtual PRegisterTypeMap GetRegTypes() const = 0;

    /*! Create new device of given type */
    virtual PSerialDevice CreateDevice(PDeviceConfig config, PAbstractSerialPort port) = 0;
};

template<typename S>
class TBasicProtocolConverter
{
public:
    //! Slave ID type for concrete TBasicProtocol
    typedef S TSlaveId;

    S ConvertSlaveId(const std::string &s);
};


template<>
int TBasicProtocolConverter<int>::ConvertSlaveId(const std::string &s);

/*!
 * Basic protocol implementation with slave ID collision check
 * using Slave ID extractor and registered slave ID map
 */
template<class Dev, typename SlaveId = int>
class TBasicProtocol : public IProtocol, public TBasicProtocolConverter<SlaveId>
{
public:
    //! TSerialDevice type for concrete TBasicProtocol
    typedef Dev TDevice;
    typedef SlaveId TSlaveId;

    /*! Construct new protocol with given name and register types list */
    TBasicProtocol(std::string name, const TRegisterTypes &reg_types)
        : _Name(name)
    {
        _RegTypes = std::make_shared<TRegisterTypeMap>();
        for (const auto& rt : reg_types)
            _RegTypes->insert(std::make_pair(rt.Name, rt));
    }

protected:
    // for inheritance
    TBasicProtocol()
        : _Name("")
        , _RegTypes()
    {}

public:
    std::string GetName() const { return _Name; }
    PRegisterTypeMap GetRegTypes() const { return _RegTypes; }

    /*!
     * New concrete device builder
     * Make slave ID collision check, throws exception in case of collision
     * or creates new device and returns pointer to it
     * \param config    PDeviceConfig for device
     * \param port      Serial port
     * \return          Smart pointer to created device
     */
    PSerialDevice CreateDevice(PDeviceConfig config, PAbstractSerialPort port)
    {
        TSlaveId sid = this->ConvertSlaveId(config->SlaveId); // to avoid -fpermissive

        if (_Devices.find(sid) != _Devices.end()) {
            std::stringstream ss;
            ss << "device address collision for slave id " << sid << " (\"" << config->SlaveId << "\")";
            throw TSerialDeviceException(ss.str());
        }

        return std::make_shared<Dev>(config, port, shared_from_this());
    }

private:
    std::string _Name;
    PRegisterTypeMap _RegTypes;
    std::unordered_set<TSlaveId> _Devices;
};


/*!
 * Base class for devices which use given protocol.
 * Need to convert Slave ID value according
 */
template<class Proto>
class TBasicProtocolSerialDevice : public TSerialDevice
{
public:
    TBasicProtocolSerialDevice(PDeviceConfig config, PAbstractSerialPort port, PProtocol protocol)
        : TSerialDevice(config, port, protocol)
    {
        auto p = std::dynamic_pointer_cast<Proto>(protocol);

        if (!p) {
            throw std::runtime_error("Wrong protocol cast, check registration code and class header");
        }

        SlaveId = p->ConvertSlaveId(config->SlaveId);
    }

protected:
    typename Proto::TSlaveId SlaveId;
};


class TSerialDeviceFactory {
public:
    TSerialDeviceFactory() = delete;
    /* static void RegisterProtocol(const std::string& name, TSerialDeviceMaker maker,
                                    const TRegisterTypes& register_types); */
    static void RegisterProtocol(PProtocol protocol);
    static PRegisterTypeMap GetRegisterTypes(PDeviceConfig device_config);
    static PSerialDevice CreateDevice(PDeviceConfig device_config, PAbstractSerialPort port);
    static PProtocol GetProtocolInstance(const std::string &name);

private:
    static const PProtocol GetProtocolEntry(PDeviceConfig device_config);
    static std::unordered_map<std::string, PProtocol> *Protocols;
};

class TProtocolRegistrator
{
public:
    TProtocolRegistrator(PProtocol p)
    {
        TSerialDeviceFactory::RegisterProtocol(p);
    }
};

#define REGISTER_BASIC_INT_PROTOCOL(name, cls, regTypes) \
    TProtocolRegistrator reg__##cls(std::make_shared<TBasicProtocol<cls>>(name, regTypes))


/* Usage:
 *
 * class MyProtocol : public IProtocol {
 *      ...
 * public:
 *      MyProtocol(int arg1, char arg2);
 *      ...
 * };
 *
 * REGISTER_NEW_PROTOCOL(MyProtocol, arg1, arg2);
 */
#define REGISTER_NEW_PROTOCOL(prot, ...) \
    TProtocolRegistrator reg__##prot(std::make_shared<prot>(__VA_ARGS__))

/* Usage:
 *
 * class MyProtocol : public IProtocol {
 *      ...
 * public:
 *      MyProtocol(int arg1, char arg2);
 *      ...
 * };
 *
 * PProtocol my_proto_instance(std::make_shared(arg1, arg2));
 *
 * REGISTER_PROTOCOL(my_proto_instance);
 */
#define REGISTER_PROTOCOL(prot) \
    TProtocolRegistrator reg__##prot(prot)
