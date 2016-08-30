#include "em_expectations.h"

void TEMDeviceExpectations::EnqueueMilurIgnoredPacketWorkaround()
{
    /* Milur 104 ignores the request after receiving any packet
    with length of 8 bytes. The last answer of the previously polled device
    could make Milur 104 unresponsive. To make sure the meter is operational,
    we send dummy packet (0xFF in this case) which will restore normal meter operation. */

    Expector()->Expect(
        {
            0xff, // dummy packet
        },
        { //no response
        }, __func__);
}

void TEMDeviceExpectations::EnqueueMilurSessionSetupResponse()
{
    Expector()->Expect(
        {
            0xff, // unit id
            0x08, // op
            0x01, // access level
            0xff, // pw
            0xff, // pw
            0xff, // pw
            0xff, // pw
            0xff, // pw
            0xff, // pw
            0x5f, // crc
            0xed  // crc
        },
        {
            // Session setup response
            0xff, // unit id
            0x08, // op
            0x01, // result
            0x87, // crc
            0xf0  // crc
        }, __func__);
}

void TEMDeviceExpectations::EnqueueMilurAccessLevel2SessionSetupResponse()
{
    Expector()->Expect(
        {
            0xff, // unit id
            0x08, // op
            0x02, // access level
            0x02, // pw
            0x03, // pw
            0x04, // pw
            0x05, // pw
            0x06, // pw
            0x07, // pw
            0x7b, // crc
            0xd3  // crc
        },
        {
            // Session setup response
            // Different access level, thus not using EnqueueMilurSessionSetupResponse() here
            0xff, // unit id
            0x08, // op
            0x02, // result
            0xc7, // crc
            0xf1  // crc
        }, __func__);
}

void TEMDeviceExpectations::EnqueueMilurPhaseCVoltageResponse()
{
    Expector()->Expect(
        {
            0xff, // unit id
            0x01, // op
            0x66, // register
            0xc0, // crc
            0x4a  // crc
        },
        {
            // Read response
            0xff, // unit id
            0x01, // op
            0x66, // register
            0x03, // len
            0x6f, // data 1
            0x94, // data 2
            0x03, // data 3
            0x03, // crc
            0x4e  // crc
        }, __func__);
}

void TEMDeviceExpectations::EnqueueMilurTotalConsumptionResponse()
{
    Expector()->Expect(
        {
            0xff, // unit id
            0x01, // op
            0x76, // register
            0xc1, // crc
            0x86  // crc
        },
        {
            // Read response
            0xff, // unit id
            0x01, // op
            0x76, // register
            0x04, // len
            0x44, // data 1
            0x11, // data 2
            0x10, // data 3
            0x00, // data 4
            0xac, // crc
            0x6c  // crc
        }, __func__);
}

void TEMDeviceExpectations::EnqueueMilurNoSessionResponse()
{
    Expector()->Expect(
        {
            0xff, // unit id
            0x01, // op
            0x66, // register
            0xc0, // crc
            0x4a  // crc
        },
        {
            // Session setup response
            0xff, // unit id
            0x81, // op + exception flag
            0x08, // error code
            0x00, // service data
            0x67, // crc
            0xd8  // crc
        }, __func__);
}

void TEMDeviceExpectations::EnqueueMilurExceptionResponse()
{
    Expector()->Expect(
        {
            0xff, // unit id
            0x01, // op
            0x66, // register
            0xc0, // crc
            0x4a  // crc
        },
        {
            // Session setup response
            0xff, // unit id
            0x81, // op + exception flag
            0x07, // error code
            0x00, // service data
            0x62, // crc
            0x28  // crc
        }, __func__);
}

void TEMDeviceExpectations::EnqueueMercury230SessionSetupResponse()
{
    Expector()->Expect(
        {
            0x00, // unit id (group)
            0x01, // op
            0x01, // access level
            0x01, // pw
            0x01, // pw
            0x01, // pw
            0x01, // pw
            0x01, // pw
            0x01, // pw
            0x77, // crc
            0x81  // crc
        },
        {
            // Session setup response
            0x00, // unit id (group)
            0x00, // state
            0x01, // crc
            0xb0  // crc
        }, __func__);
}

void TEMDeviceExpectations::EnqueueMercury230AccessLevel2SessionSetupResponse()
{
    Expector()->Expect(
        {
            0x00, // unit id (group)
            0x01, // op
            0x02, // access level
            0x12, // pw
            0x13, // pw
            0x14, // pw
            0x15, // pw
            0x16, // pw
            0x17, // pw
            0x34, // crc
            0x17  // crc
        },
        {
            // Session setup response
            0x00, // unit id (group)
            0x00, // state
            0x01, // crc
            0xb0  // crc
        }, __func__);
}

void TEMDeviceExpectations::EnqueueMercury230EnergyResponse1()
{
    Expector()->Expect(
        {
            0x00, // unit id (group)
            0x05, // op
            0x00, // addr
            0x00, // addr
            0x10, // crc
            0x25  // crc
        },
        {
            // Read response
            0x00, // unit id (group)
            0x30, // A+
            0x00, // A+
            0x28, // A+
            0xc5, // A+
            0xff, // A-
            0xff, // A-
            0xff, // A-
            0xff, // A-
            0x04, // R+
            0x00, // R+
            0x9c, // R+
            0x95, // R+
            0xff, // R-
            0xff, // R-
            0xff, // R-
            0xff, // R-
            0x44, // crc
            0xab  // crc
        }, __func__);
}

void TEMDeviceExpectations::EnqueueMercury230EnergyResponse2()
{
    Expector()->Expect(
        {
            0x00, // unit id (group)
            0x05, // op
            0x00, // addr
            0x00, // addr
            0x10, // crc
            0x25  // crc
        },
        {
            // Read response
            0x00, // unit id (group)
            0x30, // A+
            0x00, // A+
            0x29, // A+
            0xc5, // A+
            0xff, // A-
            0xff, // A-
            0xff, // A-
            0xff, // A-
            0x04, // R+
            0x00, // R+
            0x9d, // R+
            0x95, // R+
            0xff, // R-
            0xff, // R-
            0xff, // R-
            0xff, // R-
            0x45, // crc
            0xbb  // crc
        }, __func__);
}

void TEMDeviceExpectations::EnqueueMercury230U1Response()
{
    Expector()->Expect(
        {
            0x00, // unit id (group)
            0x08, // op
            0x11, // addr
            0x11, // addr
            0x4d, // crc 
            0xba  // crc
        },
        {
            0x00, // unit id (group)
            0x00, // U1
            0x40, // U1
            0x5e, // U1
            0xb0, // crc
            0x1c  // crc
        }, __func__);
}

void TEMDeviceExpectations::EnqueueMercury230I1Response()
{
    Expector()->Expect(
        {
            0x00, // unit id (group)
            0x08, // op
            0x11, // addr
            0x21, // addr
            0x4d, // crc
            0xae  // crc
        },
        {
            0x00, // unit id (group)
            0x00, // I1
            0x45, // I1
            0x00, // I1
            0x32, // crc
            0xb4  // crc
        }, __func__);
}

void TEMDeviceExpectations::EnqueueMercury230U2Response()
{
    Expector()->Expect(
        {
            0x00, // unit id (group)
            0x08, // op
            0x11, // addr
            0x12, // addr
            0x0d, // crc
            0xbb  // crc
        },
        {
            0x00, // unit id (group)
            0x00, // U2
            0xeb, // U2
            0x5d, // U2
            0x8f, // crc
            0x2d  // crc
        }, __func__);
}

void TEMDeviceExpectations::EnqueueMercury230PResponse()
{
    Expector()->Expect(
        {
            0x00, // unit id (group)
            0x08, // op
            0x11, // addr
            0x00, // addr
            0x8d, // crc
            0xb6  // crc
        },
        {
            0x00, // unit id (group)
            0x48, // 2 bits direction + 6 bits P value 
            0x87, // P value, 3rd byte
            0x70, // P value, 2nd byte
            0xe2, // crc
            0x26  // crc
        }, __func__);
}

void TEMDeviceExpectations::EnqueueMercury230NoSessionResponse()
{
    Expector()->Expect(
        {
            0x00, // unit id (group)
            0x08, // op
            0x11, // addr
            0x12, // addr
            0x0d, // crc
            0xbb  // crc
        },
        {
            0x00, // unit id (group)
            0x05, // error 5 = no session
            0xc1, // crc
            0xb3  // crc
        }, __func__);
}

void TEMDeviceExpectations::EnqueueMercury230InternalMeterErrorResponse()
{
    Expector()->Expect(
        {
            0x00, // unit id
            0x08, // op
            0x11, // addr
            0x12, // addr
            0x0d, // crc
            0xbb  // crc
        },
        {
            0x00, // unit id (group)
            0x02, // error 2 = internal meter error
            0x80, // crc
            0x71  // crc
        }, __func__);
}
