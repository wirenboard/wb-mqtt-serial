#include <wblib/testing/testlog.h>

#include "expression_evaluator.h"

#include <fstream>

using namespace std;
using namespace WBMQTT;
using namespace WBMQTT::Testing;
using namespace Expressions;

class TExpressionsTest: public TLoggedFixture
{
protected:
    void PrintOp(const TToken* expr, const std::string& op, const std::string& prefix)
    {
        Print(expr->GetLeft(), prefix + "  ");
        Emit() << prefix << op;
        Print(expr->GetRight(), prefix + "  ");
    }

    void Print(const TToken* expr, const std::string& prefix = std::string())
    {
        if (!expr) {
            Emit() << prefix << "undefined token";
        }
        switch (expr->GetType()) {
            case TTokenType::Number: {
                Emit() << prefix << expr->GetValue();
                break;
            }
            case TTokenType::Equal: {
                PrintOp(expr, "==", prefix + "  ");
                break;
            }
            case TTokenType::NotEqual: {
                PrintOp(expr, "!=", prefix + "  ");
                break;
            }
            case TTokenType::Greater: {
                PrintOp(expr, ">", prefix + "  ");
                break;
            }
            case TTokenType::Less: {
                PrintOp(expr, "<", prefix + "  ");
                break;
            }
            case TTokenType::GreaterEqual: {
                PrintOp(expr, ">=", prefix + "  ");
                break;
            }
            case TTokenType::LessEqual: {
                PrintOp(expr, "<=", prefix + "  ");
                break;
            }
            case TTokenType::Or: {
                PrintOp(expr, "||", prefix + "  ");
                break;
            }
            case TTokenType::And: {
                PrintOp(expr, "&&", prefix + "  ");
                break;
            }
            case TTokenType::Ident: {
                Emit() << prefix << expr->GetValue();
                break;
            }
            case TTokenType::LeftBr: {
                Emit() << prefix << expr->GetValue();
                break;
            }
            case TTokenType::RightBr: {
                Emit() << prefix << expr->GetValue();
                break;
            }
            case TTokenType::EOL: {
                Emit() << "EOL";
                break;
            }
            default: {
                throw std::runtime_error("unknown token");
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
        Print(parser.Parse(buf).get());
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
        } catch (const std::exception& e) {
            Emit() << e.what();
        }
        Emit() << "\n";
    }
}

TEST_F(TExpressionsTest, Eval)
{
    Json::Value params;
    params["a"] = 1;
    params["b"] = 2;
    std::vector<std::string> trueExpressions =
        {"a==1", "a!=3", "a<2", "a>-1", "a>=1", "a<=1", "a==1||b==10", "a==1&&b==2", "a==1||c==2"};
    std::vector<std::string> falseExpressions =
        {"a!=1", "a==3", "a>1", "a<-1", "a>=2", "a<=0", "a==2||b==10", "a==2&&b==2", "a==1&&c==2"};

    TParser parser;
    for (const auto& expr: trueExpressions) {
        ASSERT_TRUE(Eval(parser.Parse(expr).get(), params)) << expr;
    }
    for (const auto& expr: falseExpressions) {
        ASSERT_FALSE(Eval(parser.Parse(expr).get(), params)) << expr;
    }
}
