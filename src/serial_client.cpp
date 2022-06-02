#include "serial_client.h"

#include <chrono>
#include <iostream>
#include <unistd.h>

#ifdef ENABLE_METRICS
#define METRICS(x) x
#else
#define METRICS(x)
#endif

using namespace std::chrono_literals;

#define LOG(logger) logger.Log() << "[serial client] "

namespace
{
    const auto PORT_OPEN_ERROR_NOTIFICATION_INTERVAL = 5min;
    const auto MAX_CLOSED_PORT_CYCLE_TIME = 500ms;
    const auto MAX_POLL_TIME = 100ms;
    const auto MAX_LOW_PRIORITY_LAG = 1s;
    const auto MAX_FLUSHES_WHEN_POLL_IS_DUE = 20;

    bool IsHighPriority(const TRegister& reg)
    {
        return bool(reg.ReadPeriod);
    }

#ifdef ENABLE_METRICS
    Metrics::TPollItem ToMetricsPollItem(const std::string& device, const std::list<PRegister>& regs)
    {
        Metrics::TPollItem res;
        res.Device = device;
        for (const auto& reg: regs) {
            res.Controls.push_back(reg->GetChannelName());
        }
        return res;
    }
#endif

    class TRegisterReader
    {
        PRegisterRange RegisterRange;
        std::chrono::milliseconds MaxPollTime;
        PSerialDevice Device;

    public:
        TRegisterReader(std::chrono::milliseconds maxPollTime): MaxPollTime(maxPollTime)
        {}

        bool operator()(const PRegister& reg, TItemAccumulationPolicy policy, std::chrono::milliseconds pollLimit)
        {
            if (!Device) {
                RegisterRange = reg->Device()->CreateRegisterRange();
                Device = reg->Device();
            }
            if (Device != reg->Device()) {
                return false;
            }
            if (policy == TItemAccumulationPolicy::Force) {
                return RegisterRange->Add(reg, std::chrono::milliseconds::max());
            }
            return RegisterRange->Add(reg, std::min(MaxPollTime, pollLimit));
        }

        PRegisterRange GetRegisterRange() const
        {
            return RegisterRange;
        }
    };

    class TClosedPortRegisterReader
    {
        std::list<PRegister> Regs;

    public:
        bool operator()(const PRegister& reg, TItemAccumulationPolicy policy, std::chrono::milliseconds pollLimit)
        {
            Regs.emplace_back(reg);
            return true;
        }

        std::list<PRegister>& GetRegisters()
        {
            return Regs;
        }
    };

    std::vector<PRegisterHandler> CopyCommands(std::vector<PRegisterHandler>& commands, size_t maxCount)
    {
        std::vector<PRegisterHandler> commandsToExecute;
        auto begin = commands.begin();
        auto end = maxCount < commands.size() ? begin + maxCount : commands.end();
        std::copy(begin, end, std::back_inserter(commandsToExecute));
        commands.erase(begin, end);
        return commandsToExecute;
    }
};

TSerialClient::TSerialClient(const std::vector<PSerialDevice>& devices,
                             PPort port,
                             const TPortOpenCloseLogic::TSettings& openCloseSettings
#ifdef ENABLE_METRICS
                             ,
                             Metrics::TMetrics& metrics
#endif
                             )
    : Port(port),
      Devices(devices),
      Active(false),
      Scheduler(MAX_LOW_PRIORITY_LAG),
      OpenCloseLogic(openCloseSettings),
      ConnectLogger(PORT_OPEN_ERROR_NOTIFICATION_INTERVAL, "[serial client] ")
#ifdef ENABLE_METRICS
      ,
      Metrics(metrics)
#endif
{}

TSerialClient::~TSerialClient()
{
    Active = false;
    if (Port->IsOpen()) {
        Port->Close();
    }

    // remove all registered devices
    ClearDevices();
}

void TSerialClient::AddRegister(PRegister reg)
{
    if (Active)
        throw TSerialDeviceException("can't add registers to the active client");
    if (std::find(RegList.begin(), RegList.end(), reg) != RegList.end())
        throw TSerialDeviceException("duplicate register");
    RegList.push_back(reg);
    LOG(Debug) << "AddRegister: " << reg;
}

void TSerialClient::Activate()
{
    if (!Active) {
        if (RegList.empty())
            throw TSerialDeviceException(Port->GetDescription() + " no registers defined");
        Active = true;
        PrepareRegisterRanges();
    }
}

void TSerialClient::Connect()
{
    OpenCloseLogic.OpenIfAllowed(Port);
}

void TSerialClient::PrepareRegisterRanges()
{
    auto now = std::chrono::steady_clock::now();
    for (auto& reg: RegList) {
        if (reg->AccessType != TRegisterConfig::EAccessType::WRITE_ONLY) {
            // All registers are marked as high priority with poll time set to now.
            // So they will be polled as soon as possible after service start.
            // During next polls registers will be divided to low or high priority according to poll interval
            Scheduler.AddEntry(reg, now, TPriority::High);
        }
    }
}

void TSerialClient::RetryCommand(PRegisterHandler command)
{
    std::unique_lock<std::mutex> lock(CommandsQueueMutex);
    auto it = std::find_if(Commands.begin(), Commands.end(), [&](const auto& item) {
        return item->Register() == command->Register();
    });
    if (it == Commands.end()) {
        Commands.emplace_back(command);
        lock.unlock();
        CommandsCV.notify_all();
    }
}

void TSerialClient::DoFlush(const std::vector<PRegisterHandler>& commandsToExecute)
{
    for (const auto& command: commandsToExecute) {
        if (PrepareToAccessDevice(command->Device())) {
            LastAccessedDevice = command->Device();
            METRICS(Metrics.StartPoll({LastAccessedDevice->DeviceConfig()->Id, "Command"}));
            if (!command->Flush()) {
                RetryCommand(command);
            }
        } else {
            command->Register()->SetError(TRegister::TError::WriteError);
        }
        METRICS(Metrics.StartPoll(Metrics::NON_BUS_POLLING_TASKS));
        if (command->Register()->GetErrorState().test(TRegister::TError::WriteError)) {
            if (ErrorCallback) {
                ErrorCallback(command->Register());
            }
        } else {
            if (ReadCallback) {
                ReadCallback(command->Register());
            }
        }
    }
}

void TSerialClient::WaitForPollAndFlush(std::chrono::steady_clock::time_point waitUntil)
{
    auto now = std::chrono::steady_clock::now();
    if (now >= waitUntil) {
        std::unique_lock<std::mutex> lock(CommandsQueueMutex);
        auto commandsToExecute = CopyCommands(Commands, MAX_FLUSHES_WHEN_POLL_IS_DUE);
        lock.unlock();
        DoFlush(commandsToExecute);
        return;
    }

    if (Debug.IsEnabled()) {
        LOG(Debug) << Port->GetDescription()
                   << std::chrono::duration_cast<std::chrono::milliseconds>(now.time_since_epoch()).count()
                   << ": Wait until "
                   << std::chrono::duration_cast<std::chrono::milliseconds>(waitUntil.time_since_epoch()).count();
    }

    // Limit waiting time to be responsive
    waitUntil = std::min(waitUntil, now + MAX_POLL_TIME);
    std::unique_lock<std::mutex> lk(CommandsQueueMutex);
    while (CommandsCV.wait_until(lk, waitUntil) == std::cv_status::no_timeout) {
        auto commandsToExecute = CopyCommands(Commands, MAX_FLUSHES_WHEN_POLL_IS_DUE);
        lk.unlock();
        DoFlush(commandsToExecute);
        METRICS(Metrics.StartPoll(Metrics::BUS_IDLE));
        lk.lock();
    }
}

void TSerialClient::SetReadError(PRegister reg)
{
    reg->SetError(TRegister::TError::ReadError);
    if (ErrorCallback) {
        ErrorCallback(reg);
    }
}

void TSerialClient::ProcessPolledRegister(PRegister reg)
{
    if (reg->GetErrorState().test(TRegister::ReadError) || reg->GetErrorState().test(TRegister::WriteError)) {
        if (ErrorCallback) {
            ErrorCallback(reg);
        }
    } else {
        if (ReadCallback) {
            ReadCallback(reg);
        }
    }
}

void TSerialClient::Cycle()
{
    METRICS(Metrics.StartPoll(Metrics::NON_BUS_POLLING_TASKS));
    Activate();

    try {
        OpenCloseLogic.OpenIfAllowed(Port);
    } catch (const std::exception& e) {
        ConnectLogger.Log(e.what(), Debug, Error);
    }

    if (Port->IsOpen()) {
        ConnectLogger.DropTimeout();
        OpenPortCycle();
    } else {
        ClosedPortCycle();
    }
}

void TSerialClient::ScheduleNextPoll(PRegister reg, std::chrono::steady_clock::time_point pollStartTime)
{
    if (reg->GetAvailable() == TRegisterAvailability::UNAVAILABLE) {
        return;
    }
    if (IsHighPriority(*reg)) {
        Scheduler.AddEntry(reg, pollStartTime + *(reg->ReadPeriod), TPriority::High);
        return;
    }
    if (reg->ReadRateLimit) {
        Scheduler.AddEntry(reg, pollStartTime + *(reg->ReadRateLimit), TPriority::Low);
        return;
    }
    // Low priority tasks should be scheduled to read as soon as possible,
    // but with a small delay after current read.
    Scheduler.AddEntry(reg, pollStartTime + 1us, TPriority::Low);
}

void TSerialClient::ClosedPortCycle()
{
    auto now = std::chrono::steady_clock::now();
    auto waitUntil = Scheduler.GetDeadline();
    if (waitUntil - now > MAX_CLOSED_PORT_CYCLE_TIME) {
        waitUntil = now + MAX_CLOSED_PORT_CYCLE_TIME;
    }

    std::unique_lock<std::mutex> lk(CommandsQueueMutex);
    while (CommandsCV.wait_until(lk, waitUntil) == std::cv_status::no_timeout) {
        std::vector<PRegisterHandler> tmpQueue;
        tmpQueue.swap(Commands);
        lk.unlock();
        for (const auto& command: tmpQueue) {
            command->Register()->SetError(TRegister::TError::WriteError);
            if (ErrorCallback) {
                ErrorCallback(command->Register());
            }
        }
        lk.lock();
    }
    TClosedPortRegisterReader reader;
    Scheduler.AccumulateNext(waitUntil, reader);
    for (auto& reg: reader.GetRegisters()) {
        SetReadError(reg);
        ScheduleNextPoll(reg, waitUntil);
        reg->Device()->SetTransferResult(false);
    }
}

void TSerialClient::ClearDevices()
{
    Devices.clear();
}

void TSerialClient::SetTextValue(PRegister reg, const std::string& value)
{
    {
        std::unique_lock<std::mutex> lock(CommandsQueueMutex);
        auto it =
            std::find_if(Commands.begin(), Commands.end(), [&](const auto& item) { return item->Register() == reg; });
        if (it != Commands.end()) {
            (*it)->SetValue(value);
        } else {
            Commands.emplace_back(std::make_shared<TRegisterHandler>(reg, value));
        }
    }
    CommandsCV.notify_all();
}

void TSerialClient::SetReadCallback(const TSerialClient::TCallback& callback)
{
    ReadCallback = callback;
}

void TSerialClient::SetErrorCallback(const TSerialClient::TCallback& callback)
{
    ErrorCallback = callback;
}

void TSerialClient::SetRegistersAvailability(PSerialDevice dev, TRegisterAvailability availability)
{
    for (auto& reg: RegList) {
        if (reg->Device() == dev) {
            reg->SetAvailable(availability);
        }
    }
}

void TSerialClient::OpenPortCycle()
{
    WaitForPollAndFlush(Scheduler.GetDeadline());
    auto pollStartTime = std::chrono::steady_clock::now();
    METRICS(Metrics.StartPoll(Metrics::NON_BUS_POLLING_TASKS));

    TRegisterReader reader(MAX_POLL_TIME);

    Scheduler.AccumulateNext(pollStartTime, reader);
    auto range = reader.GetRegisterRange();
    if (!range) {
        // Nothing to read
        return;
    }
    if (range->RegisterList().empty()) {
        // There are registers waiting read but they don't fit in allowed poll limit
        // Wait for high priority registers
        WaitForPollAndFlush(Scheduler.GetHighPriorityDeadline());
        return;
    }

    auto device = range->RegisterList().front()->Device();
    bool deviceWasConnected = !device->GetIsDisconnected();
    if (PrepareToAccessDevice(device)) {
        LastAccessedDevice = device;
        METRICS(Metrics.StartPoll(ToMetricsPollItem(device->DeviceConfig()->Id, range->RegisterList())));
        device->ReadRegisterRange(range);
        METRICS(Metrics.StartPoll(Metrics::NON_BUS_POLLING_TASKS));
        for (auto& reg: range->RegisterList()) {
            reg->SetLastPollTime(pollStartTime);
            ProcessPolledRegister(reg);
            ScheduleNextPoll(reg, pollStartTime);
        }
    } else {
        for (auto& reg: range->RegisterList()) {
            reg->SetLastPollTime(pollStartTime);
            ScheduleNextPoll(reg, pollStartTime);
            SetReadError(reg);
        }
    }
    if (deviceWasConnected && device->GetIsDisconnected()) {
        SetRegistersAvailability(device, TRegisterAvailability::UNKNOWN);
    }
    OpenCloseLogic.CloseIfNeeded(Port, device->GetIsDisconnected());
    METRICS(Metrics.StartPoll(Metrics::BUS_IDLE));
}

bool TSerialClient::PrepareToAccessDevice(PSerialDevice dev)
{
    if (LastAccessedDevice && dev != LastAccessedDevice) {
        try {
            METRICS(metrics.StartPoll({LastAccessedDevice->DeviceConfig()->Id, "End session"}));
            LastAccessedDevice->EndSession();
        } catch (const TSerialDeviceException& e) {
            auto& logger = LastAccessedDevice->GetIsDisconnected() ? Debug : Warn;
            LOG(logger) << "TSerialDevice::EndSession(): " << e.what() << " [slave_id is "
                        << LastAccessedDevice->ToString() + "]";
        }
    }
    try {
        METRICS(metrics.StartPoll({dev->DeviceConfig()->Id, "Start session"}));
        bool deviceWasDisconnected = dev->GetIsDisconnected();
        if (deviceWasDisconnected || dev != LastAccessedDevice) {
            dev->Prepare();
        }
        return true;
    } catch (const TSerialDeviceException& e) {
        auto& logger = dev->GetIsDisconnected() ? Debug : Warn;
        LOG(logger) << "Failed to open session: " << e.what() << " [slave_id is " << dev->ToString() + "]";
        return false;
    }
}

bool TRegisterComparePredicate::operator()(const PRegister& r1, const PRegister& r2) const
{
    if (r1->Device() != r2->Device()) {
        return r1->Device()->DeviceConfig()->SlaveId > r2->Device()->DeviceConfig()->SlaveId;
    }
    if (r1->Type != r2->Type) {
        return r1->Type > r2->Type;
    }
    auto cmp = r1->GetAddress().Compare(r2->GetAddress());
    if (cmp < 0) {
        return false;
    }
    if (cmp > 0) {
        return true;
    }
    // addresses are equal, compare offsets
    return r1->GetDataOffset() > r2->GetDataOffset();
}
