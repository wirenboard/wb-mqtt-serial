#include "serial_config.h"

std::string GetChannelPropertyName(size_t index);

void AddDeviceWithGroupsUISchema(const TDeviceTemplate& deviceTemplate,
                                 TSerialDeviceFactory& deviceFactory,
                                 Json::Value& devicesArray,
                                 Json::Value& definitions,
                                 Json::Value& translations);
