#pragma once

#include "rpc_port_setup_request.h"
#include "serial_client.h"

PRPCPortSetupRequest ParseRPCPortSetupRequest(const Json::Value& request, const Json::Value& requestSchema);

void RPCPortSetupHandler(PRPCPortSetupRequest rpcRequest,
                         PSerialClient serialClient,
                         WBMQTT::TMqttRpcServer::TResultCallback onResult,
                         WBMQTT::TMqttRpcServer::TErrorCallback onError);

void RPCPortSetupHandler(PRPCPortSetupRequest rpcRequest,
                         TPort& port,
                         WBMQTT::TMqttRpcServer::TResultCallback onResult,
                         WBMQTT::TMqttRpcServer::TErrorCallback onError);
