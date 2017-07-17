#include "fake_serial_port.h"


class TReconnectDetectionTest: public TSerialDeviceIntegrationTest
{
public:
    void SetUp() override;
    void TearDown() override;

    const char* ConfigPath() const override { return "configs/reconnect_test.json"; }
    void EnqueueSetupSectionI1(bool reset, bool read);
    void EnqueueSetupSectionI2(bool reset, bool read);
    void TryInitiateDisconnect(std::chrono::milliseconds delay);
private:
    void EnqueueSetupSection(bool read, int addr, int value, const char* func);
};

void TReconnectDetectionTest::SetUp()
{
    TSerialDeviceIntegrationTest::SetUp();
}

void TReconnectDetectionTest::TearDown()
{
    TSerialDeviceIntegrationTest::TearDown();
}

void TReconnectDetectionTest::EnqueueSetupSection(bool read, int addr, int value, const char* func)
{
    auto op = read ? 5: 6;

    auto writeValue = read ? 0: value;

    auto sum = op + addr + value;

    auto req = std::vector<int> {
        0xff, // sync
        0xff, // sync
        op, // op (5 = read)
        0x01, // unit id
        writeValue,
        addr, // addr
        0x00, // not used
        op + writeValue + addr + 1  // sum
    };

    auto res = std::vector<int> {
        0xff, // sync
        0xff, // sync
        op, // op
        0x00, // unit id 0 = module response
        value, // value
        addr, // addr
        0x00, // not used
        sum  // sum
    };

    SerialPort->Expect(
        req,
        res,
        func);
}

void TReconnectDetectionTest::EnqueueSetupSectionI1(bool reset, bool read)
{
    EnqueueSetupSection(read, 1, 42, __func__);
}

void TReconnectDetectionTest::EnqueueSetupSectionI2(bool reset, bool read)
{
    EnqueueSetupSection(read, 2, 24, __func__);
}

void TReconnectDetectionTest::TryInitiateDisconnect(std::chrono::milliseconds delay)
{
    {   // Test initial WriteInitValues
        Observer->SetUp();
        ASSERT_TRUE(!!SerialPort);
        EnqueueSetupSectionI1(false, false);
        EnqueueSetupSectionI2(false, false);
        Observer->WriteInitValues();
    }

    {   // Test read
        EnqueueSetupSectionI1(false, true);
        EnqueueSetupSectionI2(false, true);
        Note() << "LoopOnce()";
        Observer->LoopOnce();
        SerialPort->DumpWhatWasRead();
    }

    {   // Device disconnected
        Note() << "SimulateDisconnect(true)";
        SerialPort->SimulateDisconnect(true);

        auto disconnectTimepoint = std::chrono::steady_clock::now();

        // Couple of unsuccessful reads
        while (std::chrono::steady_clock::now() - disconnectTimepoint < delay) {
            Note() << "LoopOnce()";
            Observer->LoopOnce();
            usleep(std::chrono::duration_cast<std::chrono::microseconds>(delay).count() / 10);
        }

        // Final unsuccessful read after timeout, after this loop we expect device to be counted as disconnected
        Note() << "LoopOnce()";
        Observer->LoopOnce();
    }
}


TEST_F(TReconnectDetectionTest, Reconnect)
{
    // Disconnect device for time timeout
    TryInitiateDisconnect(std::chrono::milliseconds(DEFAULT_DEVICE_TIMEOUT_MS));

    {   // Device is connected back
        Note() << "SimulateDisconnect(false)";
        SerialPort->SimulateDisconnect(false);
        // After first successful range read ...
        EnqueueSetupSectionI1(false, true);
        // we expect reconnect so we expect setup section to be written
        EnqueueSetupSectionI1(false, false);
        EnqueueSetupSectionI2(false, false);
        // And then all remaining registers will be read
        EnqueueSetupSectionI2(false, true);

        Note() << "LoopOnce()";
        Observer->LoopOnce();
        SerialPort->DumpWhatWasRead();
    }

    {   // Test read
        EnqueueSetupSectionI1(false, true);
        EnqueueSetupSectionI2(false, true);
        Note() << "LoopOnce()";
        Observer->LoopOnce();
        SerialPort->DumpWhatWasRead();
    }

    SerialPort->Close();
}

TEST_F(TReconnectDetectionTest, ReconnectMiss)
{
    // Disconnect device for time less than timeout
    TryInitiateDisconnect(std::chrono::milliseconds(DEFAULT_DEVICE_TIMEOUT_MS - 100));

    {   // Device is connected back before timeout
        Note() << "SimulateDisconnect(false)";
        SerialPort->SimulateDisconnect(false);
        // We expect normal read, because no reconnect should've been triggered
        EnqueueSetupSectionI1(false, true);
        EnqueueSetupSectionI2(false, true);

        Note() << "LoopOnce()";
        Observer->LoopOnce();
        SerialPort->DumpWhatWasRead();
    }

    SerialPort->Close();
}

