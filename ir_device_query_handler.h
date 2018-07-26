#pragma once

#include "declarations.h"

class TIRDeviceQuerySetHandler
{
    /**
     * @brief helper class to enable and disable query
     *  in RAII style
     */
    class TQueryDisableGuard
    {
        PIRDeviceQuery Query;

    public:
        TQueryDisableGuard(PIRDeviceQuery);
        TQueryDisableGuard(const TQueryDisableGuard &) = delete;
        TQueryDisableGuard(TQueryDisableGuard &&) = default;
        ~TQueryDisableGuard();
    };

    std::unordered_map<PSerialDevice, std::vector<TQueryDisableGuard>> DisabledQueries;

public:
    /**
     * @brief perform needed operations on query set after all queries of set were executed
     */
    void HandleQuerySetPostExecution(const PIRDeviceQuerySet &);

    /**
     * @brief enable all disabled queries of given device
     */
    void ResetDisabledQueries(const PSerialDevice &);

private:
    /**
     * @brief if query has permanent error and holes - disable holes
     */
    void DisableHolesIfNeeded(const PIRDeviceQuerySet &);

    /**
     * @brief if query has permanent error but without holes split by virtual registers
     */
    void SplitByRegisterIfNeeded(const PIRDeviceQuerySet &);

    /**
     * @brief if query has permanent error and associated with only one virtual register - disable it
     */
    void DisableRegistersIfNeeded(const PIRDeviceQuerySet &);

    /**
     * @brief reset statuses of queries (must run last)
     */
    void ResetQueriesStatuses(const PIRDeviceQuerySet &);

    /**
     * @brief invalidate protocol registers values for all affected virtual registers
     *
     * @note A protocol register value that was read inside cycle expires at end of that cycle
     */
    void InvalidateReadValues(const PIRDeviceQuerySet &);
};
