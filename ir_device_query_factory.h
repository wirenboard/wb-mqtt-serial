#pragma once

#include "declarations.h"
#include "utils.h"


class TIRDeviceQueryFactory
{
    TIRDeviceQueryFactory() = delete;

public:
    /**
     * Generate query sets grouping protocol registers by virtual registers' type and poll interval
     * and return query sets grouped by poll interval
     */
    static std::map<TIntervalMs, std::vector<PIRDeviceQuerySet>> GenerateQuerySets(const TPUnorderedSet<PVirtualRegister> &, EQueryOperation);

    static TQueries GenerateQueries(std::list<TPSet<PProtocolRegister>> && registerSets, bool enableHoles, EQueryOperation);

    template <class Query>
    static PIRDeviceQuery CreateQuery(const TPSet<PProtocolRegister> & registerSet)
    {
        return PIRDeviceQuery(new Query(registerSet));
    }

private:
    static void MergeSets(std::list<TPSet<PProtocolRegister>> & registerSets, uint32_t maxHole, uint32_t maxRegs);
};
