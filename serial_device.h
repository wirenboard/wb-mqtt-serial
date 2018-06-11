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
#include <cassert>

#include "serial_exc.h"
#include "serial_config.h"
#include "port.h"
#include "utils.h"
#include "ir_device_memory_view.h"


using TRegisterTypes = std::vector<TMemoryBlockType>;

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
        inline size_t operator()(const TAggregatedSlaveId & x) const noexcept
        {
            return hash<string>{}(to_string(x));
        }
    };
}

/**
 * describes general protocol properties
 */
struct TProtocolInfo
{
    virtual ~TProtocolInfo() = default;

    virtual bool IsSingleBitType(const TMemoryBlockType & type) const;
    virtual int GetMaxReadRegisters() const;
    virtual int GetMaxReadBits() const;
    virtual int GetMaxWriteRegisters() const;
    virtual int GetMaxWriteBits() const;
};

class TSerialDevice: public std::enable_shared_from_this<TSerialDevice> {
public:
    TSerialDevice(PDeviceConfig config, PPort port, PProtocol protocol);
    TSerialDevice(const TSerialDevice&) = delete;
    TSerialDevice& operator=(const TSerialDevice&) = delete;
    virtual ~TSerialDevice();
    virtual const TProtocolInfo & GetProtocolInfo() const;
    PMemoryBlock GetCreateMemoryBlock(uint32_t address, uint32_t type, uint16_t size = 0);
    TPSetRange<PMemoryBlock> CreateMemoryBlockRange(const PMemoryBlock & first, const PMemoryBlock & last) const;

    // Prepare to access device (pauses for configured delay by default)
    virtual void Prepare();
    // Handle end of poll cycle e.g. by resetting values caches
    virtual void EndPollCycle();

    virtual std::string ToString() const;

    void Execute(const PIRDeviceQuery &);

    // Initialize setup items' registers
    void InitSetupItems();
    bool HasSetupItems() const;
    bool WriteSetupRegisters(bool tryAll = true);

    PPort Port() const { return SerialPort; }
    PDeviceConfig DeviceConfig() const { return _DeviceConfig; }
    PProtocol Protocol() const { return _Protocol; }

    virtual void OnCycleEnd(bool ok);
    bool GetIsDisconnected() const;

    void InitializeMemoryBlocksCache();

    static TPSetRange<PMemoryBlock> StaticCreateMemoryBlockRange(const PMemoryBlock & first, const PMemoryBlock & last);

protected:
    void SleepGuardInterval() const;

    /**
     * Override these methods if protocol supports multiple read / write
     */
    virtual void Read(const TIRDeviceQuery &);
    virtual void Write(const TIRDeviceValueQuery &);

private:
    std::chrono::milliseconds Delay;
    PPort SerialPort;
    PDeviceConfig _DeviceConfig;
    PProtocol _Protocol;
    std::vector<PDeviceSetupItem> SetupItems;
    TPSet<PMemoryBlock> MemoryBlocks;
    std::vector<uint8_t> Cache;
    std::chrono::steady_clock::time_point LastSuccessfulCycle;
    bool IsDisconnected;
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

    /*! Get register type by name */
    virtual const TRegisterType & GetRegType(const std::string & name) const = 0;

    /*! Get register type by index */
    virtual const TRegisterType & GetRegType(int index) const = 0;

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
int TBasicProtocolConverter<int>::ConvertSlaveId(const std::string &s) const;

template<>
TAggregatedSlaveId TBasicProtocolConverter<TAggregatedSlaveId>::ConvertSlaveId(const std::string &s) const;

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
        int maxIndex = 0;

        _RegTypes = std::make_shared<TRegisterTypeMap>();
        for (const auto& rt : reg_types) {
            assert(rt.Index >= 0);

            _RegTypes->insert(std::make_pair(rt.Name, rt));
            maxIndex = std::max(maxIndex, rt.Index);
        }

        _RegTypeNameByIndex.resize(maxIndex + 1);

        for (const auto& rt : reg_types) {
            _RegTypeNameByIndex[rt.Index] = rt.Name;
        }
    }

protected:
    // for inheritance
    TBasicProtocol()
        : _Name("")
        , _RegTypes()
    {}

public:
    std::string GetName() const override { return _Name; }
    PRegisterTypeMap GetRegTypes() const override { return _RegTypes; }
    const TRegisterType & GetRegType(const std::string & name) const override
    {
        try {
            return _RegTypes->at(name);
        } catch (std::out_of_range&) {
            throw TSerialDeviceException("unknown register type: '" + name + "'");
        }
    }
    const TRegisterType & GetRegType(int index) const override
    {
        assert(index < static_cast<int>(_RegTypeNameByIndex.size()));

        return GetRegType(_RegTypeNameByIndex[index]);
    }

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
    std::vector<std::string> _RegTypeNameByIndex;
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
            std::cerr << "cannot cast " << protocol->GetName() << " to " << typeid(Proto).name() << std::endl;
            throw std::runtime_error("Wrong protocol cast, check registration code and class header");
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
