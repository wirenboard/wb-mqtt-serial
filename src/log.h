#pragma once

#include <wblib/log.h>
#include <functional>

extern WBMQTT::TLogger Error;
extern WBMQTT::TLogger Warn;
extern WBMQTT::TLogger Info;
extern WBMQTT::TLogger Debug;

class TLoggerWithTimeout
{
    std::chrono::steady_clock::time_point LastErrorNotificationTime;
    std::chrono::milliseconds             NotificationInterval;
    std::string                           Prefix;

public:
    TLoggerWithTimeout(const std::chrono::milliseconds& notificationInterval, const std::string& prefix);

    void Log(const std::string& msg, WBMQTT::TLogger& debugLogger, WBMQTT::TLogger& errorLogger);

    void DropTimeout();
};
