#pragma once

#include <wblib/rpc.h>

#include "serial_client.h"

void RPCPortLoadHandler(const Json::Value& request,
                        PSerialClient serialClient,
                        WBMQTT::TMqttRpcServer::TResultCallback onResult,
                        WBMQTT::TMqttRpcServer::TErrorCallback onError);

void RPCPortLoadHandler(const Json::Value& request,
                        TPort& port,
                        WBMQTT::TMqttRpcServer::TResultCallback onResult,
                        WBMQTT::TMqttRpcServer::TErrorCallback onError);
