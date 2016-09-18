#pragma once

#include <list>
#include <set>
#include <vector>
#include <unordered_map>
#include <string>
#include <memory>
#include <exception>
#include <algorithm>
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

    virtual std::string ToString() const;
    
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

    /*! Get device with specified Slave ID */
    virtual PSerialDevice GetDevice(const std::string &slave_id) const = 0;

    /*! Remove device */
    virtual void RemoveDevice(PSerialDevice device) = 0;

};

template<typename S>
class TBasicProtocolConverter : public IProtocol
{
public:
    //! Slave ID type for concrete TBasicProtocol
    typedef S TSlaveId;

    virtual S ConvertSlaveId(const std::string &s) const;
    virtual std::string SlaveIdToString(const S &s) const;
};


template<>
int TBasicProtocolConverter<int>::ConvertSlaveId(const std::string &s) const;

template<>
std::string TBasicProtocolConverter<int>::SlaveIdToString(const int &s) const;

/*!
 * Basic protocol implementation with slave ID collision check
 * using Slave ID extractor and registered slave ID map
 */
template<class Dev, typename SlaveId = int>
class TBasicProtocol : public TBasicProtocolConverter<SlaveId>
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
     *
     * Make slave ID collision check, throws exception in case of collision
     * or creates new device and returns pointer to it
     * \param config    PDeviceConfig for device
     * \param port      Serial port
     * \return          Smart pointer to created device
     */
    PSerialDevice CreateDevice(PDeviceConfig config, PAbstractSerialPort port)
    {
        TSlaveId sid = this->ConvertSlaveId(config->SlaveId); // "this->" to avoid -fpermissive

        if (_Devices.find(sid) != _Devices.end()) {
            std::stringstream ss;
            ss << "device address collision for slave id " << this->SlaveIdToString(sid) << " (\"" << config->SlaveId << "\")";
            throw TSerialDeviceException(ss.str());
        }

        return _Devices[sid] = std::make_shared<Dev>(config, port, IProtocol::shared_from_this());
    }

    /*!
     * Return existing device instance by string Slave ID representation
     */
    PSerialDevice GetDevice(const std::string &slave_id) const
    {
        TSlaveId sid = this->ConvertSlaveId(slave_id);

        if (_Devices.find(sid) == _Devices.cend()) {
            std::stringstream ss;
            ss << "no device with slave id " << this->SlaveIdToString(sid) << " (\"" << slave_id << "\") found in " << this->GetName();
            throw TSerialDeviceException(ss.str());
        }

        return _Devices.find(sid)->second;
    }

    void RemoveDevice(PSerialDevice dev)
    {
        if (!dev) {
            throw TSerialDeviceException("can't remove null device");
        }
        const auto &it = std::find_if(_Devices.begin(), _Devices.end(), [dev](const std::pair<TSlaveId, PSerialDevice>& p) -> bool { return dev == p.second; });
        if (it == _Devices.end()) {
            throw TSerialDeviceException("no such device found: " + dev->ToString());
        }

        _Devices.erase(it);
    }

private:
    std::string _Name;
    PRegisterTypeMap _RegTypes;
    std::unordered_map<TSlaveId, PSerialDevice> _Devices;
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

    std::string ToString() const
    {
        auto p = std::dynamic_pointer_cast<Proto>(Protocol());

        if (!p) {
            throw std::runtime_error("Wrong protocol cast, check registration code and class header");
        }

        return Protocol()->GetName() + ":" + p->SlaveIdToString(SlaveId);
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
    static void RemoveDevice(PSerialDevice device);
    static PProtocol GetProtocol(const std::string &name);

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

/* A piece of legacy-friendly code for not to rewrite all existing stuff
 */
#define REGISTER_BASIC_INT_PROTOCOL(name, cls, regTypes) \
    TProtocolRegistrator reg__##cls(std::make_shared<TBasicProtocol<cls>>(name, regTypes))


/* Make new instance of given protocol class and register it
 * Usage:
 *
 * class MyProtocol : public IProtocol {
 *      ...
 * public:
 *      MyProtocol(int arg1, char arg2);
 *      ...
 * };
 *
 * REGISTER_AND_MAKE_PROTOCOL(MyProtocol, arg1, arg2);
 */
#define REGISTER_AND_MAKE_PROTOCOL(prot, ...) \
    TProtocolRegistrator reg__##prot(std::make_shared<prot>(__VA_ARGS__))

/* Register existing instance of given protocol
 * Usage:
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
