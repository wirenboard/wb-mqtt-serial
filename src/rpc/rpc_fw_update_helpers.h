#pragma once

#include <string>

#include "rpc_fw_downloader.h"
#include <wblib/json_utils.h>

struct TFwDeviceInfo;

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

/**
 * @param deviceIsUpdatable the device can be flashed over the port it is polled on
 */
Json::Value BuildFirmwareInfoResponse(const TFwDeviceInfo& deviceInfo,
                                      TFwDownloader& downloader,
                                      const std::string& releaseSuite,
                                      bool deviceIsUpdatable,
                                      ENetworkAccess networkAccess = ENetworkAccess::Allowed);
