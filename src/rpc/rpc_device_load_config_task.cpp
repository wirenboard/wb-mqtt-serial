#include "rpc_device_load_config_task.h"
#include "config_merge_template.h"
#include "rpc_helpers.h"
#include "serial_port.h"
#include "wb_registers.h"

#define LOG(logger) ::logger.Log() << "[RPC] "

namespace
{
    const auto MAX_RETRIES = 2;

    PSerialDevice FindDevice(const std::list<PSerialDevice>& polledDevices,
                             const std::string& slaveId,
                             const std::string& deviceType)
    {
        for (const auto& device: polledDevices) {
            auto deviceConfig = device->DeviceConfig();
            if (deviceConfig->SlaveId == slaveId && deviceConfig->DeviceType == deviceType) {
                return device;
            }
        }

        return nullptr;
    }

    PSerialDevice CreateDevice(PPort port,
                               PRPCDeviceLoadConfigRequest rpcRequest,
                               const TDeviceProtocolParams& protocolParams)
    {
        auto config = std::make_shared<TDeviceConfig>("RPC Device",
                                                      rpcRequest->SlaveId,
                                                      rpcRequest->DeviceTemplate->GetProtocol());
        if (rpcRequest->DeviceTemplate->GetProtocol() == "modbus") {
            config->MaxRegHole = Modbus::MAX_HOLE_CONTINUOUS_16_BIT_REGISTERS;
            config->MaxBitHole = Modbus::MAX_HOLE_CONTINUOUS_1_BIT_REGISTERS;
            config->MaxReadRegisters = Modbus::MAX_READ_REGISTERS;
        }
        return protocolParams.factory->CreateDevice(rpcRequest->DeviceTemplate->GetTemplate(),
                                                    config,
                                                    port,
                                                    protocolParams.protocol);
    }

    bool ReadModbusRegister(Modbus::IModbusTraits& traits,
                            TPort& port,
                            PRPCDeviceLoadConfigRequest rpcRequest,
                            PRegisterConfig registerConfig,
                            TRegisterValue& value)
    {
        uint8_t slaveId = static_cast<uint8_t>(std::stoi(rpcRequest->SlaveId));
        for (int i = 0; i <= MAX_RETRIES; ++i) {
            try {
                value = Modbus::ReadRegister(traits,
                                             port,
                                             slaveId,
                                             *registerConfig,
                                             std::chrono::microseconds(0),
                                             rpcRequest->ResponseTimeout,
                                             rpcRequest->FrameTimeout);
                return true;
            } catch (const Modbus::TModbusExceptionError& err) {
                if (err.GetExceptionCode() == Modbus::ILLEGAL_FUNCTION ||
                    err.GetExceptionCode() == Modbus::ILLEGAL_DATA_ADDRESS ||
                    err.GetExceptionCode() == Modbus::ILLEGAL_DATA_VALUE)
                {
                    break;
                }
            } catch (const Modbus::TErrorBase& err) {
                if (i == MAX_RETRIES) {
                    throw;
                }
            } catch (const TResponseTimeoutException& e) {
                if (i == MAX_RETRIES) {
                    throw;
                }
            }
        }

        return false;
    }

    std::string ReadFirmwareVersion(TPort& port, PRPCDeviceLoadConfigRequest rpcRequest)
    {
        if (!rpcRequest->IsWBDevice) {
            return std::string();
        }

        std::string error;
        try {
            Modbus::TModbusRTUTraits traits;
            auto config = WbRegisters::GetRegisterConfig(WbRegisters::FW_VERSION_REGISTER_NAME);
            TRegisterValue value;
            std::string version;
            if (ReadModbusRegister(traits, port, rpcRequest, config, value)) {
                version = value.Get<std::string>();
            }
            return version;
        } catch (const Modbus::TErrorBase& err) {
            error = err.what();
        } catch (const TResponseTimeoutException& e) {
            error = e.what();
        }
        LOG(Warn) << port.GetDescription() << " modbus:" << rpcRequest->SlaveId << " unable to read \""
                  << WbRegisters::FW_VERSION_REGISTER_NAME << "\" register: " << error;
        throw TRPCException(error, TRPCResultCode::RPC_WRONG_PARAM_VALUE);
    }

    void ReadParameters(PSerialDevice device, TRPCRegisterList& registerList, Json::Value& parameters)
    {
        TRegisterComparePredicate compare;
        std::sort(registerList.begin(),
                  registerList.end(),
                  [compare](std::pair<std::string, PRegister>& a, std::pair<std::string, PRegister>& b) {
                      return compare(b.second, a.second);
                  });

        for (int i = 0; i <= MAX_RETRIES; i++) {
            try {
                device->Prepare(TDeviceSetupMode::WITHOUT_SETUP);
                break;
            } catch (const TSerialDeviceException& e) {
                if (i == MAX_RETRIES) {
                    LOG(Warn) << device->Port()->GetDescription() << " " << device->Protocol()->GetName() << ":"
                              << device->DeviceConfig()->SlaveId << " unable to prepare session: " << e.what();
                    throw TRPCException(e.what(), TRPCResultCode::RPC_WRONG_PARAM_VALUE);
                }
            }
        }

        size_t index = 0;
        bool success = true;
        while (index < registerList.size() && success) {
            auto range = device->CreateRegisterRange();
            auto offset = index;
            while (index < registerList.size() &&
                   range->Add(registerList[index].second, std::chrono::milliseconds::max()))
            {
                ++index;
            }
            success = true;
            for (int i = 0; i <= MAX_RETRIES; ++i) {
                device->ReadRegisterRange(range);
                while (offset < index) {
                    if (registerList[offset++].second->GetErrorState().count()) {
                        success = false;
                        break;
                    }
                }
                if (success) {
                    break;
                }
            }
        }

        try {
            device->EndSession();
        } catch (const TSerialDeviceException& e) {
            LOG(Warn) << device->Port()->GetDescription() << " " << device->Protocol()->GetName() << ":"
                      << device->DeviceConfig()->SlaveId << " unable to end session: " << e.what();
        }

        if (!success) {
            std::string error = "unable to read parameters register range";
            LOG(Warn) << device->Port()->GetDescription() << " " << device->Protocol()->GetName() << ":"
                      << device->DeviceConfig()->SlaveId << " " << error;
            throw TRPCException(error, TRPCResultCode::RPC_WRONG_PARAM_VALUE);
        }

        for (size_t i = 0; i < registerList.size(); ++i) {
            auto& reg = registerList[i];
            parameters[reg.first] = RawValueToJSON(*reg.second->GetConfig(), reg.second->GetValue());
        }
    }
} // namespace

TRPCDeviceLoadConfigRequest::TRPCDeviceLoadConfigRequest(const TSerialDeviceFactory& deviceFactory,
                                                         PDeviceTemplate deviceTemplate,
                                                         TRPCDeviceParametersCache& parametersCache)
    : DeviceFactory(deviceFactory),
      DeviceTemplate(deviceTemplate),
      ParametersCache(parametersCache)
{
    ContinuousReadSupported = DeviceTemplate->GetTemplate()["enable_wb_continuous_read"].asBool();
    IsWBDevice = ContinuousReadSupported || !DeviceTemplate->GetHardware().empty();

    Json::Value responseTimeout = DeviceTemplate->GetTemplate()["response_timeout_ms"];
    if (responseTimeout.isInt()) {
        ResponseTimeout = std::chrono::milliseconds(responseTimeout.asInt());
    }

    Json::Value frameTimeout = DeviceTemplate->GetTemplate()["frame_timeout_ms"];
    if (frameTimeout.isInt()) {
        FrameTimeout = std::chrono::milliseconds(frameTimeout.asInt());
    }
}

PRPCDeviceLoadConfigRequest ParseRPCDeviceLoadConfigRequest(const Json::Value& request,
                                                            const TSerialDeviceFactory& deviceFactory,
                                                            PDeviceTemplate deviceTemplate,
                                                            TRPCDeviceParametersCache& parametersCache)
{
    PRPCDeviceLoadConfigRequest res =
        std::make_shared<TRPCDeviceLoadConfigRequest>(deviceFactory, deviceTemplate, parametersCache);
    res->SerialPortSettings = ParseRPCSerialPortSettings(request);
    res->SlaveId = request["slave_id"].asString();
    WBMQTT::JSON::Get(request, "response_timeout", res->ResponseTimeout);
    WBMQTT::JSON::Get(request, "frame_timeout", res->FrameTimeout);
    WBMQTT::JSON::Get(request, "total_timeout", res->TotalTimeout);
    return res;
}

void ExecRPCDeviceLoadConfigRequest(PPort port,
                                    PRPCDeviceLoadConfigRequest rpcRequest,
                                    const std::list<PSerialDevice>& polledDevices)
{
    if (!rpcRequest->OnResult) {
        return;
    }

    std::string id = rpcRequest->ParametersCache.GetId(*port, rpcRequest->SlaveId);
    if (rpcRequest->ParametersCache.Contains(id)) {
        rpcRequest->OnResult(rpcRequest->ParametersCache.Get(id));
        return;
    }

    TDeviceProtocolParams protocolParams =
        rpcRequest->DeviceFactory.GetProtocolParams(rpcRequest->DeviceTemplate->GetProtocol());
    auto device = FindDevice(polledDevices, rpcRequest->SlaveId, rpcRequest->DeviceTemplate->Type);
    bool useCache = true;
    if (device == nullptr) {
        device = CreateDevice(port, rpcRequest, protocolParams);
        useCache = false;
    }

    port->SkipNoise();

    std::string fwVersion;
    if (rpcRequest->DeviceTemplate->GetProtocol() == "modbus") {
        fwVersion = ReadFirmwareVersion(*port, rpcRequest);
    }

    Json::Value templateParams = rpcRequest->DeviceTemplate->GetTemplate()["parameters"];
    Json::Value parameters;
    TRPCRegisterList registerList = CreateRegisterList(protocolParams, device, templateParams, fwVersion);
    ReadParameters(device, registerList, parameters);
    CheckParametersConditions(templateParams, parameters);

    Json::Value result;
    if (!fwVersion.empty()) {
        result["fw"] = fwVersion;
    }
    result["parameters"] = parameters;
    rpcRequest->OnResult(result);

    if (useCache) {
        rpcRequest->ParametersCache.Add(id, result);
    }
}

TRPCDeviceLoadConfigSerialClientTask::TRPCDeviceLoadConfigSerialClientTask(PRPCDeviceLoadConfigRequest request)
    : Request(request)
{
    ExpireTime = std::chrono::steady_clock::now() + Request->TotalTimeout;
}

ISerialClientTask::TRunResult TRPCDeviceLoadConfigSerialClientTask::Run(
    PPort port,
    TSerialClientDeviceAccessHandler& lastAccessedDevice,
    const std::list<PSerialDevice>& polledDevices)
{
    if (std::chrono::steady_clock::now() > ExpireTime) {
        if (Request->OnError) {
            Request->OnError(WBMQTT::E_RPC_REQUEST_TIMEOUT, "RPC request timeout");
        }
        return ISerialClientTask::TRunResult::OK;
    }

    try {
        if (!port->IsOpen()) {
            port->Open();
        }
        lastAccessedDevice.PrepareToAccess(nullptr);
        TSerialPortSettingsGuard settingsGuard(port, Request->SerialPortSettings);
        ExecRPCDeviceLoadConfigRequest(port, Request, polledDevices);
    } catch (const std::exception& error) {
        if (Request->OnError) {
            Request->OnError(WBMQTT::E_RPC_SERVER_ERROR, std::string("Port IO error: ") + error.what());
        }
    }

    return ISerialClientTask::TRunResult::OK;
}

TRPCRegisterList CreateRegisterList(const TDeviceProtocolParams& protocolParams,
                                    const PSerialDevice& device,
                                    const Json::Value& templateParams,
                                    const std::string& fwVersion)
{
    TRPCRegisterList registerList;
    for (auto it = templateParams.begin(); it != templateParams.end(); ++it) {
        const Json::Value& registerData = *it;
        std::string id = templateParams.isObject() ? it.key().asString() : registerData["id"].asString();
        bool duplicate = false;
        for (size_t i = 0; i < registerList.size(); ++i) {
            if (registerList[i].first == id) {
                duplicate = true;
                break;
            }
        }
        if (duplicate || registerData["address"].isNull() || registerData["readonly"].asBool()) {
            continue;
        }
        if (!fwVersion.empty()) {
            std::string fw = registerData["fw"].asString();
            if (!fw.empty() && util::CompareVersionStrings(fw, fwVersion) > 0) {
                continue;
            }
        }
        auto config = LoadRegisterConfig(registerData,
                                         *protocolParams.protocol->GetRegTypes(),
                                         std::string(),
                                         *protocolParams.factory,
                                         protocolParams.factory->GetRegisterAddressFactory().GetBaseRegisterAddress(),
                                         0);
        auto reg = std::make_shared<TRegister>(device, config.RegisterConfig);
        reg->SetAvailable(TRegisterAvailability::AVAILABLE);
        registerList.push_back(std::make_pair(id, reg));
    }
    return registerList;
}

void CheckParametersConditions(const Json::Value& templateParams, Json::Value& parameters)
{
    TJsonParams jsonParams(parameters);
    TExpressionsCache expressionsCache;
    bool check = true;
    while (check) {
        std::unordered_map<std::string, bool> matches;
        for (auto it = templateParams.begin(); it != templateParams.end(); ++it) {
            const Json::Value& registerData = *it;
            std::string id = templateParams.isObject() ? it.key().asString() : registerData["id"].asString();
            if (!parameters.isMember(id)) {
                continue;
            }
            bool match = CheckCondition(registerData, jsonParams, &expressionsCache);
            if (matches.find(id) == matches.end() || match) {
                matches[id] = match;
            }
        }
        check = false;
        for (auto it = matches.begin(); it != matches.end(); ++it) {
            if (!it->second) {
                parameters.removeMember(it->first);
                check = true;
            }
        }
    }
}

Json::Value RawValueToJSON(const TRegisterConfig& reg, TRegisterValue val)
{
    switch (reg.Format) {
        case U8:
            return val.Get<uint8_t>();
        case S8:
            return val.Get<int8_t>();
        case S16:
            return val.Get<int16_t>();
        case S24: {
            uint32_t v = val.Get<uint64_t>() & 0xffffff;
            if (v & 0x800000)
                v |= 0xff000000;
            return static_cast<int32_t>(v);
        }
        case S32:
            return val.Get<int32_t>();
        case S64:
            return val.Get<int64_t>();
        case BCD8:
            return PackedBCD2Int(val.Get<uint64_t>(), WordSizes::W8_SZ);
        case BCD16:
            return PackedBCD2Int(val.Get<uint64_t>(), WordSizes::W16_SZ);
        case BCD24:
            return PackedBCD2Int(val.Get<uint64_t>(), WordSizes::W24_SZ);
        case BCD32:
            return PackedBCD2Int(val.Get<uint64_t>(), WordSizes::W32_SZ);
        case Float: {
            float v;
            auto rawValue = val.Get<uint64_t>();

            // codacy static code analysis fails on this memcpy call, have no idea how to fix it
            memcpy(&v, &rawValue, sizeof(v));

            return v;
        }
        case Double: {
            double v;
            auto rawValue = val.Get<uint64_t>();
            memcpy(&v, &rawValue, sizeof(v));
            return v;
        }
        case Char8:
            return std::string(1, val.Get<uint8_t>());
        case String:
        case String8:
            return val.Get<std::string>();
        default:
            return val.Get<uint64_t>();
    }
}
