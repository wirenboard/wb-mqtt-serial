#pragma once

#include "serial_client_events_reader.h"
#include "serial_device.h"

class TSerialClientDeviceAccessHandler
{
public:
    TSerialClientDeviceAccessHandler(PSerialClientEventsReader eventsReader);
    bool PrepareToAccess(PSerialDevice dev);

private:
    PSerialDevice LastAccessedDevice;
    PSerialClientEventsReader EventsReader;
};
