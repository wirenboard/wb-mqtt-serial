#include <chrono>
#include <fcntl.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <thread>

#include "port/serial_port.h"
#include "pty_based_fake_serial.h"
#include "serial_device.h"

#include <wblib/promise.h>
#include <wblib/testing/testlog.h>

using namespace WBMQTT::Testing;

class TImxFloodThread
{
public:
    TImxFloodThread(PPort serial, std::chrono::milliseconds duration): Serial(serial), Duration(duration)
    {}

    void Run(WBMQTT::TPromise<void>& started)
    {
        uint8_t buf[8] = {};
        memset(buf, 0xFF, sizeof(buf));

        IsRunning = true;
        auto start = std::chrono::steady_clock::now();
        bool sentSomething = false;
        while (IsRunning) {
            auto diff = std::chrono::steady_clock::now() - start;
            if (diff > Duration) {
                Expired = true;
                return;
            }
            try {
                Serial->WriteBytes(buf, sizeof(buf));
                if (!sentSomething) {
                    sentSomething = true;
                    started.Complete();
                }
            } catch (const TSerialDeviceErrnoException& e) {
                if (e.GetErrnoValue() != EAGAIN) { // We write too fast. Let's give some time to a reader
                    throw;
                }
            }
            usleep(1);
        }
        if (!sentSomething) {
            throw std::runtime_error("TImxFloodThread sent nothing");
        }
    }

    bool IsExpired()
    {
        return Expired;
    }

    void Start()
    {
        if (!IsRunning) {
            Expired = false;
            IsRunning = true;
            WBMQTT::TPromise<void> initialized;
            FloodThread = std::thread([&]() { this->Run(initialized); });
            initialized.GetFuture().Wait();
        }
    }

    void Stop()
    {
        if (IsRunning) {
            IsRunning = false;
            FloodThread.join();
        }
    }

    bool IsRunning = false;
    std::thread FloodThread;
    bool Expired = false;
    PPort Serial;
    std::chrono::milliseconds Duration;
};

class TSerialPortTestWrapper: public TSerialPort
{
public:
    TSerialPortTestWrapper(const TSerialPortSettings& settings, TLoggedFixture& fixture, PPort other_port)
        : TSerialPort(settings),
          Fixture(fixture),
          OtherEndPort(other_port),
          FloodThread(OtherEndPort, std::chrono::milliseconds(3000)){};

    void SkipNoise() override
    {
        Fixture.Emit() << "SkipNoise()";
        TSerialPort::SkipNoise();
    }

    uint8_t ReadByte(const std::chrono::microseconds& timeout) override
    {
        Fixture.Emit() << "ReadByte()";
        return TSerialPort::ReadByte(timeout);
    }

    void EmptyReadBuffer()
    {
        while (true) {
            try {
                TSerialPort::ReadByte(std::chrono::milliseconds(100));
            } catch (const TSerialDeviceTransientErrorException&) {
                return;
            }
        }
    }

protected:
    void Reopen() override
    {
        Fixture.Emit() << "Reopen()";
        if (StopFloodOnReconnect) {
            FloodThread.Stop();
            EmptyReadBuffer();
        }
    };

    TLoggedFixture& Fixture;
    PPort OtherEndPort;

public:
    TImxFloodThread FloodThread;
    bool StopFloodOnReconnect = true;
};

class TSerialPortTest: public TLoggedFixture
{
protected:
    void SetUp();
    void TearDown();

    PPtyBasedFakeSerial FakeSerial;
    PPort Serial;
    std::shared_ptr<TSerialPortTestWrapper> SecondarySerial;
};

void TSerialPortTest::SetUp()
{
    TLoggedFixture::SetUp();
    FakeSerial = PPtyBasedFakeSerial(new TPtyBasedFakeSerial(*this));
    TSerialPortSettings settings(FakeSerial->GetPrimaryPtsName(), TSerialPortConnectionSettings(9600, 'N', 8, 1));
    Serial = PPort(new TSerialPort(settings));
    Serial->Open();

    FakeSerial->StartForwarding();
    TSerialPortSettings secondary_settings(FakeSerial->GetSecondaryPtsName(),
                                           TSerialPortConnectionSettings(9600, 'N', 8, 1));
    SecondarySerial =
        std::shared_ptr<TSerialPortTestWrapper>(new TSerialPortTestWrapper(secondary_settings, *this, Serial));
    SecondarySerial->Open();
}

void TSerialPortTest::TearDown()
{
    if (Serial) {
        Serial->Close();
    }
    Serial.reset();
    FakeSerial.reset();
    if (SecondarySerial) {
        SecondarySerial->Close();
    }
    TLoggedFixture::TearDown();
}

TEST_F(TSerialPortTest, TestSkipNoise)
{
    uint8_t buf[] = {1, 2, 3};
    Serial->WriteBytes(buf, sizeof(buf));
    usleep(300);
    SecondarySerial->SkipNoise();

    buf[0] = 0x04;
    // Should read 0x04, not 0x01
    Serial->WriteBytes(buf, 1);
    uint8_t read_back = SecondarySerial->ReadByte(std::chrono::milliseconds(1000));
    ASSERT_EQ(read_back, buf[0]);

    FakeSerial->Flush(); // shouldn't change anything here, but shouldn't hang either
}

/* on imx6, a glitch with precise timing can trigger a bug in UART IP. This will result
in continuously reception of FF bytes until either UART is reset or a couple of valid UART frames
are received */
// !!!! The test is not stable on build server
// TEST_F(TSerialPortTest, TestImxBug)
// {
//     uint8_t buf[10] = {0};
//     FakeSerial->SetDumpForwardingLogs(false); // exact data dump is not stable

//     SecondarySerial->FloodThread.Start();
//     usleep(10);
//     SecondarySerial->SkipNoise();
//     // If flood thread is expired then skip noise was stuck forever
//     ASSERT_FALSE(SecondarySerial->FloodThread.IsExpired());
//     usleep(100);
//     buf[0] = 0x04;
//     // Should read 0x04, not 0x01
//     Serial->WriteBytes(buf, 1);
//     uint8_t read_back = SecondarySerial->ReadByte(std::chrono::milliseconds(1000));
//     ASSERT_EQ(read_back, buf[0]);

//     // in case reconnect won't help with cont. data flow, exception must be raised
//     SecondarySerial->StopFloodOnReconnect = false;
//     SecondarySerial->FloodThread.Start();
//     usleep(10);
//     EXPECT_THROW(SecondarySerial->SkipNoise(), TSerialDeviceTransientErrorException);
//     SecondarySerial->FloodThread.Stop();

//     FakeSerial->Flush(); // shouldn't change anything here, but shouldn't hang either
// }
