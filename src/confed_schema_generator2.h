#include "serial_config.h"

void AddDeviceUISchema2(const TDeviceTemplate& deviceTemplate,
                        TSerialDeviceFactory& deviceFactory,
                        Json::Value& devicesArray,
                        Json::Value& definitions,
                        Json::Value& translations);
