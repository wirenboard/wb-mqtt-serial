#include "milur_expectations.h"

void TMilurExpectations::EnqueueMilurIgnoredPacketWorkaround()
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

void TMilurExpectations::EnqueueMilurSessionSetupResponse()
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

void TMilurExpectations::EnqueueMilur32SessionSetupResponse()
{
    Expector()->Expect(
        {
            0x0C, // unit id
            0xC3, // unit id
            0x00, // unit id
            0x00, // unit id
            0x08, // op
            0x01, // access level
            0xff, // pw
            0xff, // pw
            0xff, // pw
            0xff, // pw
            0xff, // pw
            0xff, // pw
            0x97, // crc
            0xBC  // crc
        },
        {
            // Session setup response
            0x0C, // unit id
            0xC3, // unit id
            0x00, // unit id
            0x00, // unit id
            0x08, // op
            0x01, // result
            0x82, // crc
            0xC6  // crc
        }, __func__);
}

void TMilurExpectations::EnqueueMilurAccessLevel2SessionSetupResponse()
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

void TMilurExpectations::EnqueueMilurPhaseAVoltageResponse()
{
	Expector()->Expect(
		{
			0xff, // unit id
			0x01, // op
			100, // register
			0x41, // crc
			0x8b  // crc
		},
		{
			// Read response
			0xff, // unit id
			0x01, // op
			100, // register
			0x03, // len
			0x6f, // data 1
			0x94, // data 2
			0x03, // data 3
			0x7a, // crc
			0x8e  // crc
		}, __func__);
}

void TMilurExpectations::EnqueueMilurPhaseBVoltageResponse()
{
	Expector()->Expect(
		{
			0xff, // unit id
			0x01, // op
			101, // register
			0x80, // crc
			0x4b  // crc
		},
		{
			// Read response
			0xff, // unit id
			0x01, // op
			101, // register
			0x03, // len
			0x6f, // data 1
			0x94, // data 2
			0x03, // data 3
			0x47, // crc
			0x4e  // crc
		}, __func__);
}

void TMilurExpectations::EnqueueMilurPhaseCVoltageResponse()
{
    Expector()->Expect(
        {
            0xff, // unit id
            0x01, // op
            102, // register
            0xc0, // crc
            0x4a  // crc
        },
        {
            // Read response
            0xff, // unit id
            0x01, // op
            102, // register
            0x03, // len
            0x6f, // data 1
            0x94, // data 2
            0x03, // data 3
            0x03, // crc
            0x4e  // crc
        }, __func__);
}

void TMilurExpectations::EnqueueMilurPhaseACurrentResponse() {
    Expector()->Expect(
        {
            0xff, // unit id
            0x01, // op
            103, // register
            0x01, // crc
            0x8a  // crc
        },
        {
            // Read response
            0xff, // unit id
            0x01, // op
            103, // register
            0x03, // len
            0xf0, // data 1
            0xd8, // data 2
            0xff, // data 3
            0x3a, // crc
            0x21  // crc
        }, __func__);
}

void TMilurExpectations::EnqueueMilurPhaseBCurrentResponse()
{
    Expector()->Expect(
        {
            0xff, // unit id
            0x01, // op
            104, // register
            0x41, // crc
            0x8e  // crc
        },
        {
            // Read response
            0xff, // unit id
            0x01, // op
            104, // register
            0x03, // len
            0xf0, // data 1
            0xd8, // data 2
            0xff, // data 3
            0x6e, // crc
            0x20  // crc
        }, __func__);
}

void TMilurExpectations::EnqueueMilurPhaseCCurrentResponse()
{
    Expector()->Expect(
        {
            0xff, // unit id
            0x01, // op
            105, // register
            0x80, // crc
            0x4e  // crc
        },
        {
            // Read response
            0xff, // unit id
            0x01, // op
            105, // register
            0x03, // len
            0xf0, // data 1
            0xd8, // data 2
            0xff, // data 3
            0x53, // crc
            0xe0  // crc
        }, __func__);
}

void TMilurExpectations::EnqueueMilurPhaseAActivePowerResponse()
{
    Expector()->Expect(
        {
            0xff, // unit id
            0x01, // op
            106, // register
            0xc0, // crc
            0x4f  // crc
        },
        {
            // Read response
            0xff, // unit id
            0x01, // op
            106, // register
            0x04, // len
            0xf0, // data 1
            0xd8, // data 2
            0xff, // data 3
			0xff, // data 4
            0x55, // crc
            0x8e  // crc
        }, __func__);
}

void TMilurExpectations::EnqueueMilurPhaseBActivePowerResponse()
{
    Expector()->Expect(
        {
            0xff, // unit id
            0x01, // op
            107, // register
            0x01, // crc
            0x8f  // crc
        },
        {
            // Read response
            0xff, // unit id
            0x01, // op
            107, // register
            0x04, // len
            0xf0, // data 1
            0xd8, // data 2
            0xff, // data 3
			0xff, // data 4
            0x54, // crc
            0x5f  // crc
        }, __func__);
}

void TMilurExpectations::EnqueueMilurPhaseCActivePowerResponse()
{
    Expector()->Expect(
        {
            0xff, // unit id
            0x01, // op
            108, // register
            0x40, // crc
            0x4d  // crc
        },
        {
            // Read response
            0xff, // unit id
            0x01, // op
            108, // register
            0x04, // len
            0xf0, // data 1
            0xd8, // data 2
            0xff, // data 3
			0xff, // data 4
            0x55, // crc
            0xe8  // crc
        }, __func__);
}

void TMilurExpectations::EnqueueMilurTotalActivePowerResponse()
{
    Expector()->Expect(
        {
            0xff, // unit id
            0x01, // op
            109, // register
            0x81, // crc
            0x8d  // crc
        },
        {
            // Read response
            0xff, // unit id
            0x01, // op
            109, // register
            0x04, // len
            0xf0, // data 1
            0xd8, // data 2
            0xff, // data 3
			0xff, // data 4
            0x54, // crc
            0x39  // crc
        }, __func__);
}

void TMilurExpectations::EnqueueMilurPhaseAReactivePowerResponse()
{
    Expector()->Expect(
        {
            0xff, // unit id
            0x01, // op
            110, // register
            0xc1, // crc
            0x8c  // crc
        },
        {
            // Read response
            0xff, // unit id
            0x01, // op
            110, // register
            0x04, // len
            0xf0, // data 1
            0xd8, // data 2
            0xff, // data 3
			0xff, // data 4
            0x54, // crc
            0x0a  // crc
        }, __func__);
}

void TMilurExpectations::EnqueueMilurPhaseBReactivePowerResponse()
{
    Expector()->Expect(
        {
            0xff, // unit id
            0x01, // op
            111, // register
            0x00, // crc
            0x4c  // crc
        },
        {
            // Read response
            0xff, // unit id
            0x01, // op
            111, // register
            0x04, // len
            0xf0, // data 1
            0xd8, // data 2
            0xff, // data 3
			0xff, // data 4
            0x55, // crc
            0xdb  // crc
        }, __func__);
}

void TMilurExpectations::EnqueueMilurPhaseCReactivePowerResponse()
{
    Expector()->Expect(
        {
            0xff, // unit id
            0x01, // op
            112, // register
            0x41, // crc
            0x84  // crc
        },
        {
            // Read response
            0xff, // unit id
            0x01, // op
            112, // register
            0x04, // len
            0xf0, // data 1
            0xd8, // data 2
            0xff, // data 3
			0xff, // data 4
            0x57, // crc
            0xb4  // crc
        }, __func__);
}

void TMilurExpectations::EnqueueMilurTotalReactivePowerResponse()
{
    Expector()->Expect(
        {
            0xff, // unit id
            0x01, // op
            113, // register
            0x80, // crc
            0x44  // crc
        },
        {
            // Read response
            0xff, // unit id
            0x01, // op
            113, // register
            0x04, // len
            0xf0, // data 1
            0xd8, // data 2
            0xff, // data 3
			0xff, // data 4
            0x56, // crc
            0x65  // crc
        }, __func__);
}

void TMilurExpectations::EnqueueMilurTotalReactiveEnergyResponse()
{
    Expector()->Expect(
        {
            0xff, // unit id
            0x01, // op
            127, // register
            0x01, // crc
            0x80  // crc
        },
        {
            // Read response
            0xff, // unit id
            0x01, // op
            127, // register
            0x04, // len
            0x87, // data 1
            0x65, // data 2
            0x43, // data 3
			0x21, // data 4
            0x2c, // crc
            0x43  // crc
        }, __func__);
}

void TMilurExpectations::EnqueueMilurFrequencyResponse()
{
    Expector()->Expect(
        {
            0xff, // unit id
            0x01, // op
            0x09, // register
            0x80, // crc
            0x66  // crc
        },
        {
            // Read response
            0xff, // unit id
            0x01, // op
            0x09, // register
            0x02, // len
            0xA0, // data 1
            0xC3, // data 2
            0xB3, // crc
            0xD9  // crc
        }, __func__);
}

void TMilurExpectations::EnqueueMilurTotalConsumptionResponse()
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

void TMilurExpectations::EnqueueMilur32TotalConsumptionResponse()
{
    Expector()->Expect(
        {
            0x0C, // unit id
            0xC3,
            0x00,
            0x00,
            0x01, // op
            0x76, // register
            0xC4, // crc
            0xB0  // crc
        },
        {
            // Read response
            0x0C, // unit id
            0xC3,
            0x00,
            0x00,
            0x01, // op
            0x76, // register
            0x04, // len
            0x44, // data 1
            0x11, // data 2
            0x10, // data 3
            0x00, // data 4
            0x6F, // crc
            0xE4  // crc
        }, __func__);
}

void TMilurExpectations::EnqueueMilurNoSessionResponse()
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

void TMilurExpectations::EnqueueMilurExceptionResponse()
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
