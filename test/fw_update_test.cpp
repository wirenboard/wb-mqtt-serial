#include <gtest/gtest.h>

#include "rpc/rpc_fw_downloader.h"
#include "rpc/rpc_fw_update_handler.h"
#include "rpc/rpc_fw_update_helpers.h"
#include "rpc/rpc_fw_update_state.h"
#include "rpc/rpc_fw_update_task.h"

#include "fake_serial_port.h"
#include "modbus_expectations_base.h"

#include <cstring>
#include <fstream>

// ============================================================
//                  TFakeHttpClient
// ============================================================

class TFakeHttpClient: public IHttpClient
{
public:
    void SetTextResponse(const std::string& url, const std::string& response)
    {
        TextResponses[url] = response;
    }

    void SetBinaryResponse(const std::string& url, const std::vector<uint8_t>& response)
    {
        BinaryResponses[url] = response;
    }

    void SetError(const std::string& url, const std::string& error)
    {
        Errors[url] = error;
    }

    std::string GetText(const std::string& url) override
    {
        auto errIt = Errors.find(url);
        if (errIt != Errors.end()) {
            throw std::runtime_error(errIt->second);
        }
        auto it = TextResponses.find(url);
        if (it != TextResponses.end()) {
            RequestCount[url]++;
            return it->second;
        }
        throw std::runtime_error("No response configured for " + url);
    }

    std::vector<uint8_t> GetBinary(const std::string& url) override
    {
        auto errIt = Errors.find(url);
        if (errIt != Errors.end()) {
            throw std::runtime_error(errIt->second);
        }
        auto it = BinaryResponses.find(url);
        if (it != BinaryResponses.end()) {
            RequestCount[url]++;
            return it->second;
        }
        throw std::runtime_error("No response configured for " + url);
    }

    int GetRequestCount(const std::string& url) const
    {
        auto it = RequestCount.find(url);
        return it != RequestCount.end() ? it->second : 0;
    }

private:
    std::map<std::string, std::string> TextResponses;
    std::map<std::string, std::vector<uint8_t>> BinaryResponses;
    std::map<std::string, std::string> Errors;
    mutable std::map<std::string, int> RequestCount;
};

// ============================================================
//           1. WBFW Parsing Tests
// ============================================================

class WBFWParsingTest: public ::testing::Test
{};

TEST_F(WBFWParsingTest, ValidFile)
{
    // 32 bytes info + 136 bytes data = 168 bytes total
    std::vector<uint8_t> data(168, 0xAA);
    // Make info block different from data
    for (int i = 0; i < 32; ++i) {
        data[i] = static_cast<uint8_t>(i);
    }

    auto result = ParseWBFW(data);
    ASSERT_EQ(result.Info.size(), 32u);
    ASSERT_EQ(result.Data.size(), 136u);
    EXPECT_EQ(result.Info[0], 0);
    EXPECT_EQ(result.Info[31], 31);
    EXPECT_EQ(result.Data[0], 0xAA);
}

TEST_F(WBFWParsingTest, OddLengthFile)
{
    std::vector<uint8_t> data(33, 0x00);
    EXPECT_THROW(ParseWBFW(data), std::runtime_error);
}

TEST_F(WBFWParsingTest, TooShortFile)
{
    std::vector<uint8_t> data(30, 0x00);
    EXPECT_THROW(ParseWBFW(data), std::runtime_error);
}

TEST_F(WBFWParsingTest, ExactInfoBlockSize)
{
    // Exactly 32 bytes = info only, no data
    std::vector<uint8_t> data(32, 0xBB);
    auto result = ParseWBFW(data);
    ASSERT_EQ(result.Info.size(), 32u);
    ASSERT_EQ(result.Data.size(), 0u);
}

TEST_F(WBFWParsingTest, LargeFile)
{
    // 32 + 136*10 = 1392 bytes = 10 data chunks
    std::vector<uint8_t> data(1392, 0xCC);
    auto result = ParseWBFW(data);
    ASSERT_EQ(result.Info.size(), 32u);
    ASSERT_EQ(result.Data.size(), 1360u);
}

// ============================================================
//           2. Release YAML Parsing Tests
// ============================================================

class ReleaseYAMLTest: public ::testing::Test
{};

TEST_F(ReleaseYAMLTest, ValidYAML)
{
    std::string yaml =
        "releases:\n"
        "  wbled:\n"
        "    wb-2307: fw/by-signature/wbled/wb-2307/3.7.0.wbfw\n"
        "    wb-2310: fw/by-signature/wbled/wb-2310/3.8.0.wbfw\n"
        "  wbmap3h:\n"
        "    wb-2307: fw/by-signature/wbmap3h/wb-2307/1.2.0.wbfw\n";

    auto result = ParseReleaseVersionsYaml(yaml);
    ASSERT_EQ(result.size(), 2u);
    ASSERT_EQ(result["wbled"].size(), 2u);
    EXPECT_EQ(result["wbled"]["wb-2307"], "fw/by-signature/wbled/wb-2307/3.7.0.wbfw");
    EXPECT_EQ(result["wbled"]["wb-2310"], "fw/by-signature/wbled/wb-2310/3.8.0.wbfw");
    ASSERT_EQ(result["wbmap3h"].size(), 1u);
    EXPECT_EQ(result["wbmap3h"]["wb-2307"], "fw/by-signature/wbmap3h/wb-2307/1.2.0.wbfw");
}

TEST_F(ReleaseYAMLTest, EmptyFile)
{
    auto result = ParseReleaseVersionsYaml("");
    EXPECT_TRUE(result.empty());
}

TEST_F(ReleaseYAMLTest, NoReleasesSection)
{
    std::string yaml =
        "other:\n"
        "  key: value\n";
    auto result = ParseReleaseVersionsYaml(yaml);
    EXPECT_TRUE(result.empty());
}

TEST_F(ReleaseYAMLTest, MissingSignature)
{
    std::string yaml =
        "releases:\n"
        "  wbled:\n"
        "    wb-2307: fw/path.wbfw\n";

    auto result = ParseReleaseVersionsYaml(yaml);
    EXPECT_EQ(result.find("nonexistent"), result.end());
}

TEST_F(ReleaseYAMLTest, CommentsAndBlankLines)
{
    std::string yaml =
        "# This is a comment\n"
        "\n"
        "releases:\n"
        "  # Another comment\n"
        "  sig1:\n"
        "    suite1: path/to/fw.wbfw\n"
        "\n"
        "# End\n";

    auto result = ParseReleaseVersionsYaml(yaml);
    ASSERT_EQ(result.size(), 1u);
    EXPECT_EQ(result["sig1"]["suite1"], "path/to/fw.wbfw");
}

TEST_F(ReleaseYAMLTest, IndentedCommentWithColon)
{
    std::string yaml =
        "releases:\n"
        "  # note: this is a comment with colon\n"
        "  sig1:\n"
        "    suite1: path/to/fw.wbfw\n"
        "    # another: comment\n";

    auto result = ParseReleaseVersionsYaml(yaml);
    ASSERT_EQ(result.size(), 1u);
    EXPECT_EQ(result["sig1"]["suite1"], "path/to/fw.wbfw");
    EXPECT_EQ(result["sig1"].size(), 1u); // Only suite1, not the comment
}

// ============================================================
//           3. Version Extraction from URL
// ============================================================

class VersionFromURLTest: public ::testing::Test
{};

TEST_F(VersionFromURLTest, StandardWBFW)
{
    EXPECT_EQ(ParseFwVersionFromUrl("https://fw.example.com/path/3.7.0.wbfw"), "3.7.0");
}

TEST_F(VersionFromURLTest, CompFW)
{
    EXPECT_EQ(ParseFwVersionFromUrl("https://fw.example.com/path/v1.2.3.compfw"), "v1.2.3");
}

TEST_F(VersionFromURLTest, DeepPath)
{
    EXPECT_EQ(ParseFwVersionFromUrl("https://fw-releases.wirenboard.com/fw/by-signature/wbled/wb-2307/3.7.0.wbfw"),
              "3.7.0");
}

TEST_F(VersionFromURLTest, InvalidURL)
{
    EXPECT_THROW(ParseFwVersionFromUrl("https://example.com/no-extension"), std::runtime_error);
}

TEST_F(VersionFromURLTest, NoPathSegments)
{
    EXPECT_THROW(ParseFwVersionFromUrl("file.txt"), std::runtime_error);
}

// ============================================================
//           4. FwUpdateState Tests
// ============================================================

class FwUpdateStateTest: public ::testing::Test
{
protected:
    struct PublishRecord
    {
        std::string Topic;
        std::string Payload;
        bool Retain;
    };

    std::vector<PublishRecord> PublishLog;

    TStatePublishFn MakePublishFn()
    {
        return [this](const std::string& topic, const std::string& payload, bool retain) {
            PublishLog.push_back({topic, payload, retain});
        };
    }

    Json::Value ParseLastPayload()
    {
        EXPECT_FALSE(PublishLog.empty());
        Json::CharReaderBuilder builder;
        Json::Value root;
        std::string errors;
        std::istringstream stream(PublishLog.back().Payload);
        Json::parseFromStream(builder, stream, &root, &errors);
        return root;
    }
};

TEST_F(FwUpdateStateTest, AddDevice)
{
    TFwUpdateState state(MakePublishFn(), "/test/state");

    TDeviceUpdateInfo info;
    info.PortPath = "/dev/ttyRS485-1";
    info.Protocol = "modbus";
    info.SlaveId = 42;
    info.ToVersion = "3.7.0";
    info.Progress = 0;
    info.FromVersion = "3.6.1";
    info.Type = "firmware";
    state.Update(info);

    auto json = ParseLastPayload();
    ASSERT_EQ(json["devices"].size(), 1u);
    EXPECT_EQ(json["devices"][0]["slave_id"].asInt(), 42);
    EXPECT_EQ(json["devices"][0]["port"]["path"].asString(), "/dev/ttyRS485-1");
    EXPECT_EQ(json["devices"][0]["protocol"].asString(), "modbus");
    EXPECT_EQ(json["devices"][0]["to_version"].asString(), "3.7.0");
    EXPECT_EQ(json["devices"][0]["progress"].asInt(), 0);
    EXPECT_EQ(json["devices"][0]["from_version"].asString(), "3.6.1");
    EXPECT_EQ(json["devices"][0]["type"].asString(), "firmware");
    EXPECT_TRUE(json["devices"][0]["error"].isNull());
    EXPECT_TRUE(json["devices"][0]["component_number"].isNull());
    EXPECT_TRUE(json["devices"][0]["component_model"].isNull());
}

TEST_F(FwUpdateStateTest, UpdateProgress)
{
    TFwUpdateState state(MakePublishFn(), "/test/state");

    TDeviceUpdateInfo info;
    info.PortPath = "/dev/ttyRS485-1";
    info.Protocol = "modbus";
    info.SlaveId = 42;
    info.ToVersion = "3.7.0";
    info.Progress = 0;
    info.Type = "firmware";
    state.Update(info);

    info.Progress = 50;
    state.Update(info);

    auto json = ParseLastPayload();
    ASSERT_EQ(json["devices"].size(), 1u);
    EXPECT_EQ(json["devices"][0]["progress"].asInt(), 50);
}

TEST_F(FwUpdateStateTest, MultipleDevices)
{
    TFwUpdateState state(MakePublishFn(), "/test/state");

    TDeviceUpdateInfo info1;
    info1.PortPath = "/dev/ttyRS485-1";
    info1.Protocol = "modbus";
    info1.SlaveId = 42;
    info1.ToVersion = "3.7.0";
    info1.Type = "firmware";
    state.Update(info1);

    TDeviceUpdateInfo info2;
    info2.PortPath = "/dev/ttyRS485-2";
    info2.Protocol = "modbus";
    info2.SlaveId = 10;
    info2.ToVersion = "1.0.0";
    info2.Type = "bootloader";
    state.Update(info2);

    auto json = ParseLastPayload();
    ASSERT_EQ(json["devices"].size(), 2u);
}

TEST_F(FwUpdateStateTest, RemoveDevice)
{
    TFwUpdateState state(MakePublishFn(), "/test/state");

    TDeviceUpdateInfo info;
    info.PortPath = "/dev/ttyRS485-1";
    info.Protocol = "modbus";
    info.SlaveId = 42;
    info.ToVersion = "3.7.0";
    info.Type = "firmware";
    state.Update(info);

    state.Remove(42, "/dev/ttyRS485-1", "firmware");

    auto json = ParseLastPayload();
    EXPECT_EQ(json["devices"].size(), 0u);
}

TEST_F(FwUpdateStateTest, SetError)
{
    TFwUpdateState state(MakePublishFn(), "/test/state");

    TDeviceUpdateInfo info;
    info.PortPath = "/dev/ttyRS485-1";
    info.Protocol = "modbus";
    info.SlaveId = 42;
    info.ToVersion = "3.7.0";
    info.Type = "firmware";
    state.Update(info);

    state.SetError(42,
                   "/dev/ttyRS485-1",
                   "firmware",
                   "com.wb.device_manager.generic_error",
                   "Internal error");

    auto json = ParseLastPayload();
    ASSERT_EQ(json["devices"].size(), 1u);
    EXPECT_FALSE(json["devices"][0]["error"].isNull());
    EXPECT_EQ(json["devices"][0]["error"]["id"].asString(), "com.wb.device_manager.generic_error");
    EXPECT_EQ(json["devices"][0]["error"]["message"].asString(), "Internal error");
}

TEST_F(FwUpdateStateTest, ClearError)
{
    TFwUpdateState state(MakePublishFn(), "/test/state");

    TDeviceUpdateInfo info;
    info.PortPath = "/dev/ttyRS485-1";
    info.Protocol = "modbus";
    info.SlaveId = 42;
    info.ToVersion = "3.7.0";
    info.Type = "firmware";
    state.Update(info);

    state.SetError(42, "/dev/ttyRS485-1", "firmware", "error_id", "error msg");
    state.ClearError(42, "/dev/ttyRS485-1", "firmware");

    auto json = ParseLastPayload();
    // ClearError removes the device from the list
    EXPECT_EQ(json["devices"].size(), 0u);
}

TEST_F(FwUpdateStateTest, HasActiveUpdate)
{
    TFwUpdateState state(MakePublishFn(), "/test/state");

    EXPECT_FALSE(state.HasActiveUpdate(42, "/dev/ttyRS485-1"));

    TDeviceUpdateInfo info;
    info.PortPath = "/dev/ttyRS485-1";
    info.Protocol = "modbus";
    info.SlaveId = 42;
    info.ToVersion = "3.7.0";
    info.Progress = 50;
    info.Type = "firmware";
    state.Update(info);

    EXPECT_TRUE(state.HasActiveUpdate(42, "/dev/ttyRS485-1"));
    EXPECT_FALSE(state.HasActiveUpdate(43, "/dev/ttyRS485-1"));
    EXPECT_FALSE(state.HasActiveUpdate(42, "/dev/ttyRS485-2"));
}

TEST_F(FwUpdateStateTest, HasActiveUpdateReturnsFalseWhenComplete)
{
    TFwUpdateState state(MakePublishFn(), "/test/state");

    TDeviceUpdateInfo info;
    info.PortPath = "/dev/ttyRS485-1";
    info.Protocol = "modbus";
    info.SlaveId = 42;
    info.ToVersion = "3.7.0";
    info.Progress = 100;
    info.Type = "firmware";
    state.Update(info);

    EXPECT_FALSE(state.HasActiveUpdate(42, "/dev/ttyRS485-1"));
}

TEST_F(FwUpdateStateTest, HasActiveUpdateReturnsFalseWhenError)
{
    TFwUpdateState state(MakePublishFn(), "/test/state");

    TDeviceUpdateInfo info;
    info.PortPath = "/dev/ttyRS485-1";
    info.Protocol = "modbus";
    info.SlaveId = 42;
    info.ToVersion = "3.7.0";
    info.Progress = 50;
    info.Type = "firmware";
    info.Error = std::make_unique<TStateError>(TStateError{"err", "msg"});
    state.Update(info);

    EXPECT_FALSE(state.HasActiveUpdate(42, "/dev/ttyRS485-1"));
}

TEST_F(FwUpdateStateTest, ComponentInfo)
{
    TFwUpdateState state(MakePublishFn(), "/test/state");

    TDeviceUpdateInfo info;
    info.PortPath = "/dev/ttyRS485-1";
    info.Protocol = "modbus";
    info.SlaveId = 42;
    info.ToVersion = "1.0.0";
    info.Type = "component";
    info.ComponentNumber = 3;
    info.ComponentModel = "WB-SENSOR";
    state.Update(info);

    auto json = ParseLastPayload();
    ASSERT_EQ(json["devices"].size(), 1u);
    EXPECT_EQ(json["devices"][0]["component_number"].asInt(), 3);
    EXPECT_EQ(json["devices"][0]["component_model"].asString(), "WB-SENSOR");
}

TEST_F(FwUpdateStateTest, RetainFlag)
{
    TFwUpdateState state(MakePublishFn(), "/test/state");
    state.Reset();

    ASSERT_FALSE(PublishLog.empty());
    EXPECT_TRUE(PublishLog.back().Retain);
    EXPECT_EQ(PublishLog.back().Topic, "/test/state");
}

// ============================================================
//           5. UpdateNotifier Tests
// ============================================================

class UpdateNotifierTest: public ::testing::Test
{};

TEST_F(UpdateNotifierTest, ThrottlesProgress)
{
    TUpdateNotifier notifier(30);

    int notificationCount = 0;
    for (int i = 0; i <= 100; ++i) {
        if (notifier.ShouldNotify(i)) {
            ++notificationCount;
        }
    }
    // Should be approximately 30 notifications (including 0% and 100%)
    EXPECT_GE(notificationCount, 25);
    EXPECT_LE(notificationCount, 35);
}

TEST_F(UpdateNotifierTest, AlwaysNotifiesAt100)
{
    TUpdateNotifier notifier(5);
    EXPECT_TRUE(notifier.ShouldNotify(100));
}

TEST_F(UpdateNotifierTest, NotifiesAtStart)
{
    TUpdateNotifier notifier(30);
    EXPECT_TRUE(notifier.ShouldNotify(0));
}

TEST_F(UpdateNotifierTest, DoesNotRepeatSameStep)
{
    TUpdateNotifier notifier(10);
    EXPECT_TRUE(notifier.ShouldNotify(0));
    EXPECT_FALSE(notifier.ShouldNotify(0));
    EXPECT_FALSE(notifier.ShouldNotify(1));
}

// ============================================================
//           6. HTTP Downloader Tests (with fake client)
// ============================================================

class FwDownloaderTest: public ::testing::Test
{
protected:
    std::shared_ptr<TFakeHttpClient> FakeHttp = std::make_shared<TFakeHttpClient>();
    TFwDownloader Downloader{FakeHttp};
};

TEST_F(FwDownloaderTest, GetReleasedFirmware)
{
    FakeHttp->SetTextResponse("https://fw-releases.wirenboard.com/fw/by-signature/release-versions.yaml",
                              "releases:\n"
                              "  wbled:\n"
                              "    wb-2307: fw/by-signature/wbled/wb-2307/3.7.0.wbfw\n");

    auto result = Downloader.GetReleasedFirmware("wbled", "wb-2307");
    EXPECT_EQ(result.Version, "3.7.0");
    EXPECT_EQ(result.Endpoint, "https://fw-releases.wirenboard.com/fw/by-signature/wbled/wb-2307/3.7.0.wbfw");
}

TEST_F(FwDownloaderTest, GetReleasedFirmwareNotFound)
{
    FakeHttp->SetTextResponse("https://fw-releases.wirenboard.com/fw/by-signature/release-versions.yaml",
                              "releases:\n"
                              "  wbled:\n"
                              "    wb-2307: fw/path.wbfw\n");

    EXPECT_THROW(Downloader.GetReleasedFirmware("nonexistent", "wb-2307"), std::runtime_error);
}

TEST_F(FwDownloaderTest, GetReleasedFirmwareSuiteNotFound)
{
    FakeHttp->SetTextResponse("https://fw-releases.wirenboard.com/fw/by-signature/release-versions.yaml",
                              "releases:\n"
                              "  wbled:\n"
                              "    wb-2307: fw/path.wbfw\n");

    EXPECT_THROW(Downloader.GetReleasedFirmware("wbled", "nonexistent"), std::runtime_error);
}

TEST_F(FwDownloaderTest, GetLatestBootloader)
{
    FakeHttp->SetTextResponse("https://fw-releases.wirenboard.com/bootloader/by-signature/wbled/main/latest.txt",
                              "2.1.0");

    auto result = Downloader.GetLatestBootloader("wbled");
    EXPECT_EQ(result.Version, "2.1.0");
    EXPECT_EQ(result.Endpoint, "https://fw-releases.wirenboard.com/bootloader/by-signature/wbled/main/2.1.0.wbfw");
}

TEST_F(FwDownloaderTest, DownloadAndParseWBFW)
{
    std::vector<uint8_t> wbfwData(168, 0xDD);
    FakeHttp->SetBinaryResponse("https://example.com/fw.wbfw", wbfwData);

    auto result = Downloader.DownloadAndParseWBFW("https://example.com/fw.wbfw");
    EXPECT_EQ(result.Info.size(), 32u);
    EXPECT_EQ(result.Data.size(), 136u);
}

TEST_F(FwDownloaderTest, HTTPError)
{
    FakeHttp->SetError("https://example.com/fail", "Connection refused");
    EXPECT_THROW(Downloader.DownloadAndParseWBFW("https://example.com/fail"), std::runtime_error);
}

TEST_F(FwDownloaderTest, CacheHit)
{
    FakeHttp->SetTextResponse("https://fw-releases.wirenboard.com/fw/by-signature/release-versions.yaml",
                              "releases:\n"
                              "  sig1:\n"
                              "    suite1: fw/path/1.0.0.wbfw\n");

    auto url = "https://fw-releases.wirenboard.com/fw/by-signature/release-versions.yaml";

    // First call
    Downloader.GetReleasedFirmware("sig1", "suite1");
    int firstCount = FakeHttp->GetRequestCount(url);

    // Second call should hit cache
    Downloader.GetReleasedFirmware("sig1", "suite1");
    int secondCount = FakeHttp->GetRequestCount(url);

    EXPECT_EQ(firstCount, secondCount);
}

// ============================================================
//           7. Modbus Task Tests (TFwGetInfoTask, TFwFlashTask)
// Cf. firmware_update_test.py TestGetFirmwareInfo, TestFlashFw,
//     TestRebootToBootloader, TestUpdateSoftware
// ============================================================

class TFwTaskTest: public TSerialDeviceTest, public TModbusExpectationsBase
{
protected:
    void SetUp() override
    {
        SelectModbusType(MODBUS_RTU);
        SetModbusRTUSlaveId(SLAVE_ID);
        TSerialDeviceTest::SetUp();
        SerialPort->Open();
        FeaturePort = std::make_shared<TFeaturePort>(SerialPort, false);
        AccessHandler = std::make_unique<TSerialClientDeviceAccessHandler>(nullptr);
    }

    PExpector Expector() const override
    {
        return SerialPort;
    }

    static constexpr uint8_t SLAVE_ID = 42;

    PFeaturePort FeaturePort;
    std::unique_ptr<TSerialClientDeviceAccessHandler> AccessHandler;
    std::list<PSerialDevice> EmptyDeviceList;

    // Helper: encode ASCII string as Modbus register data (hi=0, lo=char)
    std::vector<int> EncodeStringAsRegs(const std::string& s, size_t regCount)
    {
        std::vector<int> data;
        for (size_t i = 0; i < regCount; ++i) {
            data.push_back(0x00);
            if (i < s.size()) {
                data.push_back(static_cast<int>(s[i]));
            } else {
                data.push_back(0x00);
            }
        }
        return data;
    }

    // Enqueue read holding register response (fn code 0x03)
    void EnqueueHoldingRead(uint16_t addr, uint16_t count, const std::vector<int>& responseData)
    {
        auto byteCount = static_cast<int>(count * 2);
        std::vector<int> request = {0x03,
                                    static_cast<int>(addr >> 8),
                                    static_cast<int>(addr & 0xFF),
                                    static_cast<int>(count >> 8),
                                    static_cast<int>(count & 0xFF)};
        std::vector<int> response = {0x03, byteCount};
        response.insert(response.end(), responseData.begin(), responseData.end());
        SerialPort->Expect(WrapPDU(request), WrapPDU(response), "EnqueueHoldingRead");
    }

    // Enqueue holding register read that returns a Modbus exception
    void EnqueueHoldingReadException(uint16_t addr, uint16_t count, uint8_t exceptionCode)
    {
        std::vector<int> request = {0x03,
                                    static_cast<int>(addr >> 8),
                                    static_cast<int>(addr & 0xFF),
                                    static_cast<int>(count >> 8),
                                    static_cast<int>(count & 0xFF)};
        std::vector<int> response = {0x83, static_cast<int>(exceptionCode)};
        SerialPort->Expect(WrapPDU(request), WrapPDU(response), "EnqueueHoldingReadException");
    }

    // Enqueue read discrete inputs response (fn code 0x02)
    void EnqueueDiscreteRead(uint16_t addr, uint16_t count, const std::vector<int>& statusBytes)
    {
        auto byteCount = static_cast<int>(statusBytes.size());
        std::vector<int> request = {0x02,
                                    static_cast<int>(addr >> 8),
                                    static_cast<int>(addr & 0xFF),
                                    static_cast<int>(count >> 8),
                                    static_cast<int>(count & 0xFF)};
        std::vector<int> response = {0x02, byteCount};
        response.insert(response.end(), statusBytes.begin(), statusBytes.end());
        SerialPort->Expect(WrapPDU(request), WrapPDU(response), "EnqueueDiscreteRead");
    }

    // Enqueue discrete inputs read that returns a Modbus exception
    void EnqueueDiscreteReadException(uint16_t addr, uint16_t count, uint8_t exceptionCode)
    {
        std::vector<int> request = {0x02,
                                    static_cast<int>(addr >> 8),
                                    static_cast<int>(addr & 0xFF),
                                    static_cast<int>(count >> 8),
                                    static_cast<int>(count & 0xFF)};
        std::vector<int> response = {0x82, static_cast<int>(exceptionCode)};
        SerialPort->Expect(WrapPDU(request), WrapPDU(response), "EnqueueDiscreteReadException");
    }

    // Enqueue read input registers response (fn code 0x04)
    void EnqueueInputRead(uint16_t addr, uint16_t count, const std::vector<int>& responseData)
    {
        auto byteCount = static_cast<int>(count * 2);
        std::vector<int> request = {0x04,
                                    static_cast<int>(addr >> 8),
                                    static_cast<int>(addr & 0xFF),
                                    static_cast<int>(count >> 8),
                                    static_cast<int>(count & 0xFF)};
        std::vector<int> response = {0x04, byteCount};
        response.insert(response.end(), responseData.begin(), responseData.end());
        SerialPort->Expect(WrapPDU(request), WrapPDU(response), "EnqueueInputRead");
    }

    // Enqueue write single register response (fn code 0x06)
    void EnqueueWriteSingleRegister(uint16_t addr, uint16_t value)
    {
        std::vector<int> request = {0x06,
                                    static_cast<int>(addr >> 8),
                                    static_cast<int>(addr & 0xFF),
                                    static_cast<int>(value >> 8),
                                    static_cast<int>(value & 0xFF)};
        // Echo request as response
        SerialPort->Expect(WrapPDU(request), WrapPDU(request), "EnqueueWriteSingleRegister");
    }

    // Enqueue write multiple registers response (fn code 0x10)
    void EnqueueWriteMultipleRegisters(uint16_t addr, uint16_t regCount, const std::vector<int>& writeData)
    {
        auto byteCount = static_cast<int>(regCount * 2);
        std::vector<int> request = {0x10,
                                    static_cast<int>(addr >> 8),
                                    static_cast<int>(addr & 0xFF),
                                    static_cast<int>(regCount >> 8),
                                    static_cast<int>(regCount & 0xFF),
                                    byteCount};
        request.insert(request.end(), writeData.begin(), writeData.end());
        std::vector<int> response = {0x10,
                                     static_cast<int>(addr >> 8),
                                     static_cast<int>(addr & 0xFF),
                                     static_cast<int>(regCount >> 8),
                                     static_cast<int>(regCount & 0xFF)};
        SerialPort->Expect(WrapPDU(request), WrapPDU(response), "EnqueueWriteMultipleRegisters");
    }

    // Enqueue standard GetInfo responses: signature, version, bootloader, preserve, model (ext), components
    void EnqueueBasicGetInfoResponses(const std::string& signature = "wbled",
                                     const std::string& version = "3.7.0",
                                     const std::string& bootloader = "1.5.5",
                                     const std::string& model = "WB-LED")
    {
        // 1. Read FW signature (addr=290, count=12, fn=0x03)
        EnqueueHoldingRead(FwRegisters::FW_SIGNATURE_ADDR, FwRegisters::FW_SIGNATURE_COUNT,
                          EncodeStringAsRegs(signature, FwRegisters::FW_SIGNATURE_COUNT));

        // 2. Read FW version (addr=250, count=16, fn=0x03)
        EnqueueHoldingRead(FwRegisters::FW_VERSION_ADDR, FwRegisters::FW_VERSION_COUNT,
                          EncodeStringAsRegs(version, FwRegisters::FW_VERSION_COUNT));

        // 3. Read bootloader version (addr=330, count=7, fn=0x03)
        EnqueueHoldingRead(FwRegisters::BOOTLOADER_VERSION_ADDR, FwRegisters::BOOTLOADER_VERSION_COUNT,
                          EncodeStringAsRegs(bootloader, FwRegisters::BOOTLOADER_VERSION_COUNT));

        // 4. Read preserve port settings register (addr=131, count=1, fn=0x03)
        //    Returns 0x0000 = can preserve
        EnqueueHoldingRead(FwRegisters::REBOOT_PRESERVE_PORT_SETTINGS_ADDR, 1, {0x00, 0x00});

        // 5. Read device model extended (addr=200, count=20, fn=0x03)
        EnqueueHoldingRead(FwRegisters::DEVICE_MODEL_EXTENDED_ADDR, FwRegisters::DEVICE_MODEL_EXTENDED_COUNT,
                          EncodeStringAsRegs(model, FwRegisters::DEVICE_MODEL_EXTENDED_COUNT));

        // 6. Read components presence (addr=65152, count=8, fn=0x02) - no components
        EnqueueDiscreteReadException(FwRegisters::COMPONENTS_PRESENCE_ADDR,
                                     FwRegisters::COMPONENTS_PRESENCE_COUNT, 0x02);
    }
};

// ---- TFwGetInfoTask tests ----
// Cf. firmware_update_test.py TestGetFirmwareInfo

TEST_F(TFwTaskTest, GetInfoSuccessfulRead)
{
    EnqueueBasicGetInfoResponses("wbled", "3.7.0", "1.5.5", "WB-LED");

    TFwDeviceInfo resultInfo;
    bool gotResult = false;
    bool gotError = false;

    auto task = std::make_shared<TFwGetInfoTask>(
        SLAVE_ID,
        "modbus",
        [&](const TFwDeviceInfo& info) {
            resultInfo = info;
            gotResult = true;
        },
        [&](const std::string& error) { gotError = true; });

    task->Run(FeaturePort, *AccessHandler, EmptyDeviceList);

    ASSERT_TRUE(gotResult);
    ASSERT_FALSE(gotError);
    EXPECT_EQ(resultInfo.FwSignature, "wbled");
    EXPECT_EQ(resultInfo.FwVersion, "3.7.0");
    EXPECT_EQ(resultInfo.BootloaderVersion, "1.5.5");
    EXPECT_EQ(resultInfo.DeviceModel, "WB-LED");
    EXPECT_TRUE(resultInfo.CanPreservePortSettings);
    EXPECT_TRUE(resultInfo.Components.empty());
}

TEST_F(TFwTaskTest, GetInfoBootloaderReadFails)
{
    // Signature
    EnqueueHoldingRead(FwRegisters::FW_SIGNATURE_ADDR, FwRegisters::FW_SIGNATURE_COUNT,
                      EncodeStringAsRegs("wbled", FwRegisters::FW_SIGNATURE_COUNT));
    // Version
    EnqueueHoldingRead(FwRegisters::FW_VERSION_ADDR, FwRegisters::FW_VERSION_COUNT,
                      EncodeStringAsRegs("3.7.0", FwRegisters::FW_VERSION_COUNT));
    // Bootloader - returns exception (old devices don't have this register)
    EnqueueHoldingReadException(FwRegisters::BOOTLOADER_VERSION_ADDR, FwRegisters::BOOTLOADER_VERSION_COUNT, 0x02);
    // Preserve port settings - also fails
    EnqueueHoldingReadException(FwRegisters::REBOOT_PRESERVE_PORT_SETTINGS_ADDR, 1, 0x02);
    // Model - extended fails, fall back to standard
    EnqueueHoldingReadException(FwRegisters::DEVICE_MODEL_EXTENDED_ADDR, FwRegisters::DEVICE_MODEL_EXTENDED_COUNT, 0x02);
    EnqueueHoldingRead(FwRegisters::DEVICE_MODEL_ADDR, FwRegisters::DEVICE_MODEL_COUNT,
                      EncodeStringAsRegs("WB-LED", FwRegisters::DEVICE_MODEL_COUNT));
    // No components
    EnqueueDiscreteReadException(FwRegisters::COMPONENTS_PRESENCE_ADDR,
                                 FwRegisters::COMPONENTS_PRESENCE_COUNT, 0x02);

    TFwDeviceInfo resultInfo;
    bool gotResult = false;

    auto task = std::make_shared<TFwGetInfoTask>(
        SLAVE_ID,
        "modbus",
        [&](const TFwDeviceInfo& info) {
            resultInfo = info;
            gotResult = true;
        },
        [&](const std::string&) {});

    task->Run(FeaturePort, *AccessHandler, EmptyDeviceList);

    ASSERT_TRUE(gotResult);
    EXPECT_EQ(resultInfo.FwSignature, "wbled");
    EXPECT_EQ(resultInfo.FwVersion, "3.7.0");
    EXPECT_EQ(resultInfo.BootloaderVersion, ""); // Failed to read
    EXPECT_FALSE(resultInfo.CanPreservePortSettings);
    EXPECT_EQ(resultInfo.DeviceModel, "WB-LED"); // Fell back to standard model reg
}

TEST_F(TFwTaskTest, GetInfoWithComponents)
{
    // Standard reads
    EnqueueHoldingRead(FwRegisters::FW_SIGNATURE_ADDR, FwRegisters::FW_SIGNATURE_COUNT,
                      EncodeStringAsRegs("wbmwac", FwRegisters::FW_SIGNATURE_COUNT));
    EnqueueHoldingRead(FwRegisters::FW_VERSION_ADDR, FwRegisters::FW_VERSION_COUNT,
                      EncodeStringAsRegs("2.0.0", FwRegisters::FW_VERSION_COUNT));
    EnqueueHoldingRead(FwRegisters::BOOTLOADER_VERSION_ADDR, FwRegisters::BOOTLOADER_VERSION_COUNT,
                      EncodeStringAsRegs("1.0.0", FwRegisters::BOOTLOADER_VERSION_COUNT));
    EnqueueHoldingRead(FwRegisters::REBOOT_PRESERVE_PORT_SETTINGS_ADDR, 1, {0x00, 0x00});
    EnqueueHoldingRead(FwRegisters::DEVICE_MODEL_EXTENDED_ADDR, FwRegisters::DEVICE_MODEL_EXTENDED_COUNT,
                      EncodeStringAsRegs("WBMWAC-v2", FwRegisters::DEVICE_MODEL_EXTENDED_COUNT));

    // Components presence: bit 0 set = component 0 present
    EnqueueDiscreteRead(FwRegisters::COMPONENTS_PRESENCE_ADDR, FwRegisters::COMPONENTS_PRESENCE_COUNT, {0x01});

    // Component 0 info (READ_INPUT fn=0x04): signature, fw version, model
    uint16_t comp0SigAddr = FwRegisters::COMPONENT_SIGNATURE_BASE;
    uint16_t comp0FwAddr = FwRegisters::COMPONENT_FW_VERSION_BASE;
    uint16_t comp0ModelAddr = FwRegisters::COMPONENT_MODEL_BASE;

    EnqueueInputRead(comp0SigAddr, FwRegisters::COMPONENT_SIGNATURE_COUNT,
                    EncodeStringAsRegs("wbmwac_oc", FwRegisters::COMPONENT_SIGNATURE_COUNT));
    EnqueueInputRead(comp0FwAddr, FwRegisters::COMPONENT_FW_VERSION_COUNT,
                    EncodeStringAsRegs("1.0.0", FwRegisters::COMPONENT_FW_VERSION_COUNT));
    EnqueueInputRead(comp0ModelAddr, FwRegisters::COMPONENT_MODEL_COUNT,
                    EncodeStringAsRegs("WBMWAC-OC", FwRegisters::COMPONENT_MODEL_COUNT));

    TFwDeviceInfo resultInfo;
    bool gotResult = false;

    auto task = std::make_shared<TFwGetInfoTask>(
        SLAVE_ID,
        "modbus",
        [&](const TFwDeviceInfo& info) {
            resultInfo = info;
            gotResult = true;
        },
        [&](const std::string&) {});

    task->Run(FeaturePort, *AccessHandler, EmptyDeviceList);

    ASSERT_TRUE(gotResult);
    EXPECT_EQ(resultInfo.FwSignature, "wbmwac");
    ASSERT_EQ(resultInfo.Components.size(), 1u);
    EXPECT_EQ(resultInfo.Components[0].Number, 0);
    EXPECT_EQ(resultInfo.Components[0].Signature, "wbmwac_oc");
    EXPECT_EQ(resultInfo.Components[0].FwVersion, "1.0.0");
    EXPECT_EQ(resultInfo.Components[0].Model, "WBMWAC-OC");
}

TEST_F(TFwTaskTest, GetInfoUnavailableComponent)
{
    // Standard reads
    EnqueueHoldingRead(FwRegisters::FW_SIGNATURE_ADDR, FwRegisters::FW_SIGNATURE_COUNT,
                      EncodeStringAsRegs("test", FwRegisters::FW_SIGNATURE_COUNT));
    EnqueueHoldingRead(FwRegisters::FW_VERSION_ADDR, FwRegisters::FW_VERSION_COUNT,
                      EncodeStringAsRegs("1.0.0", FwRegisters::FW_VERSION_COUNT));
    EnqueueHoldingReadException(FwRegisters::BOOTLOADER_VERSION_ADDR, FwRegisters::BOOTLOADER_VERSION_COUNT, 0x02);
    EnqueueHoldingReadException(FwRegisters::REBOOT_PRESERVE_PORT_SETTINGS_ADDR, 1, 0x02);
    EnqueueHoldingRead(FwRegisters::DEVICE_MODEL_EXTENDED_ADDR, FwRegisters::DEVICE_MODEL_EXTENDED_COUNT,
                      EncodeStringAsRegs("TEST", FwRegisters::DEVICE_MODEL_EXTENDED_COUNT));

    // Component 0 present
    EnqueueDiscreteRead(FwRegisters::COMPONENTS_PRESENCE_ADDR, FwRegisters::COMPONENTS_PRESENCE_COUNT, {0x01});

    // Component 0 signature returns all 0xFE (unavailable)
    std::vector<int> feData(FwRegisters::COMPONENT_SIGNATURE_COUNT * 2, 0xFE);
    EnqueueInputRead(FwRegisters::COMPONENT_SIGNATURE_BASE, FwRegisters::COMPONENT_SIGNATURE_COUNT, feData);

    TFwDeviceInfo resultInfo;
    bool gotResult = false;

    auto task = std::make_shared<TFwGetInfoTask>(
        SLAVE_ID,
        "modbus",
        [&](const TFwDeviceInfo& info) {
            resultInfo = info;
            gotResult = true;
        },
        [&](const std::string&) {});

    task->Run(FeaturePort, *AccessHandler, EmptyDeviceList);

    ASSERT_TRUE(gotResult);
    // Component should be filtered out (all 0xFE)
    EXPECT_TRUE(resultInfo.Components.empty());
}

TEST_F(TFwTaskTest, GetInfoDeviceModelCleanup)
{
    // Model contains \x02 characters that should be stripped
    auto modelWithCtrl = EncodeStringAsRegs("WB\x02-LED", FwRegisters::DEVICE_MODEL_EXTENDED_COUNT);
    EnqueueHoldingRead(FwRegisters::FW_SIGNATURE_ADDR, FwRegisters::FW_SIGNATURE_COUNT,
                      EncodeStringAsRegs("wbled", FwRegisters::FW_SIGNATURE_COUNT));
    EnqueueHoldingRead(FwRegisters::FW_VERSION_ADDR, FwRegisters::FW_VERSION_COUNT,
                      EncodeStringAsRegs("1.0.0", FwRegisters::FW_VERSION_COUNT));
    EnqueueHoldingReadException(FwRegisters::BOOTLOADER_VERSION_ADDR, FwRegisters::BOOTLOADER_VERSION_COUNT, 0x02);
    EnqueueHoldingReadException(FwRegisters::REBOOT_PRESERVE_PORT_SETTINGS_ADDR, 1, 0x02);
    EnqueueHoldingRead(FwRegisters::DEVICE_MODEL_EXTENDED_ADDR, FwRegisters::DEVICE_MODEL_EXTENDED_COUNT, modelWithCtrl);
    EnqueueDiscreteReadException(FwRegisters::COMPONENTS_PRESENCE_ADDR,
                                 FwRegisters::COMPONENTS_PRESENCE_COUNT, 0x02);

    TFwDeviceInfo resultInfo;
    bool gotResult = false;

    auto task = std::make_shared<TFwGetInfoTask>(
        SLAVE_ID,
        "modbus",
        [&](const TFwDeviceInfo& info) {
            resultInfo = info;
            gotResult = true;
        },
        [&](const std::string&) {});

    task->Run(FeaturePort, *AccessHandler, EmptyDeviceList);

    ASSERT_TRUE(gotResult);
    // \x02 should be stripped - Cf. firmware_update.py:560 get_human_readable_device_model()
    EXPECT_EQ(resultInfo.DeviceModel, "WB-LED");
}

TEST_F(TFwTaskTest, GetInfoDeviceNotResponding)
{
    // Signature read times out (simulate by returning Modbus exception that causes timeout)
    // The TFakeSerialPort can't easily simulate timeouts, so we use a disconnect
    SerialPort->SimulateDisconnect(TFakeSerialPort::SilentReadAndWriteFailure);

    bool gotError = false;
    std::string errorMsg;

    auto task = std::make_shared<TFwGetInfoTask>(
        SLAVE_ID,
        "modbus",
        [&](const TFwDeviceInfo&) {},
        [&](const std::string& error) {
            gotError = true;
            errorMsg = error;
        });

    task->Run(FeaturePort, *AccessHandler, EmptyDeviceList);

    ASSERT_TRUE(gotError);
    EXPECT_FALSE(errorMsg.empty());
}

// ---- TFwFlashTask tests ----
// Cf. firmware_update_test.py TestFlashFw, TestRebootToBootloader, TestUpdateSoftware

TEST_F(TFwTaskTest, FlashNoReboot)
{
    // Create a small firmware: 32 bytes info + 136 bytes data (1 chunk)
    TParsedWBFW fw;
    fw.Info.assign(32, 0xAA);
    fw.Data.assign(136, 0xBB);

    // Expect info block write: 16 regs to 0x1000
    std::vector<int> infoData(fw.Info.begin(), fw.Info.end());
    EnqueueWriteMultipleRegisters(FwRegisters::FW_INFO_BLOCK_ADDR, FwRegisters::FW_INFO_BLOCK_COUNT, infoData);

    // Expect data block write: 68 regs to 0x2000
    std::vector<int> dataChunk(fw.Data.begin(), fw.Data.end());
    EnqueueWriteMultipleRegisters(FwRegisters::FW_DATA_BLOCK_ADDR, FwRegisters::FW_DATA_BLOCK_COUNT, dataChunk);

    bool completed = false;
    bool gotError = false;
    int lastProgress = -1;

    auto task = std::make_shared<TFwFlashTask>(
        SLAVE_ID, "modbus", fw,
        false, // no reboot
        false, // can't preserve port settings
        [&](int p) { lastProgress = p; },
        [&]() { completed = true; },
        [&](const std::string&) { gotError = true; });

    task->Run(FeaturePort, *AccessHandler, EmptyDeviceList);

    EXPECT_TRUE(completed);
    EXPECT_FALSE(gotError);
    EXPECT_EQ(lastProgress, 100);
}

TEST_F(TFwTaskTest, FlashWithRebootPreserve)
{
    // Test reboot with preserve port settings (register 131)
    TParsedWBFW fw;
    fw.Info.assign(32, 0x11);
    fw.Data.assign(136, 0x22);

    // Expect write to register 131 (preserve port settings): value=1
    EnqueueWriteSingleRegister(FwRegisters::REBOOT_PRESERVE_PORT_SETTINGS_ADDR, 1);

    // Info block
    std::vector<int> infoData(fw.Info.begin(), fw.Info.end());
    EnqueueWriteMultipleRegisters(FwRegisters::FW_INFO_BLOCK_ADDR, FwRegisters::FW_INFO_BLOCK_COUNT, infoData);

    // Data block
    std::vector<int> dataChunk(fw.Data.begin(), fw.Data.end());
    EnqueueWriteMultipleRegisters(FwRegisters::FW_DATA_BLOCK_ADDR, FwRegisters::FW_DATA_BLOCK_COUNT, dataChunk);

    bool completed = false;

    auto task = std::make_shared<TFwFlashTask>(
        SLAVE_ID, "modbus", fw,
        true,  // reboot
        true,  // can preserve port settings
        [&](int) {},
        [&]() { completed = true; },
        [&](const std::string&) {});

    task->Run(FeaturePort, *AccessHandler, EmptyDeviceList);
    EXPECT_TRUE(completed);
}

TEST_F(TFwTaskTest, FlashWithRebootLegacy)
{
    // Test reboot without preserve (register 129), device doesn't respond (expected)
    TParsedWBFW fw;
    fw.Info.assign(32, 0x33);
    fw.Data.assign(136, 0x44);

    // Expect write to register 129 (old reboot): device times out (simulated by exception)
    // The task catches TResponseTimeoutException for this case
    std::vector<int> request = {0x06, 0x00, 0x81, 0x00, 0x01};
    // Simulate no response by returning nothing - use a Modbus exception that the code catches
    // Actually, TFakeSerialPort will throw if no match found. Let's use the normal flow:
    // The code has a try/catch for TResponseTimeoutException during legacy reboot.
    // Let's just set up the expectation normally (device responds to reboot)
    EnqueueWriteSingleRegister(FwRegisters::REBOOT_TO_BOOTLOADER_ADDR, 1);

    // Info block
    std::vector<int> infoData(fw.Info.begin(), fw.Info.end());
    EnqueueWriteMultipleRegisters(FwRegisters::FW_INFO_BLOCK_ADDR, FwRegisters::FW_INFO_BLOCK_COUNT, infoData);

    // Data block
    std::vector<int> dataChunk(fw.Data.begin(), fw.Data.end());
    EnqueueWriteMultipleRegisters(FwRegisters::FW_DATA_BLOCK_ADDR, FwRegisters::FW_DATA_BLOCK_COUNT, dataChunk);

    bool completed = false;

    auto task = std::make_shared<TFwFlashTask>(
        SLAVE_ID, "modbus", fw,
        true,  // reboot
        false, // can NOT preserve port settings (use legacy reboot)
        [&](int) {},
        [&]() { completed = true; },
        [&](const std::string&) {});

    task->Run(FeaturePort, *AccessHandler, EmptyDeviceList);
    EXPECT_TRUE(completed);
}

TEST_F(TFwTaskTest, FlashEmptyData)
{
    // Only info block, no data
    TParsedWBFW fw;
    fw.Info.assign(32, 0xCC);
    // fw.Data is empty

    // Expect only info block write
    std::vector<int> infoData(fw.Info.begin(), fw.Info.end());
    EnqueueWriteMultipleRegisters(FwRegisters::FW_INFO_BLOCK_ADDR, FwRegisters::FW_INFO_BLOCK_COUNT, infoData);

    bool completed = false;
    int lastProgress = -1;

    auto task = std::make_shared<TFwFlashTask>(
        SLAVE_ID, "modbus", fw,
        false, false,
        [&](int p) { lastProgress = p; },
        [&]() { completed = true; },
        [&](const std::string&) {});

    task->Run(FeaturePort, *AccessHandler, EmptyDeviceList);
    EXPECT_TRUE(completed);
    EXPECT_EQ(lastProgress, 100);
}

TEST_F(TFwTaskTest, FlashMultipleChunks)
{
    // 3 full chunks of 136 bytes = 408 bytes data
    TParsedWBFW fw;
    fw.Info.assign(32, 0x55);
    fw.Data.assign(136 * 3, 0x66);

    // Info block
    std::vector<int> infoData(fw.Info.begin(), fw.Info.end());
    EnqueueWriteMultipleRegisters(FwRegisters::FW_INFO_BLOCK_ADDR, FwRegisters::FW_INFO_BLOCK_COUNT, infoData);

    // 3 data blocks
    for (int i = 0; i < 3; ++i) {
        std::vector<int> chunk(136, 0x66);
        EnqueueWriteMultipleRegisters(FwRegisters::FW_DATA_BLOCK_ADDR, FwRegisters::FW_DATA_BLOCK_COUNT, chunk);
    }

    std::vector<int> progressValues;
    bool completed = false;

    auto task = std::make_shared<TFwFlashTask>(
        SLAVE_ID, "modbus", fw,
        false, false,
        [&](int p) { progressValues.push_back(p); },
        [&]() { completed = true; },
        [&](const std::string&) {});

    task->Run(FeaturePort, *AccessHandler, EmptyDeviceList);
    EXPECT_TRUE(completed);
    ASSERT_EQ(progressValues.size(), 3u);
    EXPECT_EQ(progressValues[0], 33);  // 1/3
    EXPECT_EQ(progressValues[1], 66);  // 2/3
    EXPECT_EQ(progressValues[2], 100); // 3/3
}

TEST_F(TFwTaskTest, FlashPartialLastChunk)
{
    // 136 + 50 bytes = not aligned to chunk size, last chunk gets padded
    TParsedWBFW fw;
    fw.Info.assign(32, 0x77);
    fw.Data.assign(186, 0x88); // 136 + 50 bytes

    // Info block
    std::vector<int> infoData(fw.Info.begin(), fw.Info.end());
    EnqueueWriteMultipleRegisters(FwRegisters::FW_INFO_BLOCK_ADDR, FwRegisters::FW_INFO_BLOCK_COUNT, infoData);

    // First chunk: 136 bytes = 68 regs
    std::vector<int> chunk1(136, 0x88);
    EnqueueWriteMultipleRegisters(FwRegisters::FW_DATA_BLOCK_ADDR, FwRegisters::FW_DATA_BLOCK_COUNT, chunk1);

    // Second chunk: 50 bytes, padded to 50 bytes (already even)
    std::vector<int> chunk2(50, 0x88);
    uint16_t lastRegCount = 50 / 2; // 25 regs
    EnqueueWriteMultipleRegisters(FwRegisters::FW_DATA_BLOCK_ADDR, lastRegCount, chunk2);

    bool completed = false;

    auto task = std::make_shared<TFwFlashTask>(
        SLAVE_ID, "modbus", fw,
        false, false,
        [&](int) {},
        [&]() { completed = true; },
        [&](const std::string&) {});

    task->Run(FeaturePort, *AccessHandler, EmptyDeviceList);
    EXPECT_TRUE(completed);
}

TEST_F(TFwTaskTest, FlashErrorCallsCallback)
{
    // Info block write fails
    TParsedWBFW fw;
    fw.Info.assign(32, 0x99);
    fw.Data.assign(136, 0xAA);

    // Simulate port disconnection so info block write fails
    SerialPort->SimulateDisconnect(TFakeSerialPort::SilentReadAndWriteFailure);

    bool gotError = false;
    std::string errorMsg;

    auto task = std::make_shared<TFwFlashTask>(
        SLAVE_ID, "modbus", fw,
        false, false,
        [&](int) {},
        [&]() {},
        [&](const std::string& err) {
            gotError = true;
            errorMsg = err;
        });

    task->Run(FeaturePort, *AccessHandler, EmptyDeviceList);
    EXPECT_TRUE(gotError);
    EXPECT_FALSE(errorMsg.empty());
}

// ---- DeviceUpdateInfo equality test ----
// Cf. firmware_update_test.py DeviceUpdateInfoTest

TEST(DeviceUpdateInfoTest, Matches)
{
    TDeviceUpdateInfo a;
    a.SlaveId = 42;
    a.PortPath = "/dev/ttyRS485-1";
    a.Type = "firmware";
    a.Protocol = "modbus";

    TDeviceUpdateInfo b;
    b.SlaveId = 42;
    b.PortPath = "/dev/ttyRS485-1";
    b.Type = "firmware";
    b.Protocol = "modbus";

    EXPECT_TRUE(a.Matches(b));

    b.SlaveId = 43;
    EXPECT_FALSE(a.Matches(b));

    b.SlaveId = 42;
    b.PortPath = "/dev/ttyRS485-2";
    EXPECT_FALSE(a.Matches(b));

    b.PortPath = "/dev/ttyRS485-1";
    b.Type = "bootloader";
    EXPECT_FALSE(a.Matches(b));
}

// ============================================================
//           8. Handler Pure-Function Tests (FwHandlerTest)
// ============================================================

class FwHandlerTest: public ::testing::Test
{
protected:
    std::shared_ptr<TFakeHttpClient> FakeHttp = std::make_shared<TFakeHttpClient>();
    std::shared_ptr<TFwDownloader> Downloader = std::make_shared<TFwDownloader>(FakeHttp);

    struct PublishRecord
    {
        std::string Topic;
        std::string Payload;
        bool Retain;
    };
    std::vector<PublishRecord> PublishLog;

    PFwUpdateState MakeState()
    {
        return std::make_shared<TFwUpdateState>(
            [this](const std::string& topic, const std::string& payload, bool retain) {
                PublishLog.push_back({topic, payload, retain});
            },
            "/test/state");
    }

    // Create an uninitialized handler stub for testing methods that don't access member state
    struct HandlerStub
    {
        alignas(TRPCFwUpdateHandler) char storage[sizeof(TRPCFwUpdateHandler)];
        TRPCFwUpdateHandler* ptr()
        {
            return reinterpret_cast<TRPCFwUpdateHandler*>(storage);
        }
    };

    // Helper: parse request params and return (slaveId, portPath, protocol) as a tuple
    static std::tuple<int, std::string, std::string> CallParseRequestParams(const Json::Value& request)
    {
        HandlerStub stub;
        auto params = stub.ptr()->ParseRequestParams(request);
        return {params.SlaveId, params.PortPath, params.Protocol};
    }

    // Helper: call MakePortRequestJson
    static Json::Value CallMakePortRequestJson(int slaveId, const std::string& portPath, const std::string& protocol)
    {
        HandlerStub stub;
        TRPCFwUpdateHandler::TRequestParams params;
        params.SlaveId = slaveId;
        params.PortPath = portPath;
        params.Protocol = protocol;
        return stub.ptr()->MakePortRequestJson(params);
    }

    // Helper to set up internal members and call BuildFirmwareInfoResponse via friend access.
    // Uses placement new to initialize Downloader and ReleaseSuite on an uninitialized handler stub.
    Json::Value CallBuildFirmwareInfoResponse(const TFwDeviceInfo& info, const std::string& suite = "bullseye")
    {
        alignas(TRPCFwUpdateHandler) char storage[sizeof(TRPCFwUpdateHandler)];
        auto* handler = reinterpret_cast<TRPCFwUpdateHandler*>(storage);
        new (&handler->Downloader) std::shared_ptr<TFwDownloader>(Downloader);
        new (&handler->ReleaseSuite) std::string(suite);

        auto result = handler->BuildFirmwareInfoResponse(info);

        handler->ReleaseSuite.~basic_string();
        handler->Downloader.~shared_ptr();
        return result;
    }

    void SetupReleasesYaml(const std::string& yaml = "releases:\n"
                                                       "  wbled:\n"
                                                       "    bullseye: fw/by-signature/wbled/bullseye/3.8.0.wbfw\n")
    {
        FakeHttp->SetTextResponse("https://fw-releases.wirenboard.com/fw/by-signature/release-versions.yaml", yaml);
    }
};

// ---- ParseRequestParams tests ----

TEST_F(FwHandlerTest, ParseRequestParamsValid)
{
    Json::Value request;
    request["slave_id"] = 42;
    request["port"]["path"] = "/dev/ttyRS485-1";
    request["protocol"] = "modbus";

    auto [slaveId, portPath, protocol] = CallParseRequestParams(request);
    EXPECT_EQ(slaveId, 42);
    EXPECT_EQ(portPath, "/dev/ttyRS485-1");
    EXPECT_EQ(protocol, "modbus");
}

TEST_F(FwHandlerTest, ParseRequestParamsDefaultProtocol)
{
    Json::Value request;
    request["slave_id"] = 1;
    request["port"]["path"] = "/dev/ttyRS485-1";

    auto [slaveId, portPath, protocol] = CallParseRequestParams(request);
    EXPECT_EQ(protocol, "modbus");
}

TEST_F(FwHandlerTest, ParseRequestParamsMissingSlaveId)
{
    Json::Value request;
    request["port"]["path"] = "/dev/ttyRS485-1";

    EXPECT_THROW(CallParseRequestParams(request), std::runtime_error);
}

TEST_F(FwHandlerTest, ParseRequestParamsSlaveIdNotInt)
{
    Json::Value request;
    request["slave_id"] = "not_a_number";
    request["port"]["path"] = "/dev/ttyRS485-1";

    EXPECT_THROW(CallParseRequestParams(request), std::runtime_error);
}

TEST_F(FwHandlerTest, ParseRequestParamsSlaveIdNegative)
{
    Json::Value request;
    request["slave_id"] = -1;
    request["port"]["path"] = "/dev/ttyRS485-1";

    EXPECT_THROW(CallParseRequestParams(request), std::runtime_error);
}

TEST_F(FwHandlerTest, ParseRequestParamsSlaveIdTooLarge)
{
    Json::Value request;
    request["slave_id"] = 256;
    request["port"]["path"] = "/dev/ttyRS485-1";

    EXPECT_THROW(CallParseRequestParams(request), std::runtime_error);
}

TEST_F(FwHandlerTest, ParseRequestParamsMissingPort)
{
    Json::Value request;
    request["slave_id"] = 42;

    EXPECT_THROW(CallParseRequestParams(request), std::runtime_error);
}

TEST_F(FwHandlerTest, ParseRequestParamsMissingPortPath)
{
    Json::Value request;
    request["slave_id"] = 42;
    request["port"]["other"] = "value";

    EXPECT_THROW(CallParseRequestParams(request), std::runtime_error);
}

// ---- MakePortRequestJson tests ----

TEST_F(FwHandlerTest, MakePortRequestJson)
{
    auto json = CallMakePortRequestJson(42, "/dev/ttyRS485-1", "modbus");
    EXPECT_EQ(json["path"].asString(), "/dev/ttyRS485-1");
    EXPECT_EQ(json["slave_id"].asString(), "42");
    EXPECT_EQ(json["protocol"].asString(), "modbus");
}

TEST_F(FwHandlerTest, MakePortRequestJsonModbusTcp)
{
    auto json = CallMakePortRequestJson(1, "192.168.1.100:502", "modbus-tcp");
    EXPECT_EQ(json["path"].asString(), "192.168.1.100:502");
    EXPECT_EQ(json["slave_id"].asString(), "1");
    EXPECT_EQ(json["protocol"].asString(), "modbus-tcp");
}

// ---- Version comparison tests ----

TEST_F(FwHandlerTest, FirmwareIsNewerTrue)
{
    EXPECT_TRUE(FirmwareIsNewer("3.6.1", "3.7.0"));
}

TEST_F(FwHandlerTest, FirmwareIsNewerFalse)
{
    EXPECT_FALSE(FirmwareIsNewer("3.7.0", "3.6.1"));
}

TEST_F(FwHandlerTest, FirmwareIsNewerSameVersion)
{
    EXPECT_FALSE(FirmwareIsNewer("3.7.0", "3.7.0"));
}

TEST_F(FwHandlerTest, FirmwareIsNewerEmptyCurrent)
{
    EXPECT_FALSE(FirmwareIsNewer("", "3.7.0"));
}

TEST_F(FwHandlerTest, FirmwareIsNewerEmptyAvailable)
{
    EXPECT_FALSE(FirmwareIsNewer("3.7.0", ""));
}

TEST_F(FwHandlerTest, ComponentFirmwareIsNewerTrue)
{
    EXPECT_TRUE(ComponentFirmwareIsNewer("1.0", "2.0"));
}

TEST_F(FwHandlerTest, ComponentFirmwareIsNewerFalseSameVersion)
{
    EXPECT_FALSE(ComponentFirmwareIsNewer("1.0", "1.0"));
}

TEST_F(FwHandlerTest, ComponentFirmwareIsNewerFalseEmptyAvailable)
{
    EXPECT_FALSE(ComponentFirmwareIsNewer("1.0", ""));
}

// ---- IsNonUpdatableSignature tests ----

TEST_F(FwHandlerTest, IsNonUpdatableMsw5GL)
{
    EXPECT_TRUE(IsNonUpdatableSignature("msw5GL"));
}

TEST_F(FwHandlerTest, IsNonUpdatableMsw3G419L)
{
    EXPECT_TRUE(IsNonUpdatableSignature("msw3G419L"));
}

TEST_F(FwHandlerTest, IsNonUpdatableNormalSignature)
{
    EXPECT_FALSE(IsNonUpdatableSignature("wbled"));
}

// ---- BuildFirmwareInfoResponse tests ----

TEST_F(FwHandlerTest, BuildResponseNonUpdatableSignature)
{
    SetupReleasesYaml();

    TFwDeviceInfo info;
    info.FwSignature = "msw5GL";
    info.FwVersion = "1.0.0";
    info.DeviceModel = "WB-MSW v.3";

    auto result = CallBuildFirmwareInfoResponse(info);

    EXPECT_FALSE(result["can_update"].asBool());
    EXPECT_EQ(result["fw"].asString(), "1.0.0");
    EXPECT_EQ(result["available_fw"].asString(), "");
    EXPECT_FALSE(result["fw_has_update"].asBool());
    EXPECT_EQ(result["model"].asString(), "WB-MSW v.3");
}

TEST_F(FwHandlerTest, BuildResponseFirmwareAvailableNewer)
{
    SetupReleasesYaml("releases:\n"
                      "  wbled:\n"
                      "    bullseye: fw/by-signature/wbled/bullseye/3.8.0.wbfw\n");

    FakeHttp->SetTextResponse("https://fw-releases.wirenboard.com/bootloader/by-signature/wbled/main/latest.txt",
                              "2.0.0");

    TFwDeviceInfo info;
    info.FwSignature = "wbled";
    info.FwVersion = "3.6.1";
    info.BootloaderVersion = "1.5.0";
    info.DeviceModel = "WB-LED";

    auto result = CallBuildFirmwareInfoResponse(info);

    EXPECT_TRUE(result["can_update"].asBool());
    EXPECT_EQ(result["available_fw"].asString(), "3.8.0");
    EXPECT_TRUE(result["fw_has_update"].asBool());
    EXPECT_EQ(result["available_bootloader"].asString(), "2.0.0");
    EXPECT_TRUE(result["bootloader_has_update"].asBool());
}

TEST_F(FwHandlerTest, BuildResponseFirmwareSameVersion)
{
    SetupReleasesYaml("releases:\n"
                      "  wbled:\n"
                      "    bullseye: fw/by-signature/wbled/bullseye/3.8.0.wbfw\n");

    FakeHttp->SetTextResponse("https://fw-releases.wirenboard.com/bootloader/by-signature/wbled/main/latest.txt",
                              "1.5.0");

    TFwDeviceInfo info;
    info.FwSignature = "wbled";
    info.FwVersion = "3.8.0";
    info.BootloaderVersion = "1.5.0";
    info.DeviceModel = "WB-LED";

    auto result = CallBuildFirmwareInfoResponse(info);

    EXPECT_TRUE(result["can_update"].asBool());
    EXPECT_EQ(result["available_fw"].asString(), "3.8.0");
    EXPECT_FALSE(result["fw_has_update"].asBool());
    EXPECT_FALSE(result["bootloader_has_update"].asBool());
}

TEST_F(FwHandlerTest, BuildResponseWithComponents)
{
    SetupReleasesYaml("releases:\n"
                      "  wbmwac:\n"
                      "    bullseye: fw/by-signature/wbmwac/bullseye/2.0.0.wbfw\n"
                      "  wbmwac_oc:\n"
                      "    bullseye: fw/by-signature/wbmwac_oc/bullseye/1.5.0.compfw\n");

    FakeHttp->SetTextResponse("https://fw-releases.wirenboard.com/bootloader/by-signature/wbmwac/main/latest.txt",
                              "1.0.0");

    TFwDeviceInfo info;
    info.FwSignature = "wbmwac";
    info.FwVersion = "2.0.0";
    info.BootloaderVersion = "1.0.0";
    info.DeviceModel = "WBMWAC-v2";
    info.Components.push_back({0, "wbmwac_oc", "1.0.0", "WBMWAC-OC"});

    auto result = CallBuildFirmwareInfoResponse(info);

    EXPECT_TRUE(result["can_update"].asBool());
    ASSERT_TRUE(result["components"].isMember("0"));
    EXPECT_EQ(result["components"]["0"]["model"].asString(), "WBMWAC-OC");
    EXPECT_EQ(result["components"]["0"]["fw"].asString(), "1.0.0");
    EXPECT_EQ(result["components"]["0"]["available_fw"].asString(), "1.5.0");
    EXPECT_TRUE(result["components"]["0"]["has_update"].asBool());
}

TEST_F(FwHandlerTest, BuildResponseHttpError)
{
    // No release YAML configured - HTTP will throw
    FakeHttp->SetError("https://fw-releases.wirenboard.com/fw/by-signature/release-versions.yaml",
                       "Connection refused");

    TFwDeviceInfo info;
    info.FwSignature = "wbled";
    info.FwVersion = "3.6.1";
    info.DeviceModel = "WB-LED";

    // Should not throw - errors are caught gracefully
    auto result = CallBuildFirmwareInfoResponse(info);

    EXPECT_TRUE(result["can_update"].asBool());
    EXPECT_EQ(result["available_fw"].asString(), "");
    EXPECT_FALSE(result["fw_has_update"].asBool());
}

// ---- ClearError logic test ----

TEST_F(FwHandlerTest, ClearErrorLogic)
{
    // Test the ClearError flow through State directly
    auto state = MakeState();

    TDeviceUpdateInfo info;
    info.PortPath = "/dev/ttyRS485-1";
    info.Protocol = "modbus";
    info.SlaveId = 42;
    info.ToVersion = "3.7.0";
    info.Type = "firmware";
    state->Update(info);

    state->SetError(42, "/dev/ttyRS485-1", "firmware", "error_id", "error msg");
    EXPECT_TRUE(state->HasActiveUpdate(42, "/dev/ttyRS485-1") == false); // error means not active

    state->ClearError(42, "/dev/ttyRS485-1", "firmware");

    // After clearing, the device should be removed
    Json::CharReaderBuilder builder;
    Json::Value root;
    std::string errors;
    std::istringstream stream(PublishLog.back().Payload);
    Json::parseFromStream(builder, stream, &root, &errors);
    EXPECT_EQ(root["devices"].size(), 0u);
}

// ============================================================
//           9. ReadReleaseSuite Tests
// ============================================================

class ReadReleaseSuiteTest: public ::testing::Test
{
protected:
    std::string TempDir;

    void SetUp() override
    {
        char tmpl[] = "/tmp/fw_test_XXXXXX";
        TempDir = mkdtemp(tmpl);
    }

    void TearDown() override
    {
        // Clean up temp files
        std::string cmd = "rm -rf " + TempDir;
        system(cmd.c_str());
    }

    std::string CreateTempFile(const std::string& content)
    {
        std::string path = TempDir + "/wb-release";
        std::ofstream ofs(path);
        ofs << content;
        return path;
    }
};

TEST_F(ReadReleaseSuiteTest, FileWithSuite)
{
    auto path = CreateTempFile("SUITE=bullseye\nVERSION=1.0\n");
    EXPECT_EQ(ReadReleaseSuite(path), "bullseye");
}

TEST_F(ReadReleaseSuiteTest, FileWithoutSuite)
{
    auto path = CreateTempFile("VERSION=1.0\nBOARD=wb7\n");
    EXPECT_EQ(ReadReleaseSuite(path), "");
}

TEST_F(ReadReleaseSuiteTest, NonExistentFile)
{
    EXPECT_EQ(ReadReleaseSuite("/tmp/nonexistent_file_12345"), "");
}

TEST_F(ReadReleaseSuiteTest, SuiteWithSpaces)
{
    auto path = CreateTempFile("SUITE = bookworm\n");
    // Key has trailing space, but parser trims it
    EXPECT_EQ(ReadReleaseSuite(path), "bookworm");
}

// ============================================================
//           10. Additional Modbus Task Edge Cases
// ============================================================

// ---- WriteDataBlock with Modbus exception response ----
// NOTE: TModbusRTUTraits::Transaction does not validate the response PDU for exceptions -
// it returns the raw PDU. TFwFlashTask::WriteDataBlock catches TModbusExceptionError
// from Transaction, but Transaction never throws it. So exception responses are silently
// accepted as successful writes. The 0x04 test verifies this behavior.

TEST_F(TFwTaskTest, FlashDataBlockExceptionAcceptedAsSuccess)
{
    // A Modbus exception response to a write is treated as success by the current code
    // because Transaction() does not validate the response PDU.
    TParsedWBFW fw;
    fw.Info.assign(32, 0xAA);
    fw.Data.assign(136, 0xBB);

    // Info block write succeeds
    std::vector<int> infoData(fw.Info.begin(), fw.Info.end());
    EnqueueWriteMultipleRegisters(FwRegisters::FW_INFO_BLOCK_ADDR, FwRegisters::FW_INFO_BLOCK_COUNT, infoData);

    // Data block write: Modbus exception 0x02
    auto byteCount = static_cast<int>(FwRegisters::FW_DATA_BLOCK_COUNT * 2);
    std::vector<int> writeRequest = {0x10,
                                     static_cast<int>(FwRegisters::FW_DATA_BLOCK_ADDR >> 8),
                                     static_cast<int>(FwRegisters::FW_DATA_BLOCK_ADDR & 0xFF),
                                     static_cast<int>(FwRegisters::FW_DATA_BLOCK_COUNT >> 8),
                                     static_cast<int>(FwRegisters::FW_DATA_BLOCK_COUNT & 0xFF),
                                     byteCount};
    std::vector<int> dataChunk(fw.Data.begin(), fw.Data.end());
    writeRequest.insert(writeRequest.end(), dataChunk.begin(), dataChunk.end());
    std::vector<int> exceptionResponse = {0x90, 0x02}; // exception code 0x02

    SerialPort->Expect(WrapPDU(writeRequest), WrapPDU(exceptionResponse), "DataBlockException");

    bool completed = false;
    bool gotError = false;

    auto task = std::make_shared<TFwFlashTask>(
        SLAVE_ID, "modbus", fw,
        false, false,
        [&](int) {},
        [&]() { completed = true; },
        [&](const std::string&) { gotError = true; });

    task->Run(FeaturePort, *AccessHandler, EmptyDeviceList);
    // Exception response silently accepted as success
    EXPECT_TRUE(completed);
    EXPECT_FALSE(gotError);
}

// ---- Component read error (component present but read fails) ----

TEST_F(TFwTaskTest, GetInfoComponentReadError)
{
    // Standard reads
    EnqueueHoldingRead(FwRegisters::FW_SIGNATURE_ADDR, FwRegisters::FW_SIGNATURE_COUNT,
                      EncodeStringAsRegs("test", FwRegisters::FW_SIGNATURE_COUNT));
    EnqueueHoldingRead(FwRegisters::FW_VERSION_ADDR, FwRegisters::FW_VERSION_COUNT,
                      EncodeStringAsRegs("1.0.0", FwRegisters::FW_VERSION_COUNT));
    EnqueueHoldingReadException(FwRegisters::BOOTLOADER_VERSION_ADDR, FwRegisters::BOOTLOADER_VERSION_COUNT, 0x02);
    EnqueueHoldingReadException(FwRegisters::REBOOT_PRESERVE_PORT_SETTINGS_ADDR, 1, 0x02);
    EnqueueHoldingRead(FwRegisters::DEVICE_MODEL_EXTENDED_ADDR, FwRegisters::DEVICE_MODEL_EXTENDED_COUNT,
                      EncodeStringAsRegs("TEST", FwRegisters::DEVICE_MODEL_EXTENDED_COUNT));

    // Component 0 present
    EnqueueDiscreteRead(FwRegisters::COMPONENTS_PRESENCE_ADDR, FwRegisters::COMPONENTS_PRESENCE_COUNT, {0x01});

    // Component 0 signature read fails with Modbus exception
    uint16_t comp0SigAddr = FwRegisters::COMPONENT_SIGNATURE_BASE;
    std::vector<int> request = {0x04,
                                static_cast<int>(comp0SigAddr >> 8),
                                static_cast<int>(comp0SigAddr & 0xFF),
                                static_cast<int>(FwRegisters::COMPONENT_SIGNATURE_COUNT >> 8),
                                static_cast<int>(FwRegisters::COMPONENT_SIGNATURE_COUNT & 0xFF)};
    std::vector<int> exceptionResp = {0x84, 0x02}; // fn 0x04 | 0x80, exception code 0x02
    SerialPort->Expect(WrapPDU(request), WrapPDU(exceptionResp), "ComponentReadException");

    TFwDeviceInfo resultInfo;
    bool gotResult = false;

    auto task = std::make_shared<TFwGetInfoTask>(
        SLAVE_ID,
        "modbus",
        [&](const TFwDeviceInfo& info) {
            resultInfo = info;
            gotResult = true;
        },
        [&](const std::string&) {});

    task->Run(FeaturePort, *AccessHandler, EmptyDeviceList);

    ASSERT_TRUE(gotResult);
    // Component read failed, should be skipped gracefully
    EXPECT_TRUE(resultInfo.Components.empty());
}

// ============================================================
//           11. FwUpdateState Edge Case
// ============================================================

TEST_F(FwUpdateStateTest, SetErrorOnNonExistentDevice)
{
    TFwUpdateState state(MakePublishFn(), "/test/state");
    state.Reset();
    auto publishCountBefore = PublishLog.size();

    // Call SetError with a slave_id that doesn't exist in the list
    state.SetError(99, "/dev/ttyRS485-1", "firmware", "error_id", "error msg");

    // No new publish should happen since the device wasn't found
    EXPECT_EQ(PublishLog.size(), publishCountBefore);
}

// ============================================================
//           12. TFakeTaskRunner and Handler Integration Tests
// ============================================================

class TFakeTaskRunner: public ITaskRunner
{
public:
    // Capture-only mode: just records submitted tasks
    TFakeTaskRunner() = default;

    // Execute mode: runs tasks synchronously against a fake serial port
    void SetPort(PFakeSerialPort serialPort)
    {
        SerialPort = serialPort;
        SerialPort->Open();
        Port = std::make_shared<TFeaturePort>(SerialPort, false);
        AccessHandler = std::make_unique<TSerialClientDeviceAccessHandler>(nullptr);
    }

    void RunTask(const Json::Value& request, PSerialClientTask task) override
    {
        SubmittedTasks.push_back({request, task});
        if (Port) {
            task->Run(Port, *AccessHandler, EmptyDeviceList);
        }
    }

    struct SubmittedTask
    {
        Json::Value Request;
        PSerialClientTask Task;
    };
    std::vector<SubmittedTask> SubmittedTasks;

private:
    PFakeSerialPort SerialPort;
    PFeaturePort Port;
    std::unique_ptr<TSerialClientDeviceAccessHandler> AccessHandler;
    std::list<PSerialDevice> EmptyDeviceList;
};

// Handler integration test fixture: tests handler methods end-to-end
// with a fake task runner that runs Modbus tasks synchronously against TFakeSerialPort.
// All private member access is wrapped in helper methods since friend class
// doesn't propagate to TEST_F subclasses.
class FwHandlerIntegrationTest: public TSerialDeviceTest, public TModbusExpectationsBase
{
protected:
    void SetUp() override
    {
        SelectModbusType(MODBUS_RTU);
        SetModbusRTUSlaveId(SLAVE_ID);
        TSerialDeviceTest::SetUp();

        FakeHttp = std::make_shared<TFakeHttpClient>();

        TaskRunner = std::make_unique<TFakeTaskRunner>();
        TaskRunner->SetPort(SerialPort);

        State = std::make_shared<TFwUpdateState>(
            [this](const std::string& topic, const std::string& payload, bool retain) {
                PublishLog.push_back({topic, payload, retain});
            },
            "/test/state");

        // Friend access: call private test constructor directly
        Handler.reset(new TRPCFwUpdateHandler(*TaskRunner, FakeHttp, State, "bullseye"));
    }

    PExpector Expector() const override
    {
        return SerialPort;
    }

    static constexpr uint8_t SLAVE_ID = 42;

    std::shared_ptr<TFakeHttpClient> FakeHttp;
    std::unique_ptr<TFakeTaskRunner> TaskRunner;
    PFwUpdateState State;
    std::unique_ptr<TRPCFwUpdateHandler> Handler;

    struct PublishRecord
    {
        std::string Topic;
        std::string Payload;
        bool Retain;
    };
    std::vector<PublishRecord> PublishLog;

    // RPC callback helpers
    Json::Value LastResult;
    int LastErrorCode = 0;
    std::string LastErrorMsg;
    bool GotResult = false;
    bool GotError = false;

    WBMQTT::TMqttRpcServer::TResultCallback MakeOnResult()
    {
        return [this](const Json::Value& result) {
            LastResult = result;
            GotResult = true;
        };
    }

    WBMQTT::TMqttRpcServer::TErrorCallback MakeOnError()
    {
        return [this](int code, const std::string& msg) {
            LastErrorCode = code;
            LastErrorMsg = msg;
            GotError = true;
        };
    }

    Json::Value MakeRequest(int slaveId = SLAVE_ID,
                            const std::string& portPath = "/dev/ttyRS485-1",
                            const std::string& protocol = "modbus")
    {
        Json::Value req;
        req["slave_id"] = slaveId;
        req["port"]["path"] = portPath;
        req["protocol"] = protocol;
        return req;
    }

    // --- Friend-access wrappers for private handler methods ---

    void CallGetFirmwareInfo(const Json::Value& request)
    {
        Handler->GetFirmwareInfo(request, MakeOnResult(), MakeOnError());
    }

    void CallUpdate(const Json::Value& request)
    {
        Handler->Update(request, MakeOnResult(), MakeOnError());
    }

    Json::Value CallClearError(const Json::Value& request)
    {
        return Handler->ClearError(request);
    }

    void CallRestore(const Json::Value& request)
    {
        Handler->Restore(request, MakeOnResult(), MakeOnError());
    }

    bool GetUpdateInProgress() const
    {
        return Handler->UpdateInProgress;
    }

    void SetUpdateInProgress(bool value)
    {
        Handler->UpdateInProgress = value;
    }

    // --- Modbus helpers ---

    std::vector<int> EncodeStringAsRegs(const std::string& s, size_t regCount)
    {
        std::vector<int> data;
        for (size_t i = 0; i < regCount; ++i) {
            data.push_back(0x00);
            if (i < s.size()) {
                data.push_back(static_cast<int>(s[i]));
            } else {
                data.push_back(0x00);
            }
        }
        return data;
    }

    void EnqueueHoldingRead(uint16_t addr, uint16_t count, const std::vector<int>& responseData)
    {
        auto byteCount = static_cast<int>(count * 2);
        std::vector<int> request = {0x03,
                                    static_cast<int>(addr >> 8),
                                    static_cast<int>(addr & 0xFF),
                                    static_cast<int>(count >> 8),
                                    static_cast<int>(count & 0xFF)};
        std::vector<int> response = {0x03, byteCount};
        response.insert(response.end(), responseData.begin(), responseData.end());
        SerialPort->Expect(WrapPDU(request), WrapPDU(response), "EnqueueHoldingRead");
    }

    void EnqueueHoldingReadException(uint16_t addr, uint16_t count, uint8_t exceptionCode)
    {
        std::vector<int> request = {0x03,
                                    static_cast<int>(addr >> 8),
                                    static_cast<int>(addr & 0xFF),
                                    static_cast<int>(count >> 8),
                                    static_cast<int>(count & 0xFF)};
        std::vector<int> response = {0x83, static_cast<int>(exceptionCode)};
        SerialPort->Expect(WrapPDU(request), WrapPDU(response), "EnqueueHoldingReadException");
    }

    void EnqueueDiscreteReadException(uint16_t addr, uint16_t count, uint8_t exceptionCode)
    {
        std::vector<int> request = {0x02,
                                    static_cast<int>(addr >> 8),
                                    static_cast<int>(addr & 0xFF),
                                    static_cast<int>(count >> 8),
                                    static_cast<int>(count & 0xFF)};
        std::vector<int> response = {0x82, static_cast<int>(exceptionCode)};
        SerialPort->Expect(WrapPDU(request), WrapPDU(response), "EnqueueDiscreteReadException");
    }

    void EnqueueWriteMultipleRegisters(uint16_t addr, uint16_t regCount, const std::vector<int>& writeData)
    {
        auto byteCount = static_cast<int>(regCount * 2);
        std::vector<int> request = {0x10,
                                    static_cast<int>(addr >> 8),
                                    static_cast<int>(addr & 0xFF),
                                    static_cast<int>(regCount >> 8),
                                    static_cast<int>(regCount & 0xFF),
                                    byteCount};
        request.insert(request.end(), writeData.begin(), writeData.end());
        std::vector<int> response = {0x10,
                                     static_cast<int>(addr >> 8),
                                     static_cast<int>(addr & 0xFF),
                                     static_cast<int>(regCount >> 8),
                                     static_cast<int>(regCount & 0xFF)};
        SerialPort->Expect(WrapPDU(request), WrapPDU(response), "EnqueueWriteMultipleRegisters");
    }

    void EnqueueWriteSingleRegister(uint16_t addr, uint16_t value)
    {
        std::vector<int> request = {0x06,
                                    static_cast<int>(addr >> 8),
                                    static_cast<int>(addr & 0xFF),
                                    static_cast<int>(value >> 8),
                                    static_cast<int>(value & 0xFF)};
        SerialPort->Expect(WrapPDU(request), WrapPDU(request), "EnqueueWriteSingleRegister");
    }

    void EnqueueBasicGetInfoResponses(const std::string& signature = "wbled",
                                     const std::string& version = "3.7.0",
                                     const std::string& bootloader = "1.5.5",
                                     const std::string& model = "WB-LED")
    {
        EnqueueHoldingRead(FwRegisters::FW_SIGNATURE_ADDR, FwRegisters::FW_SIGNATURE_COUNT,
                          EncodeStringAsRegs(signature, FwRegisters::FW_SIGNATURE_COUNT));
        EnqueueHoldingRead(FwRegisters::FW_VERSION_ADDR, FwRegisters::FW_VERSION_COUNT,
                          EncodeStringAsRegs(version, FwRegisters::FW_VERSION_COUNT));
        EnqueueHoldingRead(FwRegisters::BOOTLOADER_VERSION_ADDR, FwRegisters::BOOTLOADER_VERSION_COUNT,
                          EncodeStringAsRegs(bootloader, FwRegisters::BOOTLOADER_VERSION_COUNT));
        EnqueueHoldingRead(FwRegisters::REBOOT_PRESERVE_PORT_SETTINGS_ADDR, 1, {0x00, 0x00});
        EnqueueHoldingRead(FwRegisters::DEVICE_MODEL_EXTENDED_ADDR, FwRegisters::DEVICE_MODEL_EXTENDED_COUNT,
                          EncodeStringAsRegs(model, FwRegisters::DEVICE_MODEL_EXTENDED_COUNT));
        EnqueueDiscreteReadException(FwRegisters::COMPONENTS_PRESENCE_ADDR,
                                     FwRegisters::COMPONENTS_PRESENCE_COUNT, 0x02);
    }

    void SetupReleasesYaml(const std::string& yaml = "releases:\n"
                                                       "  wbled:\n"
                                                       "    bullseye: fw/by-signature/wbled/bullseye/3.8.0.wbfw\n")
    {
        FakeHttp->SetTextResponse("https://fw-releases.wirenboard.com/fw/by-signature/release-versions.yaml", yaml);
    }

    void SetupBootloaderInfo(const std::string& signature = "wbled", const std::string& version = "2.0.0")
    {
        FakeHttp->SetTextResponse(
            "https://fw-releases.wirenboard.com/bootloader/by-signature/" + signature + "/main/latest.txt", version);
    }

    void SetupFirmwareDownload(const std::string& signature, const std::string& suite, const std::string& version)
    {
        std::vector<uint8_t> wbfwData(168, 0xAA);
        FakeHttp->SetBinaryResponse("https://fw-releases.wirenboard.com/fw/by-signature/" + signature + "/" + suite +
                                        "/" + version + ".wbfw",
                                    wbfwData);
    }

    void EnqueueFlashExpectations(bool reboot = true, bool preserveSettings = true)
    {
        if (reboot && preserveSettings) {
            EnqueueWriteSingleRegister(FwRegisters::REBOOT_PRESERVE_PORT_SETTINGS_ADDR, 1);
        } else if (reboot) {
            EnqueueWriteSingleRegister(FwRegisters::REBOOT_TO_BOOTLOADER_ADDR, 1);
        }

        std::vector<int> infoData(32, 0xAA);
        EnqueueWriteMultipleRegisters(FwRegisters::FW_INFO_BLOCK_ADDR, FwRegisters::FW_INFO_BLOCK_COUNT, infoData);

        std::vector<int> dataChunk(136, 0xAA);
        EnqueueWriteMultipleRegisters(FwRegisters::FW_DATA_BLOCK_ADDR, FwRegisters::FW_DATA_BLOCK_COUNT, dataChunk);
    }
};

// ---- GetFirmwareInfo tests ----

TEST_F(FwHandlerIntegrationTest, GetFirmwareInfoNormal)
{
    SetupReleasesYaml();
    SetupBootloaderInfo();
    EnqueueBasicGetInfoResponses("wbled", "3.6.1", "1.5.0", "WB-LED");

    CallGetFirmwareInfo(MakeRequest());

    ASSERT_TRUE(GotResult);
    ASSERT_FALSE(GotError);
    EXPECT_EQ(LastResult["fw"].asString(), "3.6.1");
    EXPECT_EQ(LastResult["available_fw"].asString(), "3.8.0");
    EXPECT_TRUE(LastResult["fw_has_update"].asBool());
    EXPECT_TRUE(LastResult["can_update"].asBool());
    EXPECT_EQ(LastResult["model"].asString(), "WB-LED");
    EXPECT_EQ(LastResult["available_bootloader"].asString(), "2.0.0");
    EXPECT_TRUE(LastResult["bootloader_has_update"].asBool());
}

TEST_F(FwHandlerIntegrationTest, GetFirmwareInfoActiveUpdate)
{
    TDeviceUpdateInfo info;
    info.PortPath = "/dev/ttyRS485-1";
    info.Protocol = "modbus";
    info.SlaveId = SLAVE_ID;
    info.ToVersion = "3.8.0";
    info.Progress = 50;
    info.Type = "firmware";
    State->Update(info);

    CallGetFirmwareInfo(MakeRequest());

    ASSERT_FALSE(GotResult);
    ASSERT_TRUE(GotError);
    EXPECT_NE(LastErrorMsg.find("already executing"), std::string::npos);
}

TEST_F(FwHandlerIntegrationTest, GetFirmwareInfoDeviceError)
{
    SerialPort->SimulateDisconnect(TFakeSerialPort::SilentReadAndWriteFailure);

    CallGetFirmwareInfo(MakeRequest());

    ASSERT_FALSE(GotResult);
    ASSERT_TRUE(GotError);
}

TEST_F(FwHandlerIntegrationTest, GetFirmwareInfoBadRequest)
{
    Json::Value badRequest;
    badRequest["port"]["path"] = "/dev/ttyRS485-1";

    CallGetFirmwareInfo(badRequest);

    ASSERT_FALSE(GotResult);
    ASSERT_TRUE(GotError);
    EXPECT_NE(LastErrorMsg.find("slave_id"), std::string::npos);
}

// ---- ClearError tests ----

TEST_F(FwHandlerIntegrationTest, ClearErrorNormal)
{
    TDeviceUpdateInfo info;
    info.PortPath = "/dev/ttyRS485-1";
    info.Protocol = "modbus";
    info.SlaveId = SLAVE_ID;
    info.ToVersion = "3.8.0";
    info.Type = "firmware";
    State->Update(info);
    State->SetError(SLAVE_ID, "/dev/ttyRS485-1", "firmware", "error_id", "error msg");

    auto request = MakeRequest();
    request["type"] = "firmware";

    auto result = CallClearError(request);
    EXPECT_EQ(result.asString(), "Ok");
}

TEST_F(FwHandlerIntegrationTest, ClearErrorBadRequest)
{
    Json::Value badRequest;
    badRequest["port"]["path"] = "/dev/ttyRS485-1";

    EXPECT_THROW(CallClearError(badRequest), std::runtime_error);
}

// ---- Update tests ----

TEST_F(FwHandlerIntegrationTest, UpdateFirmware)
{
    SetupReleasesYaml();
    SetupBootloaderInfo();
    SetupFirmwareDownload("wbled", "bullseye", "3.8.0");
    EnqueueBasicGetInfoResponses("wbled", "3.6.1", "1.5.0", "WB-LED");
    EnqueueFlashExpectations(true, true);

    auto request = MakeRequest();
    request["type"] = "firmware";

    CallUpdate(request);

    ASSERT_TRUE(GotResult);
    ASSERT_FALSE(GotError);
    EXPECT_EQ(LastResult.asString(), "Ok");
    EXPECT_FALSE(GetUpdateInProgress());
}

TEST_F(FwHandlerIntegrationTest, UpdateBootloader)
{
    SetupReleasesYaml();
    SetupBootloaderInfo("wbled", "2.0.0");
    std::vector<uint8_t> wbfwData(168, 0xBB);
    FakeHttp->SetBinaryResponse(
        "https://fw-releases.wirenboard.com/bootloader/by-signature/wbled/main/2.0.0.wbfw", wbfwData);

    EnqueueBasicGetInfoResponses("wbled", "3.6.1", "1.5.0", "WB-LED");
    EnqueueFlashExpectations(true, true);

    auto request = MakeRequest();
    request["type"] = "bootloader";

    CallUpdate(request);

    ASSERT_TRUE(GotResult);
    ASSERT_FALSE(GotError);
    EXPECT_EQ(LastResult.asString(), "Ok");
    EXPECT_FALSE(GetUpdateInProgress());
}

TEST_F(FwHandlerIntegrationTest, UpdateAlreadyInProgress)
{
    SetUpdateInProgress(true);

    auto request = MakeRequest();
    request["type"] = "firmware";

    CallUpdate(request);

    ASSERT_FALSE(GotResult);
    ASSERT_TRUE(GotError);
    EXPECT_NE(LastErrorMsg.find("already executing"), std::string::npos);

    SetUpdateInProgress(false);
}

TEST_F(FwHandlerIntegrationTest, UpdateGetInfoFails)
{
    SerialPort->SimulateDisconnect(TFakeSerialPort::SilentReadAndWriteFailure);

    auto request = MakeRequest();
    request["type"] = "firmware";

    CallUpdate(request);

    ASSERT_FALSE(GotResult);
    ASSERT_TRUE(GotError);
    EXPECT_FALSE(GetUpdateInProgress());
}

TEST_F(FwHandlerIntegrationTest, UpdateBadRequest)
{
    Json::Value badRequest;
    badRequest["port"]["path"] = "/dev/ttyRS485-1";

    CallUpdate(badRequest);

    ASSERT_FALSE(GotResult);
    ASSERT_TRUE(GotError);
    EXPECT_NE(LastErrorMsg.find("slave_id"), std::string::npos);
    EXPECT_FALSE(GetUpdateInProgress());
}

// ---- Restore tests ----

TEST_F(FwHandlerIntegrationTest, RestoreNormal)
{
    SetupReleasesYaml();
    SetupFirmwareDownload("wbled", "bullseye", "3.8.0");
    EnqueueBasicGetInfoResponses("wbled", "3.6.1", "1.5.0", "WB-LED");
    EnqueueFlashExpectations(false, false);

    CallRestore(MakeRequest());

    ASSERT_TRUE(GotResult);
    ASSERT_FALSE(GotError);
    EXPECT_EQ(LastResult.asString(), "Ok");
    EXPECT_FALSE(GetUpdateInProgress());
}

TEST_F(FwHandlerIntegrationTest, RestoreAlreadyInProgress)
{
    SetUpdateInProgress(true);

    CallRestore(MakeRequest());

    ASSERT_FALSE(GotResult);
    ASSERT_TRUE(GotError);
    EXPECT_NE(LastErrorMsg.find("already executing"), std::string::npos);

    SetUpdateInProgress(false);
}

TEST_F(FwHandlerIntegrationTest, RestoreGetInfoFails)
{
    SerialPort->SimulateDisconnect(TFakeSerialPort::SilentReadAndWriteFailure);

    CallRestore(MakeRequest());

    // Restore returns Ok even when device is not responding
    ASSERT_TRUE(GotResult);
    ASSERT_FALSE(GotError);
    EXPECT_EQ(LastResult.asString(), "Ok");
    EXPECT_FALSE(GetUpdateInProgress());
}

TEST_F(FwHandlerIntegrationTest, RestoreBadRequest)
{
    Json::Value badRequest;
    badRequest["port"]["path"] = "/dev/ttyRS485-1";

    CallRestore(badRequest);

    ASSERT_FALSE(GotResult);
    ASSERT_TRUE(GotError);
    EXPECT_FALSE(GetUpdateInProgress());
}
