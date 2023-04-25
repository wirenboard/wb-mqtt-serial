#include "serial_client_device_access_handler.h"
#include "log.h"

#define LOG(logger) logger.Log() << "[serial client] "

TSerialClientDeviceAccessHandler::TSerialClientDeviceAccessHandler(TSerialClientEventsReader& eventsReader)
    : EventsReader(eventsReader)
{}

bool TSerialClientDeviceAccessHandler::PrepareToAccess(PSerialDevice dev)
{
    if (LastAccessedDevice && dev != LastAccessedDevice) {
        try {
            LastAccessedDevice->EndSession();
        } catch (const TSerialDeviceException& e) {
            auto& logger = LastAccessedDevice->GetIsDisconnected() ? Debug : Warn;
            LOG(logger) << "TSerialDevice::EndSession(): " << e.what() << " [slave_id is "
                        << LastAccessedDevice->ToString() + "]";
        }
        LastAccessedDevice = nullptr;
    }
    if (dev) {
        try {
            if (dev->GetIsDisconnected() || dev != LastAccessedDevice) {
                dev->Prepare();
                EventsReader.EnableEvents(dev, *(dev->Port()));
            }
            LastAccessedDevice = dev;
            return true;
        } catch (const TSerialDeviceException& e) {
            auto& logger = dev->GetIsDisconnected() ? Debug : Warn;
            LOG(logger) << "Failed to open session: " << e.what() << " [slave_id is " << dev->ToString() + "]";
        }
    }
    return false;
}
