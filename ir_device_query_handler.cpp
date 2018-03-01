#include "ir_device_query_handler.h"
#include "ir_device_query_factory.h"
#include "ir_device_query.h"
#include "virtual_register.h"
#include "serial_device.h"

#include <cassert>

using namespace std;

namespace
{

}

void TIRDeviceQuerySetHandler::HandleQuerySetPostExecution(const PIRDeviceQuerySet & querySet)
{
    DisableHolesIfNeeded(querySet);
    SplitByRegisterIfNeeded(querySet);
    DisableRegistersIfNeeded(querySet);
}

void TIRDeviceQuerySetHandler::DisableHolesIfNeeded(const PIRDeviceQuerySet & querySet)
{
    static auto filter = [](const PIRDeviceQuery & query){
        return query->GetStatus() == EQueryStatus::DevicePermanentError
            && query->VirtualRegisters.size() > 1
            && query->HasHoles;
    };

    for (auto itQuery = querySet->Queries.begin(); itQuery != querySet->Queries.end();) {
        const auto & query = *itQuery;

        if (!filter(query)) {
            ++itQuery; continue;
        }

        std::list<TPSet<PProtocolRegister>> groupedRegisters;
        for (const auto & virtualRegister: query->VirtualRegisters) {
            groupedRegisters.push_back(virtualRegister->GetProtocolRegisters());
        }

        const auto & generatedQueries = TIRDeviceQueryFactory::GenerateQueries(move(groupedRegisters), false, query->Operation);

        itQuery = querySet->Queries.erase(itQuery);
        querySet->Queries.insert(itQuery, generatedQueries.begin(), generatedQueries.end());
    }

    assert(!querySet->Queries.empty());
}

void TIRDeviceQuerySetHandler::SplitByRegisterIfNeeded(const PIRDeviceQuerySet & querySet)
{
    static auto filter = [](const PIRDeviceQuery & query){
        return query->GetStatus() == EQueryStatus::DevicePermanentError
            && query->VirtualRegisters.size() > 1
            && !query->HasHoles;
    };

    const auto & protocolInfo = querySet->GetDevice()->GetProtocolInfo();

    const bool isRead        = (*querySet->Queries.begin())->Operation == EQueryOperation::Read;
    const bool singleBitType = protocolInfo.IsSingleBitType((*querySet->Queries.begin())->GetType());

    const auto & createQuery = isRead ? TIRDeviceQueryFactory::CreateQuery<TIRDeviceQuery>
                                      : singleBitType ? TIRDeviceQueryFactory::CreateQuery<TIRDeviceSingleBitQuery>
                                                      : TIRDeviceQueryFactory::CreateQuery<TIRDevice64BitQuery>;

    for (auto itQuery = querySet->Queries.begin(); itQuery != querySet->Queries.end();) {
        const auto & query = *itQuery;

        if (!filter(query)) {
            ++itQuery; continue;
        }

        itQuery = querySet->Queries.erase(itQuery);
        for (const auto & virtualRegister: query->VirtualRegisters) {
            querySet->Queries.insert(itQuery, createQuery(virtualRegister->GetProtocolRegisters()));
        }
    }
}

void TIRDeviceQuerySetHandler::DisableRegistersIfNeeded(const PIRDeviceQuerySet & querySet)
{
    static auto filter = [](const PIRDeviceQuery & query){
        return query->GetStatus() == EQueryStatus::DevicePermanentError
            && query->VirtualRegisters.size() == 1;
    };

    for (const auto & query: querySet->Queries) {
        if (!filter(query)) {
            continue;
        }

        query->SetEnabledWithRegisters(false);
    }
}
