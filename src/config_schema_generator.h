#include "confed_protocol_schemas_map.h"
#include "serial_config.h"

void ValidateConfig(const Json::Value& config,
                    TSerialDeviceFactory& deviceFactory,
                    const Json::Value& commonDeviceSchema,
                    const Json::Value& portsSchema,
                    TTemplateMap& templates,
                    TProtocolConfedSchemasMap& protocolSchemas);
