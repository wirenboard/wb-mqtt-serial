#include "test_utils.h"
#include "serial_config.h"
#include <sstream>
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

std::string SerializeJson(const Json::Value& value)
{
    Json::StreamWriterBuilder builder;
    builder["indentation"] = "  ";
    return Json::writeString(builder, value);
}

Json::Value ParseJson(const std::string& text)
{
    Json::CharReaderBuilder builder;
    Json::Value root;
    std::string errors;
    std::istringstream stream(text);
    EXPECT_TRUE(Json::parseFromStream(builder, stream, &root, &errors)) << errors;
    return root;
}

PHandlerConfig LoadTestConfig(const std::string& filePath, TSerialDeviceFactory& deviceFactory)
{
    auto commonDeviceSchema(GetCommonDeviceSchema());
    auto portsSchema(WBMQTT::JSON::Parse(TLoggedFixture::GetDataFilePath("../wb-mqtt-serial-ports.schema.json")));
    TProtocolConfedSchemasMap protocolSchemas(TLoggedFixture::GetDataFilePath("../protocols"), commonDeviceSchema);
    TTemplateMap templateMap(GetTemplatesSchema());
    templateMap.AddTemplatesDir(TLoggedFixture::GetDataFilePath("device-templates/"));
    return LoadConfig(TLoggedFixture::GetDataFilePath(filePath),
                      deviceFactory,
                      commonDeviceSchema,
                      templateMap,
                      portsSchema,
                      protocolSchemas);
}
