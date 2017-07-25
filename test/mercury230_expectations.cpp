#include "mercury230_expectations.h"


void TMercury230Expectations::EnqueueMercury230SessionSetupResponse()
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

void TMercury230Expectations::EnqueueMercury230AccessLevel2SessionSetupResponse()
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

void TMercury230Expectations::EnqueueMercury230EnergyResponse1()
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

void TMercury230Expectations::EnqueueMercury230EnergyResponse2()
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

void TMercury230Expectations::EnqueueMercury230PerPhaseEnergyResponse()
{
    Expector()->Expect(
        {
            0x00, // unit id (group)
            0x05, // op
            0x60, // addr
            0x00, // addr
            0x38, // crc
            0x25  // crc
        },
        {
            // Read response
            0x00, // unit id (group)
            0x30, // Phase A
            0x00, // Phase A
            0x29, // Phase A
            0xc5, // Phase A
            0x30, // Phase B
            0x00, // Phase B
            0x29, // Phase B
            0x00, // Phase B
            0x04, // Phase C
            0x00, // Phase C
            0x9d, // Phase C
            0x95, // Phase C
            0x50, // crc
            0x99  // crc
        }, __func__);    
}

void TMercury230Expectations::EnqueueMercury230U1Response()
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

void TMercury230Expectations::EnqueueMercury230U2Response()
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

void TMercury230Expectations::EnqueueMercury230U3Response()
{
    Expector()->Expect(
        {
            0x00, // unit id (group)
            0x08, // op
            0x11, // addr
            0x13, // addr
            0xcc, // crc
            0x7b  // crc
        },
        {
            0x00, // unit id (group)
            0x00, // U2
            0xe5, // U3
            0xc4, // U3
            0x4b, // crc
            0x27  // crc
        }, __func__);
}

void TMercury230Expectations::EnqueueMercury230I1Response()
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

void TMercury230Expectations::EnqueueMercury230I2Response()
{
    Expector()->Expect(
        {
            0x00, // unit id (group)
            0x08, // op
            0x11, // addr
            0x22, // addr
            0x0d, // crc
            0xaf  // crc
        },
        {
            0x00, // unit id (group)
            0x00, // I2
            0x60, // I2
            0x00, // I2
            0x28, // crc
            0x24  // crc
        }, __func__);
}

void TMercury230Expectations::EnqueueMercury230I3Response()
{
    Expector()->Expect(
        {
            0x00, // unit id (group)
            0x08, // op
            0x11, // addr
            0x23, // addr
            0xcc, // crc
            0x6f  // crc
        },
        {
            0x00, // unit id (group)
            0x00, // I3
            0x66, // I3
            0x00, // I3
            0x2b, // crc
            0x84  // crc
        }, __func__);
}

void TMercury230Expectations::EnqueueMercury230FrequencyResponse()
{
	Expector()->Expect(
		{
			0x00, // unit id (group)
			0x08, // op
			0x11, // addr
			0x40, // addr
			0x8c, // crc
			0x46  // crc
		},
		{
			0x00, // unit id (group)
			0x00, // Freq
			0x90, // Freq
			0x00, // Freq
			0x6c, // crc
			0x24  // crc
		}, __func__);
}

void TMercury230Expectations::EnqueueMercury230KU1Response()
{
	Expector()->Expect(
		{
			0x00, // unit id (group)
			0x08, // op
			0x11, // addr
			0x61, // addr
			0x4c, // crc
			0x5e  // crc
		},
		{
			0x00, // unit id (group)
			0x01, // KU1
			0x90, // KU1
			0x70, // crc
			0x3c  // crc
		}, __func__);
}

void TMercury230Expectations::EnqueueMercury230KU2Response()
{
	Expector()->Expect(
		{
			0x00, // unit id (group)
			0x08, // op
			0x11, // addr
			0x62, // addr
			0x0c, // crc
			0x5f  // crc
		},
		{
			0x00, // unit id (group)
			0x01, // KU2
			0xfe, // KU2
			0xf1, // crc
			0xd0  // crc
		}, __func__);
}

void TMercury230Expectations::EnqueueMercury230KU3Response()
{
	Expector()->Expect(
		{
			0x00, // unit id (group)
			0x08, // op
			0x11, // addr
			0x63, // addr
			0xcd, // crc
			0x9f  // crc
		},
		{
			0x00, // unit id (group)
			0x0c, // KU3
			0x88, // KU3
			0x74, // crc
			0xa6  // crc
		}, __func__);
}

void TMercury230Expectations::EnqueueMercury230TempResponse()
{
    Expector()->Expect(
        {
            0x00, // unit id (group)
            0x08, // op
            0x11, // addr
            0x70, // addr
            0x8c, // crc
            0x52  // crc
        },
        {
            0x00, // unit id (group)
            0x00, // temp
            0x18, // temp
            0x71, // crc
            0xca  // crc
        }, __func__);
}

void TMercury230Expectations::EnqueueMercury230PResponse()
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


void TMercury230Expectations::EnqueueMercury230P1Response()
{
    Expector()->Expect(
        {
            0x00, // unit id (group)
            0x08, // op
            0x11, // addr
            0x01, // addr
            0x4c, // crc
            0x76  // crc
        },
        {
            0x00, // unit id (group)
            0xC8, // 2 bits direction (both active  and reactive negative) + 6 bits P value
            0x87, // P value, 3rd byte
            0x70, // P value, 2nd byte
            0xe3, // crc
            0xce  // crc
        }, __func__);
}

void TMercury230Expectations::EnqueueMercury230P2Response()
{
    Expector()->Expect(
        {
            0x00, // unit id (group)
            0x08, // op
            0x11, // addr
            0x02, // addr
            0x0c, // crc
            0x77  // crc
        },
        {
            0x00, // unit id (group)
            0xC8, // 2 bits direction (both active  and reactive negative) + 6 bits P value
            0x87, // P value, 3rd byte
            0x76, // P value, 2nd byte
            0x63, // crc
            0xcc  // crc
        }, __func__);
}

void TMercury230Expectations::EnqueueMercury230P3Response()
{
    Expector()->Expect(
        {
            0x00, // unit id (group)
            0x08, // op
            0x11, // addr
            0x03, // addr
            0xcd, // crc
            0xb7  // crc
        },
        {
            0x00, // unit id (group)
            0xC8, // 2 bits direction (both active  and reactive negative) + 6 bits P value
            0x87, // P value, 3rd byte
            0xbb, // P value, 2nd byte
            0xa2, // crc
            0x59  // crc
        }, __func__);
}

void TMercury230Expectations::EnqueueMercury230PFResponse()
{
	Expector()->Expect(
		{
			0x00, // unit id (group)
			0x08, // op
			0x11, // addr
			0x30, // addr
			0x8d, // crc
			0xa2  // crc
		},
		{
			0x00, // unit id (group)
			0x88, // 2 bits direction (active negative) + 6 bits Q value
			0x87, // PF value, 3rd byte
			0x90, // PF value, 2nd byte
			0xe3, // crc
			0x92  // crc
		}, __func__);
}

void TMercury230Expectations::EnqueueMercury230PF1Response()
{
	Expector()->Expect(
		{
			0x00, // unit id (group)
			0x08, // op
			0x11, // addr
			0x31, // addr
			0x4c, // crc
			0x62  // crc
		},
		{
			0x00, // unit id (group)
			0x88, // 2 bits direction (active negative) + 6 bits Q value
			0x87, // PF value, 3rd byte
			0x88, // PF value, 2nd byte
			0xe3, // crc
			0x98  // crc
		}, __func__);
}

void TMercury230Expectations::EnqueueMercury230PF2Response()
{
	Expector()->Expect(
		{
			0x00, // unit id (group)
			0x08, // op
			0x11, // addr
			0x32, // addr
			0x0c, // crc
			0x63  // crc
		},
		{
			0x00, // unit id (group)
			0x88, // 2 bits direction (active negative) + 6 bits Q value
			0x87, // PF value, 3rd byte
			0x58, // PF value, 2nd byte
			0xe2, // crc
			0x04  // crc
		}, __func__);
}

void TMercury230Expectations::EnqueueMercury230PF3Response()
{
	Expector()->Expect(
		{
			0x00, // unit id (group)
			0x08, // op
			0x11, // addr
			0x33, // addr
			0xcd, // crc
			0xa3  // crc
		},
		{
			0x00, // unit id (group)
			0x88, // 2 bits direction (active negative) + 6 bits Q value
			0x66, // PF value, 3rd byte
			0x58, // PF value, 2nd byte
			0xaa, // crc
			0x54  // crc
		}, __func__);
}

void TMercury230Expectations::EnqueueMercury230QResponse()
{
    Expector()->Expect(
        {
            0x00, // unit id (group)
            0x08, // op
            0x11, // addr
            0x04, // addr
            0x8c, // crc
            0x75  // crc
        },
        {
            0x00, // unit id (group)
            0xC8, // 2 bits direction (both active and reactive negative) + 6 bits Q value
            0x87, // Q value, 3rd byte
            0x70, // Q value, 2nd byte
            0xe3, // crc
            0xce  // crc
        }, __func__);
}

void TMercury230Expectations::EnqueueMercury230Q1Response()
{
    Expector()->Expect(
        {
            0x00, // unit id (group)
            0x08, // op
            0x11, // addr
            0x05, // addr
            0x4d, // crc
            0xb5  // crc
        },
        {
            0x00, // unit id (group)
            0xC8, // 2 bits direction (both active and reactive negative) + 6 bits Q value
            0x87, // Q value, 3rd byte
            0x70, // Q value, 2nd byte
            0xe3, // crc
            0xce  // crc
        }, __func__);
}

void TMercury230Expectations::EnqueueMercury230Q2Response()
{
    Expector()->Expect(
        {
            0x00, // unit id (group)
            0x08, // op
            0x11, // addr
            0x06, // addr
            0x0d, // crc
            0xb4  // crc
        },
        {
            0x00, // unit id (group)
            0x88, // 2 bits direction (active negative) + 6 bits Q value
            0x87, // Q value, 3rd byte
            0x70, // Q value, 2nd byte
            0xe2, // crc
            0x1a  // crc
        }, __func__);
}

void TMercury230Expectations::EnqueueMercury230Q3Response()
{
    Expector()->Expect(
        {
            0x00, // unit id (group)
            0x08, // op
            0x11, // addr
            0x07, // addr
            0xcc, // crc
            0x74  // crc
        },
        {
            0x00, // unit id (group)
            0x88, // 2 bits direction (active negative) + 6 bits Q value
            0x87, // Q value, 3rd byte
            0x70, // Q value, 2nd byte
            0xe2, // crc
            0x1a  // crc
        }, __func__);
}

void TMercury230Expectations::EnqueueMercury230NoSessionResponse()
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

void TMercury230Expectations::EnqueueMercury230InternalMeterErrorResponse()
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
