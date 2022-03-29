#include <wblib/json_utils.h>

Json::Value ConfigToHomeuiChannel(const Json::Value& channel, bool addDefaultMode = true);
Json::Value HomeuiToConfigChannel(const Json::Value& channel);
void AddChannelModes(Json::Value& channelSchema);
