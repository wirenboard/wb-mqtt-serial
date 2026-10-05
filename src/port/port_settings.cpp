#include "port_settings.h"
#include "feature_port.h"
#include "serial_port.h"
#include "tcp_port.h"

bool PortMatches(const TPortSettings& portSettings, const TFeaturePort& port)
{
    const auto& basePort = *port.GetBasePort();
    if (const auto* serialPort = dynamic_cast<const TSerialPort*>(&basePort)) {
        const auto* requested = std::get_if<TSerialPortSettings>(&portSettings);
        return requested != nullptr && serialPort->GetInitialSettings().Device == requested->Device;
    }
    if (const auto* tcpPort = dynamic_cast<const TTcpPort*>(&basePort)) {
        const auto* requested = std::get_if<TFramedTcpPortSettings>(&portSettings);
        const auto& settings = tcpPort->GetInitialSettings();
        return requested != nullptr && settings.Address == requested->Address && settings.Port == requested->Port;
    }
    return false;
}
