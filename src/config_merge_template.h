#pragma once

#include "expression_evaluator.h"
#include "serial_config.h"

Json::Value MergeDeviceConfigWithTemplate(const Json::Value& deviceData,
                                          const std::string& deviceType,
                                          const Json::Value& deviceTemplate);

typedef std::unordered_map<std::string, std::unique_ptr<Expressions::TToken>> TExpressionsCache;

class TJsonParams: public Expressions::IParams
{
    const Json::Value& Params;

public:
    explicit TJsonParams(const Json::Value& params);

    std::optional<int32_t> Get(const std::string& name) const override;
};

bool CheckCondition(const Json::Value& item, const TJsonParams& params, TExpressionsCache* exprs);
