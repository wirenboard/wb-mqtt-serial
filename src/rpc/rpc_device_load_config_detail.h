#pragma once
#include "rpc_device_load_config_task.h"

namespace rpc_device_load_config_detail
{
    extern const char* UNSUPPORTED_VALUE;
    std::string ReadWbRegister(TPort& port, PRPCDeviceLoadConfigRequest rpcRequest, const std::string& registerName);
    std::string ReadDeviceModel(TPort& port, PRPCDeviceLoadConfigRequest rpcRequest);
    void SetContinuousRead(TPort& port, PRPCDeviceLoadConfigRequest rpcRequest, bool enabled);
    void LoadConfigParameters(PPort port, PRPCDeviceLoadConfigRequest rpcRequest, Json::Value& parameters);
    void CheckTemplate(PPort port, PRPCDeviceLoadConfigRequest rpcRequest, std::string& model);
    void MarkUnsupportedParameters(TPort& port,
                                   PRPCDeviceLoadConfigRequest rpcRequest,
                                   TRPCRegisterList& registerList,
                                   Json::Value& parameters);
    void ExecRPCRequest(PPort port, PRPCDeviceLoadConfigRequest rpcRequest);
}
