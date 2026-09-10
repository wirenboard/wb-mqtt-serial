#pragma once

#include "json_common.h"
#include "serial_config.h"
#include "serial_exc.h"
#include <gtest/gtest.h>

template<class FnType> void CheckExceptionMsg(FnType fn, const std::string& msg)
{
    try {
        fn();
    } catch (const TSerialDeviceTransientErrorException& e) {
        ASSERT_EQ(e.what(), "Serial protocol error: " + msg);
        throw;
    } catch (const std::exception& e) {
        ASSERT_EQ(e.what(), msg);
        throw;
    }
}

Json::Value GetCommonDeviceSchema();
Json::Value GetTemplatesSchema();

//! Serializes JSON with indentation, for comparing and printing whole documents in tests
std::string SerializeJson(const Json::Value& value);

//! Parses a JSON document from a string
Json::Value ParseJson(const std::string& text);

/**
 * @brief Loads a configuration from test/configs with the templates from test/device-templates
 *
 * @param filePath path of the configuration relative to the test data directory
 */
PHandlerConfig LoadTestConfig(const std::string& filePath, TSerialDeviceFactory& deviceFactory);
