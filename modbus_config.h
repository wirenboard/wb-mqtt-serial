#pragma once

#include <string>
#include <vector>
#include <exception>

#include "modbus_client.h"
#include "jsoncpp/json/json.h"

struct TModbusChannel
{
    TModbusChannel(std::string name = "", std::string type = "text",
                   double scale = 1, std::string device_id = "", int order = 0,
                   int on_value = -1, int max = - 1,
                   TModbusParameter param = TModbusParameter())
        : Name(name), Type(type), Scale(scale), DeviceId(device_id),
          Order(order), OnValue(on_value), Max(max), Parameter(param) {}

    std::string Name;
    std::string Type;
    double Scale;
    std::string DeviceId; // FIXME
    int Order;
    int OnValue;
    int Max;
    TModbusParameter Parameter;
};

struct TDeviceSetupItem
{
    TDeviceSetupItem(std::string name, int address, int value)
        : Name(name), Address(address), Value(value) {}
    std::string Name;
    int Address;
    int Value;
};

struct TDeviceConfig
{
    TDeviceConfig(std::string name = "")
        : Name(name) {}
    int NextOrderValue() const { return ModbusChannels.size() + 1; }
    void AddChannel(const TModbusChannel& channel) { ModbusChannels.push_back(channel); };
    void AddSetupItem(const TDeviceSetupItem& item) { SetupItems.push_back(item); }
    std::string Id;
    std::string Name;
    int SlaveId;
    std::vector<TModbusChannel> ModbusChannels;
    std::vector<TDeviceSetupItem> SetupItems;
};
    
struct TPortConfig
{
    void AddDeviceConfig(const TDeviceConfig& device_config) { DeviceConfigs.push_back(device_config); }
    std::string Path;
    int BaudRate = 115200;
    char Parity = 'N';
    int DataBits = 8;
    int StopBits = 1;
    int PollInterval = 2000;
    bool Debug = false;
    std::vector<TDeviceConfig> DeviceConfigs;
};

struct THandlerConfig
{
    void AddPortConfig(const TPortConfig& port_config) {
        PortConfigs.push_back(port_config);
        PortConfigs[PortConfigs.size() - 1].Debug = Debug;
    }
    bool Debug = false;
    std::vector<TPortConfig> PortConfigs;
};

class TConfigParserException: public std::exception {
public:
    TConfigParserException(std::string _message): message(_message) {}
    const char* what () const throw () {
        return ("Error parsing config file: " + message).c_str();
    }
private:
    std::string message;
};

class TConfigParser
{
public:
    TConfigParser(std::string config_fname, bool force_debug)
        : ConfigFileName(config_fname) { HandlerConfig.Debug = force_debug; }
    const THandlerConfig& parse();
    void LoadChannel(TDeviceConfig& device_config, const Json::Value& channel_data);
    void LoadSetupItem(TDeviceConfig& device_config, const Json::Value& item_data);
    void LoadDevice(TPortConfig& port_config, const Json::Value& device_data,
                    const std::string& default_id);
    void LoadPort(const Json::Value& port_data, const std::string& id_prefix);
    void LoadConfig();
private:
    THandlerConfig HandlerConfig;
    std::string ConfigFileName;
    Json::Value root;
};
