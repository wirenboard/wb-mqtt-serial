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
typedef IProtocol* PProtocol;

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


class TAggregatedSlaveId
{
public:
    int PrimaryId;
    int SecondaryId;

    TAggregatedSlaveId(const std::string& slaveId);

    bool operator==(const TAggregatedSlaveId & other) const;
};

class TUInt32SlaveId
{
public:
    //! Slave ID type for concrete TBasicProtocol
    typedef uint32_t TSlaveId;

    uint32_t SlaveId;

    TUInt32SlaveId(const std::string& slaveId);
};

namespace std
{
    inline string to_string(const TAggregatedSlaveId & x)
    {
        return to_string(x.PrimaryId) + ':' + to_string(x.SecondaryId);
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

class TSerialDevice: public std::enable_shared_from_this<TSerialDevice>
{
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
    virtual uint64_t ReadRegister(PRegister reg);
    // Write register value
    virtual void WriteRegister(PRegister reg, uint64_t value) = 0;
    // Handle end of poll cycle e.g. by resetting values caches
    virtual void EndPollCycle();
    // Read multiple registers
    virtual std::list<PRegisterRange> ReadRegisterRange(PRegisterRange range);

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
    int RemainingFailCycles;
};

typedef std::shared_ptr<TSerialDevice> PSerialDevice;

/*!
 * Protocol interface
 */
class IProtocol
{
public:

    /*! Construct new protocol with given name and register types list */
    IProtocol(const std::string& name, const TRegisterTypes& reg_types);

    virtual ~IProtocol();

    /*! Get protocol name */
    virtual const std::string& GetName() const;

    /*! Get map of register types */
    virtual PRegisterTypeMap GetRegTypes() const;

    /*! Create new device of given type */
    virtual PSerialDevice CreateDevice(PDeviceConfig config, PPort port) = 0;

private:
    std::string Name;
    PRegisterTypeMap RegTypes;
};

/*!
 * Basic protocol implementation with slave ID collision check
 * using Slave ID extractor and registered slave ID map
 */
template<class Dev> class TBasicProtocol : public IProtocol
{
public:
    /*! Construct new protocol with given name and register types list */
    TBasicProtocol(const std::string& name, const TRegisterTypes& reg_types) 
        : IProtocol(name, reg_types)
    {}

public:
    /*!
     * New concrete device builder
     *
     * Make slave ID collision check, throws exception in case of collision
     * or creates new device and returns pointer to it
     * \param config    PDeviceConfig for device
     * \param port      Serial port
     * \return          Smart pointer to created device
     */
    PSerialDevice CreateDevice(PDeviceConfig config, PPort port) override
    {
        PSerialDevice dev = std::make_shared<Dev>(config, port, this);
        dev->InitSetupItems();
        return dev;
    }
};

class TSerialDeviceFactory
{
public:
    static void RegisterProtocol(PProtocol protocol);
    static PRegisterTypeMap GetRegisterTypes(PDeviceConfig device_config);
    static PSerialDevice CreateDevice(PDeviceConfig device_config, PPort port);
    static PProtocol GetProtocol(const std::string& name);

private:
    static const PProtocol GetProtocolEntry(PDeviceConfig device_config);
    static std::unordered_map<std::string, PProtocol> Protocols;
};

class TProtocolRegistrator
{
public:
    TProtocolRegistrator(PProtocol p)
    {
        TSerialDeviceFactory::RegisterProtocol(p);
    }
};

#define REGISTER_BASIC_PROTOCOL(name, cls, regTypes) \
    TProtocolRegistrator reg__##cls(new TBasicProtocol<cls>(name, regTypes))

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
    TProtocolRegistrator reg__##prot(new prot(__VA_ARGS__))

/* Usage:
 *
 * class MyProtocol : public IProtocol {
 *      ...
 * public:
 *      MyProtocol(int arg1, char arg2);
 *      ...
 * };
 *
 * MyProtocol my_proto_instance(arg1, arg2);
 *
 * REGISTER_PROTOCOL(my_proto_instance);
 */
#define REGISTER_PROTOCOL(prot) \
    TProtocolRegistrator reg__##prot(&prot)
