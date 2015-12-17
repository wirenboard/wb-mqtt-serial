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
    int ReadFrame(uint8_t* buf, int count, int timeout);
    void SkipNoise();
    void USleep(int usec);

    void Expect(const std::vector<int>& request, const std::vector<int>& response);

private:
    void DumpWhatWasRead();
    void SkipFrameBoundary();
    const int FRAME_BOUNDARY = -1;

    TLoggedFixture& Fixture;
    bool IsPortOpen;
    std::vector<int> Req;
    std::vector<int> Resp;
    size_t ReqPos, RespPos, DumpPos;
};

typedef std::shared_ptr<TFakeSerialPort> PFakeSerialPort;
