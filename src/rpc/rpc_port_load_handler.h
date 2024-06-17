#pragma once

#include "rpc_port_load_request.h"
#include "serial_client.h"

TSerialPortConnectionSettings ParseRPCSerialPortSettings(const Json::Value& request);

PRPCPortLoadRequest ParseRPCPortLoadRequest(const Json::Value& request, const Json::Value& requestSchema);

void RPCPortLoadHandler(PRPCPortLoadRequest rpcRequest,
                        PSerialClient serialClient,
                        WBMQTT::TMqttRpcServer::TResultCallback onResult,
                        WBMQTT::TMqttRpcServer::TErrorCallback onError);

void RPCPortLoadHandler(PRPCPortLoadRequest rpcRequest,
                        PPort port,
                        WBMQTT::TMqttRpcServer::TResultCallback onResult,
                        WBMQTT::TMqttRpcServer::TErrorCallback onError);
