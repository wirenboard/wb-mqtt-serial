#pragma once
#include <bitset>
#include <chrono>
#include <cmath>
#include <experimental/optional>
#include <functional>
#include <list>
#include <map>
#include <memory>
#include <mutex>
#include <sstream>
#include <tuple>
#include <unordered_map>
#include <unordered_set>
#include <utility>
#include <vector>

#include "channel_value.h"
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
    String32
};

enum class EWordOrder
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
                  EWordOrder defaultWordOrder = EWordOrder::BigEndian);

    int Index = 0;
    std::string Name, DefaultControlType;
    RegisterFormat DefaultFormat = U16;
    EWordOrder DefaultWordOrder = EWordOrder::BigEndian;
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

struct TRegisterConfig;
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

class TRegisterConfig: public std::enable_shared_from_this<TRegisterConfig>
{
    std::shared_ptr<IRegisterAddress> Address;

public:
    int Type;
    RegisterFormat Format;
    double Scale;
    double Offset;
    double RoundTo;
    bool WriteOnly = false;
    bool ReadOnly;
    std::string TypeName;

    // Minimal interval between register reads, if ReadPeriod is not set
    std::experimental::optional<std::chrono::milliseconds> ReadRateLimit;

    // Desired interval between register reads
    std::experimental::optional<std::chrono::milliseconds> ReadPeriod;
    std::experimental::optional<TChannelValue> ErrorValue;
    EWordOrder WordOrder;

    // Offset of data in response. Could be bit offset or index in array depending on protocol
    uint8_t DataOffset;

    // Width of data in response. Could be bit width or anything else depending on protocol
    uint8_t DataWidth;

    std::experimental::optional<TChannelValue> UnsupportedValue;

    TRegisterConfig(int type,
                    std::shared_ptr<IRegisterAddress> address,
                    RegisterFormat format,
                    double scale,
                    double offset,
                    double round_to,
                    bool readonly,
                    const std::string& type_name,
                    const EWordOrder word_order,
                    uint32_t bit_offset,
                    uint32_t bit_width);

    uint32_t GetBitWidth() const;
    uint32_t GetByteWidth() const;

    //! Get occupied space in 16-bit words
    uint8_t Get16BitWidth() const;

    std::string ToString() const;

    static PRegisterConfig Create(int type = 0,
                                  std::shared_ptr<IRegisterAddress> address = std::shared_ptr<IRegisterAddress>(),
                                  RegisterFormat format = U16,
                                  double scale = 1,
                                  double offset = 0,
                                  double round_to = 0,
                                  bool readonly = false,
                                  const std::string& type_name = "",
                                  const EWordOrder word_order = EWordOrder::BigEndian,
                                  uint32_t bit_offset = 0,
                                  uint32_t bit_width = 0);

    //! Create register with TUint32RegisterAddress
    static PRegisterConfig Create(int type = 0,
                                  uint32_t address = 0,
                                  RegisterFormat format = U16,
                                  double scale = 1,
                                  double offset = 0,
                                  double round_to = 0,
                                  bool readonly = false,
                                  const std::string& type_name = "",
                                  const EWordOrder word_order = EWordOrder::BigEndian,
                                  uint32_t bit_offset = 0,
                                  uint32_t bit_width = 0);

    const IRegisterAddress& GetAddress() const;
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
    TReadPeriodMissChecker(const std::experimental::optional<std::chrono::milliseconds>& readPeriod);

    bool IsMissed(std::chrono::steady_clock::time_point readTime);
};

struct TRegister: public TRegisterConfig
{
    enum TError
    {
        ReadError = 0,
        WriteError,
        PollIntervalMissError,
        MAX_ERRORS
    };

    typedef std::bitset<TError::MAX_ERRORS> TErrorState;

    TRegister(PSerialDevice device, PRegisterConfig config, const std::string& channelName = std::string());

    std::string ToString() const;

    PSerialDevice Device() const
    {
        return _Device.lock();
    }

    //! The register is available in the device. It is allowed to read or write it
    TRegisterAvailability GetAvailable() const;

    //! Set register's availability
    void SetAvailable(TRegisterAvailability available);

    TChannelValue GetValue() const;
    void SetValue(const TChannelValue& value, bool clearReadError = true);

    void SetError(TError error);
    void ClearError(TError error);
    const TErrorState& GetErrorState() const;

    void SetLastPollTime(std::chrono::steady_clock::time_point pollTime);

    //! Used for metrics
    const std::string& GetChannelName() const;

private:
    std::weak_ptr<TSerialDevice> _Device;
    TRegisterAvailability Available = TRegisterAvailability::UNKNOWN;
    TChannelValue Value;
    std::string ChannelName;
    TErrorState ErrorState;
    TReadPeriodMissChecker ReadPeriodMissChecker;

    // Intern() implementation for TRegister
private:
    static std::map<std::tuple<PSerialDevice, PRegisterConfig>, PRegister> RegStorage;
    static std::mutex Mutex;

public:
    static PRegister Intern(PSerialDevice device,
                            PRegisterConfig config,
                            const std::string& channelName = std::string())
    {
        std::unique_lock<std::mutex> lock(Mutex); // thread-safe
        std::tuple<PSerialDevice, PRegisterConfig> args(device, config);

        auto it = RegStorage.find(args);

        if (it == RegStorage.end()) {
            auto ret = std::make_shared<TRegister>(device, config, channelName);
            return RegStorage[args] = ret;
        }

        return it->second;
    }

    static void DeleteIntern()
    {
        RegStorage.clear();
    }
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
        case String32:
            return "String32";
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
    else if (name == "string32")
        return String32;
    else
        return U16;
}

size_t RegisterFormatByteWidth(RegisterFormat format);

inline EWordOrder WordOrderFromName(const std::string& name)
{
    if (name == "big_endian")
        return EWordOrder::BigEndian;
    else if (name == "little_endian")
        return EWordOrder::LittleEndian;
    else
        return EWordOrder::BigEndian;
}

class TRegisterRange
{
public:
    virtual ~TRegisterRange() = default;

    const std::list<PRegister>& RegisterList() const;
    std::list<PRegister>& RegisterList();

    virtual bool Add(PRegister reg, std::chrono::milliseconds pollLimit) = 0;

protected:
    bool HasOtherDeviceAndType(PRegister reg) const;

private:
    std::list<PRegister> RegList;
};

typedef std::shared_ptr<TRegisterRange> PRegisterRange;

class TSameAddressRegisterRange: public TRegisterRange
{
public:
    bool Add(PRegister reg, std::chrono::milliseconds pollLimit) override;
};

TChannelValue InvertWordOrderIfNeeded(const TRegisterConfig& reg, TChannelValue value);

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
TChannelValue ConvertToRawValue(const TRegisterConfig& reg, const std::string& str);

/**
 * @brief Converts raw bytes to string according to register config
 *        Performs scaling, rounding and byte order inversion of a value,
 *        if specified in config.
 * @param reg register config
 * @param val raw bytes
 */
std::string ConvertFromRawValue(const TRegisterConfig& reg, TChannelValue val);
