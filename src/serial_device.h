#pragma once

#include <algorithm>
#include <exception>
#include <iostream>
#include <list>
#include <memory>
#include <set>
#include <stdint.h>
#include <string>
#include <unordered_map>
#include <vector>

#include "port.h"
#include "register.h"
#include "serial_exc.h"

typedef std::unordered_map<std::string, std::string> TTitleTranslations;

struct TDeviceChannelConfig
{
    std::string MqttId; // MQTT topic name. If empty Name is used
    std::string Type;
    std::string DeviceId;
    int Order = 0;
    std::string OnValue;
    std::string OffValue;
    double Max = std::numeric_limits<double>::signaling_NaN();
    double Min = std::numeric_limits<double>::signaling_NaN();
    double Precision = 0;
    bool ReadOnly = false;
    std::string Units;
    std::vector<PRegister> Registers;

    TDeviceChannelConfig(const std::string& type = "text",
                         const std::string& deviceId = "",
                         int order = 0,
                         bool readOnly = false,
                         const std::string& mqttId = "",
                         const std::vector<PRegister>& regs = std::vector<PRegister>());

    //! Will be published in /devices/+/meta/name and used in log messages
    const std::string& GetName() const;

    const TTitleTranslations& GetTitles() const;
    void SetTitle(const std::string& name, const std::string& lang);

    const std::map<std::string, TTitleTranslations>& GetEnumTitles() const;
    void SetEnumTitles(const std::string& value, const TTitleTranslations& titles);

private:
    TTitleTranslations Titles;
    std::map<std::string, TTitleTranslations> EnumTitles;
};

typedef std::shared_ptr<TDeviceChannelConfig> PDeviceChannelConfig;

class TDeviceSetupItemConfig
{
    std::string Name;
    PRegisterConfig RegisterConfig;
    std::string Value;
    TRegisterValue RawValue;
    std::string ParameterId;

public:
    TDeviceSetupItemConfig(const std::string& name,
                           PRegisterConfig reg,
                           const std::string& value,
                           const std::string& parameterId = std::string());

    const std::string& GetName() const;
    const std::string& GetValue() const;
    const std::string& GetParameterId() const;
    TRegisterValue GetRawValue() const;
    PRegisterConfig GetRegisterConfig() const;
};

typedef std::shared_ptr<TDeviceSetupItemConfig> PDeviceSetupItemConfig;

const int DEFAULT_ACCESS_LEVEL = 1;
const int DEFAULT_DEVICE_FAIL_CYCLES = 2;

const std::chrono::milliseconds DefaultFrameTimeout(20);
const std::chrono::milliseconds DefaultResponseTimeout(500);
const std::chrono::milliseconds DefaultDeviceTimeout(3000);
const std::chrono::seconds MaxUnchangedIntervalLowLimit(5);
const std::chrono::seconds DefaultMaxUnchangedInterval(-1);
const std::chrono::seconds DefaultMaxWriteFailTime(600);

struct TDeviceConfig
{
    //! Part of /devices/+ topic name
    std::string Id;

    //! Will be published in /devices/+/meta/name and used in log messages
    std::string Name;
    std::string SlaveId;
    std::string DeviceType;
    std::string Protocol;
    std::vector<uint8_t> Password;

    //! Maximum allowed time from request to response. -1 if not set, DefaultResponseTimeout will be used.
    std::chrono::milliseconds ResponseTimeout = std::chrono::milliseconds(-1);

    //! Minimum inter-frame delay.
    std::chrono::milliseconds FrameTimeout = DefaultFrameTimeout;

    //! The period of unsuccessful requests after which the device is considered disconnected.
    std::chrono::milliseconds DeviceTimeout = DefaultDeviceTimeout;

    //! Delay before sending any request
    std::chrono::microseconds RequestDelay = std::chrono::microseconds::zero();

    //! Minimal time between two consecutive requests to the device.
    std::chrono::milliseconds MinRequestInterval = std::chrono::milliseconds::zero();

    std::chrono::seconds MaxWriteFailTime = DefaultMaxWriteFailTime;

    int AccessLevel = DEFAULT_ACCESS_LEVEL;
    int MaxRegHole = 0;
    int MaxBitHole = 0;
    int MaxReadRegisters = 1;
    size_t MinReadRegisters = 1;
    size_t MaxWriteRegisters = 1;
    int Stride = 0;
    int Shift = 0;
    int DeviceMaxFailCycles = DEFAULT_DEVICE_FAIL_CYCLES;

    explicit TDeviceConfig(const std::string& name = "",
                           const std::string& slave_id = "",
                           const std::string& protocol = "");
};

typedef std::shared_ptr<TDeviceConfig> PDeviceConfig;

class IProtocol;
typedef IProtocol* PProtocol;

class TDeviceSetupItem
{
public:
    std::string Name;
    std::string ParameterId;
    TRegisterValue RawValue;
    std::string HumanReadableValue;
    PRegisterConfig RegisterConfig;
    PSerialDevice Device;

    TDeviceSetupItem(PDeviceSetupItemConfig config, PSerialDevice device);
    std::string ToString();
};

typedef std::shared_ptr<TDeviceSetupItem> PDeviceSetupItem;

struct TDeviceSetupItemComparePredicate
{
    bool operator()(const PDeviceSetupItem& a, const PDeviceSetupItem& b) const;
};

typedef std::set<PDeviceSetupItem, TDeviceSetupItemComparePredicate> TDeviceSetupItems;

struct TUInt32SlaveId
{
    uint32_t SlaveId;
    bool HasBroadcastSlaveId;

    TUInt32SlaveId(const std::string& slaveId, bool allowBroadcast = false);

    bool operator==(const TUInt32SlaveId& id) const;
};

enum class TDeviceConnectionState
{
    UNKNOWN,
    CONNECTED,
    DISCONNECTED
};

enum class TDevicePrepareMode
{
    WITH_SETUP_IF_WAS_DISCONNECTED,
    WITHOUT_SETUP
};

class TSerialDevice: public std::enable_shared_from_this<TSerialDevice>
{
public:
    typedef std::function<void(PSerialDevice dev)> TDeviceCallback;

    TSerialDevice(PDeviceConfig config, PPort port, PProtocol protocol);
    TSerialDevice(const TSerialDevice&) = delete;
    TSerialDevice& operator=(const TSerialDevice&) = delete;
    virtual ~TSerialDevice() = default;

    /**
     * @brief Create a Register Range object
     */
    virtual PRegisterRange CreateRegisterRange() const;

    /**
     * @brief Prepare to access device (pauses for configured delay by default) i.e. "StartSession".
     *        Called before any read/write/etc after communicating with another device.
     *
     * @throws exceptions inherited from TSerialDeviceException on internal errors
     */
    void Prepare(TDevicePrepareMode setupMode = TDevicePrepareMode::WITH_SETUP_IF_WAS_DISCONNECTED);

    // Ends communication session with the device. Called before communicating with another device
    virtual void EndSession();

    // Write register value
    void WriteRegister(PRegister reg, const TRegisterValue& value);

    void WriteRegister(PRegister reg, uint64_t value);

    /**
     * Reads multiple registers.
     * Throws exceptions inherited from TSerialDeviceException.
     */
    virtual void ReadRegisterRange(PRegisterRange range);

    virtual std::string ToString() const;

    PPort Port() const;
    PDeviceConfig DeviceConfig() const;
    PProtocol Protocol() const;

    /**
     * @brief Sets the result of the last data transfer operation.
     *        Successful transfer indicates that a device is connected and responding.
     *
     * @warning When reading a register, this method must be called before TRegister::SetValue
     *          because it clears SnRegister's value.
     *
     * @param ok A boolean value indicating whether the transfer was successful (true) or not (false).
     */
    virtual void SetTransferResult(bool ok);

    TDeviceConnectionState GetConnectionState() const;
    void SetDisconnected();

    bool GetSupportsHoles() const;
    void SetSupportsHoles(bool supportsHoles);

    bool IsSporadicOnly() const;
    void SetSporadicOnly(bool sporadicOnly);

    // Reset values caches
    virtual void InvalidateReadCache();

    PRegister AddRegister(PRegisterConfig config);

    const std::list<PRegister>& GetRegisters() const;

    std::chrono::steady_clock::time_point GetLastReadTime() const;
    void SetLastReadTime(std::chrono::steady_clock::time_point readTime);

    void AddOnConnectionStateChangedCallback(TDeviceCallback callback);

    PRegister GetSnRegister() const;
    void SetSnRegister(PRegisterConfig regConfig);

    void AddSetupItem(PDeviceSetupItemConfig item);
    const TDeviceSetupItems& GetSetupItems() const;

    virtual void WriteSetupRegisters(const TDeviceSetupItems& setupItems);

protected:
    virtual void PrepareImpl();
    virtual TRegisterValue ReadRegisterImpl(const TRegisterConfig& reg);
    virtual void WriteRegisterImpl(const TRegisterConfig& reg, const TRegisterValue& value);

private:
    PPort SerialPort;
    PDeviceConfig _DeviceConfig;
    PProtocol _Protocol;
    std::chrono::steady_clock::time_point LastSuccessfulCycle;
    TDeviceConnectionState ConnectionState;
    int RemainingFailCycles;
    bool SupportsHoles;
    bool SporadicOnly;
    std::list<PRegister> Registers;
    std::chrono::steady_clock::time_point LastReadTime;
    std::vector<TDeviceCallback> ConnectionStateChangedCallbacks;
    PRegister SnRegister;

    // map key is setup item address
    std::unordered_map<std::string, PDeviceSetupItem> SetupItemsByAddress;
    TDeviceSetupItems SetupItems;

    void SetConnectionState(TDeviceConnectionState state);
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
class TUint32SlaveIdProtocol: public IProtocol
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
