#include "expression_evaluator.h"
#include <experimental/optional>

using namespace std::experimental;
using namespace Expressions;

namespace
{
    bool IsOperator(TTokenType type)
    {
        return (type == TTokenType::Equal) || (type == TTokenType::NotEqual) || (type == TTokenType::Greater) ||
               (type == TTokenType::Less) || (type == TTokenType::GreaterEqual) || (type == TTokenType::LessEqual) ||
               (type == TTokenType::Or) || (type == TTokenType::And);
    }

    /**
     * @brief Get operator priority according to
     *        https://en.cppreference.com/w/cpp/language/operator_precedence
     *        Lower value, higher priority
     */
    size_t GetPriority(TTokenType type)
    {
        switch (type) {
            case TTokenType::Greater:
            case TTokenType::Less:
            case TTokenType::GreaterEqual:
            case TTokenType::LessEqual:
                return 9;
            case TTokenType::Equal:
            case TTokenType::NotEqual:
                return 10;
            case TTokenType::And:
                return 14;
            case TTokenType::Or:
                return 15;
            default:
                return 0;
        }
    }

    // Input token sequence must be valid. It must have odd length.
    // Odd tokens are values or expression trees, even values are operators.
    std::unique_ptr<TToken> TreeByPriority(std::vector<std::unique_ptr<TToken>>::iterator begin,
                                           std::vector<std::unique_ptr<TToken>>::iterator end)
    {
        if (begin + 1 == end) {
            return std::move(*begin);
        }
        auto rootIt = begin;
        for (auto it = begin; it != end; ++it) {
            if (!(*it)->GetLeft()) {
                if (GetPriority((*rootIt)->GetType()) <= GetPriority((*it)->GetType())) {
                    rootIt = it;
                }
            }
        }
        std::unique_ptr<TToken> root(std::move(*rootIt));
        root->SetLeft(TreeByPriority(begin, rootIt));
        root->SetRight(TreeByPriority(rootIt + 1, end));
        return root;
    }

    void ThrowParserError(const std::string& msg, const TLexer& lexer)
    {
        throw std::runtime_error(msg + std::to_string(lexer.GetLastTokenPosition()));
    }

    void ThrowMissingRightBracketError(const TLexer& lexer)
    {
        ThrowParserError("right bracket expected at position ", lexer);
    }

    void ThrowMissingOperatorError(const TLexer& lexer)
    {
        ThrowParserError("operator expected at position ", lexer);
    }

    void ThrowMissingIdentifierError(const TLexer& lexer)
    {
        ThrowParserError("identifier, number or left bracket are expected at position ", lexer);
    }

    optional<int32_t> EvalImpl(const TToken* expr, const Json::Value& params)
    {
        if (!expr) {
            throw std::runtime_error("undefined token");
        }
        switch (expr->GetType()) {
            case TTokenType::Number:
                return optional<int32_t>(atoi(expr->GetValue().c_str()));
            case TTokenType::Equal: {
                auto v1 = EvalImpl(expr->GetLeft(), params);
                if (!v1) {
                    return false;
                }
                auto v2 = EvalImpl(expr->GetRight(), params);
                return v2 ? v1 == v2 : false;
            }
            case TTokenType::NotEqual: {
                auto v1 = EvalImpl(expr->GetLeft(), params);
                if (!v1)
                    return true;
                auto v2 = EvalImpl(expr->GetRight(), params);
                return v2 ? v1 != v2 : true;
            }
            case TTokenType::Greater: {
                auto v1 = EvalImpl(expr->GetLeft(), params);
                if (!v1)
                    return false;
                auto v2 = EvalImpl(expr->GetRight(), params);
                return v2 ? v1 > v2 : false;
            }
            case TTokenType::Less: {
                auto v1 = EvalImpl(expr->GetLeft(), params);
                if (!v1)
                    return false;
                auto v2 = EvalImpl(expr->GetRight(), params);
                return v2 ? v1 < v2 : false;
            }
            case TTokenType::GreaterEqual: {
                auto v1 = EvalImpl(expr->GetLeft(), params);
                if (!v1)
                    return false;
                auto v2 = EvalImpl(expr->GetRight(), params);
                return v2 ? v1 >= v2 : false;
            }
            case TTokenType::LessEqual: {
                auto v1 = EvalImpl(expr->GetLeft(), params);
                if (!v1)
                    return false;
                auto v2 = EvalImpl(expr->GetRight(), params);
                return v2 ? v1 <= v2 : false;
            }
            case TTokenType::Or: {
                auto v1 = EvalImpl(expr->GetLeft(), params);
                if (!v1)
                    return false;
                auto v2 = EvalImpl(expr->GetRight(), params);
                return v2 ? v1.value() || v2.value() : false;
            }
            case TTokenType::And: {
                auto v1 = EvalImpl(expr->GetLeft(), params);
                if (!v1)
                    return false;
                auto v2 = EvalImpl(expr->GetRight(), params);
                return v2 ? v1.value() && v2.value() : false;
            }
            case TTokenType::Ident: {
                const auto& param = params[expr->GetValue()];
                if (param.isInt()) {
                    return optional<int32_t>(param.asInt());
                }
                return nullopt;
            }
            case TTokenType::LeftBr:
            case TTokenType::RightBr:
            case TTokenType::EOL:
                throw std::runtime_error("bad token value");
        }
        return nullopt;
    }

}

TToken::TToken(TTokenType type, const std::string& value): Type(type), Value(value)
{}

void TToken::SetLeft(std::unique_ptr<TToken> node)
{
    Left.swap(node);
}

void TToken::SetRight(std::unique_ptr<TToken> node)
{
    Right.swap(node);
}

const TToken* TToken::GetLeft() const
{
    return Left.get();
}

const TToken* TToken::GetRight() const
{
    return Right.get();
}

const std::string& TToken::GetValue() const
{
    return Value;
}

TTokenType TToken::GetType() const
{
    return Type;
}

void TLexer::ThrowUnexpected() const
{
    if (!SomethingToParse()) {
        throw std::runtime_error("unexpected end of line");
    }
    throw std::runtime_error(std::string("unexpected symbol '") + *Pos + "' at position " +
                             std::to_string(Pos - Start + 1));
}

bool TLexer::SomethingToParse() const
{
    return Pos != End;
}

bool TLexer::CompareChar(char c) const
{
    return SomethingToParse() && (*Pos == c);
}

TLexer::TLexer(const std::string& str)
{
    Start = str.begin();
    Pos = Start;
    TokenStart = Start;
    End = str.end();
}

std::unique_ptr<TToken> TLexer::GetOpOrBracket()
{
    switch (*Pos) {
        case '=': {
            ++Pos;
            if (CompareChar('=')) {
                ++Pos;
                return std::make_unique<TToken>(TTokenType::Equal);
            }
            ThrowUnexpected();
        }
        case '!': {
            ++Pos;
            if (CompareChar('=')) {
                ++Pos;
                return std::make_unique<TToken>(TTokenType::NotEqual);
            }
            ThrowUnexpected();
        }
        case '<': {
            ++Pos;
            if (CompareChar('=')) {
                ++Pos;
                return std::make_unique<TToken>(TTokenType::LessEqual);
            }
            return std::make_unique<TToken>(TTokenType::Less);
        }
        case '>': {
            ++Pos;
            if (CompareChar('=')) {
                ++Pos;
                return std::make_unique<TToken>(TTokenType::GreaterEqual);
            }
            return std::make_unique<TToken>(TTokenType::Greater);
        }
        case '|': {
            ++Pos;
            if (CompareChar('|')) {
                ++Pos;
                return std::make_unique<TToken>(TTokenType::Or);
            }
            ThrowUnexpected();
        }
        case '&': {
            ++Pos;
            if (CompareChar('&')) {
                ++Pos;
                return std::make_unique<TToken>(TTokenType::And);
            }
            ThrowUnexpected();
        }
        case '(': {
            ++Pos;
            return std::make_unique<TToken>(TTokenType::LeftBr);
        }
        case ')': {
            ++Pos;
            return std::make_unique<TToken>(TTokenType::RightBr);
        }
        default: {
            return nullptr;
        }
    }
}

std::unique_ptr<TToken> TLexer::GetNumber()
{
    std::string value;
    if (*Pos == '-') {
        ++Pos;
        value += '-';
    }
    if (SomethingToParse() && isdigit(*Pos)) {
        value += *Pos;
        ++Pos;
        for (; SomethingToParse() && isdigit(*Pos); ++Pos) {
            value += *Pos;
        }
        return std::make_unique<TToken>(TTokenType::Number, value);
    }
    if (!value.empty()) {
        ThrowUnexpected();
    }
    return nullptr;
}

std::unique_ptr<TToken> TLexer::GetIdent()
{
    if (isalpha(*Pos)) {
        std::string identifier;
        identifier += *Pos;
        ++Pos;
        for (; SomethingToParse() && (isalpha(*Pos) || isdigit(*Pos) || *Pos == '_'); ++Pos) {
            identifier += *Pos;
        }
        return std::make_unique<TToken>(TTokenType::Ident, identifier);
    }
    return nullptr;
}

std::unique_ptr<TToken> TLexer::GetNextToken()
{
    TokenStart = Pos;
    if (!SomethingToParse()) {
        return std::make_unique<TToken>(TTokenType::EOL);
    }
    auto token = GetOpOrBracket();
    if (token) {
        return token;
    }
    token = GetNumber();
    if (token) {
        return token;
    }
    token = GetIdent();
    if (token) {
        return token;
    }
    ThrowUnexpected();
    return nullptr;
}

size_t TLexer::GetLastTokenPosition() const
{
    return TokenStart - Start + 1;
}

std::unique_ptr<TToken> TParser::ParseRightOperand(TLexer& lexer)
{
    if (Token->GetType() == TTokenType::Number || Token->GetType() == TTokenType::Ident) {
        auto nextToken = lexer.GetNextToken();
        Token.swap(nextToken);
        return nextToken;
    }
    if (Token->GetType() == TTokenType::LeftBr) {
        Token = lexer.GetNextToken();
        auto res = ParseExpression(lexer);
        if (Token->GetType() != TTokenType::RightBr) {
            ThrowMissingRightBracketError(lexer);
        }
        Token = lexer.GetNextToken();
        return res;
    }
    ThrowMissingIdentifierError(lexer);
    return nullptr;
}

std::unique_ptr<TToken> TParser::ParseConditionWithBrackets(TLexer& lexer)
{
    if (Token->GetType() != TTokenType::LeftBr) {
        return nullptr;
    }
    std::vector<std::unique_ptr<TToken>> tokens;
    Token = lexer.GetNextToken();
    tokens.emplace_back(ParseExpression(lexer));
    if (Token->GetType() != TTokenType::RightBr) {
        ThrowMissingRightBracketError(lexer);
    }
    Token = lexer.GetNextToken();
    while (IsOperator(Token->GetType())) {
        tokens.emplace_back(std::move(Token));
        Token = lexer.GetNextToken();
        tokens.emplace_back(ParseRightOperand(lexer));
    }
    return TreeByPriority(tokens.begin(), tokens.end());
}

std::unique_ptr<TToken> TParser::ParseCondition(TLexer& lexer)
{
    if (Token->GetType() != TTokenType::Number && Token->GetType() != TTokenType::Ident) {
        return nullptr;
    }
    std::vector<std::unique_ptr<TToken>> tokens;
    tokens.emplace_back(std::move(Token));
    Token = lexer.GetNextToken();
    if (!IsOperator(Token->GetType())) {
        ThrowMissingOperatorError(lexer);
    }
    tokens.emplace_back(std::move(Token));
    Token = lexer.GetNextToken();
    tokens.emplace_back(ParseRightOperand(lexer));
    while (IsOperator(Token->GetType())) {
        tokens.emplace_back(std::move(Token));
        Token = lexer.GetNextToken();
        tokens.emplace_back(ParseRightOperand(lexer));
    }
    return TreeByPriority(tokens.begin(), tokens.end());
}

std::unique_ptr<TToken> TParser::ParseExpression(TLexer& lexer)
{
    auto root = ParseCondition(lexer);
    if (root) {
        return root;
    }
    root = ParseConditionWithBrackets(lexer);
    if (root) {
        return root;
    }
    ThrowMissingIdentifierError(lexer);
    return nullptr;
}

std::unique_ptr<TToken> TParser::Parse(const std::string& str)
{
    TLexer lexer(str);
    Token = lexer.GetNextToken();
    auto root = ParseExpression(lexer);
    if (Token->GetType() != TTokenType::EOL) {
        if (root) {
            ThrowMissingOperatorError(lexer);
        }
        ThrowParserError("unexpected symbol at position ", lexer);
    }
    return root;
}

bool Expressions::Eval(const Expressions::TToken* expr, const Json::Value& params)
{
    auto res = EvalImpl(expr, params);
    return res && res.value();
}
