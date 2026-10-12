#pragma once

#include "confed_device_schemas_map.h"
#include "port_task_dispatcher.h"
#include "serial_config.h"

struct TTemplateChange
{
    std::string DeviceType;

    //! Empty for a deletion
    std::string WritePath;
    std::string Content;
    Json::Value Root;

    //! The file of a deletion or the previous user template file of an upload
    std::string RemovePath;
};

//! @throws std::runtime_error if the template or the file name is invalid
TTemplateChange MakeUploadChange(TTemplateMap& templates, const std::string& content, const std::string& fileName);

//! @throws std::runtime_error if the type has no user template
TTemplateChange MakeDeleteChange(TTemplateMap& templates, const std::string& deviceType);

//! A copy of the templates with the change, the files are not changed
std::shared_ptr<TTemplateMap> MakeChangedTemplates(TTemplateMap& templates, const TTemplateChange& change);

//! @throws std::exception, the files are not changed then
void WriteTemplateFile(const TTemplateChange& change);

//! Both files are written to the temporary files first, so a failure of the writing changes nothing
//! @throws std::exception
void WriteTemplateAndConfigFiles(const TTemplateChange& change,
                                 const std::string& configPath,
                                 const Json::Value& config);

/**
 * @brief Changes the templates by the written files in place, so the changes made by the files watcher meanwhile
 *        are kept. The previous file of an upload is removed if its name differs, a failure is only logged
 */
void CommitTemplates(const TTemplateChange& change,
                     TTemplateMap& templates,
                     TDevicesConfedSchemasMap& deviceConfedSchemas);

//! Null if the type has no template
PDeviceTemplate FindTemplate(TTemplateMap& templates, const std::string& deviceType);

//! The disabled devices are counted too
size_t CountDevicesOfType(const Json::Value& config, const std::string& deviceType);

struct TConfigDeviceRef
{
    //! The path or "address:port"
    std::string Port;
    std::string SlaveId;
};

struct TApplyProblems
{
    //! The device or its port is disabled
    std::vector<TConfigDeviceRef> DisabledDevices;
    std::vector<TConfigDeviceRef> UnreadDevices;
    //! The device fails the check of the new template
    std::vector<TConfigDeviceRef> InvalidDevices;

    bool Empty() const;
};

struct TChangedConfig
{
    Json::Value Config;
    //! Null if Config fails the check
    PHandlerConfig HandlerConfig;
    TApplyProblems Problems;
};

/**
 * @brief Reads the enabled devices of the type by the new template if it has "hw", the read devices get the read
 *        parameters, the other ones keep theirs. Each device is checked on its port alone
 *
 * @throws the exceptions of TPortTaskDispatcher::AddTask
 */
TChangedConfig MakeChangedConfig(const Json::Value& config,
                                 const std::string& deviceType,
                                 const TConfigLoadParams& loadParams,
                                 const std::string& configPath,
                                 TTemplateMap& oldTemplates,
                                 TTemplateMap& newTemplates,
                                 TPortTaskDispatcher& portTasks);
