#pragma once

#include <list>
#include <string>

#include <wblib/json_utils.h>

struct TFwDeviceInfo;
class TFwDownloader;

// Non-updatable signatures (e.g. WB-MSW-LORA devices)
const std::list<std::string> NonUpdatableSignatures = {"msw5GL", "msw3G419L"};

// Pure helper functions extracted from rpc_fw_update_handler.cpp for testability.
// These are used internally by TRPCFwUpdateHandler and exposed here for unit tests.

bool IsNonUpdatableSignature(const std::string& sig);
bool FirmwareIsNewer(const std::string& currentVersion, const std::string& availableVersion);
bool ComponentFirmwareIsNewer(const std::string& currentVersion, const std::string& availableVersion);

// True if every byte of s is printable 7-bit ASCII (0x20..0x7E).
bool IsPrintableAscii(const std::string& s);

// True if sig is a plausible firmware signature: non-empty and printable ASCII.
bool IsValidFwSignature(const std::string& sig);

// Returns a firmware/bootloader version string if it is printable ASCII, otherwise
// an empty string.
std::string SanitizeVersionString(const std::string& s);

Json::Value BuildFirmwareInfoResponse(const TFwDeviceInfo& deviceInfo,
                                      TFwDownloader& downloader,
                                      const std::string& releaseSuite);
