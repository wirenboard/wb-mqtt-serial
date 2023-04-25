#pragma once

#include "serial_client_events_reader.h"
#include "serial_device.h"

class TSerialClientDeviceAccessHandler
{
public:
    TSerialClientDeviceAccessHandler(TSerialClientEventsReader& eventsReader);
    bool PrepareToAccess(PSerialDevice dev);

private:
    PSerialDevice LastAccessedDevice;
    TSerialClientEventsReader& EventsReader;
};
