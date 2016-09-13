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

#include "registry.h"
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

struct TRegisterType {
    TRegisterType(int index, const std::string& name, const std::string& defaultControlType,
                  RegisterFormat defaultFormat = U16, bool read_only = false):
        Index(index), Name(name), DefaultControlType(defaultControlType),
        DefaultFormat(defaultFormat), ReadOnly(read_only) {}
    int Index;
    std::string Name, DefaultControlType;
    RegisterFormat DefaultFormat;
    bool ReadOnly;
};

typedef std::vector<TRegisterType> TRegisterTypes;
typedef std::map<std::string, TRegisterType> TRegisterTypeMap;
typedef std::shared_ptr<TRegisterTypeMap> PRegisterTypeMap;

struct TRegisterConfig;
typedef std::shared_ptr<TRegisterConfig> PRegisterConfig;

class TSerialDevice;
typedef std::shared_ptr<TSerialDevice> PSerialDevice;

struct TRegisterConfig : public std::enable_shared_from_this<TRegisterConfig>
{
    TRegisterConfig(int type, int address,
              RegisterFormat format, double scale,
              bool poll, bool readonly,
              const std::string& type_name,
              bool has_error_value, uint64_t error_value)
        : Type(type), Address(address), Format(format),
          Scale(scale), Poll(poll), ReadOnly(readonly), TypeName(type_name),
          HasErrorValue(has_error_value), ErrorValue(error_value)
    {
        if (TypeName.empty())
            TypeName = "(type " + std::to_string(Type) + ")";
    }

    static PRegisterConfig Intern(int type = 0, int address = 0,
                            RegisterFormat format = U16, double scale = 1,
                            bool poll = true, bool readonly = false,
                            const std::string& type_name = "",
                            bool has_error_value = false,
                            uint64_t error_value = 0)
    {
        return TRegistry::Intern<TRegisterConfig>(type, address, format, scale, poll, readonly, 
                                            type_name, has_error_value, error_value);
    }


    uint8_t ByteWidth() const {
        switch (Format) {
            case S64:
            case U64:
            case Double:
                return 8;
            case U32:
            case S32:
            case BCD32:
            case Float:
                return 4;
            case U24:
            case S24:
            case BCD24:
                return 3;
            case Char8:
                return 1;
            default:
                return 2;
        }
    }

    uint8_t Width() const {
        return (ByteWidth() + 1) / 2;
    }

    std::string ToString() const;

    int Type;
    int Address;
    RegisterFormat Format;
    double Scale;
    bool Poll;
    bool ReadOnly;
    std::string TypeName;
    std::chrono::milliseconds PollInterval = std::chrono::milliseconds::zero();

    bool HasErrorValue;
    uint64_t ErrorValue;
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
    {
        /* if (FromIntern) { */
            /* TRegistry::RemoveIntern<TRegister(shared_from_this()); */
        /* } */
    }

    /* static PRegister Intern(PSerialDevice device, PRegisterConfig config) */
    /* { */
        /* PRegister r = TRegistry::Intern<TRegister>(device, config); */
        /* r->FromIntern = true; */
        /* return r; */
    /* } */

    std::string ToString() const;

    PSerialDevice Device() const
    {
        return _Device.lock();
    }

    /* PSerialDevice Device; */
private:
    std::weak_ptr<TSerialDevice> _Device;
    bool FromIntern = false;


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
            ret->FromIntern = true;

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

class TRegisterRange {
public:
    typedef std::function<void(PRegister reg, uint64_t new_value)> TValueCallback;
    typedef std::function<void(PRegister reg)> TErrorCallback;

    virtual ~TRegisterRange();
    const std::list<PRegister>& RegisterList() const { return RegList; }
    PSerialDevice Device() const { return RegDevice.lock(); }
    int Type() const { return RegType; }
    std::string TypeName() const  { return RegTypeName; }
    std::chrono::milliseconds PollInterval() const { return RegPollInterval; }
    virtual void MapRange(TValueCallback value_callback, TErrorCallback error_callback) = 0;

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
    void Reset();
    void SetValue(PRegister reg, uint64_t value);
    void SetError(PRegister reg);
    void MapRange(TValueCallback value_callback, TErrorCallback error_callback);

private:
    std::unordered_map<PRegister, uint64_t> Values;
    std::unordered_set<PRegister> Errors;
};

typedef std::shared_ptr<TSimpleRegisterRange> PSimpleRegisterRange;
