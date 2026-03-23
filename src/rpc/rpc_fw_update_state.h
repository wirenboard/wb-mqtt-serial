#pragma once

#include <functional>
#include <memory>
#include <mutex>
#include <string>
#include <vector>

#include <wblib/json_utils.h>

struct TStateError
{
    std::string Id;
    std::string Message;
    Json::Value Metadata;
};

struct TDeviceUpdateInfo
{
    std::string PortPath;
    std::string Protocol;
    int SlaveId = 0;
    std::string ToVersion;
    int Progress = 0;
    std::string FromVersion;
    std::string Type; // "firmware", "bootloader", "component"
    std::unique_ptr<TStateError> Error;
    int ComponentNumber = -1; // -1 means null
    std::string ComponentModel;

    bool Matches(const TDeviceUpdateInfo& other) const;
};

using TStatePublishFn = std::function<void(const std::string& topic, const std::string& payload, bool retain)>;

class TFwUpdateState
{
public:
    TFwUpdateState(TStatePublishFn publishFn, const std::string& topic);

    void Update(const TDeviceUpdateInfo& info);
    void Remove(int slaveId, const std::string& portPath, const std::string& type);
    void SetError(int slaveId,
                  const std::string& portPath,
                  const std::string& type,
                  const std::string& errorId,
                  const std::string& errorMessage,
                  const Json::Value& metadata = Json::Value());
    bool HasActiveUpdate(int slaveId, const std::string& portPath) const;
    void ClearError(int slaveId, const std::string& portPath, const std::string& type);
    void Reset();

private:
    void Publish(const std::string& json);
    std::string ToJson() const;

    mutable std::mutex Mutex;
    std::vector<TDeviceUpdateInfo> Devices;
    TStatePublishFn PublishFn;
    std::string Topic;
};

using PFwUpdateState = std::shared_ptr<TFwUpdateState>;

// Shared guard for "update in progress" state. Owned by the handler and shared with tasks
// so that the synchronization primitives outlive any individual task.
struct TFwUpdateLock
{
    std::mutex Mutex;
    bool InProgress = false;
};
using PFwUpdateLock = std::shared_ptr<TFwUpdateLock>;

class TUpdateNotifier
{
public:
    TUpdateNotifier(int notificationsCount = 30);
    bool ShouldNotify(int progressPercent);

private:
    int Step;
    double NotificationStep;
};
