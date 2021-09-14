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
#include "port.h"

typedef std::unordered_map<std::string, std::string> TTitleTranslations;

struct TDeviceChannelConfig 
{
    std::string                  MqttId; // MQTT topic name. If empty Name is used
    std::string                  Type;
    std::string                  DeviceId;
    int                          Order            = 0;
    std::string                  OnValue;
    std::string                  OffValue;
    double                       Max              =std::numeric_limits<double>::signaling_NaN();
    double                       Min              =std::numeric_limits<double>::signaling_NaN();
    double                       Precision        = 0;
    bool                         ReadOnly         = false;
    std::vector<PRegisterConfig> RegisterConfigs;

    TDeviceChannelConfig(const std::string& name                 = "",
                         const std::string& type                 = "text",
                         const std::string& deviceId             = "",
                         int                order                = 0,
                         bool               readOnly             = false,
                         const std::string& mqttId               = "",
                         const std::vector<PRegisterConfig>& regs = std::vector<PRegisterConfig>());

    //! Will be published in /devices/+/meta/name and used in log messages
    const std::string& GetName() const;

    const TTitleTranslations& GetTitles() const;
    void SetTitle(const std::string& name, const std::string& lang);
private:
    TTitleTranslations Titles;
};

typedef std::shared_ptr<TDeviceChannelConfig> PDeviceChannelConfig;

class TDeviceSetupItemConfig 
{
    std::string     Name;
    PRegisterConfig RegisterConfig;
    std::string     Value;
    uint64_t        RawValue;
public:
    TDeviceSetupItemConfig(const std::string& name, PRegisterConfig reg, const std::string& value);

    const std::string& GetName() const;
    const std::string& GetValue() const;
    uint64_t GetRawValue() const;
    PRegisterConfig GetRegisterConfig() const;
};

typedef std::shared_ptr<TDeviceSetupItemConfig> PDeviceSetupItemConfig;

const int DEFAULT_ACCESS_LEVEL       = 1;
const int DEFAULT_DEVICE_FAIL_CYCLES = 2;

const std::chrono::milliseconds DefaultPollInterval(20);
const std::chrono::milliseconds DefaultFrameTimeout(20);
const std::chrono::milliseconds DefaultResponseTimeout(500);
const std::chrono::milliseconds DefaultDeviceTimeout(3000);
const std::chrono::seconds      MaxUnchangedIntervalLowLimit(5);
const std::chrono::seconds      DefaultMaxUnchangedInterval(-1);

struct TDeviceConfig 
{
    //! Part of /devices/+ topic name
    std::string                         Id;

    //! Will be published in /devices/+/meta/name and used in log messages
    std::string                         Name;
    std::string                         SlaveId;
    std::string                         DeviceType;
    std::string                         Protocol;
    std::vector<PDeviceChannelConfig>   DeviceChannelConfigs;
    std::vector<PDeviceSetupItemConfig> SetupItemConfigs;
    std::vector<uint8_t>                Password;

    //! Maximum allowed time from request to response. -1 if not set, DefaultResponseTimeout will be used.
    std::chrono::milliseconds           ResponseTimeout        = std::chrono::milliseconds(-1);

    //! Minimum inter-frame delay.
    std::chrono::milliseconds           FrameTimeout           = DefaultFrameTimeout;

    //! The period of unsuccessful requests after which the device is considered disconected.
    std::chrono::milliseconds           DeviceTimeout          = DefaultDeviceTimeout;

    //! Delay before sending any request
    std::chrono::microseconds           RequestDelay           = std::chrono::microseconds::zero();

    int                                 AccessLevel            = DEFAULT_ACCESS_LEVEL;
    int                                 MaxRegHole             = 0;
    int                                 MaxBitHole             = 0;
    int                                 MaxReadRegisters       = 1;
    int                                 Stride                 = 0;
    int                                 Shift                  = 0;
    PRegisterTypeMap                    TypeMap                = 0;
    int                                 DeviceMaxFailCycles    = DEFAULT_DEVICE_FAIL_CYCLES;

    explicit TDeviceConfig(const std::string& name = "", const std::string& slave_id = "", const std::string& protocol = "");

    int NextOrderValue() const;
    void AddChannel(PDeviceChannelConfig channel);
    void AddSetupItem(PDeviceSetupItemConfig item, const std::string& deviceTemplateTitle = std::string());

    std::string GetDescription() const;

private:
    // map key is setup item address
    std::unordered_map<std::string, PDeviceSetupItemConfig> SetupItemsByAddress;
};

typedef std::shared_ptr<TDeviceConfig> PDeviceConfig;


class IProtocol;
typedef IProtocol* PProtocol;

struct TDeviceSetupItem
{
    TDeviceSetupItem(PSerialDevice device, PDeviceSetupItemConfig config)
        : Name(config->GetName()),
          RawValue(config->GetRawValue()),
          HumanReadableValue(config->GetValue())
    {
        Register = TRegister::Intern(device, config->GetRegisterConfig());
    }

    std::string Name;
    uint64_t    RawValue;
    std::string HumanReadableValue;
    PRegister   Register;
};

typedef std::shared_ptr<TDeviceSetupItem> PDeviceSetupItem;

struct TUInt32SlaveId
{
    uint32_t SlaveId;
    bool     HasBroadcastSlaveId;

    TUInt32SlaveId(const std::string& slaveId, bool allowBroadcast = false);

    bool operator==(const TUInt32SlaveId& id) const;
};

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

    virtual ~IProtocol() = default;

    /*! Get protocol name */
    virtual const std::string& GetName() const;

    /*! Get map of register types */
    virtual PRegisterTypeMap GetRegTypes() const;

    virtual bool IsSameSlaveId(const std::string& id1, const std::string& id2) const = 0;

    /*! The protocol is from MODBUS family. 
     *  We check it during config validation to pevent using non MODBUS devices on modbus-tcp port.
     */
    virtual bool IsModbus() const;

    /** Check if protocol supports broadcast requests.
     *  It is used during generation of a schema for confed and during config validation.
     *  For protocols with broadcast support "slave_id" is not required.
     */
    virtual bool SupportsBroadcast() const;

private:
    std::string Name;
    PRegisterTypeMap RegTypes;
};

/*!
 * Basic protocol implementation with uint32_t slave ID without broadcast
 */
class TUint32SlaveIdProtocol : public IProtocol
{
    bool AllowBroadcast;
public:
    /*! Construct new protocol with given name and register types list */
    TUint32SlaveIdProtocol(const std::string& name, const TRegisterTypes& reg_types, bool allowBroadcast = false);

    bool IsSameSlaveId(const std::string& id1, const std::string& id2) const override;

    bool SupportsBroadcast() const override;
};

//! Copy bits from double to uint64_t with size checking
uint64_t CopyDoubleToUint64(double value);
