#include "serial_client.h"
#include <chrono>
#include <iostream>
#include <unistd.h>

#include "modbus_ext_common.h"
#include "write_channel_serial_client_task.h"

using namespace std::chrono_literals;
using namespace std::chrono;

#define LOG(logger) logger.Log() << "[serial client] "

namespace
{
    const auto PORT_OPEN_ERROR_NOTIFICATION_INTERVAL = 5min;
    const auto CLOSED_PORT_CYCLE_TIME = 500ms;
    const auto MAX_POLL_TIME = 100ms;
    // const auto MAX_FLUSHES_WHEN_POLL_IS_DUE = 20;
    const auto BALANCING_THRESHOLD = 500ms;
    // const auto MIN_READ_EVENTS_TIME = 25ms;
    const size_t MAX_EVENT_READ_ERRORS = 10;

    std::chrono::milliseconds GetReadEventsPeriod(const TPort& port)
    {
        auto sendByteTime = port.GetSendTimeBytes(1);
        // >= 115200
        if (sendByteTime < 100us) {
            return 50ms;
        }
        // >= 38400
        if (sendByteTime < 300us) {
            return 100ms;
        }
        // < 38400
        return 200ms;
    }
};

TSerialClient::TSerialClient(PFeaturePort port,
                             const TPortOpenCloseLogic::TSettings& openCloseSettings,
                             util::TGetNowFn nowFn,
                             size_t lowPriorityRateLimit)
    : Port(port),
      OpenCloseLogic(openCloseSettings, nowFn),
      ConnectLogger(PORT_OPEN_ERROR_NOTIFICATION_INTERVAL, "[serial client] "),
      NowFn(nowFn),
      LowPriorityRateLimit(lowPriorityRateLimit)
{}

TSerialClient::~TSerialClient()
{
    if (Port->IsOpen()) {
        Port->Close();
    }
}

void TSerialClient::AddDevice(PSerialDevice device)
{
    if (RegReader)
        throw TSerialDeviceException("can't add registers to the active client");
    for (const auto& reg: device->GetRegisters()) {
        if (Handlers.find(reg) != Handlers.end())
            throw TSerialDeviceException("duplicate register");
        auto handler = Handlers[reg] = std::make_shared<TRegisterHandler>(reg);
        RegList.push_back(reg);
        LOG(Debug) << "AddRegister: " << reg;
    }
    Devices.push_back(device);
}

void TSerialClient::Activate()
{
    if (!RegReader) {
        RegReader = std::make_unique<TSerialClientRegisterAndEventsReader>(Devices,
                                                                           GetReadEventsPeriod(*Port),
                                                                           NowFn,
                                                                           LowPriorityRateLimit);
        LastAccessedDevice = std::make_unique<TSerialClientDeviceAccessHandler>(RegReader->GetEventsReader());
    }
}

void TSerialClient::WaitForPollAndFlush(steady_clock::time_point currentTime, steady_clock::time_point waitUntil)
{
    if (currentTime > waitUntil) {
        waitUntil = currentTime;
    }

    if (Debug.IsEnabled()) {
        LOG(Debug) << Port->GetDescription() << duration_cast<milliseconds>(currentTime.time_since_epoch()).count()
                   << ": Wait until " << duration_cast<milliseconds>(waitUntil.time_since_epoch()).count();
    }

    std::vector<PSerialClientTask> retryTasks;
    {
        std::unique_lock<std::mutex> lock(TasksMutex);

        while (TasksCv.wait_until(lock, waitUntil, [this]() { return !Tasks.empty(); })) {
            std::vector<PSerialClientTask> tasks;
            Tasks.swap(tasks);
            lock.unlock();
            for (auto& task: tasks) {
                if (task->Run(Port, *LastAccessedDevice, Devices) == ISerialClientTask::TRunResult::RETRY) {
                    retryTasks.push_back(task);
                }
            }
            lock.lock();
        }
    }
    for (auto& task: retryTasks) {
        AddTask(task);
    }
}

void TSerialClient::ProcessPolledRegister(PRegister reg)
{
    if (reg->GetErrorState().test(TRegister::ReadError) || reg->GetErrorState().test(TRegister::WriteError)) {
        if (RegisterErrorCallback) {
            RegisterErrorCallback(reg);
        }
    } else {
        if (RegisterReadCallback) {
            RegisterReadCallback(reg);
        }
    }
}

void TSerialClient::Cycle()
{
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

void TSerialClient::ClosedPortCycle()
{
    auto currentTime = NowFn();
    auto waitUntil = currentTime + CLOSED_PORT_CYCLE_TIME;
    WaitForPollAndFlush(currentTime, waitUntil);

    RegReader->ClosedPortCycle(waitUntil, [this](PRegister reg) { ProcessPolledRegister(reg); });
}

void TSerialClient::SetTextValue(PRegister reg, const std::string& value)
{
    auto handler = GetHandler(reg);
    handler->SetTextValue(value);
    auto serialClientTask =
        std::make_shared<TWriteChannelSerialClientTask>(handler, RegisterReadCallback, RegisterErrorCallback);
    AddTask(serialClientTask);
}

void TSerialClient::SetReadCallback(const TSerialClient::TRegisterCallback& callback)
{
    RegisterReadCallback = callback;
}

void TSerialClient::SetErrorCallback(const TSerialClient::TRegisterCallback& callback)
{
    RegisterErrorCallback = callback;
}

PRegisterHandler TSerialClient::GetHandler(PRegister reg) const
{
    auto it = Handlers.find(reg);
    if (it == Handlers.end())
        throw TSerialDeviceException("register not found");
    return it->second;
}

void TSerialClient::OpenPortCycle()
{
    auto currentTime = NowFn();
    auto waitUntil = RegReader->GetDeadline(currentTime);
    // Limit waiting time to be responsive
    waitUntil = std::min(waitUntil, currentTime + MAX_POLL_TIME);
    WaitForPollAndFlush(currentTime, waitUntil);

    auto device = RegReader->OpenPortCycle(
        *Port,
        [this](PRegister reg) { ProcessPolledRegister(reg); },
        *LastAccessedDevice);

    if (device) {
        OpenCloseLogic.CloseIfNeeded(Port, device->GetConnectionState() == TDeviceConnectionState::DISCONNECTED);
    }
}

PFeaturePort TSerialClient::GetPort()
{
    return Port;
}

std::list<PSerialDevice> TSerialClient::GetDevices()
{
    return Devices;
}

void TSerialClient::AddTask(PSerialClientTask task)
{
    {
        std::unique_lock<std::mutex> lock(TasksMutex);
        Tasks.push_back(task);
    }
    TasksCv.notify_all();
}

void TSerialClient::SuspendPoll(PSerialDevice device, std::chrono::steady_clock::time_point currentTime)
{
    RegReader->SuspendPoll(device, currentTime);
}

void TSerialClient::ResumePoll(PSerialDevice device)
{
    RegReader->ResumePoll(device);
}

TSerialClientRegisterAndEventsReader::TSerialClientRegisterAndEventsReader(const std::list<PSerialDevice>& devices,
                                                                           std::chrono::milliseconds readEventsPeriod,
                                                                           util::TGetNowFn nowFn,
                                                                           size_t lowPriorityRateLimit)
    : EventsReader(std::make_shared<TSerialClientEventsReader>(MAX_EVENT_READ_ERRORS)),
      RegisterPoller(lowPriorityRateLimit),
      TimeBalancer(BALANCING_THRESHOLD),
      ReadEventsPeriod(readEventsPeriod),
      SpentTime(nowFn),
      LastCycleWasTooSmallToPoll(false),
      NowFn(nowFn)
{
    auto currentTime = NowFn();
    RegisterPoller.SetDevices(devices, currentTime);
    EventsReader->SetDevices(devices);
    TimeBalancer.AddEntry(TClientTaskType::POLLING, currentTime, TPriority::Low);
}

void TSerialClientRegisterAndEventsReader::ClosedPortCycle(std::chrono::steady_clock::time_point currentTime,
                                                           TRegisterCallback regCallback)
{
    EventsReader->SetReadErrors(regCallback);
    RegisterPoller.ClosedPortCycle(currentTime, regCallback);
}

class TSerialClientTaskHandler
{
public:
    TClientTaskType TaskType;
    TItemAccumulationPolicy Policy;
    milliseconds PollLimit;
    bool NotReady = true;

    bool operator()(TClientTaskType task, TItemAccumulationPolicy policy, milliseconds pollLimit)
    {
        PollLimit = pollLimit;
        TaskType = task;
        Policy = policy;
        NotReady = false;
        return true;
    }
};

PSerialDevice TSerialClientRegisterAndEventsReader::OpenPortCycle(TFeaturePort& port,
                                                                  TRegisterCallback regCallback,
                                                                  TSerialClientDeviceAccessHandler& lastAccessedDevice)
{
    // Count idle time as high priority task time to faster reach time balancing threshold
    if (LastCycleWasTooSmallToPoll) {
        TimeBalancer.UpdateSelectionTime(ceil<milliseconds>(SpentTime.GetSpentTime()), TPriority::High);
    }

    SpentTime.Start();
    TSerialClientTaskHandler handler;
    TimeBalancer.AccumulateNext(SpentTime.GetStartTime(), handler, TItemSelectionPolicy::All);
    if (handler.NotReady) {
        return nullptr;
    }

    if (handler.TaskType == TClientTaskType::EVENTS) {
        if (EventsReader && EventsReader->HasDevicesWithEnabledEvents()) {
            lastAccessedDevice.PrepareToAccess(port, nullptr);
            EventsReader->ReadEvents(port, MAX_POLL_TIME, regCallback, NowFn);
            TimeBalancer.UpdateSelectionTime(ceil<milliseconds>(SpentTime.GetSpentTime()), TPriority::High);
            TimeBalancer.AddEntry(TClientTaskType::EVENTS,
                                  SpentTime.GetStartTime() + ReadEventsPeriod,
                                  TPriority::High);
        }
        SpentTime.Start();

        // TODO: Need to notify port open/close logic about errors
        return nullptr;
    }

    // Some registers can have theoretical read time more than poll limit.
    // Define special cases when reading can exceed poll limit to read the registers:
    // 1. TimeBalancer can force reading of such registers.
    // 2. If there are not devices with enabled events, the only limiting timeout is MAX_POLL_TIME.
    //    We can miss it and read at least one register.
    const bool readAtLeastOneRegister =
        (handler.Policy == TItemAccumulationPolicy::Force) || !EventsReader->HasDevicesWithEnabledEvents();

    auto res = RegisterPoller.OpenPortCycle(port,
                                            SpentTime,
                                            std::min(handler.PollLimit, MAX_POLL_TIME),
                                            readAtLeastOneRegister,
                                            lastAccessedDevice,
                                            regCallback);

    TimeBalancer.AddEntry(TClientTaskType::POLLING, res.Deadline, TPriority::Low);
    if (res.NotEnoughTime) {
        LastCycleWasTooSmallToPoll = true;
    } else {
        LastCycleWasTooSmallToPoll = false;
        TimeBalancer.UpdateSelectionTime(ceil<milliseconds>(SpentTime.GetSpentTime()), TPriority::Low);
    }

    if (EventsReader->HasDevicesWithEnabledEvents() && !TimeBalancer.Contains(TClientTaskType::EVENTS)) {
        TimeBalancer.AddEntry(TClientTaskType::EVENTS, SpentTime.GetStartTime() + ReadEventsPeriod, TPriority::High);
    }

    SpentTime.Start();
    return res.Device;
}

std::chrono::steady_clock::time_point TSerialClientRegisterAndEventsReader::GetDeadline(
    std::chrono::steady_clock::time_point currentTime) const
{
    return TimeBalancer.GetDeadline();
}

PSerialClientEventsReader TSerialClientRegisterAndEventsReader::GetEventsReader() const
{
    return EventsReader;
}

void TSerialClientRegisterAndEventsReader::SuspendPoll(PSerialDevice device,
                                                       std::chrono::steady_clock::time_point currentTime)
{
    RegisterPoller.SuspendPoll(device, currentTime);
}

void TSerialClientRegisterAndEventsReader::ResumePoll(PSerialDevice device)
{
    RegisterPoller.ResumePoll(device);
}
