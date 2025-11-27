#pragma once

#include "templates_map.h"

/**
 * @brief Updates the given configuration to the latest format if it uses an old format.
 *
 * This function inspects and modifies the provided JSON configuration object in-place,
 * converting any deprecated or legacy fields and structures to the current expected format.
 * It is intended to maintain backward compatibility with older configuration files.
 *
 * @param config Reference to a Json::Value object representing the configuration to be fixed.
 */
void FixOldConfigFormat(Json::Value& config, TTemplateMap& templates);
