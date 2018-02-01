#include "log.h"

WBMQTT::TLogger Error("ERROR: ",   WBMQTT::TLogger::StdErr, WBMQTT::TLogger::RED);
WBMQTT::TLogger Warn ("WARNING: ", WBMQTT::TLogger::StdErr, WBMQTT::TLogger::YELLOW);
WBMQTT::TLogger Info ("INFO: ",    WBMQTT::TLogger::StdErr, WBMQTT::TLogger::GREY);
WBMQTT::TLogger Debug("DEBUG: ",   WBMQTT::TLogger::StdErr, WBMQTT::TLogger::WHITE, false);
