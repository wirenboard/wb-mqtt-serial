#pragma once

#include <string>
#include <memory>
#include <vector>
#include <exception>

#include "modbus_client.h"
#include "jsoncpp/json/json.h"

struct TModbusChannel
{
    TModbusChannel(std::string name = "", std::string type = "text",
                   std::string device_id = "", int order = 0,
                   int on_value = -1, int max = - 1, bool read_only = false,
                   const std::vector<TModbusRegister>& regs =
                       std::vector<TModbusRegister>())
        : Name(name), Type(type), DeviceId(device_id),
          Order(order), OnValue(on_value), Max(max),
          ReadOnly(read_only), Registers(regs) {}

    std::string Name;
    std::string Type;
    std::string DeviceId; // FIXME
    int Order;
    int OnValue;
    int Max;
    bool ReadOnly;
    std::vector<TModbusRegister> Registers;
};

typedef std::shared_ptr<TModbusChannel> PModbusChannel;

struct TDeviceSetupItem
{
    TDeviceSetupItem(std::string name, int address, int value)
        : Name(name), Address(address), Value(value) {}
    std::string Name;
    int Address;
    int Value;
};

typedef std::shared_ptr<TDeviceSetupItem> PDeviceSetupItem;

struct TDeviceConfig
{
    TDeviceConfig(std::string name = "")
        : Name(name) {}
    int NextOrderValue() const { return ModbusChannels.size() + 1; }
    void AddChannel(PModbusChannel channel) { ModbusChannels.push_back(channel); };
    void AddSetupItem(PDeviceSetupItem item) { SetupItems.push_back(item); }
    std::string Id;
    std::string Name;
    int SlaveId;
    std::vector<PModbusChannel> ModbusChannels;
    std::vector<PDeviceSetupItem> SetupItems;
};

typedef std::shared_ptr<TDeviceConfig> PDeviceConfig;
    
struct TPortConfig
{
    void AddDeviceConfig(PDeviceConfig device_config) { DeviceConfigs.push_back(device_config); }
    TModbusConnectionSettings ConnSettings;
    int PollInterval = 2000;
    bool Debug = false;
    std::vector<PDeviceConfig> DeviceConfigs;
};

typedef std::shared_ptr<TPortConfig> PPortConfig;

struct THandlerConfig
{
    void AddPortConfig(PPortConfig port_config) {
        PortConfigs.push_back(port_config);
        PortConfigs[PortConfigs.size() - 1]->Debug = Debug;
    }
    bool Debug = false;
    std::vector<PPortConfig> PortConfigs;
};

typedef std::shared_ptr<THandlerConfig> PHandlerConfig;

class TConfigParserException: public std::exception
{
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
    TConfigParser(const std::string& config_fname, bool force_debug)
        : ConfigFileName(config_fname), HandlerConfig(new THandlerConfig) {
        HandlerConfig->Debug = force_debug;
    }
    PHandlerConfig Parse();
    TModbusRegister LoadRegister(PDeviceConfig device_config, const Json::Value& register_data,
                                 std::string& default_type_str);
    void LoadChannel(PDeviceConfig device_config, const Json::Value& channel_data);
    void LoadSetupItem(PDeviceConfig device_config, const Json::Value& item_data);
    void LoadDevice(PPortConfig port_config, const Json::Value& device_data,
                    const std::string& default_id);
    void LoadPort(const Json::Value& port_data, const std::string& id_prefix);
    void LoadConfig();
private:
    std::string ConfigFileName;
    PHandlerConfig HandlerConfig;
    Json::Value root;
};
