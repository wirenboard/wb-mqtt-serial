#pragma once

#include <memory>
#include <mutex>
#include <string>
#include <unordered_map>

#include <wblib/json_utils.h>

#include "port/port.h"

class TDeviceParametersCache
{
public:
    TDeviceParametersCache() = default;

    /**
     * Creates cache item identifier string based on simplified port description and device address.
     * For example: "/dev/ttyRS485-2:12" or "192.168.18.7:2321:33"
     */
    std::string GetId(const TPort& port, const std::string& slaveId) const;

    /**
     * Puts device parameters data into cache.
     * This method is thread safe.
     */
    void Add(const std::string& id, const Json::Value& value);

    /**
     * Removes device parameters data from cache.
     * This method is thread safe.
     */
    void Remove(const std::string& id);

    /**
     * Returns true if cache contains device parameters data or false otherwise.
     * This method is thread safe.
     */
    bool Contains(const std::string& id) const;

    /**
     * Returns device parameters data if cache contains it or defaultData otherwise.
     * This method is thread safe.
     */
    const Json::Value& Get(const std::string& id, const Json::Value& defaultValue = Json::Value()) const;

private:
    mutable std::mutex Mutex;
    std::unordered_map<std::string, Json::Value> DeviceParameters;
};

typedef std::shared_ptr<TDeviceParametersCache> PDeviceParametersCache;
