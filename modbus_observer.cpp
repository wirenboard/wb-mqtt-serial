#include "modbus_observer.h"
#include "uniel_context.h"

TMQTTModbusObserver::TMQTTModbusObserver(PMQTTClientBase mqtt_client,
                                         PHandlerConfig handler_config,
                                         PModbusConnector connector)
    : MQTTClient(mqtt_client),
      Config(handler_config)
{
    for (const auto& port_config : Config->PortConfigs) {
        if (port_config->DeviceConfigs.empty()) {
            std::cerr << "Warning: no devices defined for port "
                      << port_config->ConnSettings.Device
                      << " . Skipping. " << std::endl;
            continue;
        }

        Ports.push_back(
            std::unique_ptr<TModbusPort>(
                new TModbusPort(mqtt_client, port_config,
                                connector ? connector :
                                GetConnector(port_config))));
    }
}

void TMQTTModbusObserver::SetUp()
{
    MQTTClient->Observe(shared_from_this());
    MQTTClient->Connect();
}

void TMQTTModbusObserver::OnConnect(int rc)
{
    if (Config->Debug)
        std::cerr << "Connected with code " << rc << std::endl;

    if(rc != 0)
        return;

    for (const auto& port: Ports)
        port->PubSubSetup();
}

void TMQTTModbusObserver::OnMessage(const struct mosquitto_message *message)
{
    std::string topic = message->topic;
    std::string payload = static_cast<const char *>(message->payload);
    for (const auto& port: Ports) {
        if (port->HandleMessage(topic, payload))
            break;
    }
}

void TMQTTModbusObserver::OnSubscribe(int, int, const int *)
{
    if (Config->Debug)
        std::cerr << "Subscription succeeded." << std::endl;
}

void TMQTTModbusObserver::ModbusLoopOnce()
{
    for (const auto& port: Ports)
        port->Cycle();
}

void TMQTTModbusObserver::ModbusLoop()
{
    for (;;)
        ModbusLoopOnce();
}

bool TMQTTModbusObserver::WriteInitValues()
{
    bool did_write = false;
    std::vector< std::pair<int, int> > values;
    for (const auto& port: Ports) {
        if (port->WriteInitValues())
            did_write = true;
    }

    return did_write;
}

PModbusConnector TMQTTModbusObserver::GetConnector(PPortConfig port_config)
{
    if (port_config->Type == "uniel")
        return PModbusConnector(new TUnielModbusConnector());

    if (!port_config->Type.empty() && port_config->Type != "modbus")
        std::cerr << "warning: bad port type '" << port_config->Type <<
            "', using 'modbus'" << std::endl;

    return PModbusConnector(new TDefaultModbusConnector());
}
