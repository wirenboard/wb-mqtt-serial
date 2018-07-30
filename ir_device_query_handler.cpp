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
    void RecreateQueries(
        const PIRDeviceQuerySet & querySet,
        const TCondition & condition,
        TIRDeviceQueryFactory::EQueryGenerationPolicy policy,
        const char * actionName)
    {
        for (auto itQuery = querySet->Queries.begin(); itQuery != querySet->Queries.end();) {
            auto query = *itQuery;

            if (!condition(query)) {
                ++itQuery; continue;
            }

            cerr << "INFO: [IR device query handler] " << actionName
                 << " on query " << query->Describe() << endl;

            try {
                const auto & generatedQueries = TIRDeviceQueryFactory::GenerateQueries(
                    query->VirtualValues, query->Operation, policy
                );

                if (generatedQueries.size() == 1) {
                    throw TSerialDeviceException("query is atomic");
                }

                itQuery = querySet->Queries.erase(itQuery);
                querySet->Queries.insert(itQuery, generatedQueries.begin(), generatedQueries.end());

                cerr << "INFO: [IR device query handler] recreated query " << query->Describe()
                     << " as " << PrintCollection(generatedQueries, [](ostream & s, const PIRDeviceQuery & query){
                         s << "\t" << query->Describe();
                     }, true, "") << endl;

            } catch (const TSerialDeviceException & e) {
                query->SetAbleToSplit(false);

                cerr << "INFO: unable to recreate query " << query->Describe() << ": " << e.what() << endl;

                ++itQuery; continue;
            }
        }

        assert(!querySet->Queries.empty());
    }
}

/**
 * @brief helper class to enable and disable query
 *  in RAII style
 */
class TQueryDisableGuard
{
    PIRDeviceQuery Query;

public:
    TQueryDisableGuard(const TQueryDisableGuard &) = delete;
    TQueryDisableGuard(TQueryDisableGuard &&) = default;

    TQueryDisableGuard(PIRDeviceQuery query)
        : Query(move(query))
    {
        if (Query) Query->SetEnabled(false);
    }

    ~TQueryDisableGuard()
    {
        if (Query) Query->SetEnabled(true);
    }
};

TIRDeviceQuerySetHandler::TIRDeviceQuerySetHandler() {}
TIRDeviceQuerySetHandler::~TIRDeviceQuerySetHandler() {}

void TIRDeviceQuerySetHandler::HandleQuerySetPostExecution(const PIRDeviceQuerySet & querySet)
{
    DisableHolesIfNeeded(querySet);
    SplitByRegisterIfNeeded(querySet);
    DisableRegistersIfNeeded(querySet);
    ResetQueriesStatuses(querySet);
    InvalidateReadValues(querySet);
}

void TIRDeviceQuerySetHandler::ResetDisabledQueries(const PSerialDevice & device)
{
    DisabledQueries.erase(device);
}

void TIRDeviceQuerySetHandler::DisableHolesIfNeeded(const PIRDeviceQuerySet & querySet)
{
    static auto condition = [](const PIRDeviceQuery & query){
        return query->GetStatus() == EQueryStatus::DevicePermanentError
            && query->IsAbleToSplit()
            && query->HasHoles;
    };

    RecreateQueries(querySet, condition, TIRDeviceQueryFactory::NoHoles, "disable holes");
}

void TIRDeviceQuerySetHandler::SplitByRegisterIfNeeded(const PIRDeviceQuerySet & querySet)
{
    static auto condition = [](const PIRDeviceQuery & query){
        return query->GetStatus() == EQueryStatus::DevicePermanentError
            && query->IsAbleToSplit()
            && !query->HasHoles;
    };

    RecreateQueries(querySet, condition, TIRDeviceQueryFactory::NoDuplicates, "split by register");
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

        cerr << "INFO: [IR device query handler] disable query " << query->Describe() << endl;

        DisabledQueries[query->GetDevice()].emplace_back(query);
    }
}

void TIRDeviceQuerySetHandler::ResetQueriesStatuses(const PIRDeviceQuerySet & querySet)
{
    for (const auto & query: querySet->Queries) {
        query->ResetStatus();
    }
}

void TIRDeviceQuerySetHandler::InvalidateReadValues(const PIRDeviceQuerySet & querySet)
{
    for (const auto & query: querySet->Queries) {
        query->InvalidateReadValues();
    }
}
