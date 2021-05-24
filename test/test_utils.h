#pragma once

#include <gtest/gtest.h>
#include "serial_exc.h"

::testing::AssertionResult ArraysMatch(const std::vector<uint8_t>& v1, const std::vector<uint8_t>& v2);

template<class FnType> void CheckExceptionMsg(FnType fn, const std::string& msg)
{
    try {
        fn();
    } catch (const TSerialDeviceTransientErrorException& e) {
        ASSERT_EQ(e.what(), "Serial protocol error: " + msg);
        throw;
    }
}
