#pragma once

#include "serial_exc.h"
#include <gtest/gtest.h>
#include <wblib/json_utils.h>

::testing::AssertionResult ArraysMatch(const std::vector<uint8_t>& v1, const std::vector<uint8_t>& v2);
::testing::AssertionResult JsonsMatch(const Json::Value& v1, const Json::Value& v2);

template<class FnType> void CheckExceptionMsg(FnType fn, const std::string& msg)
{
    try {
        fn();
    } catch (const TSerialDeviceTransientErrorException& e) {
        ASSERT_EQ(e.what(), "Serial protocol error: " + msg);
        throw;
    }
}
