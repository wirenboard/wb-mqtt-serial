#pragma once

#include "declarations.h"

class TQueryDisableGuard;

class TIRDeviceQuerySetHandler
{
    std::unordered_map<PSerialDevice, std::vector<TQueryDisableGuard>> DisabledQueries;

public:
    /**
     * @note this constructor and destructor declarations are only needed to be able
     *  to declare DisabledQueries with incomplete TQueryDisableGuard type
     *  by moving their definitions to same translation unit where TQueryDisableGuard is defined
     */
    TIRDeviceQuerySetHandler();
    ~TIRDeviceQuerySetHandler();

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
     * @brief if query has permanent error but without holes
     *  split by virtual registers
     */
    void SplitByRegisterIfNeeded(const PIRDeviceQuerySet &);

    /**
     * @brief if query has permanent error and associated with
     *  only one virtual register - disable it
     */
    void DisableRegistersIfNeeded(const PIRDeviceQuerySet &);

    /**
     * @brief reset statuses of queries (must run last)
     */
    void ResetQueriesStatuses(const PIRDeviceQuerySet &);

    /**
     * @brief invalidate protocol registers values for all affected virtual registers
     *
     * @note A protocol value that was read inside cycle expires at end of that cycle
     */
    void InvalidateReadValues(const PIRDeviceQuerySet &);
};
