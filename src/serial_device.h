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
#include <iostream>

#include "register.h"
#include "serial_exc.h"
#include "serial_config.h"
#include "port.h"


class IProtocol;
typedef std::shared_ptr<IProtocol> PProtocol;

struct TDeviceSetupItem : public TDeviceSetupItemConfig
{
    TDeviceSetupItem(PSerialDevice device, PDeviceSetupItemConfig config)
        : TDeviceSetupItemConfig(*config)
    {
        Register = TRegister::Intern(device, config->RegisterConfig);
    }

    PRegister Register;
};

typedef std::shared_ptr<TDeviceSetupItem> PDeviceSetupItem;


struct TAggregatedSlaveId
{
    int Primary;
    int Secondary;

    bool operator==(const TAggregatedSlaveId & other) const
    {
        return Primary == other.Primary && Secondary == other.Secondary;
    }
};

namespace std
{
    inline string to_string(const TAggregatedSlaveId & x)
    {
        return to_string(x.Primary) + ':' + to_string(x.Secondary);
    }

    template <> struct hash<TAggregatedSlaveId>
    {
        inline size_t operator()(const TAggregatedSlaveId & x) const
        {
            return hash<string>{}(to_string(x));
        }
    };

    inline string to_string(const string & x)
    {
        return x;
    }
}


class TSerialDevice: public std::enable_shared_from_this<TSerialDevice> {
public:
    TSerialDevice(PDeviceConfig config, PPort port, PProtocol protocol);
    TSerialDevice(const TSerialDevice&) = delete;
    TSerialDevice& operator=(const TSerialDevice&) = delete;
    virtual ~TSerialDevice();
    virtual std::list<PRegisterRange> SplitRegisterList(const std::list<PRegister> & reg_list, bool enableHoles = true) const;

    // Prepare to access device (pauses for configured delay by default)
    // i.e. "StartSession". Called before any read/write/etc after communicating with another device
    virtual void Prepare();
    // Ends communication session with the device. Called before communicating with another device
    virtual void EndSession() {/*do nothing by default */}
    // Read register value
    virtual uint64_t ReadRegister(PRegister reg) = 0;
    // Write register value
    virtual void WriteRegister(PRegister reg, uint64_t value) = 0;
    // Handle end of poll cycle e.g. by resetting values caches
    virtual void EndPollCycle();
    // Read multiple registers
    virtual void ReadRegisterRange(PRegisterRange range);
    // Set range as failed to read
    virtual void SetReadError(PRegisterRange range);

    virtual std::string ToString() const;

    // Initialize setup items' registers
    void InitSetupItems();
    bool HasSetupItems() const;
    virtual bool WriteSetupRegisters();

    PPort Port() const { return SerialPort; }
    PDeviceConfig DeviceConfig() const { return _DeviceConfig; }
    PProtocol Protocol() const { return _Protocol; }

    virtual void OnCycleEnd(bool ok);
    bool GetIsDisconnected() const;

    void ResetUnavailableAddresses();

    std::map<int64_t, uint16_t> ModbusCache, ModbusTmpCache;

    void ApplyTmpCache()
    {
        ModbusCache.insert(ModbusTmpCache.begin(), ModbusTmpCache.end());
        DismissTmpCache();
    }

    void DismissTmpCache()
    {
        ModbusTmpCache.clear();
    }

protected:
    std::vector<PDeviceSetupItem> SetupItems;

private:
    PPort SerialPort;
    PDeviceConfig _DeviceConfig;
    PProtocol _Protocol;
    std::chrono::steady_clock::time_point LastSuccessfulCycle;
    bool IsDisconnected;
    std::set<int> UnavailableAddresses;
    int RemainingFailCycles;
};

typedef std::shared_ptr<TSerialDevice> PSerialDevice;

typedef PSerialDevice (*TSerialDeviceMaker)(PDeviceConfig device_config,
                                                PPort port);

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
    virtual PSerialDevice CreateDevice(PDeviceConfig config, PPort port) = 0;

    /*! Get device with specified Slave ID */
    virtual PSerialDevice GetDevice(const std::string &slave_id, PPort port) const = 0;

    /*! Remove device */
    virtual void RemoveDevice(PSerialDevice device) = 0;

};

template<typename S>
class TBasicProtocolConverter : public IProtocol
{
public:
    //! Slave ID type for concrete TBasicProtocol
    typedef S TSlaveId;

    S ConvertSlaveId(const std::string &s) const;
};


template<>
unsigned long TBasicProtocolConverter<unsigned long>::ConvertSlaveId(const std::string &s) const;

template<>
std::string TBasicProtocolConverter<std::string>::ConvertSlaveId(const std::string &s) const;

template<>
TAggregatedSlaveId TBasicProtocolConverter<TAggregatedSlaveId>::ConvertSlaveId(const std::string &s) const;

/*!
 * Basic protocol implementation with slave ID collision check
 * using Slave ID extractor and registered slave ID map
 */
template<class Dev, typename SlaveId = unsigned long>
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
    PSerialDevice CreateDevice(PDeviceConfig config, PPort port)
    {
        TSlaveId sid = this->ConvertSlaveId(config->SlaveId); // "this->" to avoid -fpermissive

        try {
            _Devices.at(port.get()).at(sid);
            std::stringstream ss;
            ss << "device address collision for slave id " << std::to_string(sid) << " (\"" << config->SlaveId << "\")";
            throw TSerialDeviceException(ss.str());
        } catch (const std::out_of_range &) {
            // pass
        }

        PSerialDevice dev = _Devices[port.get()][sid] = std::make_shared<Dev>(config, port, IProtocol::shared_from_this());
        dev->InitSetupItems();
        return dev;
    }

    /*!
     * Return existing device instance by string Slave ID representation
     */
    PSerialDevice GetDevice(const std::string &slave_id, PPort port) const
    {
        TSlaveId sid = this->ConvertSlaveId(slave_id);

        try {
            return _Devices.at(port.get()).at(sid);
        } catch (const std::out_of_range &) {
            std::stringstream ss;
            ss << "no device with slave id " << std::to_string(sid) << " (\"" << slave_id << "\") found in " << this->GetName();
            throw TSerialDeviceException(ss.str());
        }
    }

    void RemoveDevice(PSerialDevice dev)
    {
        if (!dev) {
            throw TSerialDeviceException("can't remove null device");
        }
        for (auto & portDevices: _Devices) {
            auto & devices = portDevices.second;
            const auto &it = std::find_if(devices.begin(), devices.end(), [dev](const std::pair<TSlaveId, PSerialDevice>& p) -> bool { return dev == p.second; });
            if (it != devices.end()) {
                devices.erase(it);
                return;
            }
        }

        throw TSerialDeviceException("no such device found: " + dev->ToString());
    }

private:
    std::string _Name;
    PRegisterTypeMap _RegTypes;
    std::unordered_map<TPort *, std::unordered_map<TSlaveId, PSerialDevice>> _Devices;
};


/*!
 * Base class for devices which use given protocol.
 * Need to convert Slave ID value according
 */
template<class Proto>
class TBasicProtocolSerialDevice : public TSerialDevice
{
public:
    TBasicProtocolSerialDevice(PDeviceConfig config, PPort port, PProtocol protocol)
        : TSerialDevice(config, port, protocol)
    {
        auto p = std::dynamic_pointer_cast<Proto>(protocol);

        if (!p) {
            throw std::runtime_error("cannot cast " + protocol->GetName() + " to " + typeid(Proto).name());
        }

        SlaveId = p->ConvertSlaveId(config->SlaveId);
    }

    std::string ToString() const
    {
        return Protocol()->GetName() + ":" + std::to_string(SlaveId);
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
    static PSerialDevice CreateDevice(PDeviceConfig device_config, PPort port);
    static void RemoveDevice(PSerialDevice device);
    static PSerialDevice GetDevice(const std::string& slave_id, const std::string& protocol_name, PPort port);
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

#define REGISTER_BASIC_INT_PROTOCOL(name, cls, regTypes) \
    TProtocolRegistrator reg__##cls(std::make_shared<TBasicProtocol<cls>>(name, regTypes))


#define REGISTER_BASIC_PROTOCOL(name, cls, slave, regTypes) \
    TProtocolRegistrator reg__##cls(std::make_shared<TBasicProtocol<cls, slave>>(name, regTypes))


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
