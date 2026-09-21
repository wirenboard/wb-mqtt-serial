#include <gtest/gtest.h>

#include "rpc/rpc_fw_downloader.h"
#include "rpc/rpc_fw_update_handler.h"
#include "rpc/rpc_fw_update_helpers.h"
#include "rpc/rpc_fw_update_state.h"
#include "rpc/rpc_fw_update_task.h"
#include "rpc/rpc_helpers.h"

#include "fake_serial_port.h"
#include "modbus_expectations_base.h"
#include "test_utils.h"

#include <cstring>
#include <filesystem>
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
            RequestCount[url]++;
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
            RequestCount[url]++;
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
    std::string yaml = "releases:\n"
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
    std::string yaml = "other:\n"
                       "  key: value\n";
    auto result = ParseReleaseVersionsYaml(yaml);
    EXPECT_TRUE(result.empty());
}

TEST_F(ReleaseYAMLTest, MissingSignature)
{
    std::string yaml = "releases:\n"
                       "  wbled:\n"
                       "    wb-2307: fw/path.wbfw\n";

    auto result = ParseReleaseVersionsYaml(yaml);
    EXPECT_EQ(result.find("nonexistent"), result.end());
}

TEST_F(ReleaseYAMLTest, CommentsAndBlankLines)
{
    std::string yaml = "# This is a comment\n"
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
    std::string yaml = "releases:\n"
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
        return ParseJson(PublishLog.back().Payload);
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

    state.SetError(42, "/dev/ttyRS485-1", "firmware", "com.wb.serial_driver.generic_error", "Internal error");

    auto json = ParseLastPayload();
    ASSERT_EQ(json["devices"].size(), 1u);
    EXPECT_FALSE(json["devices"][0]["error"].isNull());
    EXPECT_EQ(json["devices"][0]["error"]["id"].asString(), "com.wb.serial_driver.generic_error");
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

TEST_F(FwDownloaderTest, GetReleasedBootloader)
{
    FakeHttp->SetTextResponse("https://fw-releases.wirenboard.com/boot/by-signature/release-versions.yaml",
                              "releases:\n"
                              "  wbled:\n"
                              "    wb-2307: boot/by-signature/wbled/main/2.1.0.wbfw\n");

    auto result = Downloader.GetReleasedBootloader("wbled", "wb-2307");
    EXPECT_EQ(result.Version, "2.1.0");
    EXPECT_EQ(result.Endpoint, "https://fw-releases.wirenboard.com/boot/by-signature/wbled/main/2.1.0.wbfw");
}

TEST_F(FwDownloaderTest, GetReleasedBootloaderSuiteNotFound)
{
    FakeHttp->SetTextResponse("https://fw-releases.wirenboard.com/boot/by-signature/release-versions.yaml",
                              "releases:\n"
                              "  wbled:\n"
                              "    stable: boot/by-signature/wbled/main/2.1.0.wbfw\n");

    EXPECT_THROW(Downloader.GetReleasedBootloader("wbled", "testing"), std::runtime_error);
}

TEST_F(FwDownloaderTest, GetReleasedBootloaderEmptySignatureThrowsWithoutHttp)
{
    // An empty signature has no released bootloader; the downloader must reject it
    // before making any request to the release-versions index.
    auto indexUrl = "https://fw-releases.wirenboard.com/boot/by-signature/release-versions.yaml";
    FakeHttp->SetTextResponse(indexUrl,
                              "releases:\n"
                              "  wbled:\n"
                              "    wb-2307: boot/by-signature/wbled/main/2.1.0.wbfw\n");

    EXPECT_THROW(Downloader.GetReleasedBootloader("", "wb-2307"), std::runtime_error);
    EXPECT_EQ(FakeHttp->GetRequestCount(indexUrl), 0);
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

TEST_F(FwDownloaderTest, CacheOnlyLookupDoesNotUseNetwork)
{
    auto fwUrl = "https://fw-releases.wirenboard.com/fw/by-signature/release-versions.yaml";
    auto bootUrl = "https://fw-releases.wirenboard.com/boot/by-signature/release-versions.yaml";
    FakeHttp->SetTextResponse(fwUrl,
                              "releases:\n"
                              "  sig1:\n"
                              "    suite1: fw/path/1.0.0.wbfw\n");
    FakeHttp->SetTextResponse(bootUrl,
                              "releases:\n"
                              "  sig1:\n"
                              "    suite1: boot/path/2.0.0.wbfw\n");

    EXPECT_THROW(Downloader.GetReleasedFirmware("sig1", "suite1", ENetworkAccess::CacheOnly), std::runtime_error);
    EXPECT_THROW(Downloader.GetReleasedBootloader("sig1", "suite1", ENetworkAccess::CacheOnly), std::runtime_error);
    EXPECT_EQ(FakeHttp->GetRequestCount(fwUrl), 0);
    EXPECT_EQ(FakeHttp->GetRequestCount(bootUrl), 0);
}

TEST_F(FwDownloaderTest, PrefetchedIndexesAreAvailableForCacheOnlyLookups)
{
    auto fwUrl = "https://fw-releases.wirenboard.com/fw/by-signature/release-versions.yaml";
    auto bootUrl = "https://fw-releases.wirenboard.com/boot/by-signature/release-versions.yaml";
    FakeHttp->SetTextResponse(fwUrl,
                              "releases:\n"
                              "  sig1:\n"
                              "    suite1: fw/path/1.0.0.wbfw\n");
    FakeHttp->SetTextResponse(bootUrl,
                              "releases:\n"
                              "  sig1:\n"
                              "    suite1: boot/path/2.0.0.wbfw\n");

    Downloader.PrefetchReleaseIndexes();

    EXPECT_EQ(Downloader.GetReleasedFirmware("sig1", "suite1", ENetworkAccess::CacheOnly).Version, "1.0.0");
    EXPECT_EQ(Downloader.GetReleasedBootloader("sig1", "suite1", ENetworkAccess::CacheOnly).Version, "2.0.0");
    EXPECT_EQ(FakeHttp->GetRequestCount(fwUrl), 1);
    EXPECT_EQ(FakeHttp->GetRequestCount(bootUrl), 1);
}

TEST_F(FwDownloaderTest, PrefetchIgnoresDownloadErrors)
{
    FakeHttp->SetError("https://fw-releases.wirenboard.com/fw/by-signature/release-versions.yaml", "Timeout");
    FakeHttp->SetError("https://fw-releases.wirenboard.com/boot/by-signature/release-versions.yaml", "Timeout");

    EXPECT_NO_THROW(Downloader.PrefetchReleaseIndexes());
}

TEST_F(FwDownloaderTest, FailedDownloadIsNotRetriedImmediately)
{
    auto url = "https://fw-releases.wirenboard.com/fw/by-signature/release-versions.yaml";
    FakeHttp->SetError(url, "Timeout was reached");

    EXPECT_THROW(Downloader.GetReleasedFirmware("sig1", "suite1"), std::runtime_error);
    EXPECT_THROW(Downloader.GetReleasedFirmware("sig1", "suite1"), std::runtime_error);
    Downloader.PrefetchReleaseIndexes();

    EXPECT_EQ(FakeHttp->GetRequestCount(url), 1);
}

// ============================================================
//           Shared Modbus test helpers
// ============================================================

// Mixin providing common Modbus request/response helpers for firmware update tests.
// Subclasses must inherit from TModbusExpectationsBase (for WrapPDU) and provide SerialPort.
class TFwModbusTestHelpers
{
protected:
    virtual PFakeSerialPort GetSerialPort() const = 0;
    virtual std::vector<int> WrapPDU(const std::vector<int>& pdu) = 0;

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
        GetSerialPort()->Expect(WrapPDU(request), WrapPDU(response), "EnqueueHoldingRead");
    }

    void EnqueueHoldingReadNoResponse(uint16_t addr, uint16_t count)
    {
        std::vector<int> request = {0x03,
                                    static_cast<int>(addr >> 8),
                                    static_cast<int>(addr & 0xFF),
                                    static_cast<int>(count >> 8),
                                    static_cast<int>(count & 0xFF)};
        GetSerialPort()->ExpectNoResponse(WrapPDU(request), "EnqueueHoldingReadNoResponse");
    }

    void EnqueueHoldingReadException(uint16_t addr, uint16_t count, uint8_t exceptionCode)
    {
        std::vector<int> request = {0x03,
                                    static_cast<int>(addr >> 8),
                                    static_cast<int>(addr & 0xFF),
                                    static_cast<int>(count >> 8),
                                    static_cast<int>(count & 0xFF)};
        std::vector<int> response = {0x83, static_cast<int>(exceptionCode)};
        GetSerialPort()->Expect(WrapPDU(request), WrapPDU(response), "EnqueueHoldingReadException");
    }

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
        GetSerialPort()->Expect(WrapPDU(request), WrapPDU(response), "EnqueueDiscreteRead");
    }

    void EnqueueDiscreteReadException(uint16_t addr, uint16_t count, uint8_t exceptionCode)
    {
        std::vector<int> request = {0x02,
                                    static_cast<int>(addr >> 8),
                                    static_cast<int>(addr & 0xFF),
                                    static_cast<int>(count >> 8),
                                    static_cast<int>(count & 0xFF)};
        std::vector<int> response = {0x82, static_cast<int>(exceptionCode)};
        GetSerialPort()->Expect(WrapPDU(request), WrapPDU(response), "EnqueueDiscreteReadException");
    }

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
        GetSerialPort()->Expect(WrapPDU(request), WrapPDU(response), "EnqueueInputRead");
    }

    std::vector<int> MakeWriteSingleRegisterRequest(uint16_t addr, uint16_t value)
    {
        return {0x06,
                static_cast<int>(addr >> 8),
                static_cast<int>(addr & 0xFF),
                static_cast<int>(value >> 8),
                static_cast<int>(value & 0xFF)};
    }

    void EnqueueWriteSingleRegister(uint16_t addr, uint16_t value)
    {
        auto request = MakeWriteSingleRegisterRequest(addr, value);
        GetSerialPort()->Expect(WrapPDU(request), WrapPDU(request), "EnqueueWriteSingleRegister");
    }

    void EnqueueWriteSingleRegisterNoResponse(uint16_t addr, uint16_t value)
    {
        GetSerialPort()->ExpectNoResponse(WrapPDU(MakeWriteSingleRegisterRequest(addr, value)),
                                          "EnqueueWriteSingleRegisterNoResponse");
    }

    void EnqueueWriteSingleRegisterException(uint16_t addr, uint16_t value, uint8_t exceptionCode)
    {
        std::vector<int> response = {0x86, static_cast<int>(exceptionCode)};
        GetSerialPort()->Expect(WrapPDU(MakeWriteSingleRegisterRequest(addr, value)),
                                WrapPDU(response),
                                "EnqueueWriteSingleRegisterException");
    }

    std::vector<int> MakeWriteMultipleRegistersRequest(uint16_t addr,
                                                       uint16_t regCount,
                                                       const std::vector<int>& writeData)
    {
        std::vector<int> request = {0x10,
                                    static_cast<int>(addr >> 8),
                                    static_cast<int>(addr & 0xFF),
                                    static_cast<int>(regCount >> 8),
                                    static_cast<int>(regCount & 0xFF),
                                    static_cast<int>(regCount * 2)};
        request.insert(request.end(), writeData.begin(), writeData.end());
        return request;
    }

    void EnqueueWriteMultipleRegisters(uint16_t addr, uint16_t regCount, const std::vector<int>& writeData)
    {
        std::vector<int> response = {0x10,
                                     static_cast<int>(addr >> 8),
                                     static_cast<int>(addr & 0xFF),
                                     static_cast<int>(regCount >> 8),
                                     static_cast<int>(regCount & 0xFF)};
        GetSerialPort()->Expect(WrapPDU(MakeWriteMultipleRegistersRequest(addr, regCount, writeData)),
                                WrapPDU(response),
                                "EnqueueWriteMultipleRegisters");
    }

    void EnqueueWriteMultipleRegistersNoResponse(uint16_t addr, uint16_t regCount, const std::vector<int>& writeData)
    {
        GetSerialPort()->ExpectNoResponse(WrapPDU(MakeWriteMultipleRegistersRequest(addr, regCount, writeData)),
                                          "EnqueueWriteMultipleRegistersNoResponse");
    }

    //! Everything TFwGetInfoTask reads before the components
    void EnqueueDeviceInfoReads(const std::string& signature = "wbled",
                                const std::string& version = "3.7.0",
                                const std::string& bootloader = "1.5.5",
                                const std::string& model = "WB-LED",
                                bool canPreservePortSettings = true)
    {
        EnqueueHoldingRead(FwRegisters::FW_SIGNATURE_ADDR,
                           FwRegisters::FW_SIGNATURE_COUNT,
                           EncodeStringAsRegs(signature, FwRegisters::FW_SIGNATURE_COUNT));
        EnqueueHoldingRead(FwRegisters::FW_VERSION_ADDR,
                           FwRegisters::FW_VERSION_COUNT,
                           EncodeStringAsRegs(version, FwRegisters::FW_VERSION_COUNT));
        EnqueueHoldingRead(FwRegisters::BOOTLOADER_VERSION_ADDR,
                           FwRegisters::BOOTLOADER_VERSION_COUNT,
                           EncodeStringAsRegs(bootloader, FwRegisters::BOOTLOADER_VERSION_COUNT));
        if (canPreservePortSettings) {
            EnqueueHoldingRead(FwRegisters::REBOOT_PRESERVE_PORT_SETTINGS_ADDR, 1, {0x00, 0x00});
        } else {
            EnqueueHoldingReadException(FwRegisters::REBOOT_PRESERVE_PORT_SETTINGS_ADDR, 1, 0x02);
        }
        EnqueueHoldingRead(FwRegisters::DEVICE_MODEL_EXTENDED_ADDR,
                           FwRegisters::DEVICE_MODEL_EXTENDED_COUNT,
                           EncodeStringAsRegs(model, FwRegisters::DEVICE_MODEL_EXTENDED_COUNT));
    }

    //! A device without components
    void EnqueueBasicGetInfoResponses(const std::string& signature = "wbled",
                                      const std::string& version = "3.7.0",
                                      const std::string& bootloader = "1.5.5",
                                      const std::string& model = "WB-LED",
                                      bool canPreservePortSettings = true)
    {
        EnqueueDeviceInfoReads(signature, version, bootloader, model, canPreservePortSettings);
        EnqueueDiscreteReadException(FwRegisters::COMPONENTS_PRESENCE_ADDR,
                                     FwRegisters::COMPONENTS_PRESENCE_COUNT,
                                     0x02);
    }

    void EnqueueComponentInfoReads(int number,
                                   const std::string& signature,
                                   const std::string& version,
                                   const std::string& model)
    {
        auto offset = static_cast<uint16_t>(number * FwRegisters::COMPONENT_STEP);
        EnqueueInputRead(FwRegisters::COMPONENT_SIGNATURE_BASE + offset,
                         FwRegisters::COMPONENT_SIGNATURE_COUNT,
                         EncodeStringAsRegs(signature, FwRegisters::COMPONENT_SIGNATURE_COUNT));
        EnqueueInputRead(FwRegisters::COMPONENT_FW_VERSION_BASE + offset,
                         FwRegisters::COMPONENT_FW_VERSION_COUNT,
                         EncodeStringAsRegs(version, FwRegisters::COMPONENT_FW_VERSION_COUNT));
        EnqueueInputRead(FwRegisters::COMPONENT_MODEL_BASE + offset,
                         FwRegisters::COMPONENT_MODEL_COUNT,
                         EncodeStringAsRegs(model, FwRegisters::COMPONENT_MODEL_COUNT));
    }

    //! A bootloader gives its version only as a whole and does not answer a partial read
    void EnqueueBootloaderModeResponses(const std::string& bootloader = "1.5.0")
    {
        EnqueueHoldingRead(FwRegisters::BOOTLOADER_VERSION_ADDR,
                           FwRegisters::BOOTLOADER_VERSION_FULL_COUNT,
                           EncodeStringAsRegs(bootloader, FwRegisters::BOOTLOADER_VERSION_FULL_COUNT));
        EnqueueHoldingReadException(FwRegisters::BOOTLOADER_VERSION_ADDR,
                                    FwRegisters::BOOTLOADER_VERSION_COUNT,
                                    Modbus::GATEWAY_TARGET_DEVICE_FAILED_TO_RESPOND);
    }

    //! A device in the firmware mode answers a partial read of the bootloader version
    void EnqueueFirmwareModeResponses(const std::string& bootloader = "1.5.0")
    {
        EnqueueHoldingRead(FwRegisters::BOOTLOADER_VERSION_ADDR,
                           FwRegisters::BOOTLOADER_VERSION_FULL_COUNT,
                           EncodeStringAsRegs(bootloader, FwRegisters::BOOTLOADER_VERSION_FULL_COUNT));
        EnqueueHoldingRead(FwRegisters::BOOTLOADER_VERSION_ADDR,
                           FwRegisters::BOOTLOADER_VERSION_COUNT,
                           EncodeStringAsRegs(bootloader, FwRegisters::BOOTLOADER_VERSION_COUNT));
    }

    virtual ~TFwModbusTestHelpers() = default;
};

// ============================================================
//           7. Modbus Task Tests (TFwGetInfoTask, TFwFlashTask)
// Cf. firmware_update_test.py TestGetFirmwareInfo, TestFlashFw,
//     TestRebootToBootloader, TestUpdateSoftware
// ============================================================

class TFwTaskTest: public TSerialDeviceTest, public TModbusExpectationsBase, public TFwModbusTestHelpers
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

    PFakeSerialPort GetSerialPort() const override
    {
        return SerialPort;
    }

    std::vector<int> WrapPDU(const std::vector<int>& pdu) override
    {
        return TModbusExpectationsBase::WrapPDU(pdu);
    }

    static constexpr uint8_t SLAVE_ID = 42;

    PFeaturePort FeaturePort;
    std::unique_ptr<TSerialClientDeviceAccessHandler> AccessHandler;
    std::list<PSerialDevice> EmptyDeviceList;
};

// ---- TFwGetInfoTask tests ----
// Cf. firmware_update_test.py TestGetFirmwareInfo

TEST_F(TFwTaskTest, HasDefaultPortSettings)
{
    EnqueueHoldingRead(110, 1, {0x00, 0x60}); // 96 * 100 = 9600 baud
    EnqueueHoldingRead(111, 1, {0x00, 0x00}); // no parity

    auto traits = MakeModbusTraits("modbus");
    EXPECT_TRUE(HasDefaultPortSettings(*traits, *FeaturePort, SLAVE_ID));
}

TEST_F(TFwTaskTest, HasNoDefaultPortSettings)
{
    EnqueueHoldingRead(110, 1, {0x04, 0x80}); // 1152 * 100 = 115200 baud

    auto traits = MakeModbusTraits("modbus");
    EXPECT_FALSE(HasDefaultPortSettings(*traits, *FeaturePort, SLAVE_ID));
}

//! Such a device answers the bootloader on 9600 but with parity, which the bootloader does not use
TEST_F(TFwTaskTest, HasNoDefaultPortSettingsWithParity)
{
    EnqueueHoldingRead(110, 1, {0x00, 0x60}); // 96 * 100 = 9600 baud
    EnqueueHoldingRead(111, 1, {0x00, 0x02}); // even parity

    auto traits = MakeModbusTraits("modbus");
    EXPECT_FALSE(HasDefaultPortSettings(*traits, *FeaturePort, SLAVE_ID));
}

TEST_F(TFwTaskTest, HasNoDefaultPortSettingsWhenRegistersAreUnavailable)
{
    EnqueueHoldingReadException(110, 1, 0x02);

    auto traits = MakeModbusTraits("modbus");
    EXPECT_FALSE(HasDefaultPortSettings(*traits, *FeaturePort, SLAVE_ID));
}

//! A device sitting in its bootloader on a direct line does not answer a partial version read
TEST_F(TFwTaskTest, IsInBootloaderModeWhenDeviceDoesNotAnswer)
{
    EnqueueHoldingRead(FwRegisters::BOOTLOADER_VERSION_ADDR,
                       FwRegisters::BOOTLOADER_VERSION_FULL_COUNT,
                       EncodeStringAsRegs("1.5.0", FwRegisters::BOOTLOADER_VERSION_FULL_COUNT));
    EnqueueHoldingReadNoResponse(FwRegisters::BOOTLOADER_VERSION_ADDR, FwRegisters::BOOTLOADER_VERSION_COUNT);

    auto traits = MakeModbusTraits("modbus");
    EXPECT_TRUE(IsInBootloaderMode(*traits, *FeaturePort, SLAVE_ID));
}

//! An error in the answer is not silence, such a device is not asked to restore its firmware
TEST_F(TFwTaskTest, IsInBootloaderModeWhenDeviceAnswersWithError)
{
    EnqueueHoldingRead(FwRegisters::BOOTLOADER_VERSION_ADDR,
                       FwRegisters::BOOTLOADER_VERSION_FULL_COUNT,
                       EncodeStringAsRegs("1.5.0", FwRegisters::BOOTLOADER_VERSION_FULL_COUNT));
    EnqueueHoldingReadException(FwRegisters::BOOTLOADER_VERSION_ADDR, FwRegisters::BOOTLOADER_VERSION_COUNT, 0x02);

    auto traits = MakeModbusTraits("modbus");
    EXPECT_THROW(IsInBootloaderMode(*traits, *FeaturePort, SLAVE_ID), Modbus::TModbusExceptionError);
}

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
    EnqueueHoldingRead(FwRegisters::FW_SIGNATURE_ADDR,
                       FwRegisters::FW_SIGNATURE_COUNT,
                       EncodeStringAsRegs("wbled", FwRegisters::FW_SIGNATURE_COUNT));
    // Version
    EnqueueHoldingRead(FwRegisters::FW_VERSION_ADDR,
                       FwRegisters::FW_VERSION_COUNT,
                       EncodeStringAsRegs("3.7.0", FwRegisters::FW_VERSION_COUNT));
    // Bootloader - returns exception (old devices don't have this register)
    EnqueueHoldingReadException(FwRegisters::BOOTLOADER_VERSION_ADDR, FwRegisters::BOOTLOADER_VERSION_COUNT, 0x02);
    // Preserve port settings - also fails
    EnqueueHoldingReadException(FwRegisters::REBOOT_PRESERVE_PORT_SETTINGS_ADDR, 1, 0x02);
    // Model - extended fails, fall back to standard
    EnqueueHoldingReadException(FwRegisters::DEVICE_MODEL_EXTENDED_ADDR,
                                FwRegisters::DEVICE_MODEL_EXTENDED_COUNT,
                                0x02);
    EnqueueHoldingRead(FwRegisters::DEVICE_MODEL_ADDR,
                       FwRegisters::DEVICE_MODEL_COUNT,
                       EncodeStringAsRegs("WB-LED", FwRegisters::DEVICE_MODEL_COUNT));
    // No components
    EnqueueDiscreteReadException(FwRegisters::COMPONENTS_PRESENCE_ADDR, FwRegisters::COMPONENTS_PRESENCE_COUNT, 0x02);

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
    EnqueueHoldingRead(FwRegisters::FW_SIGNATURE_ADDR,
                       FwRegisters::FW_SIGNATURE_COUNT,
                       EncodeStringAsRegs("wbmwac", FwRegisters::FW_SIGNATURE_COUNT));
    EnqueueHoldingRead(FwRegisters::FW_VERSION_ADDR,
                       FwRegisters::FW_VERSION_COUNT,
                       EncodeStringAsRegs("2.0.0", FwRegisters::FW_VERSION_COUNT));
    EnqueueHoldingRead(FwRegisters::BOOTLOADER_VERSION_ADDR,
                       FwRegisters::BOOTLOADER_VERSION_COUNT,
                       EncodeStringAsRegs("1.0.0", FwRegisters::BOOTLOADER_VERSION_COUNT));
    EnqueueHoldingRead(FwRegisters::REBOOT_PRESERVE_PORT_SETTINGS_ADDR, 1, {0x00, 0x00});
    EnqueueHoldingRead(FwRegisters::DEVICE_MODEL_EXTENDED_ADDR,
                       FwRegisters::DEVICE_MODEL_EXTENDED_COUNT,
                       EncodeStringAsRegs("WBMWAC-v2", FwRegisters::DEVICE_MODEL_EXTENDED_COUNT));

    // Components presence: bit 0 set = component 0 present
    EnqueueDiscreteRead(FwRegisters::COMPONENTS_PRESENCE_ADDR, FwRegisters::COMPONENTS_PRESENCE_COUNT, {0x01});

    // Component 0 info (READ_INPUT fn=0x04): signature, fw version, model
    uint16_t comp0SigAddr = FwRegisters::COMPONENT_SIGNATURE_BASE;
    uint16_t comp0FwAddr = FwRegisters::COMPONENT_FW_VERSION_BASE;
    uint16_t comp0ModelAddr = FwRegisters::COMPONENT_MODEL_BASE;

    EnqueueInputRead(comp0SigAddr,
                     FwRegisters::COMPONENT_SIGNATURE_COUNT,
                     EncodeStringAsRegs("wbmwac_oc", FwRegisters::COMPONENT_SIGNATURE_COUNT));
    EnqueueInputRead(comp0FwAddr,
                     FwRegisters::COMPONENT_FW_VERSION_COUNT,
                     EncodeStringAsRegs("1.0.0", FwRegisters::COMPONENT_FW_VERSION_COUNT));
    EnqueueInputRead(comp0ModelAddr,
                     FwRegisters::COMPONENT_MODEL_COUNT,
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

//! A device may be unable to report its components right after switching on
TEST_F(TFwTaskTest, GetInfoComponentsPresenceRetriedWhileDeviceIsBusy)
{
    EnqueueDeviceInfoReads("wbmwac", "2.0.0", "1.0.0", "WBMWAC-v2");

    EnqueueDiscreteReadException(FwRegisters::COMPONENTS_PRESENCE_ADDR,
                                 FwRegisters::COMPONENTS_PRESENCE_COUNT,
                                 0x06); // slave device busy
    // Bits 3 and 7 are set
    EnqueueDiscreteRead(FwRegisters::COMPONENTS_PRESENCE_ADDR, FwRegisters::COMPONENTS_PRESENCE_COUNT, {0x88});

    EnqueueComponentInfoReads(3, "wbmwac_oc", "1.0.0", "WBMWAC-OC");
    EnqueueComponentInfoReads(7, "wbmwac_ai", "2.1.0", "WBMWAC-AI");

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
    ASSERT_EQ(resultInfo.Components.size(), 2u);
    EXPECT_EQ(resultInfo.Components[0].Number, 3);
    EXPECT_EQ(resultInfo.Components[0].Signature, "wbmwac_oc");
    EXPECT_EQ(resultInfo.Components[1].Number, 7);
    EXPECT_EQ(resultInfo.Components[1].Model, "WBMWAC-AI");
}

TEST_F(TFwTaskTest, GetInfoUnavailableComponent)
{
    // Standard reads
    EnqueueHoldingRead(FwRegisters::FW_SIGNATURE_ADDR,
                       FwRegisters::FW_SIGNATURE_COUNT,
                       EncodeStringAsRegs("test", FwRegisters::FW_SIGNATURE_COUNT));
    EnqueueHoldingRead(FwRegisters::FW_VERSION_ADDR,
                       FwRegisters::FW_VERSION_COUNT,
                       EncodeStringAsRegs("1.0.0", FwRegisters::FW_VERSION_COUNT));
    EnqueueHoldingReadException(FwRegisters::BOOTLOADER_VERSION_ADDR, FwRegisters::BOOTLOADER_VERSION_COUNT, 0x02);
    EnqueueHoldingReadException(FwRegisters::REBOOT_PRESERVE_PORT_SETTINGS_ADDR, 1, 0x02);
    EnqueueHoldingRead(FwRegisters::DEVICE_MODEL_EXTENDED_ADDR,
                       FwRegisters::DEVICE_MODEL_EXTENDED_COUNT,
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
    EnqueueHoldingRead(FwRegisters::FW_SIGNATURE_ADDR,
                       FwRegisters::FW_SIGNATURE_COUNT,
                       EncodeStringAsRegs("wbled", FwRegisters::FW_SIGNATURE_COUNT));
    EnqueueHoldingRead(FwRegisters::FW_VERSION_ADDR,
                       FwRegisters::FW_VERSION_COUNT,
                       EncodeStringAsRegs("1.0.0", FwRegisters::FW_VERSION_COUNT));
    EnqueueHoldingReadException(FwRegisters::BOOTLOADER_VERSION_ADDR, FwRegisters::BOOTLOADER_VERSION_COUNT, 0x02);
    EnqueueHoldingReadException(FwRegisters::REBOOT_PRESERVE_PORT_SETTINGS_ADDR, 1, 0x02);
    EnqueueHoldingRead(FwRegisters::DEVICE_MODEL_EXTENDED_ADDR,
                       FwRegisters::DEVICE_MODEL_EXTENDED_COUNT,
                       modelWithCtrl);
    EnqueueDiscreteReadException(FwRegisters::COMPONENTS_PRESENCE_ADDR, FwRegisters::COMPONENTS_PRESENCE_COUNT, 0x02);

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

// Third-party devices may answer the FW/bootloader-version register reads with
// arbitrary non-ASCII bytes. ReadFwDeviceInfo must sanitize the version strings at
// the source so no consumer (RPC response, update-progress state, logs) sees garbage.
TEST_F(TFwTaskTest, GetInfoGarbageVersionsSanitized)
{
    EnqueueHoldingRead(FwRegisters::FW_SIGNATURE_ADDR,
                       FwRegisters::FW_SIGNATURE_COUNT,
                       EncodeStringAsRegs("engo", FwRegisters::FW_SIGNATURE_COUNT));
    // FW version: control char + high byte -> not a printable version
    EnqueueHoldingRead(FwRegisters::FW_VERSION_ADDR,
                       FwRegisters::FW_VERSION_COUNT,
                       EncodeStringAsRegs(std::string("\x0F\x80"
                                                      "F"),
                                          FwRegisters::FW_VERSION_COUNT));
    // Bootloader version: control chars only
    EnqueueHoldingRead(FwRegisters::BOOTLOADER_VERSION_ADDR,
                       FwRegisters::BOOTLOADER_VERSION_COUNT,
                       EncodeStringAsRegs(std::string("\x01\x02"), FwRegisters::BOOTLOADER_VERSION_COUNT));
    EnqueueHoldingReadException(FwRegisters::REBOOT_PRESERVE_PORT_SETTINGS_ADDR, 1, 0x02);
    EnqueueHoldingRead(FwRegisters::DEVICE_MODEL_EXTENDED_ADDR,
                       FwRegisters::DEVICE_MODEL_EXTENDED_COUNT,
                       EncodeStringAsRegs("EFAN", FwRegisters::DEVICE_MODEL_EXTENDED_COUNT));
    EnqueueDiscreteReadException(FwRegisters::COMPONENTS_PRESENCE_ADDR, FwRegisters::COMPONENTS_PRESENCE_COUNT, 0x02);

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
    EXPECT_EQ(resultInfo.FwVersion, "");         // garbage dropped at source
    EXPECT_EQ(resultInfo.BootloaderVersion, ""); // garbage dropped at source
    EXPECT_EQ(resultInfo.DeviceModel, "EFAN");
}

// A garbage (non-ASCII-code) signature must be dropped at the source so no consumer
// looks it up or fires a pointless release-server request for an unknown device.
TEST_F(TFwTaskTest, GetInfoGarbageSignatureCleared)
{
    EnqueueHoldingRead(FwRegisters::FW_SIGNATURE_ADDR,
                       FwRegisters::FW_SIGNATURE_COUNT,
                       EncodeStringAsRegs(std::string("\x0F\x80"
                                                      "F"),
                                          FwRegisters::FW_SIGNATURE_COUNT));
    EnqueueHoldingRead(FwRegisters::FW_VERSION_ADDR,
                       FwRegisters::FW_VERSION_COUNT,
                       EncodeStringAsRegs("1.0", FwRegisters::FW_VERSION_COUNT));
    EnqueueHoldingReadException(FwRegisters::BOOTLOADER_VERSION_ADDR, FwRegisters::BOOTLOADER_VERSION_COUNT, 0x02);
    EnqueueHoldingReadException(FwRegisters::REBOOT_PRESERVE_PORT_SETTINGS_ADDR, 1, 0x02);
    EnqueueHoldingRead(FwRegisters::DEVICE_MODEL_EXTENDED_ADDR,
                       FwRegisters::DEVICE_MODEL_EXTENDED_COUNT,
                       EncodeStringAsRegs("EFAN", FwRegisters::DEVICE_MODEL_EXTENDED_COUNT));
    EnqueueDiscreteReadException(FwRegisters::COMPONENTS_PRESENCE_ADDR, FwRegisters::COMPONENTS_PRESENCE_COUNT, 0x02);

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
    EXPECT_EQ(resultInfo.FwSignature, ""); // garbage signature cleared at source
}

TEST_F(TFwTaskTest, GetInfoDeviceNotResponding)
{
    // TFakeSerialPort cannot simulate a timeout, a disconnect is used instead
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

//! A device already in the bootloader is flashed on the settings of the request
TEST_F(TFwTaskTest, FlashNoReboot)
{
    SerialPort->LogSerialPortSettings(true);

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
        SLAVE_ID,
        "modbus",
        fw,
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

//! Such a bootloader keeps the port settings of the firmware, the port is left as it is
TEST_F(TFwTaskTest, FlashWithRebootPreserve)
{
    SerialPort->LogSerialPortSettings(true);

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
        SLAVE_ID,
        "modbus",
        fw,
        true, // reboot
        true, // can preserve port settings
        [&](int) {},
        [&]() { completed = true; },
        [&](const std::string&) {});

    task->Run(FeaturePort, *AccessHandler, EmptyDeviceList);
    EXPECT_TRUE(completed);
}

//! Such a bootloader starts with the factory port settings, the flashing has to switch to them
TEST_F(TFwTaskTest, FlashWithRebootLegacy)
{
    SerialPort->LogSerialPortSettings(true);

    TParsedWBFW fw;
    fw.Info.assign(32, 0x33);
    fw.Data.assign(136, 0x44);

    EnqueueWriteSingleRegister(FwRegisters::REBOOT_TO_BOOTLOADER_ADDR, 1);

    // Info block
    std::vector<int> infoData(fw.Info.begin(), fw.Info.end());
    EnqueueWriteMultipleRegisters(FwRegisters::FW_INFO_BLOCK_ADDR, FwRegisters::FW_INFO_BLOCK_COUNT, infoData);

    // Data block
    std::vector<int> dataChunk(fw.Data.begin(), fw.Data.end());
    EnqueueWriteMultipleRegisters(FwRegisters::FW_DATA_BLOCK_ADDR, FwRegisters::FW_DATA_BLOCK_COUNT, dataChunk);

    bool completed = false;

    auto task = std::make_shared<TFwFlashTask>(
        SLAVE_ID,
        "modbus",
        fw,
        true,  // reboot
        false, // can NOT preserve port settings (use legacy reboot)
        [&](int) {},
        [&]() { completed = true; },
        [&](const std::string&) {});

    task->Run(FeaturePort, *AccessHandler, EmptyDeviceList);
    EXPECT_TRUE(completed);
}

//! The last firmwares reboot without answering, the port has to be switched to the factory
//! settings all the same
TEST_F(TFwTaskTest, FlashWithRebootLegacyWithoutResponse)
{
    SerialPort->LogSerialPortSettings(true);

    TParsedWBFW fw;
    fw.Info.assign(32, 0x33);
    fw.Data.assign(136, 0x44);

    EnqueueWriteSingleRegisterNoResponse(FwRegisters::REBOOT_TO_BOOTLOADER_ADDR, 1);

    std::vector<int> infoData(fw.Info.begin(), fw.Info.end());
    EnqueueWriteMultipleRegisters(FwRegisters::FW_INFO_BLOCK_ADDR, FwRegisters::FW_INFO_BLOCK_COUNT, infoData);

    std::vector<int> dataChunk(fw.Data.begin(), fw.Data.end());
    EnqueueWriteMultipleRegisters(FwRegisters::FW_DATA_BLOCK_ADDR, FwRegisters::FW_DATA_BLOCK_COUNT, dataChunk);

    bool completed = false;

    auto task = std::make_shared<TFwFlashTask>(
        SLAVE_ID,
        "modbus",
        fw,
        true,
        false,
        [&](int) {},
        [&]() { completed = true; },
        [&](const std::string&) {});

    task->Run(FeaturePort, *AccessHandler, EmptyDeviceList);
    EXPECT_TRUE(completed);
}

//! A device which refuses to reboot stays in the firmware mode, the port must keep its settings
TEST_F(TFwTaskTest, FlashWithRebootLegacyRefused)
{
    SerialPort->LogSerialPortSettings(true);

    TParsedWBFW fw;
    fw.Info.assign(32, 0x33);
    fw.Data.assign(136, 0x44);

    EnqueueWriteSingleRegisterException(FwRegisters::REBOOT_TO_BOOTLOADER_ADDR, 1, 0x01);

    bool completed = false;
    bool gotError = false;

    auto task = std::make_shared<TFwFlashTask>(
        SLAVE_ID,
        "modbus",
        fw,
        true,
        false,
        [&](int) {},
        [&]() { completed = true; },
        [&](const std::string&) { gotError = true; });

    task->Run(FeaturePort, *AccessHandler, EmptyDeviceList);
    EXPECT_FALSE(completed);
    EXPECT_TRUE(gotError);
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
        SLAVE_ID,
        "modbus",
        fw,
        false,
        false,
        [&](int p) { lastProgress = p; },
        [&]() { completed = true; },
        [&](const std::string&) {});

    task->Run(FeaturePort, *AccessHandler, EmptyDeviceList);
    EXPECT_TRUE(completed);
    EXPECT_EQ(lastProgress, 100);
}

//! A device may miss a block, the write is repeated up to three times
TEST_F(TFwTaskTest, FlashDataBlockTimeoutThenSuccess)
{
    TParsedWBFW fw;
    fw.Info.assign(32, 0xAA);
    fw.Data.assign(136, 0xBB);

    std::vector<int> infoData(fw.Info.begin(), fw.Info.end());
    EnqueueWriteMultipleRegisters(FwRegisters::FW_INFO_BLOCK_ADDR, FwRegisters::FW_INFO_BLOCK_COUNT, infoData);

    std::vector<int> dataChunk(fw.Data.begin(), fw.Data.end());
    EnqueueWriteMultipleRegistersNoResponse(FwRegisters::FW_DATA_BLOCK_ADDR,
                                            FwRegisters::FW_DATA_BLOCK_COUNT,
                                            dataChunk);
    EnqueueWriteMultipleRegisters(FwRegisters::FW_DATA_BLOCK_ADDR, FwRegisters::FW_DATA_BLOCK_COUNT, dataChunk);

    bool completed = false;
    bool gotError = false;
    int lastProgress = -1;

    auto task = std::make_shared<TFwFlashTask>(
        SLAVE_ID,
        "modbus",
        fw,
        false,
        false,
        [&](int p) { lastProgress = p; },
        [&]() { completed = true; },
        [&](const std::string&) { gotError = true; });

    task->Run(FeaturePort, *AccessHandler, EmptyDeviceList);

    EXPECT_TRUE(completed);
    EXPECT_FALSE(gotError);
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
        SLAVE_ID,
        "modbus",
        fw,
        false,
        false,
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
    // 136 + 50 bytes, the last chunk is shorter than the block size
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
        SLAVE_ID,
        "modbus",
        fw,
        false,
        false,
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
        SLAVE_ID,
        "modbus",
        fw,
        false,
        false,
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
        return std::make_shared<TFwUpdateState>([this](const std::string& topic,
                                                       const std::string& payload,
                                                       bool retain) { PublishLog.push_back({topic, payload, retain}); },
                                                "/test/state");
    }

    // Call the free function BuildFirmwareInfoResponse directly
    Json::Value CallBuildFirmwareInfoResponse(const TFwDeviceInfo& info,
                                              const std::string& suite = "bullseye",
                                              bool updatable = true)
    {
        return BuildFirmwareInfoResponse(info, *Downloader, suite, updatable);
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

    auto params = TRPCFwUpdateHandler::ParseRequestParams(request);
    EXPECT_EQ(params.SlaveId, 42);
    EXPECT_EQ(std::get<TSerialPortSettings>(params.PortSettings).Device, "/dev/ttyRS485-1");
    EXPECT_EQ(params.Protocol, "modbus");
}

TEST_F(FwHandlerTest, ParseRequestParamsDefaultProtocol)
{
    Json::Value request;
    request["slave_id"] = 1;
    request["port"]["path"] = "/dev/ttyRS485-1";

    auto params = TRPCFwUpdateHandler::ParseRequestParams(request);
    EXPECT_EQ(params.Protocol, "modbus");
}

TEST_F(FwHandlerTest, ParseRequestParamsMissingSlaveId)
{
    Json::Value request;
    request["port"]["path"] = "/dev/ttyRS485-1";

    EXPECT_THROW(TRPCFwUpdateHandler::ParseRequestParams(request), std::runtime_error);
}

TEST_F(FwHandlerTest, ParseRequestParamsSlaveIdNotInt)
{
    Json::Value request;
    request["slave_id"] = "not_a_number";
    request["port"]["path"] = "/dev/ttyRS485-1";

    EXPECT_THROW(TRPCFwUpdateHandler::ParseRequestParams(request), std::runtime_error);
}

TEST_F(FwHandlerTest, ParseRequestParamsSlaveIdNegative)
{
    Json::Value request;
    request["slave_id"] = -1;
    request["port"]["path"] = "/dev/ttyRS485-1";

    EXPECT_THROW(TRPCFwUpdateHandler::ParseRequestParams(request), std::runtime_error);
}

TEST_F(FwHandlerTest, ParseRequestParamsSlaveIdTooLarge)
{
    Json::Value request;
    request["slave_id"] = 256;
    request["port"]["path"] = "/dev/ttyRS485-1";

    EXPECT_THROW(TRPCFwUpdateHandler::ParseRequestParams(request), std::runtime_error);
}

TEST_F(FwHandlerTest, ParseRequestParamsMissingPort)
{
    Json::Value request;
    request["slave_id"] = 42;

    EXPECT_THROW(TRPCFwUpdateHandler::ParseRequestParams(request), std::runtime_error);
}

TEST_F(FwHandlerTest, ParseRequestParamsMissingPortPath)
{
    Json::Value request;
    request["slave_id"] = 42;
    request["port"]["other"] = "value";

    EXPECT_THROW(TRPCFwUpdateHandler::ParseRequestParams(request), std::runtime_error);
}

TEST_F(FwHandlerTest, ParseRequestParamsPortSettings)
{
    Json::Value request;
    request["slave_id"] = 1;
    request["port"]["path"] = "/dev/ttyRS485-1";
    request["port"]["baud_rate"] = 115200;
    request["port"]["parity"] = "E";
    request["port"]["data_bits"] = 8;
    request["port"]["stop_bits"] = 2;

    auto params = TRPCFwUpdateHandler::ParseRequestParams(request);
    auto settings = GetRPCPortConnectionSettings(params.PortSettings);
    EXPECT_EQ(settings.BaudRate, 115200);
    EXPECT_EQ(settings.Parity, 'E');
    EXPECT_EQ(settings.DataBits, 8);
    EXPECT_EQ(settings.StopBits, 2);
}

TEST_F(FwHandlerTest, ParseRequestParamsDefaultPortSettings)
{
    Json::Value request;
    request["slave_id"] = 1;
    request["port"]["path"] = "/dev/ttyRS485-1";

    auto params = TRPCFwUpdateHandler::ParseRequestParams(request);
    auto settings = GetRPCPortConnectionSettings(params.PortSettings);
    EXPECT_EQ(settings.BaudRate, 9600);
    EXPECT_EQ(settings.Parity, 'N');
    EXPECT_EQ(settings.DataBits, 8);
    EXPECT_EQ(settings.StopBits, 2);
}

// ---- TCP port tests ----

TEST_F(FwHandlerTest, ParseRequestParamsModbusTcpPort)
{
    Json::Value request;
    request["slave_id"] = 1;
    request["port"]["address"] = "192.168.1.100";
    request["port"]["port"] = 502;
    request["protocol"] = "modbus-tcp";

    auto params = TRPCFwUpdateHandler::ParseRequestParams(request);
    EXPECT_EQ(params.Protocol, "modbus-tcp");
    EXPECT_TRUE(std::get<TRPCTcpPortSettings>(params.PortSettings).ModbusTcp);
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

// ---- IsPrintableAscii / IsValidFwSignature / SanitizeVersionString tests ----

TEST_F(FwHandlerTest, IsPrintableAsciiPlain)
{
    EXPECT_TRUE(IsPrintableAscii("3.7.0"));
    EXPECT_TRUE(IsPrintableAscii("WB-MSW v.3")); // spaces and dots are printable
    EXPECT_TRUE(IsPrintableAscii(""));           // empty is trivially printable
}

TEST_F(FwHandlerTest, IsPrintableAsciiControlChar)
{
    EXPECT_FALSE(IsPrintableAscii(std::string("\x0F"
                                              "F")));        // control char
    EXPECT_FALSE(IsPrintableAscii(std::string("\x01\x02"))); // control chars
    EXPECT_FALSE(IsPrintableAscii(std::string("ab\x7F")));   // DEL is not printable
}

TEST_F(FwHandlerTest, IsPrintableAsciiHighByte)
{
    EXPECT_FALSE(IsPrintableAscii(std::string("\xC0\xA0"))); // bytes > 0x7F
}

TEST_F(FwHandlerTest, IsValidFwSignatureValid)
{
    EXPECT_TRUE(IsValidFwSignature("wbled"));
    EXPECT_TRUE(IsValidFwSignature("wb-2307"));
    EXPECT_TRUE(IsValidFwSignature("wbmwac_oc"));
    EXPECT_TRUE(IsValidFwSignature("msw5Ge")); // mixed case, as documented for reg 290
}

TEST_F(FwHandlerTest, IsValidFwSignatureEmpty)
{
    EXPECT_FALSE(IsValidFwSignature(""));
}

TEST_F(FwHandlerTest, IsValidFwSignatureGarbage)
{
    EXPECT_FALSE(IsValidFwSignature(std::string("\x0F"
                                                "F")));       // control char
    EXPECT_FALSE(IsValidFwSignature(std::string("\xC0sig"))); // high byte
    EXPECT_FALSE(IsValidFwSignature(std::string("ab\x01")));  // control char in the middle
}

TEST_F(FwHandlerTest, SanitizeVersionString)
{
    EXPECT_EQ(SanitizeVersionString("3.7.0"), "3.7.0");
    EXPECT_EQ(SanitizeVersionString(std::string("\x0F"
                                                "F")),
              "");
    EXPECT_EQ(SanitizeVersionString(std::string("\xC0")), "");
}

// ---- Updatability over a port ----

TEST(TFwUpdatableTest, SerialPortNeedsNoDefaultSettings)
{
    EXPECT_FALSE(RequiresDefaultPortSettings(false, "modbus", false));
}

TEST(TFwUpdatableTest, ModbusTcpPortNeedsNoDefaultSettings)
{
    EXPECT_FALSE(RequiresDefaultPortSettings(true, "modbus-tcp", false));
}

TEST(TFwUpdatableTest, BootloaderPreservingSettingsNeedsNoDefaultSettings)
{
    EXPECT_FALSE(RequiresDefaultPortSettings(true, "modbus", true));
}

TEST(TFwUpdatableTest, SerialOverTcpNeedsDefaultSettings)
{
    EXPECT_TRUE(RequiresDefaultPortSettings(true, "modbus", false));
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

TEST_F(FwHandlerTest, BuildResponseDeviceIsNotUpdatable)
{
    SetupReleasesYaml();

    TFwDeviceInfo info;
    info.FwSignature = "wbled";
    info.FwVersion = "3.6.1";

    auto result = CallBuildFirmwareInfoResponse(info, "bullseye", false);

    EXPECT_FALSE(result["can_update"].asBool());
    EXPECT_EQ(result["available_fw"].asString(), "3.8.0");
    EXPECT_TRUE(result["fw_has_update"].asBool());
}

TEST_F(FwHandlerTest, BuildResponseFirmwareAvailableNewer)
{
    SetupReleasesYaml("releases:\n"
                      "  wbled:\n"
                      "    bullseye: fw/by-signature/wbled/bullseye/3.8.0.wbfw\n");

    FakeHttp->SetTextResponse("https://fw-releases.wirenboard.com/boot/by-signature/release-versions.yaml",
                              "releases:\n"
                              "  wbled:\n"
                              "    bullseye: boot/by-signature/wbled/main/2.0.0.wbfw\n");

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

    FakeHttp->SetTextResponse("https://fw-releases.wirenboard.com/boot/by-signature/release-versions.yaml",
                              "releases:\n"
                              "  wbled:\n"
                              "    bullseye: boot/by-signature/wbled/main/1.5.0.wbfw\n");

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

    FakeHttp->SetTextResponse("https://fw-releases.wirenboard.com/boot/by-signature/release-versions.yaml",
                              "releases:\n"
                              "  wbmwac:\n"
                              "    bullseye: boot/by-signature/wbmwac/main/1.0.0.wbfw\n");

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

//! The update server may know nothing about a component, the response still describes it
TEST_F(FwHandlerTest, BuildResponseComponentWithoutReleasedVersion)
{
    SetupReleasesYaml("releases:\n"
                      "  wbmwac:\n"
                      "    bullseye: fw/by-signature/wbmwac/bullseye/2.0.0.wbfw\n");
    FakeHttp->SetTextResponse("https://fw-releases.wirenboard.com/boot/by-signature/release-versions.yaml",
                              "releases:\n"
                              "  wbmwac:\n"
                              "    bullseye: boot/by-signature/wbmwac/main/1.0.0.wbfw\n");

    TFwDeviceInfo info;
    info.FwSignature = "wbmwac";
    info.FwVersion = "2.0.0";
    info.BootloaderVersion = "1.0.0";
    info.DeviceModel = "WBMWAC-v2";
    info.Components.push_back({0, "wbmwac_oc", "1.0.0", "WBMWAC-OC"});

    auto result = CallBuildFirmwareInfoResponse(info);

    ASSERT_TRUE(result["components"].isMember("0"));
    EXPECT_EQ(result["components"]["0"]["model"].asString(), "WBMWAC-OC");
    EXPECT_EQ(result["components"]["0"]["fw"].asString(), "1.0.0");
    EXPECT_TRUE(result["components"]["0"]["available_fw"].asString().empty());
    EXPECT_FALSE(result["components"]["0"]["has_update"].asBool());
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

    // Release server unreachable: nothing available, so there is nothing to flash.
    EXPECT_FALSE(result["can_update"].asBool());
    EXPECT_EQ(result["available_fw"].asString(), "");
    EXPECT_EQ(result["available_bootloader"].asString(), "");
    EXPECT_FALSE(result["fw_has_update"].asBool());
}

// ---- Third-party / garbage device scenarios (SOFT-6985) ----

// A third-party device (e.g. ENGO EFAN) whose signature is not in the release
// manifest must not be reported as updatable, and must not leak raw bytes.
// Cf. ticket: can_update=true for garbage provokes a doomed flash attempt.
TEST_F(FwHandlerTest, BuildResponseUnknownSignatureNotUpdatable)
{
    SetupReleasesYaml("releases:\n"
                      "  wbled:\n"
                      "    bullseye: fw/by-signature/wbled/bullseye/3.8.0.wbfw\n");
    // No bootloader latest.txt configured for "engo" -> downloader throws -> empty.

    TFwDeviceInfo info;
    info.FwSignature = "engo"; // valid-looking but unknown signature
    info.FwVersion = "1.0";
    info.DeviceModel = "EFAN";

    auto result = CallBuildFirmwareInfoResponse(info);

    EXPECT_FALSE(result["can_update"].asBool());
    EXPECT_EQ(result["available_fw"].asString(), "");
    EXPECT_EQ(result["available_bootloader"].asString(), "");
    EXPECT_FALSE(result["fw_has_update"].asBool());
    EXPECT_FALSE(result["bootloader_has_update"].asBool());
}

// Empty signature: must short-circuit and never query the release server.
TEST_F(FwHandlerTest, BuildResponseEmptySignatureNoS3)
{
    SetupReleasesYaml();
    auto indexUrl = "https://fw-releases.wirenboard.com/fw/by-signature/release-versions.yaml";

    TFwDeviceInfo info;
    info.FwSignature = ""; // could not read signature
    info.FwVersion = "1.0";
    info.DeviceModel = "Unknown";

    auto result = CallBuildFirmwareInfoResponse(info);

    EXPECT_FALSE(result["can_update"].asBool());
    EXPECT_EQ(result["available_bootloader"].asString(), "");
    EXPECT_EQ(FakeHttp->GetRequestCount(indexUrl), 0); // never queried the release server
}

TEST_F(FwHandlerTest, BuildResponseUpdatableWhenBootloaderOnly)
{
    // No firmware in the manifest for this signature, but a bootloader exists.
    SetupReleasesYaml("releases:\n"
                      "  wbled:\n"
                      "    bullseye: fw/by-signature/wbled/bullseye/3.8.0.wbfw\n");
    FakeHttp->SetTextResponse("https://fw-releases.wirenboard.com/boot/by-signature/release-versions.yaml",
                              "releases:\n"
                              "  wbother:\n"
                              "    bullseye: boot/by-signature/wbother/main/2.0.0.wbfw\n");

    TFwDeviceInfo info;
    info.FwSignature = "wbother";
    info.FwVersion = "1.0";
    info.BootloaderVersion = "1.0.0";
    info.DeviceModel = "WB-OTHER";

    auto result = CallBuildFirmwareInfoResponse(info);

    EXPECT_EQ(result["available_fw"].asString(), "");
    EXPECT_EQ(result["available_bootloader"].asString(), "2.0.0");
    EXPECT_TRUE(result["can_update"].asBool()); // bootloader available -> updatable
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
        std::error_code ec;
        std::filesystem::remove_all(TempDir, ec);
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
// Modbus exception 0x04 ("slave device failure") on data block write is treated as success
// because it means the chunk is already written. Cf. firmware_update.py:349
TEST_F(TFwTaskTest, FlashDataBlockException04AcceptedAsSuccess)
{
    TParsedWBFW fw;
    fw.Info.assign(32, 0xAA);
    fw.Data.assign(136, 0xBB);

    // Info block write succeeds
    std::vector<int> infoData(fw.Info.begin(), fw.Info.end());
    EnqueueWriteMultipleRegisters(FwRegisters::FW_INFO_BLOCK_ADDR, FwRegisters::FW_INFO_BLOCK_COUNT, infoData);

    // Data block write: Modbus exception 0x04 — treated as "already written"
    auto byteCount = static_cast<int>(FwRegisters::FW_DATA_BLOCK_COUNT * 2);
    std::vector<int> writeRequest = {0x10,
                                     static_cast<int>(FwRegisters::FW_DATA_BLOCK_ADDR >> 8),
                                     static_cast<int>(FwRegisters::FW_DATA_BLOCK_ADDR & 0xFF),
                                     static_cast<int>(FwRegisters::FW_DATA_BLOCK_COUNT >> 8),
                                     static_cast<int>(FwRegisters::FW_DATA_BLOCK_COUNT & 0xFF),
                                     byteCount};
    std::vector<int> dataChunk(fw.Data.begin(), fw.Data.end());
    writeRequest.insert(writeRequest.end(), dataChunk.begin(), dataChunk.end());
    std::vector<int> exceptionResponse = {0x90, 0x04}; // exception code 0x04

    SerialPort->Expect(WrapPDU(writeRequest), WrapPDU(exceptionResponse), "DataBlockException04");

    bool completed = false;
    bool gotError = false;

    auto task = std::make_shared<TFwFlashTask>(
        SLAVE_ID,
        "modbus",
        fw,
        false,
        false,
        [&](int) {},
        [&]() { completed = true; },
        [&](const std::string&) { gotError = true; });

    task->Run(FeaturePort, *AccessHandler, EmptyDeviceList);
    EXPECT_TRUE(completed);
    EXPECT_FALSE(gotError);
}

// Other Modbus exceptions on data block write cause retries and eventually an error
TEST_F(TFwTaskTest, FlashDataBlockExceptionCausesError)
{
    TParsedWBFW fw;
    fw.Info.assign(32, 0xAA);
    fw.Data.assign(136, 0xBB);

    // Info block write succeeds
    std::vector<int> infoData(fw.Info.begin(), fw.Info.end());
    EnqueueWriteMultipleRegisters(FwRegisters::FW_INFO_BLOCK_ADDR, FwRegisters::FW_INFO_BLOCK_COUNT, infoData);

    // Data block write: Modbus exception 0x02 — retried 3 times then fails
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

    // 3 retries
    for (int i = 0; i < 3; ++i) {
        SerialPort->Expect(WrapPDU(writeRequest), WrapPDU(exceptionResponse), "DataBlockException02");
    }

    bool completed = false;
    bool gotError = false;

    auto task = std::make_shared<TFwFlashTask>(
        SLAVE_ID,
        "modbus",
        fw,
        false,
        false,
        [&](int) {},
        [&]() { completed = true; },
        [&](const std::string&) { gotError = true; });

    task->Run(FeaturePort, *AccessHandler, EmptyDeviceList);
    EXPECT_FALSE(completed);
    EXPECT_TRUE(gotError);
}

// ---- Component read error (component present but read fails) ----

TEST_F(TFwTaskTest, GetInfoComponentReadError)
{
    // Standard reads
    EnqueueHoldingRead(FwRegisters::FW_SIGNATURE_ADDR,
                       FwRegisters::FW_SIGNATURE_COUNT,
                       EncodeStringAsRegs("test", FwRegisters::FW_SIGNATURE_COUNT));
    EnqueueHoldingRead(FwRegisters::FW_VERSION_ADDR,
                       FwRegisters::FW_VERSION_COUNT,
                       EncodeStringAsRegs("1.0.0", FwRegisters::FW_VERSION_COUNT));
    EnqueueHoldingReadException(FwRegisters::BOOTLOADER_VERSION_ADDR, FwRegisters::BOOTLOADER_VERSION_COUNT, 0x02);
    EnqueueHoldingReadException(FwRegisters::REBOOT_PRESERVE_PORT_SETTINGS_ADDR, 1, 0x02);
    EnqueueHoldingRead(FwRegisters::DEVICE_MODEL_EXTENDED_ADDR,
                       FwRegisters::DEVICE_MODEL_EXTENDED_COUNT,
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

    void RunTask(const TRPCPortSettings& portSettings, PSerialClientTask task) override
    {
        SubmittedTasks.push_back({portSettings, task});
        if (Port) {
            task->Run(Port, *AccessHandler, EmptyDeviceList);
        }
    }

    struct SubmittedTask
    {
        TRPCPortSettings PortSettings;
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
class FwHandlerIntegrationTest: public TSerialDeviceTest, public TModbusExpectationsBase, public TFwModbusTestHelpers
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

    PFakeSerialPort GetSerialPort() const override
    {
        return SerialPort;
    }

    std::vector<int> WrapPDU(const std::vector<int>& pdu) override
    {
        return TModbusExpectationsBase::WrapPDU(pdu);
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

    Json::Value ParseLastPublishedState()
    {
        EXPECT_FALSE(PublishLog.empty());
        return ParseJson(PublishLog.back().Payload);
    }

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

    //! A device behind a converter, the driver talks Modbus RTU over a TCP connection
    Json::Value MakeSerialOverTcpRequest(int slaveId = SLAVE_ID)
    {
        Json::Value req;
        req["slave_id"] = slaveId;
        req["port"]["address"] = "192.168.1.10";
        req["port"]["port"] = 23;
        req["protocol"] = "modbus";
        return req;
    }

    //! The settings of the line behind a converter, the driver cannot change them
    void EnqueuePortSettingsReads(bool factorySettings)
    {
        EnqueueHoldingRead(110, 1, factorySettings ? std::vector<int>{0x00, 0x60} : std::vector<int>{0x04, 0x80});
        if (factorySettings) {
            EnqueueHoldingRead(111, 1, {0x00, 0x00});
        }
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
        return Handler->UpdateLock->InProgress;
    }

    void SetUpdateInProgress(bool value)
    {
        Handler->UpdateLock->InProgress = value;
    }

    void SetupReleasesYaml(const std::string& yaml = "releases:\n"
                                                     "  wbled:\n"
                                                     "    bullseye: fw/by-signature/wbled/bullseye/3.8.0.wbfw\n")
    {
        FakeHttp->SetTextResponse("https://fw-releases.wirenboard.com/fw/by-signature/release-versions.yaml", yaml);
    }

    void SetupBootloaderInfo(const std::string& signature = "wbled", const std::string& version = "2.0.0")
    {
        FakeHttp->SetTextResponse("https://fw-releases.wirenboard.com/boot/by-signature/release-versions.yaml",
                                  "releases:\n  " + signature + ":\n    bullseye: boot/by-signature/" + signature +
                                      "/main/" + version + ".wbfw\n");
    }

    void SetupFirmwareDownload(const std::string& signature, const std::string& suite, const std::string& version)
    {
        std::vector<uint8_t> wbfwData(168, 0xAA);
        FakeHttp->SetBinaryResponse("https://fw-releases.wirenboard.com/fw/by-signature/" + signature + "/" + suite +
                                        "/" + version + ".wbfw",
                                    wbfwData);
    }

    //! fillByte is the byte the downloaded file is filled with, see SetupFirmwareDownload
    void EnqueueFlashExpectations(bool reboot = true, bool preserveSettings = true, int fillByte = 0xAA)
    {
        if (reboot && preserveSettings) {
            EnqueueWriteSingleRegister(FwRegisters::REBOOT_PRESERVE_PORT_SETTINGS_ADDR, 1);
        } else if (reboot) {
            EnqueueWriteSingleRegister(FwRegisters::REBOOT_TO_BOOTLOADER_ADDR, 1);
        }

        std::vector<int> infoData(32, fillByte);
        EnqueueWriteMultipleRegisters(FwRegisters::FW_INFO_BLOCK_ADDR, FwRegisters::FW_INFO_BLOCK_COUNT, infoData);

        std::vector<int> dataChunk(136, fillByte);
        EnqueueWriteMultipleRegisters(FwRegisters::FW_DATA_BLOCK_ADDR, FwRegisters::FW_DATA_BLOCK_COUNT, dataChunk);
    }
};

// ---- GetFirmwareInfo tests ----

TEST_F(FwHandlerIntegrationTest, GetFirmwareInfoNormal)
{
    // A request without port settings means the factory ones, 9600 8N2
    SerialPort->LogSerialPortSettings(true);

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

//! Such a device would be lost after rebooting to a bootloader which starts on 9600 8N2
TEST_F(FwHandlerIntegrationTest, GetFirmwareInfoOverSerialOverTcpWithOwnPortSettings)
{
    SetupReleasesYaml();
    SetupBootloaderInfo();
    EnqueueBasicGetInfoResponses("wbled", "3.6.1", "1.5.0", "WB-LED", false);
    EnqueuePortSettingsReads(false);

    CallGetFirmwareInfo(MakeSerialOverTcpRequest());

    ASSERT_TRUE(GotResult);
    EXPECT_EQ(LastResult["fw"].asString(), "3.6.1");
    EXPECT_EQ(LastResult["available_fw"].asString(), "3.8.0");
    EXPECT_FALSE(LastResult["can_update"].asBool());
}

//! The same device on the factory settings stays reachable in the bootloader
TEST_F(FwHandlerIntegrationTest, GetFirmwareInfoOverSerialOverTcpWithFactoryPortSettings)
{
    SetupReleasesYaml();
    SetupBootloaderInfo();
    EnqueueBasicGetInfoResponses("wbled", "3.6.1", "1.5.0", "WB-LED", false);
    EnqueuePortSettingsReads(true);

    CallGetFirmwareInfo(MakeSerialOverTcpRequest());

    ASSERT_TRUE(GotResult);
    EXPECT_TRUE(LastResult["can_update"].asBool());
}

TEST_F(FwHandlerIntegrationTest, GetFirmwareInfoWithoutNetwork)
{
    auto fwUrl = "https://fw-releases.wirenboard.com/fw/by-signature/release-versions.yaml";
    auto bootUrl = "https://fw-releases.wirenboard.com/boot/by-signature/release-versions.yaml";
    FakeHttp->SetError(fwUrl, "Timeout was reached");
    FakeHttp->SetError(bootUrl, "Timeout was reached");
    EnqueueBasicGetInfoResponses("wbled", "3.6.1", "1.5.0", "WB-LED");

    CallGetFirmwareInfo(MakeRequest());

    ASSERT_TRUE(GotResult);
    ASSERT_FALSE(GotError);
    EXPECT_EQ(LastResult["fw"].asString(), "3.6.1");
    EXPECT_EQ(LastResult["available_fw"].asString(), "");
    EXPECT_FALSE(LastResult["can_update"].asBool());
    // Only the prefetch is allowed to access network, the task itself must not
    EXPECT_EQ(FakeHttp->GetRequestCount(fwUrl), 1);
    EXPECT_EQ(FakeHttp->GetRequestCount(bootUrl), 1);
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

//! Such a bootloader answers on the settings of the firmware, the update runs on the settings
//! of the request from the first byte to the last
TEST_F(FwHandlerIntegrationTest, UpdateFirmwareKeepsPortSettings)
{
    SerialPort->LogSerialPortSettings(true);

    SetupReleasesYaml();
    SetupBootloaderInfo();
    SetupFirmwareDownload("wbled", "bullseye", "3.8.0");
    EnqueueBasicGetInfoResponses("wbled", "3.6.1", "1.5.0", "WB-LED");
    EnqueueFlashExpectations(true, true);

    auto request = MakeRequest();
    request["type"] = "firmware";
    request["port"]["baud_rate"] = 115200;
    request["port"]["stop_bits"] = 1;

    CallUpdate(request);

    ASSERT_TRUE(GotResult);
    ASSERT_FALSE(GotError);
    EXPECT_FALSE(GetUpdateInProgress());
}

//! A bootloader without register 131 starts on the factory settings whatever the device is
//! configured to, so the firmware is written on 9600 8N2 and the port is given back afterwards
TEST_F(FwHandlerIntegrationTest, UpdateFirmwareSwitchesPortToFactorySettings)
{
    SerialPort->LogSerialPortSettings(true);

    SetupReleasesYaml();
    SetupBootloaderInfo();
    SetupFirmwareDownload("wbled", "bullseye", "3.8.0");
    EnqueueBasicGetInfoResponses("wbled", "3.6.1", "1.5.0", "WB-LED", false);
    EnqueueFlashExpectations(true, false);

    auto request = MakeRequest();
    request["type"] = "firmware";
    request["port"]["baud_rate"] = 115200;
    request["port"]["stop_bits"] = 1;

    CallUpdate(request);

    ASSERT_TRUE(GotResult);
    ASSERT_FALSE(GotError);
    EXPECT_FALSE(GetUpdateInProgress());
}

//! A device stays in the bootloader after its update, so the firmware is written right after it
TEST_F(FwHandlerIntegrationTest, UpdateBootloader)
{
    SetupReleasesYaml();
    SetupBootloaderInfo("wbled", "2.0.0");
    std::vector<uint8_t> wbfwData(168, 0xBB);
    FakeHttp->SetBinaryResponse("https://fw-releases.wirenboard.com/boot/by-signature/wbled/main/2.0.0.wbfw", wbfwData);
    SetupFirmwareDownload("wbled", "bullseye", "3.8.0");

    EnqueueBasicGetInfoResponses("wbled", "3.6.1", "1.5.0", "WB-LED");
    EnqueueFlashExpectations(true, true, 0xBB);
    EnqueueFlashExpectations(false, false, 0xAA);

    auto request = MakeRequest();
    request["type"] = "bootloader";

    CallUpdate(request);

    ASSERT_TRUE(GotResult);
    ASSERT_FALSE(GotError);
    EXPECT_EQ(LastResult.asString(), "Ok");
    EXPECT_FALSE(GetUpdateInProgress());
    EXPECT_TRUE(ParseLastPublishedState()["devices"].empty());
}

//! Components are written without rebooting to the bootloader, so the port keeps its settings
TEST_F(FwHandlerIntegrationTest, UpdateComponent)
{
    SerialPort->LogSerialPortSettings(true);

    SetupReleasesYaml("releases:\n"
                      "  wbled:\n"
                      "    bullseye: fw/by-signature/wbled/bullseye/3.8.0.wbfw\n"
                      "  wbled_oc:\n"
                      "    bullseye: fw/by-signature/wbled_oc/bullseye/1.1.0.wbfw\n");
    SetupBootloaderInfo();
    SetupFirmwareDownload("wbled_oc", "bullseye", "1.1.0");

    EnqueueDeviceInfoReads("wbled", "3.6.1", "1.5.0", "WB-LED");
    EnqueueDiscreteRead(FwRegisters::COMPONENTS_PRESENCE_ADDR, FwRegisters::COMPONENTS_PRESENCE_COUNT, {0x01});
    EnqueueComponentInfoReads(0, "wbled_oc", "1.0.0", "WB-LED-OC");
    EnqueueFlashExpectations(false, false);

    auto request = MakeRequest();
    request["type"] = "component";
    request["port"]["baud_rate"] = 115200;
    request["port"]["stop_bits"] = 1;

    CallUpdate(request);

    ASSERT_TRUE(GotResult);
    ASSERT_FALSE(GotError);
    EXPECT_EQ(LastResult.asString(), "Ok");
    EXPECT_FALSE(GetUpdateInProgress());
    EXPECT_TRUE(ParseLastPublishedState()["devices"].empty());
}

//! homeui advises to check the internet connection by this error id
TEST_F(FwHandlerIntegrationTest, UpdateReportsDownloadError)
{
    SetupReleasesYaml();
    SetupBootloaderInfo();
    FakeHttp->SetError("https://fw-releases.wirenboard.com/fw/by-signature/wbled/bullseye/3.8.0.wbfw",
                       "Couldn't resolve host name");

    EnqueueBasicGetInfoResponses("wbled", "3.6.1", "1.5.0", "WB-LED");

    auto request = MakeRequest();
    request["type"] = "firmware";

    CallUpdate(request);

    ASSERT_TRUE(GotResult);
    EXPECT_FALSE(GetUpdateInProgress());

    auto devices = ParseLastPublishedState()["devices"];
    ASSERT_EQ(devices.size(), 1u);
    EXPECT_EQ(devices[0]["error"]["id"].asString(), "com.wb.serial_driver.download_error");
}

//! homeui advises to restore the device by this error id, it is left in the bootloader
TEST_F(FwHandlerIntegrationTest, UpdateReportsResponseTimeout)
{
    SetupReleasesYaml();
    SetupBootloaderInfo();
    SetupFirmwareDownload("wbled", "bullseye", "3.8.0");

    EnqueueBasicGetInfoResponses("wbled", "3.6.1", "1.5.0", "WB-LED");
    EnqueueWriteSingleRegister(FwRegisters::REBOOT_PRESERVE_PORT_SETTINGS_ADDR, 1);
    std::vector<int> infoData(32, 0xAA);
    EnqueueWriteMultipleRegistersNoResponse(FwRegisters::FW_INFO_BLOCK_ADDR,
                                            FwRegisters::FW_INFO_BLOCK_COUNT,
                                            infoData);

    auto request = MakeRequest();
    request["type"] = "firmware";

    CallUpdate(request);

    ASSERT_TRUE(GotResult);
    EXPECT_FALSE(GetUpdateInProgress());

    auto devices = ParseLastPublishedState()["devices"];
    ASSERT_EQ(devices.size(), 1u);
    EXPECT_EQ(devices[0]["error"]["id"].asString(), "com.wb.serial_driver.device.response_timeout_error");
}

//! A component left with a record of its own would block every later request for the device
TEST_F(FwHandlerIntegrationTest, UpdateComponentReportsDownloadError)
{
    SetupReleasesYaml("releases:\n"
                      "  wbled:\n"
                      "    bullseye: fw/by-signature/wbled/bullseye/3.8.0.wbfw\n"
                      "  wbled_oc:\n"
                      "    bullseye: fw/by-signature/wbled_oc/bullseye/1.1.0.wbfw\n");
    SetupBootloaderInfo();
    FakeHttp->SetError("https://fw-releases.wirenboard.com/fw/by-signature/wbled_oc/bullseye/1.1.0.wbfw",
                       "Couldn't resolve host name");

    EnqueueDeviceInfoReads();
    EnqueueDiscreteRead(FwRegisters::COMPONENTS_PRESENCE_ADDR, FwRegisters::COMPONENTS_PRESENCE_COUNT, {0x01});
    EnqueueComponentInfoReads(0, "wbled_oc", "1.0.0", "WB-LED-OC");

    auto request = MakeRequest();
    request["type"] = "component";

    CallUpdate(request);

    ASSERT_TRUE(GotResult);
    EXPECT_FALSE(GetUpdateInProgress());

    auto devices = ParseLastPublishedState()["devices"];
    ASSERT_EQ(devices.size(), 1u);
    EXPECT_EQ(devices[0]["type"].asString(), "component");
    EXPECT_EQ(devices[0]["error"]["id"].asString(), "com.wb.serial_driver.download_error");
}

TEST_F(FwHandlerIntegrationTest, UpdateOverSerialOverTcpIsRejected)
{
    SetupReleasesYaml();
    SetupBootloaderInfo();
    SetupFirmwareDownload("wbled", "bullseye", "3.8.0");
    EnqueueBasicGetInfoResponses("wbled", "3.6.1", "1.5.0", "WB-LED", false);
    EnqueuePortSettingsReads(false);

    auto request = MakeSerialOverTcpRequest();
    request["type"] = "firmware";

    CallUpdate(request);

    ASSERT_FALSE(GotResult);
    ASSERT_TRUE(GotError);
    EXPECT_NE(LastErrorMsg.find("Can't update firmware over TCP"), std::string::npos);
    EXPECT_FALSE(GetUpdateInProgress());
    EXPECT_TRUE(PublishLog.empty());
}

//! A component is written without rebooting to the bootloader, so the port settings do not matter
TEST_F(FwHandlerIntegrationTest, UpdateComponentOverSerialOverTcpIsAllowed)
{
    SetupReleasesYaml("releases:\n"
                      "  wbled:\n"
                      "    bullseye: fw/by-signature/wbled/bullseye/3.8.0.wbfw\n"
                      "  wbled_oc:\n"
                      "    bullseye: fw/by-signature/wbled_oc/bullseye/1.1.0.wbfw\n");
    SetupBootloaderInfo();
    SetupFirmwareDownload("wbled_oc", "bullseye", "1.1.0");

    EnqueueDeviceInfoReads("wbled", "3.6.1", "1.5.0", "WB-LED", false);
    EnqueueDiscreteRead(FwRegisters::COMPONENTS_PRESENCE_ADDR, FwRegisters::COMPONENTS_PRESENCE_COUNT, {0x01});
    EnqueueComponentInfoReads(0, "wbled_oc", "1.0.0", "WB-LED-OC");
    EnqueueFlashExpectations(false, false);

    auto request = MakeSerialOverTcpRequest();
    request["type"] = "component";

    CallUpdate(request);

    ASSERT_TRUE(GotResult);
    ASSERT_FALSE(GotError);
    EXPECT_FALSE(GetUpdateInProgress());
    EXPECT_TRUE(ParseLastPublishedState()["devices"].empty());
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

//! An unknown software type is rejected before the device is touched
TEST_F(FwHandlerIntegrationTest, UpdateUnknownSoftwareType)
{
    auto request = MakeRequest();
    request["type"] = "firmvare";

    CallUpdate(request);

    ASSERT_FALSE(GotResult);
    ASSERT_TRUE(GotError);
    EXPECT_NE(LastErrorMsg.find("Unknown software type"), std::string::npos);
    EXPECT_FALSE(GetUpdateInProgress());
    EXPECT_TRUE(PublishLog.empty());
}

//! The release server knows nothing about the device, the request fails instead of a silent stop
TEST_F(FwHandlerIntegrationTest, UpdateWithoutReleasedFirmwareIsRejected)
{
    SetupReleasesYaml("releases:\n"
                      "  wbmwac:\n"
                      "    bullseye: fw/by-signature/wbmwac/bullseye/1.0.0.wbfw\n");
    EnqueueBasicGetInfoResponses("wbled", "3.6.1", "1.5.0", "WB-LED");

    auto request = MakeRequest();
    request["type"] = "firmware";

    CallUpdate(request);

    ASSERT_FALSE(GotResult);
    ASSERT_TRUE(GotError);
    EXPECT_NE(LastErrorMsg.find("Released binary not found"), std::string::npos);
    EXPECT_FALSE(GetUpdateInProgress());
    EXPECT_TRUE(PublishLog.empty());
}

//! A device stays in the bootloader after its update, so the firmware to write into it is taken beforehand
TEST_F(FwHandlerIntegrationTest, UpdateBootloaderWithoutReleasedFirmwareIsRejected)
{
    SetupReleasesYaml("releases:\n"
                      "  wbmwac:\n"
                      "    bullseye: fw/by-signature/wbmwac/bullseye/1.0.0.wbfw\n");
    SetupBootloaderInfo();
    EnqueueBasicGetInfoResponses("wbled", "3.6.1", "1.5.0", "WB-LED");

    auto request = MakeRequest();
    request["type"] = "bootloader";

    CallUpdate(request);

    ASSERT_FALSE(GotResult);
    ASSERT_TRUE(GotError);
    EXPECT_NE(LastErrorMsg.find("Released binary not found"), std::string::npos);
    EXPECT_FALSE(GetUpdateInProgress());
    EXPECT_TRUE(PublishLog.empty());
}

//! The release server knows nothing about the component, the whole request fails
TEST_F(FwHandlerIntegrationTest, UpdateComponentWithoutReleasedFirmwareIsRejected)
{
    SetupReleasesYaml();
    EnqueueDeviceInfoReads("wbled", "3.6.1", "1.5.0", "WB-LED");
    EnqueueDiscreteRead(FwRegisters::COMPONENTS_PRESENCE_ADDR, FwRegisters::COMPONENTS_PRESENCE_COUNT, {0x01});
    EnqueueComponentInfoReads(0, "wbled_oc", "1.0.0", "WB-LED-OC");

    auto request = MakeRequest();
    request["type"] = "component";

    CallUpdate(request);

    ASSERT_FALSE(GotResult);
    ASSERT_TRUE(GotError);
    EXPECT_NE(LastErrorMsg.find("Released binary not found"), std::string::npos);
    EXPECT_FALSE(GetUpdateInProgress());
    EXPECT_TRUE(PublishLog.empty());
}

// ---- Restore tests ----

TEST_F(FwHandlerIntegrationTest, RestoreNormal)
{
    SerialPort->LogSerialPortSettings(true);

    SetupReleasesYaml();
    SetupFirmwareDownload("wbled", "bullseye", "3.8.0");
    EnqueueBootloaderModeResponses();
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

//! A device which works normally has nothing to restore
TEST_F(FwHandlerIntegrationTest, RestoreSkipsDeviceInFirmwareMode)
{
    SetupReleasesYaml();
    EnqueueFirmwareModeResponses();

    CallRestore(MakeRequest());

    ASSERT_TRUE(GotResult);
    ASSERT_FALSE(GotError);
    EXPECT_EQ(LastResult.asString(), "Ok");
    EXPECT_FALSE(GetUpdateInProgress());
    EXPECT_TRUE(PublishLog.empty());
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
