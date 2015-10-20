#pragma once

#include <string>
#include <memory>
#include <vector>
#include <exception>
#include <map>

#include "modbus_client.h"
#include "jsoncpp/json/json.h"

struct TModbusChannel
{
    TModbusChannel(std::string name = "", std::string type = "text",
                   std::string device_id = "", int order = 0,
                   std::string on_value = "", int max = - 1, bool read_only = false,
                   const std::vector<std::shared_ptr<TModbusRegister>> regs =
                       std::vector<std::shared_ptr<TModbusRegister>>())
        : Name(name), Type(type), DeviceId(device_id),
          Order(order), OnValue(on_value), Max(max),
          ReadOnly(read_only), Registers(regs), PrintedErrorMessage(false) {}
    std::string Name;
    std::string Type;
    std::string DeviceId; // FIXME
    int Order;
    std::string OnValue;
    int Max;
    bool ReadOnly;
    std::vector<std::shared_ptr<TModbusRegister>> Registers;
    bool PrintedErrorMessage;
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
    std::string DeviceType;
    std::vector<PModbusChannel> ModbusChannels;
    std::vector<PDeviceSetupItem> SetupItems;
};

typedef std::shared_ptr<TDeviceConfig> PDeviceConfig;

struct TPortConfig
{
    void AddDeviceConfig(PDeviceConfig device_config) { DeviceConfigs.push_back(device_config); }
    TSerialPortSettings ConnSettings;
    int PollInterval = 20;
    bool Debug = false;
    std::string Type;
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

class TConfigActionParser
{
    public :
        std::shared_ptr<TModbusRegister> LoadRegister(PDeviceConfig device_config, const Json::Value& register_data, std::string& default_type_str);
        void LoadChannel(PDeviceConfig device_config, const Json::Value& channel_data);
        void LoadSetupItem(PDeviceConfig device_config, const Json::Value& item_data);
        void LoadDeviceVectors(PDeviceConfig device_config, const Json::Value& device_data);
    protected:
        int GetInt(const Json::Value& obj, const std::string& key);

};

typedef Json::Value TDeviceJson;

class TConfigTemplateParser
{
    public :
        TConfigTemplateParser(const std::string& template_config_dir, bool debug);
        inline ~TConfigTemplateParser() { Templates.clear(); };
        std::map<std::string, TDeviceJson> Parse();

    private:
        std::string DirectoryName;
        void LoadDeviceTemplate(const Json::Value& root, const std::string& filepath);
        bool Debug;
        std::map<std::string, TDeviceJson> Templates;
};


class TConfigParser : TConfigActionParser
{
public:
    TConfigParser(const std::string& config_fname, bool force_debug)
        : ConfigFileName(config_fname), HandlerConfig(new THandlerConfig)
    {
        HandlerConfig->Debug = force_debug;
    }
    TConfigParser(const std::string& config_fname, bool force_debug, const std::map<std::string, TDeviceJson>& templates)
        : ConfigFileName(config_fname), HandlerConfig(new THandlerConfig), TemplatesMap(templates)
    {
        HandlerConfig->Debug = force_debug;
    }
    PHandlerConfig Parse();
    void LoadDevice(PPortConfig port_config, const Json::Value& device_data,
                    const std::string& default_id);
    void LoadPort(const Json::Value& port_data, const std::string& id_prefix);
    void LoadConfig();
private:

    std::string ConfigFileName;
    PHandlerConfig HandlerConfig;
    std::map<std::string, TDeviceJson> TemplatesMap;
    Json::Value root;
};
