#include <wblib/json_utils.h>

Json::Value ConfigToHomeuiSubdeviceChannel(const Json::Value& channel);
Json::Value ConfigToHomeuiGroupChannel(const Json::Value& channel, size_t channelIndex);
Json::Value HomeuiToConfigChannel(const Json::Value& channel);
void AddChannelModes(Json::Value& channelSchema);
