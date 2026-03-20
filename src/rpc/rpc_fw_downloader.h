#pragma once

#include <chrono>
#include <map>
#include <memory>
#include <mutex>
#include <string>
#include <vector>

class IHttpClient
{
public:
    virtual ~IHttpClient() = default;
    virtual std::string GetText(const std::string& url) = 0;
    virtual std::vector<uint8_t> GetBinary(const std::string& url) = 0;
};

typedef std::shared_ptr<IHttpClient> PHttpClient;

class TCurlHttpClient: public IHttpClient
{
public:
    std::vector<uint8_t> GetBinary(const std::string& url) override;
    std::string GetText(const std::string& url) override;
};

struct TReleasedBinary
{
    std::string Version;
    std::string Endpoint;
};

struct TParsedWBFW
{
    std::vector<uint8_t> Info;
    std::vector<uint8_t> Data;
};

TParsedWBFW ParseWBFW(const std::vector<uint8_t>& data);
std::string ParseFwVersionFromUrl(const std::string& url);
std::map<std::string, std::map<std::string, std::string>> ParseReleaseVersionsYaml(const std::string& text);
std::string ReadReleaseSuite(const std::string& releasePath = "/usr/lib/wb-release");

class TFwDownloader
{
public:
    TFwDownloader(PHttpClient httpClient);

    TReleasedBinary GetReleasedFirmware(const std::string& fwSignature, const std::string& releaseSuite);
    TReleasedBinary GetLatestBootloader(const std::string& fwSignature);
    TParsedWBFW DownloadAndParseWBFW(const std::string& url);

private:
    PHttpClient HttpClient;

    struct TCacheEntry
    {
        std::chrono::steady_clock::time_point ExpiresAt;
    };

    struct TReleaseCacheEntry: TCacheEntry
    {
        std::map<std::string, std::map<std::string, std::string>> Releases;
    };

    struct TBootloaderCacheEntry: TCacheEntry
    {
        TReleasedBinary Binary;
    };

    struct TWBFWCacheEntry: TCacheEntry
    {
        TParsedWBFW Firmware;
    };

    std::mutex CacheMutex;
    TReleaseCacheEntry ReleaseCache;
    std::map<std::string, TBootloaderCacheEntry> BootloaderCache;
    std::map<std::string, TWBFWCacheEntry> WBFWCache;

    static const std::string FW_RELEASES_BASE_URL;
    static const std::chrono::minutes RELEASE_CACHE_TTL;
    static const std::chrono::minutes BOOTLOADER_CACHE_TTL;
    static const std::chrono::hours WBFW_CACHE_TTL;

    std::map<std::string, std::map<std::string, std::string>> GetReleases();
};
