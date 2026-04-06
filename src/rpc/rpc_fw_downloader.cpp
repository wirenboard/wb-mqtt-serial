// Cf. fw_downloader.py - BinaryDownloader, get_released_fw(), get_latest_bootloader()
// Cf. firmware_update.py:185 parse_wbfw(), firmware_update.py:205 download_wbfw()
// Cf. releases.py:13 parse_releases(), releases.py:30 parse_fw_version()
#include <fstream>
#include <regex>
#include <sstream>
#include <stdexcept>

#ifndef __EMSCRIPTEN__
#include <curl/curl.h>
#endif

#include "log.h"
#include "rpc_fw_downloader.h"

#define LOG(logger) ::logger.Log() << "[fw-update] "

const std::string TFwDownloader::FW_RELEASES_BASE_URL = "https://fw-releases.wirenboard.com";
const std::chrono::minutes TFwDownloader::RELEASE_CACHE_TTL{10};
const std::chrono::minutes TFwDownloader::BOOTLOADER_CACHE_TTL{30};
const std::chrono::hours TFwDownloader::WBFW_CACHE_TTL{2};

#ifndef __EMSCRIPTEN__

// ============================================================
//                     TCurlHttpClient
// ============================================================

namespace
{
    size_t CurlWriteCallback(void* contents, size_t size, size_t nmemb, void* userp)
    {
        auto* buffer = static_cast<std::vector<uint8_t>*>(userp);
        auto totalSize = size * nmemb;
        auto* bytes = static_cast<uint8_t*>(contents);
        buffer->insert(buffer->end(), bytes, bytes + totalSize);
        return totalSize;
    }
}

std::vector<uint8_t> TCurlHttpClient::GetBinary(const std::string& url)
{
    struct CurlCleanup
    {
        void operator()(CURL* c)
        {
            curl_easy_cleanup(c);
        }
    };
    std::unique_ptr<CURL, CurlCleanup> curl(curl_easy_init());
    if (!curl) {
        throw std::runtime_error("Failed to initialize curl");
    }

    std::vector<uint8_t> buffer;
    curl_easy_setopt(curl.get(), CURLOPT_URL, url.c_str());
    curl_easy_setopt(curl.get(), CURLOPT_WRITEFUNCTION, CurlWriteCallback);
    curl_easy_setopt(curl.get(), CURLOPT_WRITEDATA, &buffer);
    curl_easy_setopt(curl.get(), CURLOPT_FOLLOWLOCATION, 1L);
    curl_easy_setopt(curl.get(), CURLOPT_TIMEOUT, 30L);
    curl_easy_setopt(curl.get(), CURLOPT_CONNECTTIMEOUT, 10L);

    CURLcode res = curl_easy_perform(curl.get());
    if (res != CURLE_OK) {
        throw std::runtime_error("Failed to download " + url + ": " + curl_easy_strerror(res));
    }

    long httpCode = 0;
    curl_easy_getinfo(curl.get(), CURLINFO_RESPONSE_CODE, &httpCode);

    if (httpCode != 200) {
        throw std::runtime_error("HTTP " + std::to_string(httpCode) + " downloading " + url);
    }

    return buffer;
}

std::string TCurlHttpClient::GetText(const std::string& url)
{
    auto data = GetBinary(url);
    std::string text(data.begin(), data.end());
    // Trim whitespace
    auto start = text.find_first_not_of(" \t\n\r");
    auto end = text.find_last_not_of(" \t\n\r");
    if (start == std::string::npos) {
        throw std::runtime_error(url + " is empty");
    }
    return text.substr(start, end - start + 1);
}
#endif

// ============================================================
//                     WBFW Parsing
// ============================================================

// Cf. firmware_update.py:185 parse_wbfw()
TParsedWBFW ParseWBFW(const std::vector<uint8_t>& data)
{
    // Info block is 16 registers * 2 bytes = 32 bytes
    const size_t INFO_BLOCK_SIZE = 32;

    if (data.size() % 2 != 0) {
        throw std::runtime_error("Firmware file should be even-bytes long, got " + std::to_string(data.size()) + "b");
    }

    if (data.size() < INFO_BLOCK_SIZE) {
        throw std::runtime_error("Firmware file too short: info block should be " + std::to_string(INFO_BLOCK_SIZE) +
                                 " bytes, got " + std::to_string(data.size()));
    }

    TParsedWBFW result;
    result.Info.assign(data.begin(), data.begin() + INFO_BLOCK_SIZE);
    result.Data.assign(data.begin() + INFO_BLOCK_SIZE, data.end());
    return result;
}

// ============================================================
//                  Version from URL
// ============================================================

// Cf. releases.py:30 parse_fw_version()
std::string ParseFwVersionFromUrl(const std::string& url)
{
    // Match .../version.wbfw or .../version.compfw
    std::regex re(R"(.+/(.+)\.(wbfw|compfw))");
    std::smatch match;
    if (std::regex_match(url, match, re)) {
        return match[1].str();
    }
    throw std::runtime_error("Could not parse firmware version from URL: " + url);
}

// ============================================================
//                  Release YAML Parsing
// ============================================================

// Minimal parser for release-versions.yaml from fw-releases.wirenboard.com.
// Only handles the specific format used by the release server:
//   releases:
//     signature1:
//       suite1: path/to/firmware.wbfw
//       suite2: path/to/firmware.wbfw
//     signature2:
//       suite1: path/to/firmware.wbfw
// Limitations: no support for YAML features like quoted strings, multi-line values,
// anchors, aliases, or flow syntax. Indentation-based: 2 spaces = signature, 4+ = suite.
std::map<std::string, std::map<std::string, std::string>> ParseReleaseVersionsYaml(const std::string& text)
{
    std::map<std::string, std::map<std::string, std::string>> result;
    std::istringstream stream(text);
    std::string line;
    bool inReleases = false;
    std::string currentSignature;

    while (std::getline(stream, line)) {
        // Skip empty lines and comments
        if (line.empty() || line[0] == '#') {
            continue;
        }

        // Count leading spaces for indentation level
        size_t indent = 0;
        while (indent < line.size() && line[indent] == ' ') {
            ++indent;
        }

        std::string trimmed = line.substr(indent);

        // Remove trailing whitespace and carriage return
        while (!trimmed.empty() && (trimmed.back() == ' ' || trimmed.back() == '\r' || trimmed.back() == '\n')) {
            trimmed.pop_back();
        }

        if (trimmed.empty() || trimmed[0] == '#') {
            continue;
        }

        // Look for "releases:" at indent 0
        if (indent == 0 && trimmed == "releases:") {
            inReleases = true;
            currentSignature.clear();
            continue;
        }

        if (!inReleases) {
            continue;
        }

        // New top-level key means end of releases section
        if (indent == 0) {
            break;
        }

        auto colonPos = trimmed.find(':');
        if (colonPos == std::string::npos) {
            continue;
        }

        std::string key = trimmed.substr(0, colonPos);
        std::string value = trimmed.substr(colonPos + 1);

        // Trim key
        while (!key.empty() && key.back() == ' ') {
            key.pop_back();
        }

        // Trim value
        while (!value.empty() && value.front() == ' ') {
            value.erase(value.begin());
        }

        if (indent <= 2) {
            // This is a signature line (indent level 1: 2 spaces)
            currentSignature = key;
            if (result.find(currentSignature) == result.end()) {
                result[currentSignature] = {};
            }
        } else if (!currentSignature.empty()) {
            // This is a suite:path line (indent level 2: 4+ spaces)
            result[currentSignature][key] = value;
        }
    }

    return result;
}

// ============================================================
//                  Release Suite
// ============================================================

// Cf. releases.py:13 parse_releases()
std::string ReadReleaseSuite(const std::string& releasePath)
{
    std::ifstream file(releasePath);
    if (!file.is_open()) {
        LOG(Warn) << "Cannot open release file: " << releasePath;
        return "";
    }

    std::string line;
    while (std::getline(file, line)) {
        auto eqPos = line.find('=');
        if (eqPos != std::string::npos) {
            std::string key = line.substr(0, eqPos);
            std::string value = line.substr(eqPos + 1);
            // Trim
            while (!key.empty() && (key.back() == ' ' || key.back() == '\t')) {
                key.pop_back();
            }
            while (!value.empty() && (value.front() == ' ' || value.front() == '\t')) {
                value.erase(value.begin());
            }
            if (key == "SUITE") {
                return value;
            }
        }
    }
    return "";
}

// ============================================================
//                     TFwDownloader
// ============================================================

TFwDownloader::TFwDownloader(PHttpClient httpClient): HttpClient(std::move(httpClient))
{}

std::map<std::string, std::map<std::string, std::string>> TFwDownloader::GetReleases()
{
    auto now = std::chrono::steady_clock::now();
    {
        std::lock_guard<std::mutex> lock(CacheMutex);
        if (now < ReleaseCache.ExpiresAt && !ReleaseCache.Releases.empty()) {
            return ReleaseCache.Releases;
        }
    }

    auto url = FW_RELEASES_BASE_URL + "/fw/by-signature/release-versions.yaml";
    auto text = HttpClient->GetText(url);
    auto releases = ParseReleaseVersionsYaml(text);

    {
        std::lock_guard<std::mutex> lock(CacheMutex);
        ReleaseCache.Releases = releases;
        ReleaseCache.ExpiresAt = now + RELEASE_CACHE_TTL;
    }

    return releases;
}

// Cf. fw_downloader.py:89 get_released_fw()
TReleasedBinary TFwDownloader::GetReleasedFirmware(const std::string& fwSignature, const std::string& releaseSuite)
{
    auto releases = GetReleases();

    auto sigIt = releases.find(fwSignature);
    if (sigIt == releases.end()) {
        throw std::runtime_error("Released firmware not found for " + fwSignature + ", release: " + releaseSuite);
    }

    auto suiteIt = sigIt->second.find(releaseSuite);
    if (suiteIt == sigIt->second.end()) {
        throw std::runtime_error("Released firmware not found for " + fwSignature + ", release: " + releaseSuite);
    }

    auto endpoint = FW_RELEASES_BASE_URL + "/" + suiteIt->second;
    auto version = ParseFwVersionFromUrl(endpoint);

    return {version, endpoint};
}

// Cf. fw_downloader.py:135 get_latest_bootloader()
TReleasedBinary TFwDownloader::GetLatestBootloader(const std::string& fwSignature)
{
    auto now = std::chrono::steady_clock::now();
    {
        std::lock_guard<std::mutex> lock(CacheMutex);
        auto it = BootloaderCache.find(fwSignature);
        if (it != BootloaderCache.end() && now < it->second.ExpiresAt) {
            return it->second.Binary;
        }
    }

    auto prefix = FW_RELEASES_BASE_URL + "/bootloader/by-signature/" + fwSignature + "/main";
    auto version = HttpClient->GetText(prefix + "/latest.txt");
    auto endpoint = prefix + "/" + version + ".wbfw";

    TReleasedBinary result{version, endpoint};

    {
        std::lock_guard<std::mutex> lock(CacheMutex);
        TBootloaderCacheEntry entry;
        entry.ExpiresAt = now + BOOTLOADER_CACHE_TTL;
        entry.Binary = result;
        BootloaderCache[fwSignature] = entry;
    }

    return result;
}

// Cf. firmware_update.py:205 download_wbfw()
TParsedWBFW TFwDownloader::DownloadAndParseWBFW(const std::string& url)
{
    auto now = std::chrono::steady_clock::now();
    {
        std::lock_guard<std::mutex> lock(CacheMutex);
        auto it = WBFWCache.find(url);
        if (it != WBFWCache.end() && now < it->second.ExpiresAt) {
            return it->second.Firmware;
        }
    }

    auto data = HttpClient->GetBinary(url);
    auto firmware = ParseWBFW(data);

    {
        std::lock_guard<std::mutex> lock(CacheMutex);
        TWBFWCacheEntry wbfwEntry;
        wbfwEntry.ExpiresAt = now + WBFW_CACHE_TTL;
        wbfwEntry.Firmware = firmware;
        WBFWCache[url] = wbfwEntry;
    }

    return firmware;
}
