#pragma once

#include <wblib/rpc.h>

#include "serial_client.h"

void RPCPortScanHandler(const Json::Value& request,
                        PSerialClient serialClient,
                        WBMQTT::TMqttRpcServer::TResultCallback onResult,
                        WBMQTT::TMqttRpcServer::TErrorCallback onError);

void RPCPortScanHandler(const Json::Value& request,
                        TPort& port,
                        WBMQTT::TMqttRpcServer::TResultCallback onResult,
                        WBMQTT::TMqttRpcServer::TErrorCallback onError);
