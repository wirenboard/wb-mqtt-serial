#pragma once

#include <vector>
#include <memory>
#include <deque>

#include "expector.h"
#include "testlog.h"
#include "fake_mqtt.h"
#include "../serial_device.h"
#include "../serial_observer.h"
#include "ir_device_query.h"
#include "virtual_register.h"

class TFakeSerialPort: public TPort, public TExpector {
public:
    TFakeSerialPort(TLoggedFixture& fixture);
    void SetDebug(bool debug);
    bool Debug() const;
    void SetExpectedFrameTimeout(const std::chrono::microseconds& timeout);
    void CheckPortOpen() const;
    void Open();
    void Close();
    bool IsOpen() const;
    void WriteBytes(const uint8_t* buf, int count);
    uint8_t ReadByte();
    int ReadFrame(uint8_t* buf, int count,
                  const std::chrono::microseconds& timeout = std::chrono::microseconds(-1),
                  TFrameCompletePred frame_complete = 0);
    void SkipNoise();

    void Sleep(const std::chrono::microseconds & us) override;
    bool Wait(const PBinarySemaphore & semaphore, const TTimePoint & until) override;
    TTimePoint CurrentTime() const override;
    void CycleEnd(bool ok) override;

    void Expect(const std::vector<int>& request, const std::vector<int>& response, const char* func = 0);
    void DumpWhatWasRead();
    void Elapse(const std::chrono::milliseconds& ms);
    void SimulateDisconnect(bool simulate);
    bool GetDoSimulateDisconnect() const;
    TLoggedFixture& GetFixture();

private:
    void SkipFrameBoundary();
    const int FRAME_BOUNDARY = -1;

    TLoggedFixture& Fixture;
    bool IsPortOpen;
    bool DoSimulateDisconnect;
    std::deque<const char*> PendingFuncs;
    std::vector<int> Req;
    std::vector<int> Resp;
    size_t ReqPos, RespPos, DumpPos;
    std::chrono::microseconds ExpectedFrameTimeout = std::chrono::microseconds(-1);
    TPollPlan::TTimePoint Time = TPollPlan::TTimePoint(std::chrono::milliseconds(0));
};

typedef std::shared_ptr<TFakeSerialPort> PFakeSerialPort;

// /* Value desc that stores memory block bindings instead of just referencing them */
// struct TIRDeviceStoringValueDesc: TIRDeviceValueDesc
// {
//     const TBoundMemoryBlocks StoredBoundMemoryBlocks;

//     TIRDeviceStoringValueDesc(const TBoundMemoryBlocks & blocks, EWordOrder wordOrder)
//         : TIRDeviceValueDesc{StoredBoundMemoryBlocks, wordOrder}
//         , StoredBoundMemoryBlocks(blocks)
//     {}

//     TIRDeviceStoringValueDesc(const TIRDeviceStoringValueDesc & other)
//         : TIRDeviceValueDesc{StoredBoundMemoryBlocks, other.WordOrder}
//         , StoredBoundMemoryBlocks(other.StoredBoundMemoryBlocks)
//     {}
// };

// /* Query that stores read result */
// struct TIRDeviceStoringQuery: TIRDeviceQuery
// {
//     mutable std::vector<std::pair<TIRDeviceStoringValueDesc, uint64_t>> StoredValues;

//     explicit TIRDeviceStoringQuery(const TPSet<PMemoryBlock> & blocks, const std::vector<TIRDeviceStoringValueDesc> & descs, EQueryOperation op = EQueryOperation::Read)
//         : TIRDeviceQuery(blocks, op)
//     {
//         for (const TIRDeviceStoringValueDesc & desc: descs) {
//             StoredValues.push_back({desc, 0});
//         }
//     }

//     uint64_t GetReadValue(size_t index) const
//     {
//         return StoredValues[index].second;
//     }

//     void FinalizeRead(const TIRDeviceMemoryView & memoryView) const override
//     {
//         for (auto & descValue: StoredValues) {
//             descValue.second = memoryView.ReadValue(descValue.first);
//         }
//         TIRDeviceQuery::FinalizeRead(memoryView);
//     }
// };

// using PIRDeviceStoringQuery = std::shared_ptr<TIRDeviceStoringQuery>;

class TSerialDeviceTest: public TLoggedFixture, public virtual TExpectorProvider {
protected:
    void SetUp();
    void TearDown();
    PExpector Expector() const;

    PIRDeviceQuery GetReadQuery(std::vector<PVirtualRegister> &&) const;

    PVirtualRegister Reg(PSerialDevice device, int addr, uint32_t type, ERegisterFormat fmt = U16, double scale = 1,
        double offset = 0, double round_to = 0, EWordOrder word_order = EWordOrder::BigEndian,
        uint8_t bitOffset = 0, uint8_t bitWidth = 0) {
        return TVirtualRegister::Create(
            TRegisterConfig::Create(
                type, addr, fmt, scale, offset, round_to, true, false,
                "fake", false, 0, word_order, bitOffset, bitWidth), device);
    }

    PVirtualRegister RegValue(PSerialDevice device, int addr, uint16_t valueIndex, uint32_t typeIndex,
        ERegisterFormat fmt = U16, double scale = 1,
        double offset = 0, double round_to = 0, EWordOrder word_order = EWordOrder::BigEndian) {
        const auto & type = device->Protocol()->GetRegType(typeIndex);

        const auto & shiftWidth = type.ToMaskParameters(valueIndex);

        return Reg(
            device, addr, typeIndex, fmt, scale,
            offset, round_to, word_order,
            shiftWidth.first, shiftWidth.second
        );
    }

    void TestRead(const PIRDeviceQuery &) const;

    PFakeSerialPort SerialPort;
};

class TSerialDeviceIntegrationTest: public virtual TSerialDeviceTest {
protected:
    void SetUp();
    void TearDown();
    virtual const char* ConfigPath() const = 0;
    virtual const char* GetTemplatePath() const { return nullptr;};

    PFakeMQTTClient MQTTClient;
    PMQTTSerialObserver Observer;
    PHandlerConfig Config;
    bool PortMakerCalled;
};
