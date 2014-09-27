#include "modbus_handler.h"

TMQTTModbusHandler::TMQTTModbusHandler(const TMQTTModbusHandler::TConfig& mqtt_config,
                                       const THandlerConfig& handler_config)
    : TMQTTWrapper(mqtt_config),
      Config(handler_config)
{
    for (const auto& port_config : Config.PortConfigs)
        Ports.push_back(std::unique_ptr<TModbusPort>(new TModbusPort(this, port_config)));

	Connect();
}

void TMQTTModbusHandler::OnConnect(int rc)
{
    if (Config.Debug)
        std::cerr << "Connected with code " << rc << std::endl;

	if(rc != 0)
        return;

    for (const auto& port: Ports)
        port->PubSubSetup();
}

void TMQTTModbusHandler::OnMessage(const struct mosquitto_message *message)
{
    std::string topic = message->topic;
    std::string payload = static_cast<const char *>(message->payload);
    for (const auto& port: Ports) {
        if (port->HandleMessage(topic, payload))
            break;
    }
}

void TMQTTModbusHandler::OnSubscribe(int, int, const int *)
{
	if (Config.Debug)
        std::cerr << "Subscription succeeded." << std::endl;
}

void TMQTTModbusHandler::ModbusLoop()
{
    for (;;) {
        for (const auto& port: Ports)
            port->Cycle();
    }
}

bool TMQTTModbusHandler::WriteInitValues()
{
    bool did_write = false;
    std::vector< std::pair<int, int> > values;
    for (const auto& port: Ports) {
        if (port->WriteInitValues())
            did_write = true;
    }

    return did_write;
}
