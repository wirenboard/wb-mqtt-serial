#include "rpc_port_driver_list.h"
#include "rpc_helpers.h"

#define LOG(logger) ::logger.Log() << "[RPC] "

namespace
{
    TPortSettings ParseRequestPort(const Json::Value& request)
    {
        return ParseRPCPort(request, request.get("protocol", "modbus").asString() == "modbus-tcp");
    }

    //! A request with "device_id" may have no valid port, then the port of the target is not set
    TSerialDriverCore::TTaskTarget MakeTaskTarget(const Json::Value& request)
    {
        std::string deviceId;
        if (request["device_id"].isString()) {
            deviceId = request["device_id"].asString();
        }
        TSerialDriverCore::TTaskTarget target;
        try {
            target.Port = ParseRequestPort(request);
        } catch (const TRPCException&) {
            if (deviceId.empty()) {
                throw;
            }
        }
        target.DeviceId = deviceId;
        target.SlaveId = request["slave_id"].asString();
        target.DeviceType = request["device_type"].asString();
        return target;
    }
}

TSerialClientTaskRunner::TSerialClientTaskRunner(TSerialDriverCore& serialDriverCore)
    : SerialDriverCore(serialDriverCore)
{}

void TSerialClientTaskRunner::SetPoll(const Json::Value& request, bool poll)
{
    auto target = MakeTaskTarget(request);
    try {
        SerialDriverCore.SetPoll(target, poll);
    } catch (const TSerialDriverCoreBusyError&) {
        throw TRPCException(CONFIG_BUSY_ERROR, TRPCResultCode::RPC_WRONG_PARAM_VALUE);
    } catch (const TTaskTargetNotFoundError&) {
        if (!target.Port) {
            // The error of the port given in the request
            ParseRequestPort(request);
        }
        throw TRPCException("Port or device not found", TRPCResultCode::RPC_WRONG_PARAM_VALUE);
    } catch (const std::runtime_error& e) {
        LOG(Warn) << e.what();
        throw TRPCException(e.what(), TRPCResultCode::RPC_WRONG_PARAM_VALUE);
    }
}

void TSerialClientTaskRunner::RunTask(const Json::Value& request, const TSerialDriverCore::TMakeTaskFn& makeTask)
{
    auto target = MakeTaskTarget(request);
    try {
        SerialDriverCore.AddTask(target, makeTask);
    } catch (const TSerialDriverCoreBusyError&) {
        throw TRPCException(CONFIG_BUSY_ERROR, TRPCResultCode::RPC_WRONG_PARAM_VALUE);
    } catch (const TTaskTargetNotFoundError& e) {
        if (!target.Port) {
            // The error of the port given in the request
            ParseRequestPort(request);
        }
        throw TRPCException(e.what(), TRPCResultCode::RPC_WRONG_PARAM_VALUE);
    }
}

void TSerialClientTaskRunner::RunTask(const TPortSettings& portSettings, PSerialClientTask task)
{
    try {
        TSerialDriverCore::TTaskTarget target;
        target.Port = portSettings;
        SerialDriverCore.AddTask(target, [&task](PSerialDevice, PDeviceParametersCache) { return task; });
    } catch (const TSerialDriverCoreBusyError&) {
        throw TRPCException(CONFIG_BUSY_ERROR, TRPCResultCode::RPC_WRONG_PARAM_VALUE);
    }
}
