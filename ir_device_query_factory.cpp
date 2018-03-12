#include "ir_device_query_factory.h"
#include "ir_device_value_query_impl.h"
#include "serial_device.h"
#include "protocol_register.h"
#include "virtual_register.h"

#include <iostream>
#include <cassert>

using namespace std;

namespace // utility
{
    inline uint32_t GetMaxHoleSize(const PProtocolRegister & first, const PProtocolRegister & last)
    {
        assert(first->Address <= last->Address);

        // perform lookup not on set itself but on range - thus taking into account all created registers
        const auto & registerSetView = TSerialDevice::StaticCreateRegisterSetView(first, last);

        uint32_t hole = 0;

        int prev = -1;
        auto end = registerSetView.End();
        for (auto itReg = registerSetView.First; itReg != end; ++itReg) {
            const auto & reg = *itReg;

            assert((int)reg->Address > prev);

            if (prev >= 0) {
                hole = max(hole, (reg->Address - prev) - 1);
            }

            prev = reg->Address;
        }

        return hole;
    }

    inline uint32_t GetMaxHoleSize(const TPSet<PProtocolRegister> & registerSet)
    {
        return GetMaxHoleSize(*registerSet.begin(), *registerSet.rbegin());
    }

    inline uint32_t GetRegCount(const PProtocolRegister & first, const PProtocolRegister & last)
    {
        assert(first->Address <= last->Address);

        return last->Address - first->Address + 1;
    }

    inline uint32_t GetRegCount(const TPSet<PProtocolRegister> & registerSet)
    {
        return GetRegCount(*registerSet.begin(), *registerSet.rbegin());
    }

    bool IsReadOperation(EQueryOperation operation)
    {
        switch(operation) {
            case EQueryOperation::Read:
                return true;
            case EQueryOperation::Write:
                return false;
            default:
                assert(false);
                throw TSerialDeviceException("unknown operation: " + to_string((int)operation));
        }
    }

    template <class Query>
    void AddQueryImpl(const TPSet<PProtocolRegister> & registerSet, TPSet<PIRDeviceQuery> & result)
    {
        bool inserted = result.insert(TIRDeviceQueryFactory::CreateQuery<Query>(registerSet)).second;
        assert(inserted);
    }

    template <class Query>
    void AddQueryImpl(const TPSet<PProtocolRegister> & registerSet, list<PIRDeviceQuery> & result)
    {
        result.push_back(TIRDeviceQueryFactory::CreateQuery<Query>(registerSet));
    }

    template <class Query>
    void AddQuery(const TPSet<PProtocolRegister> & registerSet, TQueries & result)
    {
        AddQueryImpl<Query>(registerSet, result);
    }
}

const TIRDeviceQueryFactory::EQueryGenerationPolicy TIRDeviceQueryFactory::Default = TIRDeviceQueryFactory::Minify;

map<TIntervalMs, vector<PIRDeviceQuerySet>> TIRDeviceQueryFactory::GenerateQuerySets(const TPSet<PVirtualRegister> & virtualRegisters, EQueryOperation operation)
{
    map<TIntervalMs, vector<PIRDeviceQuerySet>> querySetsByPollInterval;
    {
        unordered_map<int64_t, map<uint32_t, list<TPSet<PProtocolRegister>>>> protocolRegistersByTypeAndInterval;

        for (const auto & virtualRegister: virtualRegisters) {
            protocolRegistersByTypeAndInterval[virtualRegister->PollInterval.count()][virtualRegister->Type].push_back(virtualRegister->GetProtocolRegisters());
        }

        for (auto & pollIntervalTypesRegisters: protocolRegistersByTypeAndInterval) {
            auto pollInterval = TIntervalMs(pollIntervalTypesRegisters.first);

            for (auto & typeRegisters: pollIntervalTypesRegisters.second) {
                auto & registers = typeRegisters.second;

                const auto & querySet = std::make_shared<TIRDeviceQuerySet>(std::move(registers), operation);
                querySetsByPollInterval[pollInterval].push_back(querySet);
            }
        }
    }

    return querySetsByPollInterval;
}

TQueries TIRDeviceQueryFactory::GenerateQueries(list<TPSet<PProtocolRegister>> && registerSets, EQueryOperation operation, EQueryGenerationPolicy policy, PSerialDevice device)
{
    assert(!registerSets.empty());
    assert(!registerSets.front().empty());

    /** gathering data **/
    if (!device) {
        device = (*registerSets.front().begin())->GetDevice();
    }

    assert(device);

    const auto & deviceConfig = device->DeviceConfig();
    const auto & protocolInfo = device->GetProtocolInfo();

    const bool singleBitType = protocolInfo.IsSingleBitType((*registerSets.front().begin())->Type);
    const bool isRead = IsReadOperation(operation);

    const auto & addQuery = isRead ? AddQuery<TIRDeviceQuery>
                                   : singleBitType ? AddQuery<TIRDeviceSingleBitQuery>
                                                   : AddQuery<TIRDevice64BitQuery>;

    const bool performMerge = (policy == Minify || policy == NoHoles),
               enableHoles  = (policy == Minify);

    const int maxHole = enableHoles ? (singleBitType ? deviceConfig->MaxBitHole
                                                     : deviceConfig->MaxRegHole)
                                    : 0;
    int maxRegs;

    if (isRead) {
        if (singleBitType) {
            maxRegs = protocolInfo.GetMaxReadBits();
        } else {
            if ((deviceConfig->MaxReadRegisters > 0) && (deviceConfig->MaxReadRegisters <= protocolInfo.GetMaxReadRegisters())) {
                maxRegs = deviceConfig->MaxReadRegisters;
            } else {
                maxRegs = protocolInfo.GetMaxReadRegisters();
            }
        }
    } else {
        maxRegs = singleBitType ? protocolInfo.GetMaxWriteBits() : protocolInfo.GetMaxWriteRegisters();
    }
    /** done gathering data **/

    if (performMerge) {
        MergeSets(registerSets, static_cast<uint32_t>(maxHole), static_cast<uint32_t>(maxRegs));
    } else {
        CheckSets(registerSets, static_cast<uint32_t>(maxHole), static_cast<uint32_t>(maxRegs));
    }

    TQueries result;

    for (auto & registerSet: registerSets) {
        addQuery(move(registerSet), result);
    }

    assert(!result.empty());

    return result;
}

void TIRDeviceQueryFactory::CheckSets(const std::list<TPSet<PProtocolRegister>> & registerSets, uint32_t maxHole, uint32_t maxRegs)
{
    if (Global::Debug)
        cerr << "checking sets" << endl;

    for (const auto & registerSet: registerSets) {
        auto hole = GetMaxHoleSize(registerSet);
        if (hole > maxHole) {
            throw TSerialDeviceException("unable to create queries for given register configuration: max hole count exceeded (detected: " +
                to_string(hole) +
                ", max: " + to_string(maxHole) +
                ", set: " + PrintCollection(registerSet, [](ostream & s, const PProtocolRegister & reg) {
                    s << reg->Address;
                })
            );
        }

        auto regCount = GetRegCount(registerSet);
        if (regCount > maxRegs) {
            throw TSerialDeviceException("unable to create queries for given register configuration: max reg count exceeded (detected: " +
                to_string(regCount) +
                ", max: " + to_string(maxRegs) +
                ", set: " + PrintCollection(registerSet, [](ostream & s, const PProtocolRegister & reg) {
                    s << reg->Address;
                })
            );
        }
    }

    if (Global::Debug)
        cerr << "checking sets done" << endl;
}

/**
 * Following algorihm:
 *  1) tries to reduce number of sets in passed list
 *  2) ensures that maxHole and maxRegs are not exceeded
 *  3) allows same register to appear in different sets if those sets couldn't merge (same register will be read more than once during same cycle)
 *  4) doesn't split initial sets (registers that were in one set will stay in one set)
 */
void TIRDeviceQueryFactory::MergeSets(list<TPSet<PProtocolRegister>> & registerSets, uint32_t maxHole, uint32_t maxRegs)
{
    CheckSets(registerSets, maxHole, maxRegs);

    if (Global::Debug)
        cerr << "merging sets" << endl;

    for (auto itRegisterSet = registerSets.begin(); itRegisterSet != registerSets.end(); ++itRegisterSet) {
        auto & registerSet = *itRegisterSet;

        auto itOtherRegisterSet = itRegisterSet;
        ++itOtherRegisterSet;

        for (;itOtherRegisterSet != registerSets.end();) {
            assert(itRegisterSet != itOtherRegisterSet);

            const auto & otherRegisterSet = *itOtherRegisterSet;

            auto first = **registerSet.begin() < **otherRegisterSet.begin() ? *registerSet.begin() : *otherRegisterSet.begin();
            auto last = **otherRegisterSet.rbegin() < **registerSet.rbegin() ? *registerSet.rbegin() : *otherRegisterSet.rbegin();

            auto holeSizeAfterMerge = GetMaxHoleSize(first, last);
            if (holeSizeAfterMerge > maxHole) {
                ++itOtherRegisterSet;
                continue;
            }

            auto regCountAfterMerge = last->Address - first->Address + 1;
            if (regCountAfterMerge > maxRegs) {
                ++itOtherRegisterSet;
                continue;
            }

            registerSet.insert(otherRegisterSet.begin(), otherRegisterSet.end());
            itOtherRegisterSet = registerSets.erase(itOtherRegisterSet);
        }
    }

    if (Global::Debug)
        cerr << "merging sets done" << endl;
}
