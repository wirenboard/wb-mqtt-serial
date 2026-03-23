// Cf. firmware_update.py:82 UpdateState class, firmware_update.py:61 DeviceUpdateInfo
// Cf. firmware_update.py:209 UpdateNotifier class
#include "rpc_fw_update_state.h"

// ============================================================
//                     TDeviceUpdateInfo
// Cf. firmware_update.py:61 DeviceUpdateInfo dataclass
// ============================================================

// Cf. firmware_update.py:73 DeviceUpdateInfo.__eq__()
bool TDeviceUpdateInfo::Matches(const TDeviceUpdateInfo& other) const
{
    return SlaveId == other.SlaveId && PortPath == other.PortPath && Type == other.Type && Protocol == other.Protocol;
}

// ============================================================
//                     TFwUpdateState
// Cf. firmware_update.py:82 UpdateState class
// ============================================================

TFwUpdateState::TFwUpdateState(TStatePublishFn publishFn, const std::string& topic)
    : PublishFn(std::move(publishFn)),
      Topic(topic)
{}

void TFwUpdateState::Update(const TDeviceUpdateInfo& info)
{
    std::string json;
    {
        std::lock_guard<std::mutex> lock(Mutex);
        auto it = std::find_if(Devices.begin(), Devices.end(), [&info](const TDeviceUpdateInfo& d) {
            return d.Matches(info);
        });

        if (it != Devices.end()) {
            it->ToVersion = info.ToVersion;
            it->Progress = info.Progress;
            it->FromVersion = info.FromVersion;
            it->Type = info.Type;
            if (info.Error) {
                it->Error = std::make_unique<TStateError>(*info.Error);
            } else {
                it->Error.reset();
            }
            it->ComponentNumber = info.ComponentNumber;
            it->ComponentModel = info.ComponentModel;
        } else {
            TDeviceUpdateInfo newInfo;
            newInfo.PortPath = info.PortPath;
            newInfo.Protocol = info.Protocol;
            newInfo.SlaveId = info.SlaveId;
            newInfo.ToVersion = info.ToVersion;
            newInfo.Progress = info.Progress;
            newInfo.FromVersion = info.FromVersion;
            newInfo.Type = info.Type;
            if (info.Error) {
                newInfo.Error = std::make_unique<TStateError>(*info.Error);
            }
            newInfo.ComponentNumber = info.ComponentNumber;
            newInfo.ComponentModel = info.ComponentModel;
            Devices.push_back(std::move(newInfo));
        }
        json = ToJson();
    }
    Publish(json);
}

void TFwUpdateState::Remove(int slaveId, const std::string& portPath, const std::string& type)
{
    std::string json;
    {
        std::lock_guard<std::mutex> lock(Mutex);
        Devices.erase(std::remove_if(Devices.begin(),
                                     Devices.end(),
                                     [&](const TDeviceUpdateInfo& d) {
                                         return d.SlaveId == slaveId && d.PortPath == portPath && d.Type == type;
                                     }),
                      Devices.end());
        json = ToJson();
    }
    Publish(json);
}

void TFwUpdateState::SetError(int slaveId,
                              const std::string& portPath,
                              const std::string& type,
                              const std::string& errorId,
                              const std::string& errorMessage,
                              const Json::Value& metadata)
{
    std::string json;
    {
        std::lock_guard<std::mutex> lock(Mutex);
        for (auto& d: Devices) {
            if (d.SlaveId == slaveId && d.PortPath == portPath && d.Type == type) {
                d.Error = std::make_unique<TStateError>(TStateError{errorId, errorMessage, metadata});
                json = ToJson();
                break;
            }
        }
    }
    if (!json.empty()) {
        Publish(json);
    }
}

bool TFwUpdateState::HasActiveUpdate(int slaveId, const std::string& portPath) const
{
    std::lock_guard<std::mutex> lock(Mutex);
    return std::any_of(Devices.begin(), Devices.end(), [&](const TDeviceUpdateInfo& d) {
        return d.SlaveId == slaveId && d.PortPath == portPath && d.Progress < 100 && !d.Error;
    });
}

void TFwUpdateState::ClearError(int slaveId, const std::string& portPath, const std::string& type)
{
    std::string json;
    {
        std::lock_guard<std::mutex> lock(Mutex);
        auto it = std::find_if(Devices.begin(), Devices.end(), [&](const TDeviceUpdateInfo& d) {
            return d.SlaveId == slaveId && d.PortPath == portPath && d.Type == type && d.Error;
        });
        if (it != Devices.end()) {
            Devices.erase(it);
            json = ToJson();
        }
    }
    if (!json.empty()) {
        Publish(json);
    }
}

void TFwUpdateState::Reset()
{
    std::string json;
    {
        std::lock_guard<std::mutex> lock(Mutex);
        Devices.clear();
        json = ToJson();
    }
    Publish(json);
}

void TFwUpdateState::Publish(const std::string& json)
{
    if (PublishFn) {
        PublishFn(Topic, json, true);
    }
}

std::string TFwUpdateState::ToJson() const
{
    Json::Value root;
    Json::Value devices(Json::arrayValue);

    for (const auto& d: Devices) {
        Json::Value device;
        Json::Value port;
        port["path"] = d.PortPath;
        device["port"] = port;
        device["protocol"] = d.Protocol;
        device["slave_id"] = d.SlaveId;
        device["to_version"] = d.ToVersion;
        device["progress"] = d.Progress;
        device["from_version"] = d.FromVersion;
        device["type"] = d.Type;

        if (d.Error) {
            Json::Value error;
            error["id"] = d.Error->Id;
            error["message"] = d.Error->Message;
            if (!d.Error->Metadata.isNull()) {
                error["metadata"] = d.Error->Metadata;
            } else {
                error["metadata"] = Json::nullValue;
            }
            device["error"] = error;
        } else {
            device["error"] = Json::nullValue;
        }

        if (d.ComponentNumber >= 0) {
            device["component_number"] = d.ComponentNumber;
        } else {
            device["component_number"] = Json::nullValue;
        }

        if (!d.ComponentModel.empty()) {
            device["component_model"] = d.ComponentModel;
        } else {
            device["component_model"] = Json::nullValue;
        }

        devices.append(device);
    }

    root["devices"] = devices;

    Json::StreamWriterBuilder builder;
    builder["indentation"] = "";
    return Json::writeString(builder, root);
}

// ============================================================
//                     TUpdateNotifier
// Cf. firmware_update.py:209 UpdateNotifier class
// ============================================================

TUpdateNotifier::TUpdateNotifier(int notificationsCount): Step(-1)
{
    NotificationStep = std::max(100.0 / notificationsCount, 1.0);
}

bool TUpdateNotifier::ShouldNotify(int progressPercent)
{
    if (progressPercent >= 100) {
        return true;
    }
    int currentStep = static_cast<int>(progressPercent / NotificationStep);
    if (currentStep > Step) {
        Step = currentStep;
        return true;
    }
    return false;
}
