#include "serial_config.h"

std::string GetChannelPropertyName(size_t index);

Json::Value MakeDeviceWithGroupsUISchema(TDeviceTemplate& deviceTemplate,
                                         TSerialDeviceFactory& deviceFactory,
                                         const Json::Value& commonDeviceSchema);
