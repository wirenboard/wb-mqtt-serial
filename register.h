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
