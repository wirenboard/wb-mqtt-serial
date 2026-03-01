#pragma once

#include <string>

// Pure helper functions extracted from rpc_fw_update_handler.cpp for testability.
// These are used internally by TRPCFwUpdateHandler and exposed here for unit tests.

bool IsNonUpdatableSignature(const std::string& sig);
bool FirmwareIsNewer(const std::string& currentVersion, const std::string& availableVersion);
bool ComponentFirmwareIsNewer(const std::string& currentVersion, const std::string& availableVersion);
