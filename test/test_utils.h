#pragma once

#include "json_common.h"
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
