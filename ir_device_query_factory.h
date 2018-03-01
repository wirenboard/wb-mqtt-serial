#pragma once

#include "declarations.h"
#include "utils.h"


class TIRDeviceQueryFactory
{
    TIRDeviceQueryFactory() = delete;

public:
    static TQueries GenerateQueries(std::list<TPSet<PProtocolRegister>> && registerSets, bool enableHoles, EQueryOperation);

    template <class Query>
    static PIRDeviceQuery CreateQuery(const TPSet<PProtocolRegister> & registerSet)
    {
        return PIRDeviceQuery(new Query(registerSet));
    }

private:
    static void MergeSets(std::list<TPSet<PProtocolRegister>> & registerSets, uint32_t maxHole, uint32_t maxRegs);
};
