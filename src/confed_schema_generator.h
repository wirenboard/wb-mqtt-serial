#pragma once

#include "serial_config.h"

void AddTranslations(Json::Value& translations, const Json::Value& deviceSchema);

void AddUnitTypes(Json::Value& schema);

Json::Value GenerateSchemaForConfed(TDeviceTemplate& deviceTemplate,
                                    TSerialDeviceFactory& deviceFactory,
                                    const Json::Value& commonDeviceSchema);
