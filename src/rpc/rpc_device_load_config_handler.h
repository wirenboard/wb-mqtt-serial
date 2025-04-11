#pragma once
#include "serial_client.h"
#include <wblib/rpc.h>

void RPCDeviceLoadConfigHandler(const Json::Value& request,
                                PSerialClient serialClient,
                                WBMQTT::TMqttRpcServer::TResultCallback onResult,
                                WBMQTT::TMqttRpcServer::TErrorCallback onError);

void RPCDeviceLoadConfigHandler(const Json::Value& request,
                                TPort& port,
                                WBMQTT::TMqttRpcServer::TResultCallback onResult,
                                WBMQTT::TMqttRpcServer::TErrorCallback onError);
