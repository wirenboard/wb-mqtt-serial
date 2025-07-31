#pragma once

#include <wblib/json_utils.h>

#include "serial_port_settings.h"

TSerialPortConnectionSettings ParseRPCSerialPortSettings(const Json::Value& request);

/**
 * @brief Validates an RPC request against a given JSON schema.
 *
 * @param request The JSON object representing the RPC request to be validated.
 * @param schema The JSON object representing the schema to validate the request against.
 *
 * @throws TRPCException with TRPCResultCode::RPC_WRONG_PARAM_VALUE code, if the request does not conform to the schema.
 */
void ValidateRPCRequest(const Json::Value& request, const Json::Value& schema);

/**
 * @brief Loads the JSON schema for an RPC request from a specified file.
 *        Prints a message to the log if the file cannot be read and throws an exception.
 *
 * @param schemaFilePath The path to the JSON schema file.
 * @param rpcName The name of the RPC for which the schema is to be loaded. Example: "port/Load".
 * @return A Json::Value object containing the schema.
 * @throws std::runtime_error If the schema file cannot be read.
 */
Json::Value LoadRPCRequestSchema(const std::string& schemaFilePath, const std::string& rpcName);

std::chrono::milliseconds ParseResponseTimeout(const Json::Value& request,
                                               std::chrono::milliseconds portResponseTimeout);
