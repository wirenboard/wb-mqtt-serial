#include "log.h"

// clang-format off
#ifndef __EMSCRIPTEN__
WBMQTT::TLogger Error("ERROR: ",   WBMQTT::TLogger::StdErr, WBMQTT::TLogger::RED);
WBMQTT::TLogger Warn ("WARNING: ", WBMQTT::TLogger::StdErr, WBMQTT::TLogger::YELLOW);
WBMQTT::TLogger Info ("INFO: ",    WBMQTT::TLogger::StdErr, WBMQTT::TLogger::GREY);
WBMQTT::TLogger Debug("DEBUG: ",   WBMQTT::TLogger::StdErr, WBMQTT::TLogger::WHITE, false);
#else
WBMQTT::TLogger Error("ERR: ", WBMQTT::TLogger::StdErr, -1, true, WBMQTT::TLogger::EColorUsage::OFF);
WBMQTT::TLogger Warn ("WRN: ", WBMQTT::TLogger::StdOut, -1, true, WBMQTT::TLogger::EColorUsage::OFF);
WBMQTT::TLogger Info ("INF: ", WBMQTT::TLogger::StdOut, -1, true, WBMQTT::TLogger::EColorUsage::OFF);
WBMQTT::TLogger Debug("DBG: ", WBMQTT::TLogger::StdOut, -1, true, WBMQTT::TLogger::EColorUsage::OFF);
#endif
// clang-format on

TLoggerWithTimeout::TLoggerWithTimeout(const std::chrono::milliseconds& notificationInterval, const std::string& prefix)
    : NotificationInterval(notificationInterval),
      Prefix(prefix)
{}

void TLoggerWithTimeout::Log(const std::string& msg, WBMQTT::TLogger& debugLogger, WBMQTT::TLogger& errorLogger)
{
    auto currentTime = std::chrono::steady_clock::now();
    auto delta = std::chrono::duration_cast<std::chrono::milliseconds>(currentTime - LastErrorNotificationTime);
    if (delta < NotificationInterval) {
        debugLogger.Log() << Prefix << msg;
    } else {
        errorLogger.Log() << Prefix << msg;
        LastErrorNotificationTime = currentTime;
    }
}

void TLoggerWithTimeout::DropTimeout()
{
    LastErrorNotificationTime = std::chrono::steady_clock::time_point();
}
