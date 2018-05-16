#pragma once

#include "declarations.h"
#include "utils.h"

#include <vector>


class TIRDeviceQueryFactory
{
    TIRDeviceQueryFactory() = delete;

public:
    enum EQueryGenerationPolicy: uint8_t
    {
        Minify,     // produce as little queries as possible by merging sets with allowed holes (initial behaviour)
        NoHoles,    // produce as little queries as possible by merging sets but without holes
        AsIs        // do not modify sets, <number of queries> == <number of sets>
    };

    static const EQueryGenerationPolicy Default;

    /**
     * Generate query sets grouping protocol registers by virtual registers' type and poll interval
     * and return query sets grouped by poll interval
     */
    static std::vector<std::pair<TIntervalMs, PIRDeviceQuerySet>> GenerateQuerySets(const std::vector<PVirtualRegister> &, EQueryOperation);

    static TQueries GenerateQueries(std::list<TPSet<PMemoryBlock>> && memoryBlockSets, EQueryOperation, EQueryGenerationPolicy = Default);

    template <class Query>
    static std::shared_ptr<Query> CreateQuery(const TPSet<PMemoryBlock> & memoryBlockSet)
    {
        static_assert(std::is_base_of<TIRDeviceQuery, Query>::value, "Query must be derived from TIRDeviceQuery");

        return std::shared_ptr<Query>(new Query(memoryBlockSet));
    }

private:
    using TRegisterTypeInfo = std::function<std::pair<uint32_t, uint32_t>(const TMemoryBlockType &)>;

    static void CheckSets(const std::list<TPSet<PMemoryBlock>> & memoryBlockSets, const TRegisterTypeInfo &);
    static void MergeSets(std::list<TPSet<PMemoryBlock>> & memoryBlockSets, const TRegisterTypeInfo &);
};
