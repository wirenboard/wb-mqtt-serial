#pragma once

#include <set>
#include <unordered_map>

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

//! Parameter declarations selected by their conditions: the conditions are already evaluated
//! and are true for the current parameter values. Several selected declarations of one parameter
//! are allowed only as a chain of fw variants: the same condition and pairwise different "fw".
//! The declarations must outlive the object
class TActiveParameterDeclarations
{
    struct TChain
    {
        std::string Condition;
        std::set<std::string> FwVersions;
        const Json::Value* Newest;
    };
    std::unordered_map<std::string, TChain> Chains;

public:
    //! Adds a declaration of the parameter with a true condition. Returns false if it does not form
    //! a chain of fw variants with the already added declarations of the parameter, a template error
    bool Add(const std::string& id, const Json::Value& declaration);

    //! The added declaration of the parameter with the highest "fw", nullptr if nothing was added
    const Json::Value* GetNewest(const std::string& id) const;
};
