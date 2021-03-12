#pragma once
#include <map>
#include <list>
#include <mutex>
#include <chrono>
#include <vector>
#include <memory>
#include <sstream>
#include <functional>
#include <unordered_set>
#include <unordered_map>
#include <utility>
#include <mutex>
#include <tuple>
#include <cmath>

#include "serial_exc.h"

enum RegisterFormat {
    AUTO,
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
    Char8
};

enum class EWordOrder {
    BigEndian,
    LittleEndian
};

inline ::std::ostream& operator<<(::std::ostream& os, EWordOrder val) {
    switch (val) {
        case EWordOrder::BigEndian:
            return os << "BigEndian";
        case EWordOrder::LittleEndian:
            return os << "LittleEndian";
    }

    return os;
}

struct TRegisterType {
    TRegisterType(int index, const std::string& name, const std::string& defaultControlType,
                  RegisterFormat defaultFormat = U16,
                  bool read_only = false, EWordOrder defaultWordOrder = EWordOrder::BigEndian):
        Index(index), Name(name), DefaultControlType(defaultControlType),
        DefaultFormat(defaultFormat), DefaultWordOrder(defaultWordOrder), ReadOnly(read_only) {}
    int Index;
    std::string Name, DefaultControlType;
    RegisterFormat DefaultFormat;
    EWordOrder DefaultWordOrder;
    bool ReadOnly;
};

typedef std::vector<TRegisterType> TRegisterTypes;
typedef std::map<std::string, TRegisterType> TRegisterTypeMap;
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
     * @brief Compares two addresses. Throws if addresses are not comparable
     * 
     * @param addr address to campare
     * @return true - this address is less than addr
     * @return false - this address is not less than addr
     */
    virtual bool IsLessThan(IRegisterAddress& addr) const = 0;

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

inline ::std::ostream& operator<<(::std::ostream& os, std::shared_ptr<IRegisterAddress> addr) {
    return os << addr->ToString();
}

//! Register address represented by uint32_t value
class TUint32RegisterAddress: public IRegisterAddress
{
    uint32_t Address;
public:
    TUint32RegisterAddress(uint32_t address);

    uint32_t Get() const;

    std::string ToString() const override;

    bool IsLessThan(IRegisterAddress& addr) const;

    IRegisterAddress* CalcNewAddress(uint32_t offset,
                                     uint32_t stride,
                                     uint32_t registerByteWidth,
                                     uint32_t addressByteStep) const override;
};

struct TRegisterConfig : public std::enable_shared_from_this<TRegisterConfig>
{
    int                               Type;
    std::shared_ptr<IRegisterAddress> Address;
    RegisterFormat                    Format;
    double                            Scale;
    double                            Offset;
    double                            RoundTo;
    bool                              Poll;
    bool                              ReadOnly;
    std::string                       TypeName;
    std::chrono::milliseconds         PollInterval = std::chrono::milliseconds(-1);
    std::unique_ptr<uint64_t>         ErrorValue;
    EWordOrder                        WordOrder;
    uint8_t                           BitOffset;
    uint8_t                           BitWidth;
    std::unique_ptr<uint64_t>         UnsupportedValue;

    TRegisterConfig(int                               type,
                    std::shared_ptr<IRegisterAddress> address,
                    RegisterFormat                    format,
                    double                            scale,
                    double                            offset,
                    double                            round_to,
                    bool                              poll,
                    bool                              readonly,
                    const std::string&                type_name,
                    std::unique_ptr<uint64_t>         error_value,
                    const EWordOrder                  word_order,
                    uint8_t                           bit_offset,
                    uint8_t                           bit_width,
                    std::unique_ptr<uint64_t>         unsupported_value);

    TRegisterConfig(const TRegisterConfig& config);

    uint8_t GetBitWidth() const;
    uint8_t GetByteWidth() const;

    //! Get occupied space in 16-bit words
    uint8_t Get16BitWidth() const;

    std::string ToString() const;

    static PRegisterConfig Create(int type                                     = 0,
                                  std::shared_ptr<IRegisterAddress> address    = std::shared_ptr<IRegisterAddress>(),
                                  RegisterFormat format                        = U16,
                                  double scale                                 = 1,
                                  double offset                                = 0,
                                  double round_to                              = 0,
                                  bool poll                                    = true,
                                  bool readonly                                = false,
                                  const std::string& type_name                 = "",
                                  std::unique_ptr<uint64_t> error_value        = std::unique_ptr<uint64_t>(),
                                  const EWordOrder word_order                  = EWordOrder::BigEndian,
                                  uint8_t bit_offset                           = 0,
                                  uint8_t bit_width                            = 0,
                                  std::unique_ptr<uint64_t> unsupported_value  = std::unique_ptr<uint64_t>());

    //! Create register with TUint32RegisterAddress
    static PRegisterConfig Create(int type                                     = 0,
                                  uint32_t address                             = 0,
                                  RegisterFormat format                        = U16,
                                  double scale                                 = 1,
                                  double offset                                = 0,
                                  double round_to                              = 0,
                                  bool poll                                    = true,
                                  bool readonly                                = false,
                                  const std::string& type_name                 = "",
                                  std::unique_ptr<uint64_t> error_value        = std::unique_ptr<uint64_t>(),
                                  const EWordOrder word_order                  = EWordOrder::BigEndian,
                                  uint8_t bit_offset                           = 0,
                                  uint8_t bit_width                            = 0,
                                  std::unique_ptr<uint64_t> unsupported_value  = std::unique_ptr<uint64_t>());
};

struct TRegister;
typedef std::shared_ptr<TRegister> PRegister;

struct TRegister : public TRegisterConfig
{
    TRegister(PSerialDevice device, PRegisterConfig config)
        : TRegisterConfig(*config)
        , _Device(device)
    {}

    ~TRegister()
    {}

    std::string ToString() const;

    PSerialDevice Device() const
    {
        return _Device.lock();
    }

    //! The register is available in the device. It is allowed to read or write it
    bool IsAvailable() const;

    //! Set register's availability
    void SetAvailable(bool available);

    bool GetError() const;
    void SetError();

    uint64_t GetValue() const;
    void SetValue(uint64_t value);

private:
    std::weak_ptr<TSerialDevice> _Device;
    bool Available = true;
    bool Error = true;
    uint64_t Value;

    // Intern() implementation for TRegister
private:
    static std::map<std::tuple<PSerialDevice, PRegisterConfig>, PRegister> RegStorage;
    static std::mutex Mutex;

public:
    static PRegister Intern(PSerialDevice device, PRegisterConfig config)
    {
        std::unique_lock<std::mutex> lock(Mutex); // thread-safe
        std::tuple<PSerialDevice, PRegisterConfig> args(device, config);

        auto it = RegStorage.find(args);

        if (it == RegStorage.end()) {
            auto ret = std::make_shared<TRegister>(device, config);
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

inline ::std::ostream& operator<<(::std::ostream& os, PRegisterConfig reg) {
    return os << reg->ToString();
}

inline ::std::ostream& operator<<(::std::ostream& os, const TRegisterConfig& reg) {
    return os << reg.ToString();
}

inline ::std::ostream& operator<<(::std::ostream& os, PRegister reg) {
    return os << reg->ToString();
}

inline const char* RegisterFormatName(RegisterFormat fmt) {
    switch (fmt) {
    case AUTO:
        return "AUTO";
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
    default:
        return "<unknown register type>";
    }
}

inline RegisterFormat RegisterFormatFromName(const std::string& name) {
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
    else
        return U16; // FIXME!
}

size_t RegisterFormatByteWidth(RegisterFormat format);

inline EWordOrder WordOrderFromName(const std::string& name) {
    if (name == "big_endian")
        return EWordOrder::BigEndian;
    else if (name == "little_endian")
        return EWordOrder::LittleEndian;
    else
        return EWordOrder::BigEndian;
}


class TRegisterRange {
public:
    typedef std::function<void(PRegister reg, uint64_t new_value)> TValueCallback;
    typedef std::function<void(PRegister reg)> TErrorCallback;
    enum EStatus {
        ST_OK,
        ST_UNKNOWN_ERROR, // response from device either not parsed or not received at all (crc error, timeout)
        ST_DEVICE_ERROR // valid response from device, which reports error
    };

    virtual ~TRegisterRange() = default;

    const std::list<PRegister>& RegisterList() const;
    std::list<PRegister>& RegisterList();
    PSerialDevice Device() const;
    int Type() const;
    std::string TypeName() const;
    std::chrono::milliseconds PollInterval() const;

    /**
     * @brief Set error to all registers in range
     */
    void SetError();

    virtual EStatus GetStatus() const = 0;

protected:
    TRegisterRange(const std::list<PRegister>& regs);
    TRegisterRange(PRegister reg);

private:
    std::weak_ptr<TSerialDevice> RegDevice;
    int RegType;
    std::string RegTypeName;
    std::chrono::milliseconds RegPollInterval = std::chrono::milliseconds(-1);
    std::list<PRegister> RegList;
};

typedef std::shared_ptr<TRegisterRange> PRegisterRange;

class TSimpleRegisterRange: public TRegisterRange {
public:
    TSimpleRegisterRange(const std::list<PRegister>& regs);
    TSimpleRegisterRange(PRegister reg);

    EStatus GetStatus() const override;
};

typedef std::shared_ptr<TSimpleRegisterRange> PSimpleRegisterRange;
