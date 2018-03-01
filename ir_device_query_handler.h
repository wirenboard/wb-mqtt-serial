#pragma once

#include "declarations.h"

class TIRDeviceQuerySetHandler
{
    TIRDeviceQuerySetHandler() = delete;
public:
    /**
     * perform needed operations on query set after all queries of set were executed
     */
    static void HandleQuerySetPostExecution(const PIRDeviceQuerySet &);

private:
    /**
     * if query has permanent error and holes - disable holes
     */
    static void DisableHolesIfNeeded(const PIRDeviceQuerySet &);

    /**
     * if query has permanent error but without holes split by virtual registers
     */
    static void SplitByRegisterIfNeeded(const PIRDeviceQuerySet &);

    /**
     * if query has permanent error and associated with only one virtual register - disable it
     */
    static void DisableRegistersIfNeeded(const PIRDeviceQuerySet &);
};
