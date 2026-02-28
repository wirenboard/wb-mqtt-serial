#pragma once
#include "rpc_device_load_task.h"

namespace rpc_device_load_detail
{
    extern const char* UNSUPPORTED_VALUE;
    void SetContinuousRead(TPort& port, TRPCDeviceRequest& request, bool enabled);
    void MarkUnsupported(TPort& port,
                         TRPCDeviceRequest& request,
                         TRPCRegisterList& registerList,
                         Json::Value& data);
    void ReadRegistersIntoJson(PPort port,
                               PRPCDeviceLoadRequest rpcRequest,
                               TRPCRegisterList& registerList,
                               Json::Value& data,
                               Json::Value& readonlyMap);
    void ExecRPCRequest(PPort port, PRPCDeviceLoadRequest rpcRequest);
}
