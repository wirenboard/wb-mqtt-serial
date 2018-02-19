#pragma once

#include "register_config.h"

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




struct TRegisterType {
    TRegisterType(int index, const std::string& name, const std::string& defaultControlType,
                  ERegisterFormat defaultFormat = U16,
                  bool read_only = false, EWordOrder defaultWordOrder = EWordOrder::BigEndian):
        Index(index), Name(name), DefaultControlType(defaultControlType),
        DefaultFormat(defaultFormat), DefaultWordOrder(defaultWordOrder), ReadOnly(read_only) {}
    int Index;
    std::string Name, DefaultControlType;
    ERegisterFormat DefaultFormat;
    EWordOrder DefaultWordOrder;
    bool ReadOnly;
};

typedef std::vector<TRegisterType> TRegisterTypes;
typedef std::map<std::string, TRegisterType> TRegisterTypeMap;
typedef std::shared_ptr<TRegisterTypeMap> PRegisterTypeMap;




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

inline const char* RegisterFormatName(ERegisterFormat fmt) {
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

inline ERegisterFormat RegisterFormatFromName(const std::string& name) {
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
    typedef std::function<void(PProtocolRegister reg, uint64_t new_value)> TValueCallback;
    typedef std::function<void(PProtocolRegister reg)> TErrorCallback;


    virtual ~TRegisterRange();
    const std::list<PProtocolRegister>& RegisterList() const { return RegList; }
    PSerialDevice Device() const { return RegDevice.lock(); }
    int Type() const { return RegType; }
    std::string TypeName() const  { return RegTypeName; }
    virtual void MapRange(TValueCallback value_callback, TErrorCallback error_callback) = 0;
    virtual EStatus GetStatus() const = 0;

    // returns true when occured error is likely caused by hole registers
    virtual bool NeedsSplit() const = 0;

protected:
    TRegisterRange(const std::list<PProtocolRegister>& regs);
    TRegisterRange(PProtocolRegister reg);

private:
    std::weak_ptr<TSerialDevice> RegDevice;
    int RegType;
    std::string RegTypeName;
    std::list<PProtocolRegister> RegList;
};

typedef std::shared_ptr<TRegisterRange> PRegisterRange;

class TSimpleRegisterRange: public TRegisterRange {
public:
    TSimpleRegisterRange(const std::list<PProtocolRegister>& regs);
    TSimpleRegisterRange(PProtocolRegister reg);
    void Reset();
    void SetValue(PProtocolRegister reg, uint64_t value);
    void SetError(PProtocolRegister reg);
    void MapRange(TValueCallback value_callback, TErrorCallback error_callback);
    EStatus GetStatus() const override;
    bool NeedsSplit() const override;

private:
    std::unordered_map<PProtocolRegister, uint64_t> Values;
    std::unordered_set<PProtocolRegister> Errors;
};

typedef std::shared_ptr<TSimpleRegisterRange> PSimpleRegisterRange;
