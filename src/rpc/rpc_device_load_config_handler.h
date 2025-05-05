#pragma once
#include "serial_client.h"
#include <wblib/rpc.h>

void RPCDeviceLoadConfigHandler(const Json::Value& request,
                                const TSerialDeviceFactory& deviceFactory,
                                PDeviceTemplate deviceTemplate,
                                PSerialDevice device,
                                PSerialClient serialClient,
                                WBMQTT::TMqttRpcServer::TResultCallback onResult,
                                WBMQTT::TMqttRpcServer::TErrorCallback onError);

void RPCDeviceLoadConfigHandler(const Json::Value& request,
                                const TSerialDeviceFactory& deviceFactory,
                                PDeviceTemplate deviceTemplate,
                                PSerialDevice device,
                                PPort port,
                                WBMQTT::TMqttRpcServer::TResultCallback onResult,
                                WBMQTT::TMqttRpcServer::TErrorCallback onError);
