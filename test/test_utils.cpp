#include "test_utils.h"
#include "serial_config.h"
#include <wblib/testing/testlog.h>

using namespace WBMQTT;
using namespace WBMQTT::Testing;

Json::Value GetCommonDeviceSchema()
{
    std::string commonSchema = TLoggedFixture::GetDataFilePath("../wb-mqtt-serial-confed-common.schema.json");
    return WBMQTT::JSON::Parse(commonSchema);
}

Json::Value GetTemplatesSchema()
{
    std::string templateSchema = TLoggedFixture::GetDataFilePath("../wb-mqtt-serial-device-template.schema.json");
    return LoadConfigTemplatesSchema(templateSchema, GetCommonDeviceSchema());
}