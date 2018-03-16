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

    inline uint32_t GetType(const TPSet<PProtocolRegister> & registerSet)
    {
        return (*registerSet.begin())->Type;
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

vector<pair<TIntervalMs, PIRDeviceQuerySet>> TIRDeviceQueryFactory::GenerateQuerySets(const vector<PVirtualRegister> & virtualRegisters, EQueryOperation operation)
{
    vector<pair<TIntervalMs, PIRDeviceQuerySet>> querySetsByPollInterval;
    {
        map<int64_t, list<TPSet<PProtocolRegister>>> protocolRegistersByTypeAndInterval;
        vector<int64_t> pollIntervals;  // for order preservation

        for (const auto & virtualRegister: virtualRegisters) {
            int64_t pollInterval = virtualRegister->PollInterval.count();

            auto & registerSets = protocolRegistersByTypeAndInterval[pollInterval];

            if (registerSets.empty()) {
                pollIntervals.push_back(pollInterval);
            }

            registerSets.push_back(virtualRegister->GetProtocolRegisters());
        }

        for (auto & _pollInterval: pollIntervals) {
            auto pollInterval = TIntervalMs(_pollInterval);

            auto it = protocolRegistersByTypeAndInterval.find(_pollInterval);
            assert(it != protocolRegistersByTypeAndInterval.end());

            auto & registers = it->second;

            const auto & querySet = std::make_shared<TIRDeviceQuerySet>(std::move(registers), operation);
            querySetsByPollInterval.push_back({ pollInterval, querySet });
        }
    }

    return querySetsByPollInterval;
}

TQueries TIRDeviceQueryFactory::GenerateQueries(list<TPSet<PProtocolRegister>> && registerSets, EQueryOperation operation, EQueryGenerationPolicy policy)
{
    assert(!registerSets.empty());
    assert(!registerSets.front().empty());

    /** gathering data **/
    auto device = (*registerSets.front().begin())->GetDevice();

    assert(device);

    const auto & deviceConfig = device->DeviceConfig();
    const auto & protocolInfo = device->GetProtocolInfo();

    const bool isRead = IsReadOperation(operation);

    const bool performMerge = (policy == Minify || policy == NoHoles),
               enableHoles  = (policy == Minify);

    TRegisterTypeInfo getMaxHoleAndRegs = [&](uint32_t type) {
        const bool singleBitType = protocolInfo.IsSingleBitType(type);

        const int maxHole = enableHoles ? (singleBitType ? deviceConfig->MaxBitHole
                                                         : deviceConfig->MaxRegHole)
                                        : 0;
        int maxRegs;

        if (isRead) {
            const auto protocolMaximum = singleBitType ? protocolInfo.GetMaxReadBits() : protocolInfo.GetMaxReadRegisters();

            if (deviceConfig->MaxReadRegisters > 0) {
                maxRegs = min(deviceConfig->MaxReadRegisters, protocolMaximum);
            } else {
                maxRegs = protocolMaximum;
            }
        } else {
            maxRegs = singleBitType ? protocolInfo.GetMaxWriteBits() : protocolInfo.GetMaxWriteRegisters();
        }

        return pair<uint32_t, uint32_t>{ maxHole, maxRegs };
    };

    auto addQuery = [&](const TPSet<PProtocolRegister> & registerSet, TQueries & result) {
        const bool singleBitType = protocolInfo.IsSingleBitType(GetType(registerSet));

        const auto & chosenAddQuery = isRead ? AddQuery<TIRDeviceQuery>
                                             : singleBitType ? AddQuery<TIRDeviceSingleBitQuery>
                                                             : AddQuery<TIRDevice64BitQuery>;

        return chosenAddQuery(registerSet, result);
    };

    /** done gathering data **/

    if (performMerge) {
        MergeSets(registerSets, getMaxHoleAndRegs);
    } else {
        CheckSets(registerSets, getMaxHoleAndRegs);
    }

    TQueries result;

    for (auto & registerSet: registerSets) {
        addQuery(registerSet, result);
    }

    assert(!result.empty());

    return result;
}

void TIRDeviceQueryFactory::CheckSets(const std::list<TPSet<PProtocolRegister>> & registerSets, const TRegisterTypeInfo & typeInfo)
{
    if (Global::Debug)
        cerr << "checking sets" << endl;

    for (const auto & registerSet: registerSets) {
        uint32_t maxHole;
        uint32_t maxRegs;

        tie(maxHole, maxRegs) = typeInfo(GetType(registerSet));

        auto hole = GetMaxHoleSize(registerSet);
        if (hole > maxHole) {
            throw TSerialDeviceException("unable to create queries for given register configuration: max hole count exceeded (detected: " +
                to_string(hole) +
                ", max: " + to_string(maxHole) +
                ", set: " + PrintCollection(registerSet, [](ostream & s, const PProtocolRegister & reg) {
                    s << reg->Address;
                })  + ")"
            );
        }

        auto regCount = GetRegCount(registerSet);
        if (regCount > maxRegs) {
            throw TSerialDeviceException("unable to create queries for given register configuration: max reg count exceeded (detected: " +
                to_string(regCount) +
                ", max: " + to_string(maxRegs) +
                ", set: " + PrintCollection(registerSet, [](ostream & s, const PProtocolRegister & reg) {
                    s << reg->Address;
                })  + ")"
            );
        }

        {   // check types
            auto type = (*registerSet.begin())->Type;
            for (const auto & reg: registerSet) {
                if (reg->Type != type) {
                    throw TSerialDeviceException("unable to create queries for given register configuration: different register types in same set (set: "
                        + PrintCollection(registerSet, [](ostream & s, const PProtocolRegister & reg) {
                            s << reg->Address << " (type: " << reg->GetTypeName() << ")";
                        }) + ")"
                    );
                }
            }
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
void TIRDeviceQueryFactory::MergeSets(list<TPSet<PProtocolRegister>> & registerSets, const TRegisterTypeInfo & typeInfo)
{
    CheckSets(registerSets, typeInfo);

    if (Global::Debug)
        cerr << "merging sets" << endl;

    for (auto itRegisterSet = registerSets.begin(); itRegisterSet != registerSets.end(); ++itRegisterSet) {
        auto & registerSet = *itRegisterSet;

        uint32_t maxHole;
        uint32_t maxRegs;

        tie(maxHole, maxRegs) = typeInfo(GetType(registerSet));

        auto itOtherRegisterSet = itRegisterSet;
        ++itOtherRegisterSet;

        for (;itOtherRegisterSet != registerSets.end();) {
            assert(itRegisterSet != itOtherRegisterSet);

            const auto & otherRegisterSet = *itOtherRegisterSet;

            if (GetType(registerSet) != GetType(otherRegisterSet)) {
                ++itOtherRegisterSet;
                continue;
            }

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
