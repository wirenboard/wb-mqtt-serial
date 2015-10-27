#pragma once

#include <vector>
#include <memory>

#include "testlog.h"
#include "../wb-homa-modbus/serial_protocol.h"

class TFakeSerialPort: public TAbstractSerialPort {
public:
    TFakeSerialPort(TLoggedFixture& fixture);
    void SetDebug(bool debug);
    void CheckPortOpen();
    void Open();
    void Close();
    bool IsOpen() const;
    void WriteBytes(const uint8_t* buf, int count);
    uint8_t ReadByte();
    int ReadFrame(uint8_t* buf, int count);
    void SkipNoise();
    void USleep(int usec);

    void EnqueueResponse(const std::vector<int>& frame);

private:
    void DumpWhatWasRead();
    void SkipFrameBoundary();
    const int FRAME_BOUNDARY = -1;

    TLoggedFixture& Fixture;
    bool IsPortOpen;
    std::vector<int> Resp;
    size_t Pos, DumpPos;
};

typedef std::shared_ptr<TFakeSerialPort> PFakeSerialPort;
