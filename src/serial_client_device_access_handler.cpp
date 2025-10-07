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
        try {
            if (EventsReader) {
                bool devWasDisconnected = dev->GetConnectionState() != TDeviceConnectionState::CONNECTED;
                if (devWasDisconnected || dev != LastAccessedDevice) {
                    dev->Prepare(port);
                    if (devWasDisconnected) {
                        try {
                            EventsReader->EnableEvents(dev, port);
                        } catch (const TSerialDeviceException& e) {
                            dev->SetDisconnected();
                            throw;
                        }
                    }
                }
            }
            LastAccessedDevice = dev;
            return true;
        } catch (const TSerialDeviceException& e) {
            auto& logger = (dev->GetConnectionState() == TDeviceConnectionState::DISCONNECTED) ? Debug : Warn;
            LOG(logger) << "Failed to open session: " << e.what() << " [slave_id is " << dev->ToString() + "]";
        }
    }
    return false;
}
