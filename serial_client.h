#pragma once

#include <list>
#include <memory>
#include <functional>
#include <condition_variable>
#include <mutex>

#include "serial_device.h"
#include "register_handler.h"

class TSerialClient: public IClientInteraction, public std::enable_shared_from_this<TSerialClient>
{
public:
    typedef std::function<void(PRegister reg)> TCallback;
    typedef std::function<void(PRegister reg, TRegisterHandler::TErrorState errorState)> TErrorCallback;

    TSerialClient(PAbstractSerialPort port);
    TSerialClient(const TSerialClient& client) = delete;
    TSerialClient& operator=(const TSerialClient&) = delete;
    ~TSerialClient();

    void AddDevice(PDeviceConfig device_config);
    void AddRegister(PRegister reg);
    void Connect();
    void Disconnect();
    void Cycle();
    void SetTextValue(PRegister reg, const std::string& value);
    std::string GetTextValue(PRegister reg) const;
    bool DidRead(PRegister reg) const;
    void SetCallback(const TCallback& callback);
    void SetErrorCallback(const TErrorCallback& callback);
    void SetPollInterval(int ms);
    void SetDebug(bool debug);
    bool DebugEnabled() const;
    void NotifyFlushNeeded();
    void WriteSetupRegister(PRegister reg, uint64_t value);

private:
    void Flush();
    PRegisterHandler GetHandler(PRegister) const;
    PRegisterHandler CreateRegisterHandler(PRegister reg);
    void MaybeUpdateErrorState(PRegister reg, TRegisterHandler::TErrorState state);
    PSerialDevice GetDevice(int slave_id);
    void PrepareToAccessDevice(int slave);

    PAbstractSerialPort Port;
    std::list<PRegister> RegList;
    std::map<PRegister, PRegisterHandler> Handlers;
    std::unordered_map<int, PDeviceConfig> ConfigMap;
    std::unordered_map<int, PSerialDevice> DeviceMap;

    bool Active;
    int PollInterval;
    TCallback Callback;
    TErrorCallback ErrorCallback;
    bool Debug = false;
    int LastAccessedSlave = -1;

    std::condition_variable FlushNeededCond;
    std::mutex FlushNeededMutex;
    bool FlushNeeded = false;

    const int MAX_REGS = 65536;
};

typedef std::shared_ptr<TSerialClient> PSerialClient;
