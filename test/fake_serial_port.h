#pragma once

#include <vector>
#include <memory>

#include "testlog.h"
#include "../wb-homa-modbus/serial_protocol.h"

class TFakeSerialPort: public TAbstractSerialPort {
public:
    TFakeSerialPort(TLoggedFixture& fixture);
    void CheckPortOpen();
    void Open();
    void Close();
    bool IsOpen() const;
    void WriteBytes(const uint8_t* buf, int count);
    uint8_t ReadByte();
    void SkipNoise();

    void EnqueueResponse(std::vector<uint8_t> bytes);

private:
    void DumpWhatWasRead();

    TLoggedFixture& Fixture;
    bool IsPortOpen;
    size_t ResponsePosition, ResponseDumpPosition;
    std::vector<uint8_t> ResponseBytes;
};

typedef std::shared_ptr<TFakeSerialPort> PFakeSerialPort;
