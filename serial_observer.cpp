#include <thread>
#include "serial_observer.h"

TMQTTSerialObserver::TMQTTSerialObserver(PMQTTClientBase mqtt_client,
                                         PHandlerConfig handler_config,
                                         PAbstractSerialPort port_override)
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

        PortDrivers.push_back(
            std::make_shared<TSerialPortDriver>(mqtt_client, port_config, port_override));
    }
}

void TMQTTSerialObserver::SetUp()
{
    MQTTClient->Observe(shared_from_this());
    MQTTClient->Connect();
}

void TMQTTSerialObserver::OnConnect(int rc)
{
    if (Config->Debug)
        std::cerr << "Connected with code " << rc << std::endl;

    if(rc != 0)
        return;

    for (const auto& portDriver: PortDrivers)
        portDriver->PubSubSetup();
}

void TMQTTSerialObserver::OnMessage(const struct mosquitto_message *message)
{
    std::string topic = message->topic;
    std::string payload = static_cast<const char *>(message->payload);
    for (const auto& portDriver: PortDrivers) {
        if (portDriver->HandleMessage(topic, payload))
            break;
    }
}

void TMQTTSerialObserver::OnSubscribe(int, int, const int *)
{
    if (Config->Debug)
        std::cerr << "Subscription succeeded." << std::endl;
}

void TMQTTSerialObserver::LoopOnce()
{
    for (const auto& portDriver: PortDrivers)
        portDriver->Cycle();
}

void TMQTTSerialObserver::Loop()
{
    std::vector<std::thread> port_loops;

    for (const auto& portDriver: PortDrivers) {
        port_loops.emplace_back(
            [&portDriver](){
                for (;;)  portDriver->Cycle();
            }
        );
    }

    for (auto& thr : port_loops) {
        thr.join();
    }
}

bool TMQTTSerialObserver::WriteInitValues()
{
    bool did_write = false;
    std::vector< std::pair<int, int> > values;
    for (const auto& portDriver: PortDrivers) {
        if (portDriver->WriteInitValues())
            did_write = true;
    }

    return did_write;
}
