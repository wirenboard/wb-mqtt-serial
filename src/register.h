#pragma once
#include <atomic>
#include <bitset>
#include <chrono>
#include <cmath>
#include <functional>
#include <list>
#include <map>
#include <memory>
#include <mutex>
#include <optional>
#include <sstream>
#include <tuple>
#include <unordered_map>
#include <unordered_set>
#include <utility>
#include <vector>

#include "port/port.h"
#include "register_value.h"
#include "serial_exc.h"

enum RegisterFormat
{
    U8,
    S8,
    U16,
    S16,
    S24,
    U24,
    U32,
    S32,
    S64,
    U64,
    BCD8,
    BCD16,
    BCD24,
    BCD32,
    Float,
    Double,
    Char8,
    String,
    String8
};

enum class EWordOrder
{
    BigEndian,
    LittleEndian
};

enum class EByteOrder
{
    BigEndian,
    LittleEndian
};

inline ::std::ostream& operator<<(::std::ostream& os, EWordOrder val)
{
    switch (val) {
        case EWordOrder::BigEndian:
            return os << "BigEndian";
        case EWordOrder::LittleEndian:
            return os << "LittleEndian";
    }

    return os;
}

struct TRegisterType
{
    TRegisterType() = default;
    TRegisterType(int index,
                  const std::string& name,
                  const std::string& defaultControlType,
                  RegisterFormat defaultFormat = U16,
                  bool readOnly = false,
                  EWordOrder defaultWordOrder = EWordOrder::BigEndian,
                  EByteOrder defaultByteOrder = EByteOrder::BigEndian);

    int Index = 0;
    std::string Name, DefaultControlType;
    RegisterFormat DefaultFormat = U16;
    EWordOrder DefaultWordOrder = EWordOrder::BigEndian;
    EByteOrder DefaultByteOrder = EByteOrder::BigEndian;
    bool ReadOnly = false;
};

typedef std::vector<TRegisterType> TRegisterTypes;

class TRegisterTypeMap
{
    std::unordered_map<std::string, TRegisterType> RegTypes;
    TRegisterType DefaultType;

public:
    TRegisterTypeMap(const TRegisterTypes& types);

    const TRegisterType& Find(const std::string& typeName) const;
    const TRegisterType& GetDefaultType() const;
};

typedef std::shared_ptr<TRegisterTypeMap> PRegisterTypeMap;

class TRegisterConfig;
typedef std::shared_ptr<TRegisterConfig> PRegisterConfig;

class TSerialDevice;
typedef std::shared_ptr<TSerialDevice> PSerialDevice;

//! Base class for register addresses
class IRegisterAddress
{
public:
    virtual ~IRegisterAddress() = default;

    //! Get address in string representation. Used for debug messages
    virtual std::string ToString() const = 0;

    /**
     * @brief Compare two addresses. Throws if addresses are not comparable
     *
     * @param addr address to compare
     * @return <0 - this address is less than addr
     * @return 0 - the addresses are equal
     * @return >0 - this address is bigger than addr
     */
    virtual int Compare(const IRegisterAddress& addr) const = 0;

    /**
     * @brief Calculate new address based on this
     *
     * @param offset - offset in registers from this
     * @param stride - stride in registers from this
     * @param registerByteWidth - byte width of a register
     * @param addressByteStep - bytes between consecutive registers
     * @return IRegisterAddress* new address object. Life time of the object must be managed by function caller.
     */
    virtual IRegisterAddress* CalcNewAddress(uint32_t offset,
                                             uint32_t stride,
                                             uint32_t registerByteWidth,
                                             uint32_t addressByteStep) const = 0;
};

inline ::std::ostream& operator<<(::std::ostream& os, const IRegisterAddress& addr)
{
    return os << addr.ToString();
}

//! Register address represented by uint32_t value
class TUint32RegisterAddress: public IRegisterAddress
{
    uint32_t Address;

public:
    TUint32RegisterAddress(uint32_t address);

    uint32_t Get() const;

    std::string ToString() const override;

    int Compare(const IRegisterAddress& addr) const override;

    IRegisterAddress* CalcNewAddress(uint32_t offset,
                                     uint32_t stride,
                                     uint32_t registerByteWidth,
                                     uint32_t addressByteStep) const override;
};

//! Casts addr to uint32_t and returns it. Throws std::bad_cast if cast is not possible.
uint32_t GetUint32RegisterAddress(const IRegisterAddress& addr);

//! Register address represented by a string
class TStringRegisterAddress: public IRegisterAddress
{
    std::string Addr;

public:
    TStringRegisterAddress() = default;

    TStringRegisterAddress(const std::string& addr);

    std::string ToString() const override;

    int Compare(const IRegisterAddress& addr) const override;

    IRegisterAddress* CalcNewAddress(uint32_t /*offset*/,
                                     uint32_t /*stride*/,
                                     uint32_t /*registerByteWidth*/,
                                     uint32_t /*addressByteStep*/) const override;
};

//! Register addresses description
struct TRegisterDesc
{
    std::shared_ptr<IRegisterAddress> Address; //! Register address
    uint32_t DataOffset{0};                    //! Offset of data in register in bits
    uint32_t DataWidth{0};                     //! Width of data in register in bits

    std::shared_ptr<IRegisterAddress> WriteAddress; //! Write Register address
};

class TRegisterConfig: public std::enable_shared_from_this<TRegisterConfig>
{
    TRegisterDesc Address;

public:
    int Type;
    RegisterFormat Format;
    double Scale;
    double Offset;
    double RoundTo;

    enum class TSporadicMode
    {
        DISABLED,
        ONLY_EVENTS,
        EVENTS_AND_POLLING
    };
    TSporadicMode SporadicMode{TSporadicMode::DISABLED};

    enum class EAccessType
    {
        READ_WRITE,
        READ_ONLY,
        WRITE_ONLY
    };
    EAccessType AccessType{EAccessType::READ_WRITE};

    std::string FwVersion;
    std::string TypeName;

    // Minimal interval between register reads, if ReadPeriod is not set
    std::optional<std::chrono::milliseconds> ReadRateLimit;

    // Desired interval between register reads
    std::optional<std::chrono::milliseconds> ReadPeriod;
    std::optional<TRegisterValue> ErrorValue;
    EWordOrder WordOrder;
    EByteOrder ByteOrder;

    std::optional<TRegisterValue> UnsupportedValue;

    TRegisterConfig(int type,
                    const TRegisterDesc& registerAddressesDescription,
                    RegisterFormat format,
                    double scale,
                    double offset,
                    double round_to,
                    TSporadicMode sporadic,
                    bool readonly,
                    const std::string& type_name,
                    const EWordOrder word_order,
                    const EByteOrder byte_order);

    /**
     * @brief Get the width of data in bits.
     *        If the DataWidth parameter in config is zero,
     *        the value is calculated from the register width in bits according to the format.
     *        The width can be less or equal to ByteWidth of the register
     */
    uint32_t GetDataWidth() const;

    /**
     * @brief Get offset of data in register
     *
     * @return offset of data in bits.
     */
    uint32_t GetDataOffset() const;

    /**
     * @brief Get register width in bytes
     *        Returns width according to Format value, not counting DataWidth and DataOffset in config
     */
    uint32_t GetByteWidth() const;

    //! Get occupied space in 16-bit words
    uint8_t Get16BitWidth() const;

    //! Checks that the register is "partial" (register data with is not zero)
    bool IsPartial() const;

    bool IsString() const;
    std::string ToString() const;

    bool IsHighPriority() const;

    static PRegisterConfig Create(int type = 0,
                                  const TRegisterDesc& registerAddressesDescription = {},
                                  RegisterFormat format = U16,
                                  double scale = 1,
                                  double offset = 0,
                                  double round_to = 0,
                                  TSporadicMode sporadic = TSporadicMode::DISABLED,
                                  bool readonly = false,
                                  const std::string& type_name = "",
                                  const EWordOrder word_order = EWordOrder::BigEndian,
                                  const EByteOrder byte_order = EByteOrder::BigEndian);

    //! Create register with TUint32RegisterAddress
    static PRegisterConfig Create(int type = 0,
                                  uint32_t address = 0,
                                  RegisterFormat format = U16,
                                  double scale = 1,
                                  double offset = 0,
                                  double round_to = 0,
                                  TSporadicMode sporadic = TSporadicMode::DISABLED,
                                  bool readonly = false,
                                  const std::string& type_name = "",
                                  const EWordOrder word_order = EWordOrder::BigEndian,
                                  const EByteOrder byte_order = EByteOrder::BigEndian,
                                  uint32_t data_offset = 0,
                                  uint32_t data_bit_width = 0);

    const IRegisterAddress& GetAddress() const;
    const IRegisterAddress& GetWriteAddress() const;
};

struct TRegister;
typedef std::shared_ptr<TRegister> PRegister;

enum class TRegisterAvailability
{
    UNKNOWN = 0,
    AVAILABLE,
    UNAVAILABLE
};

class TReadPeriodMissChecker
{
    std::chrono::steady_clock::time_point LastReadTime;
    std::chrono::milliseconds TotalReadTime;
    std::chrono::milliseconds ReadMissCheckInterval;
    std::chrono::milliseconds MaxReadPeriod;
    size_t ReadCount;

public:
    TReadPeriodMissChecker(const std::optional<std::chrono::milliseconds>& readPeriod);

    bool IsMissed(std::chrono::steady_clock::time_point readTime);
};

struct TRegister
{
    enum TError
    {
        ReadError = 0,
        WriteError,
        PollIntervalMissError,
        MAX_ERRORS
    };

    typedef std::bitset<TError::MAX_ERRORS> TErrorState;

    TRegister(PSerialDevice device, PRegisterConfig config);

    std::string ToString() const;

    PSerialDevice Device() const
    {
        return _Device.lock();
    }

    //! The register is available in the device. It is allowed to read or write it.
    TRegisterAvailability GetAvailable() const;

    //! Set register's availability. Register must be read at least once for this.
    void SetAvailable(TRegisterAvailability available);

    TRegisterValue GetValue() const;
    void SetValue(const TRegisterValue& value, bool clearReadError = true);

    void SetError(TError error);
    void ClearError(TError error);
    const TErrorState& GetErrorState() const;

    void SetLastPollTime(std::chrono::steady_clock::time_point pollTime);

    //! The regiser is supported by the current firmware version (firmware version is equal or greater than version
    //! specified in the channel description corresponding to the register). Unsupported registers is not allowed for
    //! readind or writing and it must be excluded from polling immediately, without attempts to read.
    bool IsSupported() const;
    void SetSupported(bool supported);

    bool IsExcludedFromPolling() const;
    void ExcludeFromPolling();
    void IncludeInPolling();

    const PRegisterConfig GetConfig() const;

private:
    std::weak_ptr<TSerialDevice> _Device;
    TRegisterAvailability Available = TRegisterAvailability::UNKNOWN;
    TRegisterValue Value;
    std::string ChannelName;

    mutable std::mutex ErrorMutex;
    TErrorState ErrorState;

    TReadPeriodMissChecker ReadPeriodMissChecker;
    std::atomic<bool> Supported = true;
    bool ExcludedFromPolling = false;
    PRegisterConfig Config;
};

typedef std::vector<PRegister> TRegistersList;

inline ::std::ostream& operator<<(::std::ostream& os, PRegisterConfig reg)
{
    return os << reg->ToString();
}

inline ::std::ostream& operator<<(::std::ostream& os, const TRegisterConfig& reg)
{
    return os << reg.ToString();
}

inline ::std::ostream& operator<<(::std::ostream& os, PRegister reg)
{
    return os << reg->ToString();
}

inline const char* RegisterFormatName(RegisterFormat fmt)
{
    switch (fmt) {
        case U8:
            return "U8";
        case S8:
            return "S8";
        case U16:
            return "U16";
        case S16:
            return "S16";
        case S24:
            return "S24";
        case U24:
            return "U24";
        case U32:
            return "U32";
        case S32:
            return "S32";
        case S64:
            return "S64";
        case U64:
            return "U64";
        case BCD8:
            return "BCD8";
        case BCD16:
            return "BCD16";
        case BCD24:
            return "BCD24";
        case BCD32:
            return "BCD32";
        case Float:
            return "Float";
        case Double:
            return "Double";
        case Char8:
            return "Char8";
        case String:
            return "String";
        case String8:
            return "String8";
        default:
            return "<unknown register type>";
    }
}

inline RegisterFormat RegisterFormatFromName(const std::string& name)
{
    if (name == "s16")
        return S16;
    else if (name == "u8")
        return U8;
    else if (name == "s8")
        return S8;
    else if (name == "u24")
        return U24;
    else if (name == "s24")
        return S24;
    else if (name == "u32")
        return U32;
    else if (name == "s32")
        return S32;
    else if (name == "s64")
        return S64;
    else if (name == "u64")
        return U64;
    else if (name == "bcd8")
        return BCD8;
    else if (name == "bcd16")
        return BCD16;
    else if (name == "bcd24")
        return BCD24;
    else if (name == "bcd32")
        return BCD32;
    else if (name == "float")
        return Float;
    else if (name == "double")
        return Double;
    else if (name == "char8")
        return Char8;
    else if (name == "string")
        return String;
    else if (name == "string8")
        return String8;
    else
        return U16;
}

size_t RegisterFormatByteWidth(RegisterFormat format);

inline EWordOrder WordOrderFromName(const std::string& name)
{
    if (name == "little_endian")
        return EWordOrder::LittleEndian;
    else
        return EWordOrder::BigEndian;
}

inline EByteOrder ByteOrderFromName(const std::string& name)
{
    if (name == "little_endian")
        return EByteOrder::LittleEndian;
    else
        return EByteOrder::BigEndian;
}

class TRegisterRange
{
public:
    virtual ~TRegisterRange() = default;

    const std::list<PRegister>& RegisterList() const;
    std::list<PRegister>& RegisterList();

    virtual bool Add(TPort& port, PRegister reg, std::chrono::milliseconds pollLimit) = 0;

protected:
    bool HasOtherDeviceAndType(PRegister reg) const;

private:
    std::list<PRegister> RegList;
};

typedef std::shared_ptr<TRegisterRange> PRegisterRange;

class TSameAddressRegisterRange: public TRegisterRange
{
public:
    bool Add(TPort& port, PRegister reg, std::chrono::milliseconds pollLimit) override;
};

/**
 * @brief Tries to get a value from string and
 *        to convert it to raw bytes according to register config.
 *        Performs scaling, rounding and byte order inversion of a parsed value,
 *        if specified in config.
 *        Accepts:
 *        - signed and unsigned integers;
 *        - floating point values with or without exponent;
 *        - hex values.
 *        Throws std::invalid_argument or std::out_of_range on conversion error.
 * @param reg register config
 * @param str string to convert
 */
TRegisterValue ConvertToRawValue(const TRegisterConfig& reg, const std::string& str);

/**
 * @brief Converts raw bytes to string according to register config
 *        Performs scaling, rounding and byte order inversion of a value,
 *        if specified in config.
 * @param reg register config
 * @param val raw bytes
 */
std::string ConvertFromRawValue(const TRegisterConfig& reg, TRegisterValue val);
