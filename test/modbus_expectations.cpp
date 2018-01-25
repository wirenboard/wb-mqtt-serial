#include "modbus_expectations.h"


// read 2 coils
void TModbusExpectations::EnqueueCoilReadResponse(uint8_t exception)
{
    Expector()->Expect(
    WrapPDU({
        0x01,   //function code
        0x00,   //starting address Hi
        0,      //starting address Lo
        0x00,   //quantity Hi
        0x02,   //quantity Lo
    }),
    WrapPDU(exception == 0 ? std::vector<int> {
        0x01,   //function code
        0x01,   //byte count
        1<<1,   //coils status
    } : std::vector<int> {
        0x81,   //function code + 80
        exception
    }), __func__);
}

// read holding registers [4 - 9], 18
void TModbusExpectations::EnqueueHoldingPackReadResponse(uint8_t exception)
{
    Expector()->Expect(
    WrapPDU({
        0x03,   //function code
        0x00,   //starting address Hi
        4,      //starting address Lo
        0x00,   //quantity Hi
        0x06,   //quantity Lo
    }),
    WrapPDU(exception == 0 ? std::vector<int> {
        0x03,   //function code
        0x0c,   //byte count
        0x00,   //data Hi 4
        0x0a,   //data Lo 4
        0x00,   //data Hi 5
        0x14,   //data Lo 5
        0x00,   //data Hi 6
        0x1E,   //data Lo 6
        0x00,   //data Hi 7
        0x01,   //data Lo 7
        0x00,   //data Hi 8
        0x02,   //data Lo 8
        0x00,   //data Hi 9
        0x03,   //data Lo 9
    } : std::vector<int> {
        0x83,   //function code + 80
        exception
    }), __func__);

    // Voltage register
    Expector()->Expect(
    WrapPDU({
        0x03,   //function code
        0x00,   //starting address Hi
        18,     //starting address Lo
        0x00,   //quantity Hi
        0x01,   //quantity Lo
    }),
    WrapPDU(exception == 0 ? std::vector<int> {
        0x03,   //function code
        0x02,   //byte count
        0x00,   //data Hi 18
        0x04,   //data Lo 18
    } : std::vector<int> {
        0x83,   //function code + 80
        exception
    }), __func__);
}

// read holding registers [4 - 18]
void TModbusExpectations::EnqueueHoldingPackHoles10ReadResponse(uint8_t exception)
{
    Expector()->Expect(
    WrapPDU({
        0x03,   //function code
        0x00,   //starting address Hi
        4,      //starting address Lo
        0x00,   //quantity Hi
        0x0F,   //quantity Lo
    }),
    WrapPDU(exception == 0 ? std::vector<int> {
        0x03,   //function code
        0x1E,   //byte count
        0x00,   //data Hi 4
        0x0a,   //data Lo 4
        0x00,   //data Hi 5
        0x14,   //data Lo 5
        0x00,   //data Hi 6
        0x1E,   //data Lo 6
        0x00,   //data Hi 7
        0x01,   //data Lo 7
        0x00,   //data Hi 8
        0x02,   //data Lo 8
        0x00,   //data Hi 9
        0x03,   //data Lo 9
        0x00,   //data Hi (hole)
        0x00,   //data Lo (hole)
        0x00,   //data Hi (hole)
        0x00,   //data Lo (hole)
        0x00,   //data Hi (hole)
        0x00,   //data Lo (hole)
        0x00,   //data Hi (hole)
        0x00,   //data Lo (hole)
        0x00,   //data Hi (hole)
        0x00,   //data Lo (hole)
        0x00,   //data Hi (hole)
        0x00,   //data Lo (hole)
        0x00,   //data Hi (hole)
        0x00,   //data Lo (hole)
        0x00,   //data Hi (hole)
        0x00,   //data Lo (hole)
        0x00,   //data Hi 18
        0x04,   //data Lo 18
    } : std::vector<int> {
        0x83,   //function code + 80
        exception
    }), __func__);
}

// read holding registers [4 - 6] , [7 - 9], 18
void TModbusExpectations::EnqueueHoldingPackMax3ReadResponse(uint8_t exception)
{
    // RGB registers
    Expector()->Expect(
    WrapPDU({
        0x03,   //function code
        0x00,   //starting address Hi
        4,      //starting address Lo
        0x00,   //quantity Hi
        0x03,   //quantity Lo
    }),
    WrapPDU(exception == 0 ? std::vector<int> {
        0x03,   //function code
        0x06,   //byte count
        0x00,   //data Hi 4
        0x0a,   //data Lo 4
        0x00,   //data Hi 5
        0x14,   //data Lo 5
        0x00,   //data Hi 6
        0x1E,   //data Lo 6
    } : std::vector<int> {
        0x83,   //function code + 80
        exception
    }), __func__);

    // 3 holding registers after RGB
    Expector()->Expect(
    WrapPDU({
        0x03,   //function code
        0x00,   //starting address Hi
        7,      //starting address Lo
        0x00,   //quantity Hi
        0x03,   //quantity Lo
    }),
    WrapPDU(exception == 0 ? std::vector<int> {
        0x03,   //function code
        0x06,   //byte count
        0x00,   //data Hi 7
        0x01,   //data Lo 7
        0x00,   //data Hi 8
        0x02,   //data Lo 8
        0x00,   //data Hi 9
        0x03,   //data Lo 9
    } : std::vector<int> {
        0x83,   //function code + 80
        exception
    }), __func__);

    // Voltage register
    Expector()->Expect(
    WrapPDU({
        0x03,   //function code
        0x00,   //starting address Hi
        18,     //starting address Lo
        0x00,   //quantity Hi
        0x01,   //quantity Lo
    }),
    WrapPDU(exception == 0 ? std::vector<int> {
        0x03,   //function code
        0x02,   //byte count
        0x00,   //data Hi 18
        0x04,   //data Lo 18
    } : std::vector<int> {
        0x83,   //function code + 80
        exception
    }), __func__);
}

// read holding registers 4, 5, 6, 7, 8, 9
void TModbusExpectations::EnqueueHoldingPackDisableReadResponse(int stage)
{
    auto expect_with_error = [=](uint8_t addr) {
        Expector()->Expect(
        WrapPDU({
            0x03,   // function code
            0x00,   // starting address Hi
            addr,   // starting address Lo
            0x00,   // quantity Hi
            0x01,   // quantity Lo
        }),
        WrapPDU({
            0x83,   // function code + 80
            0x02    // illegal data address
        }), "TModbusExpectations::EnqueueHoldingPackDisableReadResponse");
    };

    switch(stage) {
        case 1:
            // disable holes first
            EnqueueHoldingPackHoles10ReadResponse(0x02);
            break;
        case 2:
            // split to individual registers and disable 18-th register
            EnqueueHoldingPackReadResponse(0x02);
            break;
        case 3:
            // disable registers 4, 5, 6, 7, 8, 9
            expect_with_error(4);  // 4
            expect_with_error(5);  // 5
            expect_with_error(6);  // 6
            expect_with_error(7);  // 7
            expect_with_error(8);  // 8
            expect_with_error(9);  // 9
            break;
        default:
            break;
    }
}

// read 1 discrete
void TModbusExpectations::EnqueueDiscreteReadResponse(uint8_t exception)
{
    Expector()->Expect(
    WrapPDU({
        0x02,   //function code
        0x00,   //starting address Hi
        20,     //starting address Lo
        0x00,   //quantity Hi
        0x01,   //quantity Lo
    }),
    WrapPDU(exception == 0 ? std::vector<int> {
        0x02,   //function code
        0x01,   //byte count
        0x01,   //data
    } : std::vector<int> {
        0x82,   //function code + 80
        exception
    }), __func__);
}

// read 4 holding
void TModbusExpectations::EnqueueHoldingReadS64Response(uint8_t exception)
{
    Expector()->Expect(
    WrapPDU({
        0x03,   //function code
        0x00,   //starting address Hi
        30,     //starting address Lo
        0x00,   //quantity Hi
        0x04,   //quantity Lo
    }),
    WrapPDU(exception == 0 ? std::vector<int> {
        0x03,   //function code
        0x08,   //byte count
        0x01,   //data Hi
        0x02,   //data Lo
        0x03,   //data Hi
        0x04,   //data Lo
        0x05,   //data Hi
        0x06,   //data Lo
        0x07,   //data Hi
        0x08,   //data Lo
    } : std::vector<int> {
        0x83,   //function code + 80
        exception
    }), __func__);
}

// read 2 holding
void TModbusExpectations::EnqueueHoldingReadF32Response(uint8_t exception)
{
    Expector()->Expect(
    WrapPDU({
        0x03,   //function code
        0x00,   //starting address Hi
        50,     //starting address Lo
        0x00,   //quantity Hi
        0x02,   //quantity Lo
    }),
    WrapPDU(exception == 0 ? std::vector<int> {
        0x03,   //function code
        0x04,   //byte count
        0x00,   //data Hi
        0x00,   //data Lo
        0x00,   //data Hi
        0x15    //data Lo
    } : std::vector<int> {
        0x83,   //function code + 80
        exception
    }), __func__);
}

// read 1 holding
void TModbusExpectations::EnqueueHoldingReadU16Response(uint8_t exception)
{
    Expector()->Expect(
    WrapPDU({
        0x03,   //function code
        0x00,   //starting address Hi
        70,     //starting address Lo
        0x00,   //quantity Hi
        0x01,   //quantity Lo
    }),
    WrapPDU(exception == 0 ? std::vector<int> {
        0x03,   //function code
        0x02,   //byte count
        0x00,   //data Hi
        0x15    //data Lo
    } : std::vector<int> {
        0x83,   //function code + 80
        exception
    }), __func__);
}

// read 1 input
void TModbusExpectations::EnqueueInputReadU16Response(uint8_t exception)
{
    Expector()->Expect(
    WrapPDU({
        0x04,   //function code
        0x00,   //starting address Hi
        40,     //starting address Lo
        0x00,   //quantity Hi
        0x01,   //quantity Lo
    }),
    WrapPDU(exception == 0 ? std::vector<int> {
        0x04,   //function code
        0x02,   //byte count
        0x00,   //data Hi
        0x66    //data Lo
    } : std::vector<int> {
        0x84,   //function code + 80
        exception
    }), __func__);
}

// write 1 coil
void TModbusExpectations::EnqueueCoilWriteResponse(uint8_t exception)
{
    Expector()->Expect(
    WrapPDU({
        0x05,   //function code
        0x00,   //starting address Hi
        0x00,   //starting address Lo
        0xFF,   //value Hi
        0x00,   //value Lo
    }),
    WrapPDU(exception == 0 ? std::vector<int> {
        0x05,   //function code
        0x00,   //starting address Hi
        0x00,   //starting address Lo
        0xFF,   //value Hi
        0x00,   //value Lo
    } : std::vector<int> {
        0x85,   //function code + 80
        exception
    }), __func__);
}

// write 1 holding
void TModbusExpectations::EnqueueHoldingWriteU16Response(uint8_t exception)
{
    Expector()->Expect(
    WrapPDU({
        0x06,   //function code
        0x00,   //starting address Hi
        70,     //starting address Lo
        0x0f,   //value Hi
        0x41,   //value Lo
    }),
    WrapPDU(exception == 0 ? std::vector<int> {
        0x06,   //function code
        0x00,   //starting address Hi
        70,     //starting address Lo
        0x0f,   //value Hi
        0x41,   //value Lo
    } : std::vector<int> {
        0x86,   //function code + 80
        exception
    }), __func__);
}

// write 2 coils
void TModbusExpectations::EnqueueCoilWriteMultipleResponse(uint8_t exception)
{
    Expector()->Expect(
    WrapPDU({
        0x0F,   //function code
        0x00,   //starting address Hi
        0x00,   //starting address Lo
        0x00,   //quantity Hi
        0x02,   //quantity Lo
        0x01,   //byte count
        0x01,   //00000001
    }),
    WrapPDU(exception == 0 ? std::vector<int> {
        0x0F,   //function code
        0x00,   //starting address Hi
        0x00,   //starting address Lo
        0x00,   //quantity Hi
        0x02,   //quantity Lo
    } : std::vector<int> {
        0x8F,   //function code + 80
        exception
    }), __func__);
}

// write 4 holding registers
void TModbusExpectations::EnqueueHoldingWriteS64Response(uint8_t exception)
{
    Expector()->Expect(
    WrapPDU({
        0x10,   //function code
        0x00,   //starting address Hi
        30,     //starting address Lo
        0x00,   //quantity Hi
        0x04,   //quantity Lo
        0x08,   //byte count
        0x01,   //data Hi
        0x23,   //data Lo
        0x45,   //data Hi
        0x67,   //data Lo
        0x89,   //data Hi
        0xAB,   //data Lo
        0xCD,   //data Hi
        0xEF,   //data Lo
    }),
    WrapPDU(exception == 0 ? std::vector<int> {
        0x10,   //function code
        0x00,   //starting address Hi
        30,     //starting address Lo
        0x00,   //quantity Hi
        0x04,   //quantity Lo
    } : std::vector<int> {
        0x90,   //function code + 80
        exception
    }), __func__);
}

// write RGB register which consists of 3 holding
void TModbusExpectations::EnqueueRGBWriteResponse(uint8_t exception)
{
    // R
    Expector()->Expect(
    WrapPDU({
        0x06,   //function code
        0x00,   //starting address Hi
        4,      //starting address Lo
        0x00,   //data Hi
        0x0a,   //data Lo
    }),
    WrapPDU(exception == 0 ? std::vector<int> {
        0x06,   //function code
        0x00,   //starting address Hi
        4,      //starting address Lo
        0x00,   //data Hi
        0x0a,   //data Lo
    } : std::vector<int> {
        0x90,   //function code + 80
        exception
    }), __func__);

    // G
    Expector()->Expect(
    WrapPDU({
        0x06,   //function code
        0x00,   //starting address Hi
        5,      //starting address Lo
        0x00,   //data Hi
        0x14,   //data Lo
    }),
    WrapPDU(exception == 0 ? std::vector<int> {
        0x06,   //function code
        0x00,   //starting address Hi
        5,      //starting address Lo
        0x00,   //data Hi
        0x14,   //data Lo
    } : std::vector<int> {
        0x90,   //function code + 80
        exception
    }), __func__);

    // B
    Expector()->Expect(
    WrapPDU({
        0x06,   //function code
        0x00,   //starting address Hi
        6,      //starting address Lo
        0x00,   //data Hi
        0x1E,   //data Lo
    }),
    WrapPDU(exception == 0 ? std::vector<int> {
        0x06,   //function code
        0x00,   //starting address Hi
        6,      //starting address Lo
        0x00,   //data Hi
        0x1E,   //data Lo
    } : std::vector<int> {
        0x90,   //function code + 80
        exception
    }), __func__);
}

// read 10 coils (more than can fit into 1 byte)
void TModbusExpectations::Enqueue10CoilsReadResponse(uint8_t exception)
{
    Expector()->Expect(
    WrapPDU({
        0x01,   //function code
        0x00,   //starting address Hi
        72,     //starting address Lo
        0x00,   //quantity Hi
        0x0a,   //quantity Lo
    }),
    WrapPDU(exception == 0 ? std::vector<int> {
        0x01,   //function code
        0x02,   //byte count
        0x49,   //coils status
        0x02,   //coils status
    } : std::vector<int> {
        0x81,   //function code + 80
        exception
    }), __func__);
}

// read 10 coils with max_read_registers = 3
void TModbusExpectations::Enqueue10CoilsMax3ReadResponse(uint8_t exception)
{
    Expector()->Expect(
    WrapPDU({
        0x01,   //function code
        0x00,   //starting address Hi
        72,     //starting address Lo
        0x00,   //quantity Hi
        0x03,   //quantity Lo
    }),
    WrapPDU(exception == 0 ? std::vector<int> {
        0x01,   //function code
        0x01,   //byte count
        0x01,   //coils status
    } : std::vector<int> {
        0x81,   //function code + 80
        exception
    }), __func__);

    Expector()->Expect(
    WrapPDU({
        0x01,   //function code
        0x00,   //starting address Hi
        75,     //starting address Lo
        0x00,   //quantity Hi
        0x03,   //quantity Lo
    }),
    WrapPDU(exception == 0 ? std::vector<int> {
        0x01,   //function code
        0x01,   //byte count
        0x01,   //coils status
    } : std::vector<int> {
        0x81,   //function code + 80
        exception
    }), __func__);

    Expector()->Expect(
    WrapPDU({
        0x01,   //function code
        0x00,   //starting address Hi
        78,     //starting address Lo
        0x00,   //quantity Hi
        0x03,   //quantity Lo
    }),
    WrapPDU(exception == 0 ? std::vector<int> {
        0x01,   //function code
        0x01,   //byte count
        0x01,   //coils status
    } : std::vector<int> {
        0x81,   //function code + 80
        exception
    }), __func__);

    Expector()->Expect(
    WrapPDU({
        0x01,   //function code
        0x00,   //starting address Hi
        81,     //starting address Lo
        0x00,   //quantity Hi
        0x01,   //quantity Lo
    }),
    WrapPDU(exception == 0 ? std::vector<int> {
        0x01,   //function code
        0x01,   //byte count
        0x01,   //coils status
    } : std::vector<int> {
        0x81,   //function code + 80
        exception
    }), __func__);
}

void TModbusExpectations::EnqueueInvalidCRCCoilReadResponse()
{
    auto response = WrapPDU(std::vector<int> {
        0x01,   //function code
        0x01,   //byte count
        1<<1,   //coils status
    });

    ++response.back(); // make sure CRC is not equal to correct value

    Expector()->Expect(
    WrapPDU({
        0x01,   //function code
        0x00,   //starting address Hi
        0x00,   //starting address Lo
        0x00,   //quantity Hi
        0x01,   //quantity Lo
    }),
    response, __func__);
}

void TModbusExpectations::EnqueueWrongDataSizeReadResponse()
{
    Expector()->Expect(
    WrapPDU({
        0x01,   //function code
        0x00,   //starting address Hi
        0x00,   //starting address Lo
        0x00,   //quantity Hi
        0x01,   //quantity Lo
    }),
    WrapPDU(std::vector<int> {
        0x01,   //function code
        0x02,   //byte count (intentionally wrong, should be 0x01)
        1<<1,   //coils status
    }), __func__);
}

void TModbusExpectations::EnqueueWrongSlaveIdCoilReadResponse(uint8_t exception)
{
    Expector()->Expect(
    WrapPDU({
        0x01,   //function code
        0x00,   //starting address Hi
        0x00,   //starting address Lo
        0x00,   //quantity Hi
        0x01,   //quantity Lo
    }),
    WrapPDU(exception == 0 ? std::vector<int> {
        0x01,   //function code
        0x01,   //byte count
        0x01,   //coils status
    } : std::vector<int> {
        0x81,   //function code + 80
        exception
    }, GetModbusRTUSlaveId() + 1), __func__);
}

void TModbusExpectations::EnqueueWrongFunctionCodeCoilReadResponse(uint8_t exception)
{
    Expector()->Expect(
    WrapPDU({
        0x01,   //function code
        0x00,   //starting address Hi
        0x00,   //starting address Lo
        0x00,   //quantity Hi
        0x01,   //quantity Lo
    }),
    WrapPDU(exception == 0 ? std::vector<int> {
        0x02,   //function code
        0x01,   //byte count
        0x01,   //coils status
    } : std::vector<int> {
        0x82,   //function code + 80
        exception
    }), __func__);
}

void TModbusExpectations::EnqueueWrongSlaveIdCoilWriteResponse(uint8_t exception)
{
    Expector()->Expect(
    WrapPDU({
        0x05,   //function code
        0x00,   //starting address Hi
        0x00,   //starting address Lo
        0xFF,   //value Hi
        0x00,   //value Lo
    }),
    WrapPDU(exception == 0 ? std::vector<int> {
        0x05,   //function code
        0x00,   //starting address Hi
        0x00,   //starting address Lo
        0xFF,   //value Hi
        0x00,   //value Lo
    } : std::vector<int> {
        0x85,   //function code + 80
        exception
    }, GetModbusRTUSlaveId() + 1), __func__);
}

void TModbusExpectations::EnqueueWrongFunctionCodeCoilWriteResponse(uint8_t exception)
{
    Expector()->Expect(
    WrapPDU({
        0x05,   //function code
        0x00,   //starting address Hi
        0x00,   //starting address Lo
        0xFF,   //value Hi
        0x00,   //value Lo
    }),
    WrapPDU(exception == 0 ? std::vector<int> {
        0x07,   //function code
        0x00,   //starting address Hi
        0x00,   //starting address Lo
        0xFF,   //value Hi
        0x00,   //value Lo
    } : std::vector<int> {
        0x87,   //function code + 80
        exception
    }), __func__);
}
