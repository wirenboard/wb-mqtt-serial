#include "devices/curtains/dooya_device.h"
#include "test_utils.h"

using namespace Dooya;

TEST(TDooyaTest, MakeRequest)
{
    ASSERT_TRUE(ArraysMatch(MakeRequest(1,      {3, 1}),       {0x55, 1, 0, 3, 1, 232,  192}));
    ASSERT_TRUE(ArraysMatch(MakeRequest(1,      {1, 2, 1}),    {0x55, 1, 0, 1, 2, 1,    160,  190}));
    ASSERT_TRUE(ArraysMatch(MakeRequest(0x0101, {3, 4, 0x17}), {0x55, 1, 1, 3, 4, 0x17, 0x82, 0xEC}));

    // Set and get position responses
    ASSERT_TRUE(ArraysMatch(MakeRequest(1, {3, 4, 30}), {0x55, 1, 0, 3, 4, 30, 67,  22}));
    ASSERT_TRUE(ArraysMatch(MakeRequest(1, {1, 1, 30}), {0x55, 1, 0, 1, 1, 30, 225, 134}));
}

TEST(TDooyaTest, ParsePositionResponse)
{
    ASSERT_EQ(ParsePositionResponse(1, 1, 1, {0x55, 1, 0, 1, 1, 30, 225, 134}), 30);

    ASSERT_THROW(CheckExceptionMsg([](){ParsePositionResponse(1, 1, 1, {0x55, 1, 0, 1, 1, 30,   226, 134});}, "Bad CRC"), TSerialDeviceTransientErrorException);
    ASSERT_THROW(CheckExceptionMsg([](){ParsePositionResponse(1, 1, 1, {0x56, 1, 0, 1, 1, 30,   225, 134});}, "Bad header"), TSerialDeviceTransientErrorException);
    ASSERT_THROW(CheckExceptionMsg([](){ParsePositionResponse(1, 1, 1, {0x55, 2, 0, 1, 1, 30,   165, 134});}, "Bad address"), TSerialDeviceTransientErrorException);
    ASSERT_THROW(CheckExceptionMsg([](){ParsePositionResponse(1, 1, 1, {0x55, 1, 0, 2, 1, 30,   17 , 134});}, "Bad response"), TSerialDeviceTransientErrorException);
    ASSERT_THROW(CheckExceptionMsg([](){ParsePositionResponse(1, 1, 1, {0x55, 1, 0, 1, 2, 30,   225, 118});}, "Bad response"), TSerialDeviceTransientErrorException);
    ASSERT_THROW(CheckExceptionMsg([](){ParsePositionResponse(1, 1, 1, {0x55, 1, 0, 1, 1, 0xFF, 33,  206});}, "No limit setting"), TSerialDeviceTransientErrorException);
    ASSERT_THROW(CheckExceptionMsg([](){ParsePositionResponse(1, 1, 1, {0x55, 1, 0, 1, 1, 101,  161, 165});}, "Unknown position"), TSerialDeviceTransientErrorException);
}
