#pragma once 

#include <string>

enum TDeviceTemplateGenerationMode
{
    PRINT = 0,                          //! Print information about generation source to stdout
    VERBOSE_PRINT,                      //! Print verbose information about generation source to stdout
    GENERATE_TEMPLATE,                  //! Generate device template file
    MAX_DEVICE_TEMPLATE_GENERATION_MODE
};

/**
 * @brief Generate device template file 
 * 
 * @param appName - apptication name 
 * @param destinationDir - directory to store generated template
 * @param options - command line options
 */
void GenerateDeviceTemplate(const std::string& appName,
                            const std::string& destinationDir,
                            const char* options);