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

#ifndef __EMSCRIPTEN__
class TCurlHttpClient: public IHttpClient
{
public:
    std::vector<uint8_t> GetBinary(const std::string& url) override;
    std::string GetText(const std::string& url) override;
};
#endif

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

// signature -> release suite -> path to a binary
typedef std::map<std::string, std::map<std::string, std::string>> TReleaseIndex;

TParsedWBFW ParseWBFW(const std::vector<uint8_t>& data);
std::string ParseFwVersionFromUrl(const std::string& url);
TReleaseIndex ParseReleaseVersionsYaml(const std::string& text);
std::string ReadReleaseSuite(const std::string& releasePath = "/usr/lib/wb-release");

enum class ENetworkAccess
{
    Allowed,
    CacheOnly
};

class TFwDownloader
{
public:
    TFwDownloader(PHttpClient httpClient);

    TReleasedBinary GetReleasedFirmware(const std::string& fwSignature,
                                        const std::string& releaseSuite,
                                        ENetworkAccess networkAccess = ENetworkAccess::Allowed);

    TReleasedBinary GetReleasedBootloader(const std::string& fwSignature,
                                          const std::string& releaseSuite,
                                          ENetworkAccess networkAccess = ENetworkAccess::Allowed);

    TParsedWBFW DownloadAndParseWBFW(const std::string& url);

    void PrefetchReleaseIndexes();

private:
    PHttpClient HttpClient;

    struct TCacheEntry
    {
        std::chrono::steady_clock::time_point ExpiresAt;
    };

    struct TReleaseCacheEntry: TCacheEntry
    {
        std::chrono::steady_clock::time_point RetryAt;
        TReleaseIndex Releases;
    };

    struct TWBFWCacheEntry: TCacheEntry
    {
        TParsedWBFW Firmware;
    };

    std::mutex CacheMutex;
    TReleaseCacheEntry ReleaseCache;
    TReleaseCacheEntry BootloaderReleaseCache;
    std::map<std::string, TWBFWCacheEntry> WBFWCache;

    static const std::string FW_RELEASES_BASE_URL;
    static const std::string FW_RELEASES_INDEX_URL;
    static const std::string BOOTLOADER_RELEASES_INDEX_URL;
    static const std::chrono::minutes RELEASE_CACHE_TTL;
    static const std::chrono::minutes BOOTLOADER_CACHE_TTL;
    static const std::chrono::hours WBFW_CACHE_TTL;
    static const std::chrono::seconds FAILED_DOWNLOAD_RETRY_INTERVAL;

    void UpdateReleaseIndex(const std::string& indexUrl,
                            TReleaseCacheEntry& cache,
                            std::chrono::minutes ttl,
                            ENetworkAccess networkAccess);

    // Cf. fw_downloader.py _get_released_binary() + the @ttl_lru_cache wrappers.
    // Fetches and caches a release-versions.yaml index (keyed by signature, then suite)
    // and returns the released binary for the given signature/suite.
    TReleasedBinary GetReleasedBinary(const std::string& indexUrl,
                                      TReleaseCacheEntry& cache,
                                      std::chrono::minutes ttl,
                                      const std::string& fwSignature,
                                      const std::string& releaseSuite,
                                      ENetworkAccess networkAccess);
};
