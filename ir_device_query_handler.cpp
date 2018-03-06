#include "ir_device_query_handler.h"
#include "ir_device_query_factory.h"
#include "ir_device_query.h"
#include "virtual_register.h"
#include "serial_device.h"

#include <cassert>

using namespace std;

namespace   // utility
{
    template <typename TCondition>
    void RecreateQueries(const PIRDeviceQuerySet & querySet, const TCondition & condition, TIRDeviceQueryFactory::EQueryGenerationPolicy policy)
    {
        for (auto itQuery = querySet->Queries.begin(); itQuery != querySet->Queries.end();) {
            const auto & query = *itQuery;

            if (!condition(query)) {
                ++itQuery; continue;
            }

            std::list<TPSet<PProtocolRegister>> groupedRegisters;
            for (const auto & virtualRegister: query->VirtualRegisters) {
                groupedRegisters.push_back(virtualRegister->GetProtocolRegisters());
            }

            try {
                const auto & generatedQueries = TIRDeviceQueryFactory::GenerateQueries(move(groupedRegisters), query->Operation, policy);
                itQuery = querySet->Queries.erase(itQuery);
                querySet->Queries.insert(itQuery, generatedQueries.begin(), generatedQueries.end());
            } catch (const TSerialDeviceException & e) {
                query->SetAbleToSplit(false);

                cerr << "INFO: unable to recreate query " << query->Describe() << ": " << e.what() << endl;

                ++itQuery; continue;
            }
        }

        assert(!querySet->Queries.empty());
    }
}

void TIRDeviceQuerySetHandler::HandleQuerySetPostExecution(const PIRDeviceQuerySet & querySet)
{
    DisableHolesIfNeeded(querySet);
    SplitByRegisterIfNeeded(querySet);
    DisableRegistersIfNeeded(querySet);
    ResetQueriesStatuses(querySet);
}

void TIRDeviceQuerySetHandler::DisableHolesIfNeeded(const PIRDeviceQuerySet & querySet)
{
    static auto condition = [](const PIRDeviceQuery & query){
        return query->GetStatus() == EQueryStatus::DevicePermanentError
            && query->IsAbleToSplit()
            && query->HasHoles;
    };

    RecreateQueries(querySet, condition, TIRDeviceQueryFactory::NoHoles);
}

void TIRDeviceQuerySetHandler::SplitByRegisterIfNeeded(const PIRDeviceQuerySet & querySet)
{
    static auto condition = [](const PIRDeviceQuery & query){
        return query->GetStatus() == EQueryStatus::DevicePermanentError
            && query->IsAbleToSplit()
            && !query->HasHoles;
    };

    RecreateQueries(querySet, condition, TIRDeviceQueryFactory::AsIs);
}

void TIRDeviceQuerySetHandler::DisableRegistersIfNeeded(const PIRDeviceQuerySet & querySet)
{
    static auto condition = [](const PIRDeviceQuery & query){
        return query->GetStatus() == EQueryStatus::DevicePermanentError
            && !query->IsAbleToSplit();
    };

    for (const auto & query: querySet->Queries) {
        if (!condition(query)) {
            continue;
        }

        query->SetEnabledWithRegisters(false);
    }
}

void TIRDeviceQuerySetHandler::ResetQueriesStatuses(const PIRDeviceQuerySet & querySet)
{
    for (const auto & query: querySet->Queries) {
        query->ResetStatus();
    }
}
