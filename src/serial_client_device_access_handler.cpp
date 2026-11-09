#include "serial_client_device_access_handler.h"
#include "log.h"

#define LOG(logger) logger.Log() << "[serial client] "

TSerialClientDeviceAccessHandler::TSerialClientDeviceAccessHandler(PSerialClientEventsReader eventsReader)
    : EventsReader(eventsReader)
{}

bool TSerialClientDeviceAccessHandler::PrepareToAccess(TFeaturePort& port, PSerialDevice dev)
{
    if (LastAccessedDevice && dev != LastAccessedDevice) {
        try {
            LastAccessedDevice->EndSession(port);
        } catch (const TSerialDeviceException& e) {
            auto& logger =
                (LastAccessedDevice->GetConnectionState() == TDeviceConnectionState::DISCONNECTED) ? Debug : Warn;
            LOG(logger) << "TSerialDevice::EndSession(): " << e.what() << " [slave_id is "
                        << LastAccessedDevice->ToString() + "]";
        }
        LastAccessedDevice = nullptr;
    }
    if (dev) {
        // Prepare may disconnect the device, the log level depends on the state before it
        auto connectionStateBeforePrepare = dev->GetConnectionState();
        try {
            if (EventsReader) {
                bool needReconnect = connectionStateBeforePrepare != TDeviceConnectionState::CONNECTED;
                if (needReconnect || dev != LastAccessedDevice) {
                    dev->Prepare(port);
                }
                EventsReader->EnableEvents(dev, port, needReconnect);
            }
            LastAccessedDevice = dev;
            return true;
        } catch (const TSerialDeviceException& e) {
            auto& logger = (connectionStateBeforePrepare == TDeviceConnectionState::DISCONNECTED) ? Debug : Warn;
            LOG(logger) << "Failed to open session: " << e.what() << " [slave_id is " << dev->ToString() + "]";
        }
    }
    return false;
}
