#pragma once

#include "expression_evaluator.h"
#include "serial_config.h"

Json::Value MergeDeviceConfigWithTemplate(const Json::Value& deviceConfigJson,
                                          const std::string& deviceType,
                                          const Json::Value& deviceTemplate);

class TJsonParams: public Expressions::IParams
{
    const Json::Value& Params;

public:
    explicit TJsonParams(const Json::Value& params);

    std::optional<int32_t> Get(const std::string& name) const override;
};

bool CheckCondition(const std::string& cond, const TJsonParams& params, Expressions::TExpressionsCache* exprs);
bool CheckCondition(const Json::Value& item, const TJsonParams& params, Expressions::TExpressionsCache* exprs);
