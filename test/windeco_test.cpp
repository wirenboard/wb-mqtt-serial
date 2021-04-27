#include "devices/curtains/windeco_device.h"
#include "test_utils.h"

using namespace WinDeco;

TEST(TWinDecoTest, MakeRequest)
{
    ASSERT_TRUE(ArraysMatch(MakeRequest(1, 2, 2), {0x81, 0, 0, 1, 2, 2, 155}));
    ASSERT_TRUE(ArraysMatch(MakeRequest(1, 2, 4), {0x81, 0, 0, 1, 2, 4, 167}));
}

TEST(TWinDecoTest, ParsePositionResponse)
{
    ASSERT_NO_THROW(ParsePositionResponse(1, 1, {1, 0, 0, 1, 1, 2, 22}));

    ASSERT_THROW(CheckExceptionMsg([](){ParsePositionResponse(1, 1, {1, 0, 0, 1, 1, 2, 21});}, 
                                   "Bad CRC"),
                 TSerialDeviceTransientErrorException);

    ASSERT_THROW(CheckExceptionMsg([](){ParsePositionResponse(1, 1, {2, 0, 0, 1, 1, 2, 22});},
                                   "Bad header"),
                 TSerialDeviceTransientErrorException);

    ASSERT_THROW(CheckExceptionMsg([](){ParsePositionResponse(1, 1, {1, 0, 0, 1, 2, 2, 27});},
                                   "Bad address"),
                TSerialDeviceTransientErrorException);

    ASSERT_THROW(CheckExceptionMsg([](){ParsePositionResponse(1, 1, {1, 0, 0, 1, 1, 0xAA, 6});},
                                   "No limit setting"),
                 TSerialDeviceTransientErrorException);

    ASSERT_THROW(CheckExceptionMsg([](){ParsePositionResponse(1, 1, {1, 0, 0, 1, 1, 0xBB, 0x6C});},
                                   "Curtain obstacle somewhere"),
                 TSerialDeviceTransientErrorException);

    ASSERT_THROW(CheckExceptionMsg([](){ParsePositionResponse(1, 1, {1, 0, 0, 1, 1, 0xAB, 0x0C});},
                                   "Unknown position"),
                 TSerialDeviceTransientErrorException);
}

TEST(TWinDecoTest, ParseStateResponse)
{
    ASSERT_EQ(ParseStateResponse(1, 1, {1, 0, 0, 1, 1, 2, 22}), 2);
    ASSERT_EQ(ParseStateResponse(1, 1, {1, 0, 0, 1, 1, 3, 28}), 3);
    ASSERT_EQ(ParseStateResponse(1, 1, {1, 0, 0, 1, 1, 4, 34}), 4);

    ASSERT_THROW(CheckExceptionMsg([](){ParseStateResponse(1, 1, {1, 0, 0, 1, 1, 2, 21});}, "Bad CRC"), TSerialDeviceTransientErrorException);
    ASSERT_THROW(CheckExceptionMsg([](){ParseStateResponse(1, 1, {2, 0, 0, 1, 1, 2, 22});}, "Bad header"), TSerialDeviceTransientErrorException);
    ASSERT_THROW(CheckExceptionMsg([](){ParseStateResponse(1, 1, {1, 0, 0, 1, 2, 2, 27});}, "Bad address"), TSerialDeviceTransientErrorException);
}
