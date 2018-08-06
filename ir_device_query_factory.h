#pragma once

#include "declarations.h"


class TIRDeviceQueryFactory
{
    TIRDeviceQueryFactory() = delete;

public:
    enum EQueryGenerationPolicy: uint8_t
    {
        Minify,         // produce as little queries as possible by merging sets with allowed holes (initial behaviour)
        NoHoles,        // produce as little queries as possible by merging sets but without holes
        NoDuplicates    // do not modify sets, <number of queries> == <number of sets>
    };

    static const EQueryGenerationPolicy Default;

    /**
     * Generate query sets grouping protocol registers by virtual registers' type and poll interval
     * and return query sets grouped by poll interval
     */
    static std::vector<std::pair<TIntervalMs, PIRDeviceQuerySet>> GenerateQuerySets(const std::vector<PVirtualRegister> &, EQueryOperation);
    static TQueries GenerateQueries(const std::vector<PVirtualRegister> &, EQueryOperation, EQueryGenerationPolicy = Default);
    static TQueries GenerateQueries(const std::vector<PVirtualValue> &, EQueryOperation, EQueryGenerationPolicy = Default);

    template <class Query>
    static std::shared_ptr<Query> CreateQuery(TAssociatedMemoryBlockSet && memoryBlocks)
    {
        static_assert(std::is_base_of<TIRDeviceQuery, Query>::value, "Query must be derived from TIRDeviceQuery");

        return std::shared_ptr<Query>(new Query(std::move(memoryBlocks)));
    }

    template <class Query>
    static std::shared_ptr<Query> CreateQuery(TPSet<PMemoryBlock> && memoryBlocks)
    {
        static_assert(std::is_base_of<TIRDeviceQuery, Query>::value, "Query must be derived from TIRDeviceQuery");

        return std::shared_ptr<Query>(new Query(std::move(memoryBlocks)));
    }

private:
    using TRegisterTypeInfo = std::function<std::pair<uint32_t, uint32_t>(const TMemoryBlockType &)>;
    using TAssociatedMemoryBlockList = std::list<TAssociatedMemoryBlockSet>;

    static TQueries GenerateQueries(
        TAssociatedMemoryBlockList &&,
        EQueryOperation,
        EQueryGenerationPolicy);

    static void CheckSets(const TAssociatedMemoryBlockList &, const TRegisterTypeInfo &);
    static void MergeSets(
        TAssociatedMemoryBlockList &,
        const TRegisterTypeInfo &,
        EQueryGenerationPolicy);
};
