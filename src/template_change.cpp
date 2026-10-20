#include "template_change.h"

#include <algorithm>
#include <condition_variable>
#include <filesystem>
#include <mutex>
#include <set>
#include <wblib/utils.h>

#include "file_utils.h"
#include "log.h"
#include "old_serial_config.h"
#include "rpc/rpc_device_load_config_task.h"
#include "rpc/rpc_helpers.h"
#include "rpc/rpc_port_settings.h"

#define LOG(logger) ::logger.Log() << "[templates] "

namespace
{
    const std::string JSON_SUFFIX = ".json";

    struct TChangedDevice
    {
        Json::ArrayIndex PortIndex = 0;
        Json::ArrayIndex DeviceIndex = 0;
        //! The device or its port is disabled
        bool Disabled = false;
        bool Unread = false;
        bool Invalid = false;
        //! Without the parameters of the old and the new template
        Json::Value ConfigWithoutParameters;
        //! Set if the device is to read
        std::optional<TPortSettings> Port;
        //! Made by the new template, null if the device is not to read
        PSerialDevice Device;
        //! The unsupported ones are UNSUPPORTED_VALUE
        Json::Value Parameters;
    };

    Json::Value ParseTemplateContent(const std::string& content)
    {
        Json::Value root;
        Json::CharReaderBuilder readerBuilder;
        Json::String errs;
        std::unique_ptr<Json::CharReader> reader(readerBuilder.newCharReader());
        if (!reader->parse(content.data(), content.data() + content.size(), &root, &errs)) {
            throw std::runtime_error("Failed to parse template JSON: " + errs);
        }
        return root;
    }

    bool IsAllowedFilenameChar(char c)
    {
        return (c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z') || (c >= '0' && c <= '9') || c == '.' || c == '_' ||
               c == '-';
    }

    //! Takes basename, replaces characters not from [A-Za-z0-9._-] with '_' and adds ".json" suffix if needed
    std::string SanitizeFilename(const std::string& filename)
    {
        std::string name = std::filesystem::path(filename).filename().string();
        for (auto& c: name) {
            if (!IsAllowedFilenameChar(c)) {
                c = '_';
            }
        }
        if (!name.ends_with(JSON_SUFFIX)) {
            name += JSON_SUFFIX;
        }
        if (name == JSON_SUFFIX) {
            throw std::runtime_error("Invalid \"filename\": \"" + filename + "\"");
        }
        return name;
    }

    bool IsEnabled(const Json::Value& item)
    {
        return !item.isMember("enabled") || item["enabled"].asBool();
    }

    bool IsDeviceOfType(const Json::Value& device, const std::string& deviceType)
    {
        return device["device_type"].isString() && device["device_type"].asString() == deviceType;
    }

    void AddParameterIds(TTemplateMap& templates, const std::string& deviceType, std::set<std::string>& ids)
    {
        try {
            auto deviceTemplate = templates.GetTemplate(deviceType);
            const auto& parameters = deviceTemplate->GetTemplate()["parameters"];
            for (auto it = parameters.begin(); it != parameters.end(); ++it) {
                ids.insert(parameters.isObject() ? it.key().asString() : (*it)["id"].asString());
            }
        } catch (const std::exception& e) {
            // No template or a broken one: there are no parameters of it in the configuration
            LOG(Debug) << "no parameters of \"" << deviceType << "\" are taken from the template: " << e.what();
        }
    }

    std::string GetPortName(const Json::Value& port)
    {
        if (port.isMember("path")) {
            return port["path"].asString();
        }
        return port["address"].asString() + ":" + port["port"].asString();
    }

    std::vector<TChangedDevice> FindDevices(const Json::Value& config, const std::string& deviceType)
    {
        std::vector<TChangedDevice> res;
        const auto& ports = config["ports"];
        for (Json::ArrayIndex portIndex = 0; portIndex < ports.size(); ++portIndex) {
            const auto& devices = ports[portIndex]["devices"];
            for (Json::ArrayIndex deviceIndex = 0; deviceIndex < devices.size(); ++deviceIndex) {
                if (!IsDeviceOfType(devices[deviceIndex], deviceType)) {
                    continue;
                }
                TChangedDevice device;
                device.PortIndex = portIndex;
                device.DeviceIndex = deviceIndex;
                res.push_back(std::move(device));
            }
        }
        return res;
    }

    const Json::Value& GetDeviceConfig(const Json::Value& config, const TChangedDevice& device)
    {
        return config["ports"][device.PortIndex]["devices"][device.DeviceIndex];
    }

    std::string GetDeviceName(const Json::Value& config, const TChangedDevice& device)
    {
        return GetPortName(config["ports"][device.PortIndex]) + " " +
               GetDeviceConfig(config, device)["slave_id"].asString();
    }

    //! The configuration with the port of the device and the device alone
    Json::Value MakeDeviceOnlyConfig(const Json::Value& config,
                                     const TChangedDevice& device,
                                     const Json::Value& deviceConfig)
    {
        Json::Value res(config);
        Json::Value port(config["ports"][device.PortIndex]);
        port["devices"] = Json::Value(Json::arrayValue);
        port["devices"].append(deviceConfig);
        res["ports"] = Json::Value(Json::arrayValue);
        res["ports"].append(std::move(port));
        return res;
    }

    //! The port alone, so the errors of its devices do not matter
    PPortConfig LoadPortConfig(const Json::Value& config,
                               Json::ArrayIndex portIndex,
                               const TConfigLoadParams& loadParams,
                               TTemplateMap& templates)
    {
        Json::Value portOnlyConfig(config);
        Json::Value port(config["ports"][portIndex]);
        port["devices"] = Json::Value(Json::arrayValue);
        portOnlyConfig["ports"] = Json::Value(Json::arrayValue);
        portOnlyConfig["ports"].append(std::move(port));
        return LoadConfigJson(portOnlyConfig, loadParams, templates)->PortConfigs.at(0);
    }

    //! The device is made as for the polling, but without the check of the configuration:
    //! the parameters are not read yet, CheckDevices checks them after reading
    PSerialDevice CreateDevice(const Json::Value& deviceConfig,
                               const TPortConfig& portConfig,
                               TSerialDeviceFactory& deviceFactory,
                               TTemplateMap& templates)
    {
        Json::Value deviceOnlyConfig;
        deviceOnlyConfig["ports"][0]["devices"][0] = deviceConfig;
        FixOldConfigFormat(deviceOnlyConfig, templates);
        TSerialDeviceFactory::TCreateDeviceParams params{};
        params.IsModbusTcp = portConfig.Port->IsModbusTcp();
        return deviceFactory.CreateDevice(deviceOnlyConfig["ports"][0]["devices"][0], params, templates)->Device;
    }

    void CheckDevices(const Json::Value& config,
                      std::vector<TChangedDevice>& devices,
                      const TConfigLoadParams& loadParams,
                      TTemplateMap& templates)
    {
        for (auto& device: devices) {
            try {
                LoadConfigJson(MakeDeviceOnlyConfig(config, device, GetDeviceConfig(config, device)),
                               loadParams,
                               templates);
            } catch (const std::exception& e) {
                LOG(Warn) << GetDeviceName(config, device) << ": the device does not pass the check: " << e.what();
                device.Invalid = true;
            }
        }
    }

    TConfigDeviceRef MakeDeviceRef(const Json::Value& config, const TChangedDevice& device)
    {
        return TConfigDeviceRef{GetPortName(config["ports"][device.PortIndex]),
                                GetDeviceConfig(config, device)["slave_id"].asString()};
    }

    TApplyProblems GetProblems(const Json::Value& config, const std::vector<TChangedDevice>& devices)
    {
        TApplyProblems res;
        for (const auto& device: devices) {
            if (device.Disabled) {
                res.DisabledDevices.push_back(MakeDeviceRef(config, device));
            } else if (device.Unread) {
                res.UnreadDevices.push_back(MakeDeviceRef(config, device));
            }
            if (device.Invalid) {
                res.InvalidDevices.push_back(MakeDeviceRef(config, device));
            }
        }
        return res;
    }

    //! The acting template of the type has "hw", only then the settings of its devices are read
    bool CanReadSettings(TTemplateMap& templates, const std::string& deviceType)
    {
        auto deviceTemplate = FindTemplate(templates, deviceType);
        return deviceTemplate && !deviceTemplate->GetHardware().empty();
    }

    //! The devices of the type. The enabled ones are to read if the new template has "hw"
    std::vector<TChangedDevice> MakeChangedDevices(const Json::Value& config,
                                                   const std::string& deviceType,
                                                   const TConfigLoadParams& loadParams,
                                                   TTemplateMap& oldTemplates,
                                                   TTemplateMap& newTemplates)
    {
        auto devices = FindDevices(config, deviceType);
        if (!CanReadSettings(newTemplates, deviceType)) {
            return devices;
        }
        std::set<std::string> parameterIds;
        AddParameterIds(oldTemplates, deviceType, parameterIds);
        AddParameterIds(newTemplates, deviceType, parameterIds);
        std::vector<PPortConfig> portConfigs(config["ports"].size());
        for (auto& device: devices) {
            const auto& deviceConfig = GetDeviceConfig(config, device);
            device.Disabled = !IsEnabled(config["ports"][device.PortIndex]) || !IsEnabled(deviceConfig);
            if (device.Disabled) {
                continue;
            }
            device.ConfigWithoutParameters = deviceConfig;
            for (const auto& id: parameterIds) {
                device.ConfigWithoutParameters.removeMember(id);
            }
            try {
                auto& portConfig = portConfigs[device.PortIndex];
                if (!portConfig) {
                    portConfig = LoadPortConfig(config, device.PortIndex, loadParams, newTemplates);
                }
                device.Device =
                    CreateDevice(device.ConfigWithoutParameters, *portConfig, loadParams.DeviceFactory, newTemplates);
                const auto& port = config["ports"][device.PortIndex];
                device.Port = ParseRPCPort(port, port["port_type"].asString() == "modbus tcp");
            } catch (const std::exception& e) {
                LOG(Warn) << GetDeviceName(config, device) << ": the device can not be read: " << e.what();
                device.Unread = true;
            }
        }
        return devices;
    }

    //! The results of the reading tasks, they may come after the waiting ends
    struct TDeviceReads
    {
        std::mutex Mutex;
        std::condition_variable Cv;
        size_t Pending = 0;
        //! By the index of the device, null if the device is not read
        std::vector<Json::Value> Parameters;
    };

    void FinishRead(TDeviceReads& reads, size_t index, const Json::Value& parameters)
    {
        std::lock_guard<std::mutex> lock(reads.Mutex);
        reads.Parameters[index] = parameters;
        --reads.Pending;
        reads.Cv.notify_all();
    }

    //! With the tasks of device/LoadConfig. The devices not read in time are unread
    void ReadDevices(std::vector<TChangedDevice>& devices,
                     PDeviceTemplate deviceTemplate,
                     TSerialDeviceFactory& deviceFactory,
                     const std::string& configPath,
                     TPortTaskDispatcher& portTasks)
    {
        auto reads = std::make_shared<TDeviceReads>();
        reads->Parameters.resize(devices.size());
        auto devicesToRead = std::count_if(devices.begin(), devices.end(), [](const auto& device) {
            return device.Device && !device.Unread;
        });
        if (devicesToRead == 0) {
            return;
        }
        reads->Pending = devicesToRead;
        // The devices of a port are read one by one
        auto timeout = DefaultRPCTotalTimeout * devicesToRead;
        for (size_t index = 0; index < devices.size(); ++index) {
            const auto& device = devices[index];
            if (!device.Device || device.Unread) {
                continue;
            }
            // Not as a config device: the cache is not used, and the port gets the settings of the configuration
            // even if the port is not polled and its executor is opened with the settings of an RPC request
            auto request = std::make_shared<TRPCDeviceLoadConfigRequest>(
                deviceFactory.GetProtocolParams(device.Device->Protocol()->GetName()),
                device.Device,
                deviceTemplate,
                false,
                configPath,
                std::make_shared<TDeviceParametersCache>());
            request->SerialPortSettings = GetRPCPortConnectionSettings(*device.Port);
            request->TotalTimeout = timeout;
            request->OnResult = [reads, index](const Json::Value& result) {
                FinishRead(*reads, index, result["parameters"]);
            };
            auto name = GetRPCPortDescription(*device.Port) + " " + device.Device->ToString();
            request->OnError = [reads, index, name](int, const std::string& error) {
                LOG(Warn) << name << ": the parameters are not read: " << error;
                FinishRead(*reads, index, Json::Value());
            };
            TTaskTarget target;
            target.Port = device.Port;
            target.SlaveId = device.Device->DeviceConfig()->SlaveId;
            target.DeviceType = device.Device->DeviceConfig()->DeviceType;
            portTasks.AddTask(target, [&request](PSerialDevice, PDeviceParametersCache) {
                return std::make_shared<TRPCDeviceLoadConfigSerialClientTask>(request);
            });
        }
        std::unique_lock<std::mutex> lock(reads->Mutex);
        reads->Cv.wait_for(lock, timeout, [&reads]() { return reads->Pending == 0; });
        for (size_t index = 0; index < devices.size(); ++index) {
            auto& device = devices[index];
            if (!device.Device || device.Unread) {
                continue;
            }
            device.Parameters = reads->Parameters[index];
            device.Unread = device.Parameters.isNull();
        }
    }

    //! A failure is only logged
    void RemoveOtherTemplateFile(const TTemplateChange& change, TTemplateMap& templates)
    {
        if (change.WritePath.empty() || change.RemovePath.empty() || change.RemovePath == change.WritePath) {
            return;
        }
        std::error_code ec;
        if (std::filesystem::remove(change.RemovePath, ec) || !ec) {
            templates.DeleteTemplate(change.RemovePath);
            return;
        }
        LOG(Warn) << "failed to remove old template file " << change.RemovePath << ": " << ec.message();
    }
}

bool TApplyProblems::Empty() const
{
    return DisabledDevices.empty() && UnreadDevices.empty() && InvalidDevices.empty();
}

TTemplateChange MakeUploadChange(TTemplateMap& templates, const std::string& content, const std::string& fileName)
{
    TTemplateChange change;
    change.Content = content;
    change.Root = ParseTemplateContent(content);
    if (!change.Root.isObject()) {
        throw std::runtime_error("Template must be a JSON object");
    }
    if (change.Root["device_type"].isString()) {
        change.DeviceType = change.Root["device_type"].asString();
    }
    if (change.DeviceType.empty()) {
        throw std::runtime_error("Template must contain a non-empty \"device_type\" property");
    }

    // Deprecated templates are exempt from schema validation on disk load,
    // so accepting them would allow storing files the schema can't vouch for
    if (change.Root.get("deprecated", false).asBool()) {
        throw std::runtime_error("Uploading of deprecated templates is not supported");
    }
    try {
        templates.ValidateTemplate(change.Root);
    } catch (const std::exception& e) {
        throw std::runtime_error("Template validation failed: " + std::string(e.what()));
    }

    auto name = SanitizeFilename(fileName);
    auto oldTemplate = templates.FindUserDefinedTemplate(change.DeviceType);
    change.RemovePath = oldTemplate ? oldTemplate->GetFilePath() : std::string();
    std::filesystem::path dir(templates.GetUserTemplatesDir());
    auto finalPath = dir / name;
    auto stem = name.substr(0, name.size() - JSON_SUFFIX.size());
    for (size_t suffix = 1; std::filesystem::exists(finalPath) && finalPath.string() != change.RemovePath; ++suffix) {
        finalPath = dir / (stem + "-" + std::to_string(suffix) + JSON_SUFFIX);
    }
    change.WritePath = finalPath.string();
    return change;
}

TTemplateChange MakeDeleteChange(TTemplateMap& templates, const std::string& deviceType)
{
    auto userTemplate = templates.FindUserDefinedTemplate(deviceType);
    if (!userTemplate) {
        throw std::runtime_error("There is no user-defined template for device type \"" + deviceType + "\"");
    }
    TTemplateChange change;
    change.DeviceType = deviceType;
    change.RemovePath = userTemplate->GetFilePath();
    const auto& dir = templates.GetUserTemplatesDir();
    if (dir.empty() || !WBMQTT::StringStartsWith(change.RemovePath, dir)) {
        throw std::runtime_error("Template file \"" + change.RemovePath + "\" is not from user templates directory");
    }
    return change;
}

std::shared_ptr<TTemplateMap> MakeChangedTemplates(TTemplateMap& templates, const TTemplateChange& change)
{
    auto res = templates.Clone();
    if (change.WritePath.empty()) {
        res->DeleteTemplate(change.RemovePath);
    } else {
        res->AddTemplate(change.Root, change.WritePath);
    }
    return res;
}

PDeviceTemplate FindTemplate(TTemplateMap& templates, const std::string& deviceType)
{
    try {
        return templates.GetTemplate(deviceType);
    } catch (const std::out_of_range&) {
        return nullptr;
    }
}

size_t CountDevicesOfType(const Json::Value& config, const std::string& deviceType)
{
    size_t count = 0;
    for (const auto& port: config["ports"]) {
        for (const auto& device: port["devices"]) {
            if (IsDeviceOfType(device, deviceType)) {
                ++count;
            }
        }
    }
    return count;
}

TChangedConfig MakeChangedConfig(const Json::Value& config,
                                 const std::string& deviceType,
                                 const TConfigLoadParams& loadParams,
                                 const std::string& configPath,
                                 TTemplateMap& oldTemplates,
                                 TTemplateMap& newTemplates,
                                 TPortTaskDispatcher& portTasks)
{
    auto devices = MakeChangedDevices(config, deviceType, loadParams, oldTemplates, newTemplates);
    ReadDevices(devices, FindTemplate(newTemplates, deviceType), loadParams.DeviceFactory, configPath, portTasks);
    TChangedConfig res;
    res.Config = config;
    for (const auto& device: devices) {
        if (!device.Device || device.Unread) {
            continue;
        }
        auto deviceConfig = device.ConfigWithoutParameters;
        for (const auto& id: device.Parameters.getMemberNames()) {
            const auto& value = device.Parameters[id];
            if (!value.isString() || value.asString() != UNSUPPORTED_VALUE) {
                deviceConfig[id] = value;
            }
        }
        res.Config["ports"][device.PortIndex]["devices"][device.DeviceIndex] = deviceConfig;
    }
    CheckDevices(res.Config, devices, loadParams, newTemplates);
    try {
        res.HandlerConfig = LoadConfigJson(res.Config, loadParams, newTemplates);
    } catch (const std::exception& e) {
        LOG(Warn) << "the configuration with the new template of \"" << deviceType
                  << "\" does not pass the check: " << e.what();
    }
    res.Problems = GetProblems(res.Config, devices);
    return res;
}

void WriteTemplateFile(const TTemplateChange& change)
{
    if (change.WritePath.empty()) {
        std::filesystem::remove(change.RemovePath);
        return;
    }
    std::filesystem::create_directories(std::filesystem::path(change.WritePath).parent_path());
    WriteFileAtomically(change.WritePath, change.Content);
}

void WriteTemplateAndConfigFiles(const TTemplateChange& change,
                                 const std::string& configPath,
                                 const Json::Value& config)
{
    std::optional<TAtomicFileWrite> templateWrite;
    if (!change.WritePath.empty()) {
        std::filesystem::create_directories(std::filesystem::path(change.WritePath).parent_path());
        templateWrite.emplace(change.WritePath, change.Content);
    }
    TAtomicFileWrite configWrite(configPath, SerializeConfig(config));
    if (templateWrite) {
        templateWrite->Commit();
    } else {
        std::filesystem::remove(change.RemovePath);
    }
    configWrite.Commit();
}

void CommitTemplates(const TTemplateChange& change,
                     TTemplateMap& templates,
                     TDevicesConfedSchemasMap& deviceConfedSchemas)
{
    if (change.WritePath.empty()) {
        templates.DeleteTemplate(change.RemovePath);
    } else {
        templates.UpdateTemplate(change.WritePath);
    }
    RemoveOtherTemplateFile(change, templates);
    deviceConfedSchemas.InvalidateCache(change.DeviceType);
}
