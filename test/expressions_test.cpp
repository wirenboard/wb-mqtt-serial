#include <wblib/testing/testlog.h>

#include "expression_evaluator.h"

#include <fstream>

using namespace std;
using namespace WBMQTT;
using namespace WBMQTT::Testing;
using namespace Expressions;

namespace
{

    class TParams: public Expressions::IParams
    {
    public:
        std::optional<int32_t> Get(const std::string& name) const override
        {
            if (name == "a") {
                return 1;
            }
            if (name == "b") {
                return 2;
            }
            return std::nullopt;
        }
    };
}
class TExpressionsTest: public TLoggedFixture
{
protected:
    void PrintOp(const TAstNode* expr, const std::string& op, const std::string& prefix)
    {
        Print(expr->GetLeft(), prefix + "  ");
        Emit() << prefix << op;
        Print(expr->GetRight(), prefix + "  ");
    }

    void Print(const TAstNode* expr, const std::string& prefix = std::string())
    {
        if (!expr) {
            Emit() << prefix << "undefined token";
            return;
        }
        switch (expr->GetType()) {
            case TAstNodeType::Number: {
                Emit() << prefix << expr->GetValue();
                break;
            }
            case TAstNodeType::Equal: {
                PrintOp(expr, "==", prefix + "  ");
                break;
            }
            case TAstNodeType::NotEqual: {
                PrintOp(expr, "!=", prefix + "  ");
                break;
            }
            case TAstNodeType::Greater: {
                PrintOp(expr, ">", prefix + "  ");
                break;
            }
            case TAstNodeType::Less: {
                PrintOp(expr, "<", prefix + "  ");
                break;
            }
            case TAstNodeType::GreaterEqual: {
                PrintOp(expr, ">=", prefix + "  ");
                break;
            }
            case TAstNodeType::LessEqual: {
                PrintOp(expr, "<=", prefix + "  ");
                break;
            }
            case TAstNodeType::Or: {
                PrintOp(expr, "||", prefix + "  ");
                break;
            }
            case TAstNodeType::And: {
                PrintOp(expr, "&&", prefix + "  ");
                break;
            }
            case TAstNodeType::Ident: {
                Emit() << prefix << expr->GetValue();
                break;
            }
            case TAstNodeType::Func: {
                Emit() << prefix << expr->GetValue() << "(" << expr->GetRight()->GetValue() << ")";
                break;
            }
        }
    }
};

TEST_F(TExpressionsTest, Ast)
{
    std::ifstream f(GetDataFilePath("expressions/good.txt"));
    std::string buf;
    TParser parser;
    while (std::getline(f, buf)) {
        Emit() << buf;
        std::unique_ptr<TAstNode> res;
        ASSERT_NO_THROW(res = parser.Parse(buf)) << buf;
        Print(res.get());
        Emit() << "\n";
    }
}

TEST_F(TExpressionsTest, ParsingError)
{
    std::ifstream f(GetDataFilePath("expressions/bad.txt"));
    std::string buf;
    TParser parser;
    while (std::getline(f, buf)) {
        Emit() << "\"" << buf << "\"";
        try {
            Print(parser.Parse(buf).get());
            FAIL() << buf << " must throw parsing error";
        } catch (const std::exception& e) {
            Emit() << e.what();
        }
        Emit() << "\n";
    }
}

TEST_F(TExpressionsTest, Eval)
{
    std::vector<std::string> trueExpressions = {"a==1",
                                                "a!=3",
                                                "a<2",
                                                "a>-1",
                                                "a>=1",
                                                "a<=1",
                                                "a==1||b==10",
                                                "a==1&&b==2",
                                                "a==1||c==2",
                                                "isDefined(a)",
                                                "a==1&&isDefined(a)"};
    std::vector<std::string> falseExpressions = {"a!=1",
                                                 "a==3",
                                                 "a>1",
                                                 "a<-1",
                                                 "a>=2",
                                                 "a<=0",
                                                 "a==2||b==10",
                                                 "a==2&&b==2",
                                                 "a==1&&c==2",
                                                 "isDefined(c)",
                                                 "a==1&&isDefined(c)"};

    TParams params;
    TParser parser;
    for (const auto& expr: trueExpressions) {
        bool res = false;
        ASSERT_NO_THROW(res = Eval(parser.Parse(expr).get(), params)) << expr;
        ASSERT_TRUE(res) << expr;
    }
    for (const auto& expr: falseExpressions) {
        bool res = true;
        ASSERT_NO_THROW(res = Eval(parser.Parse(expr).get(), params)) << expr;
        ASSERT_FALSE(res) << expr;
    }
}
