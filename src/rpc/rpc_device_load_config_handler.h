#pragma once
#include "serial_client.h"
#include <wblib/rpc.h>

void RPCDeviceLoadConfigHandler(const Json::Value& request,
                                const Json::Value& parameters,
                                PSerialClient serialClient,
                                WBMQTT::TMqttRpcServer::TResultCallback onResult,
                                WBMQTT::TMqttRpcServer::TErrorCallback onError);

void RPCDeviceLoadConfigHandler(const Json::Value& request,
                                const Json::Value& parameters,
                                TPort& port,
                                WBMQTT::TMqttRpcServer::TResultCallback onResult,
                                WBMQTT::TMqttRpcServer::TErrorCallback onError);
