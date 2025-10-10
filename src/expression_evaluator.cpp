#include "expression_evaluator.h"

#include <set>
#include <stdexcept>
#include <unordered_map>
#include <vector>

using namespace std;
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
    size_t GetPriority(TAstNodeType type)
    {
        switch (type) {
            case TAstNodeType::Greater:
            case TAstNodeType::Less:
            case TAstNodeType::GreaterEqual:
            case TAstNodeType::LessEqual:
                return 9;
            case TAstNodeType::Equal:
            case TAstNodeType::NotEqual:
                return 10;
            case TAstNodeType::And:
                return 14;
            case TAstNodeType::Or:
                return 15;
            default:
                return 0;
        }
    }

    // Input ast node sequence must be valid. It must have odd length.
    // Odd nodes are numbers, identifiers, functions or expression trees, even values are operators.
    std::unique_ptr<TAstNode> TreeByPriority(std::vector<std::unique_ptr<TAstNode>>::iterator begin,
                                             std::vector<std::unique_ptr<TAstNode>>::iterator end)
    {
        if (begin + 1 == end) {
            return std::move(*begin);
        }
        // Iterate over even nodes (operators)
        auto rootIt = begin + 1;
        for (auto it = rootIt; it != end && it + 1 != end; it += 2) {
            if (GetPriority((*rootIt)->GetType()) <= GetPriority((*it)->GetType())) {
                rootIt = it;
            }
        }
        std::unique_ptr<TAstNode> root(std::move(*rootIt));
        root->SetLeft(TreeByPriority(begin, rootIt));
        root->SetRight(TreeByPriority(rootIt + 1, end));
        return root;
    }

    void ThrowParserError(const std::string& msg, size_t pos)
    {
        throw std::runtime_error(msg + std::to_string(pos + 1));
    }

    void ThrowMissingRightBracketError(size_t pos)
    {
        ThrowParserError("right bracket expected at position ", pos);
    }

    void ThrowMissingOperatorError(size_t pos)
    {
        ThrowParserError("operator expected at position ", pos);
    }

    void ThrowMissingIdentifierError(size_t pos)
    {
        ThrowParserError("identifier expected at position ", pos);
    }

    void ThrowMissingIdentifierOrNumberOrLeftBracketError(size_t pos)
    {
        ThrowParserError("identifier, number or left bracket are expected at position ", pos);
    }

    void ThrowUnknownFunctionError(const std::string& name, size_t pos)
    {
        ThrowParserError("unknown function " + name + " at position ", pos);
    }

    optional<int32_t> EvalFunction(const TAstNode* expr, const IParams& params)
    {
        return params.Get(expr->GetRight()->GetValue()).has_value();
    }

    optional<int32_t> EvalImpl(const TAstNode* expr, const IParams& params)
    {
        if (!expr) {
            throw std::runtime_error("undefined token");
        }
        switch (expr->GetType()) {
            case TAstNodeType::Number:
                return optional<int32_t>(atoi(expr->GetValue().c_str()));
            case TAstNodeType::Equal: {
                auto v1 = EvalImpl(expr->GetLeft(), params);
                if (!v1) {
                    return false;
                }
                auto v2 = EvalImpl(expr->GetRight(), params);
                return v2 ? v1 == v2 : false;
            }
            case TAstNodeType::NotEqual: {
                auto v1 = EvalImpl(expr->GetLeft(), params);
                if (!v1)
                    return true;
                auto v2 = EvalImpl(expr->GetRight(), params);
                return v2 ? v1 != v2 : true;
            }
            case TAstNodeType::Greater: {
                auto v1 = EvalImpl(expr->GetLeft(), params);
                if (!v1)
                    return false;
                auto v2 = EvalImpl(expr->GetRight(), params);
                return v2 ? v1 > v2 : false;
            }
            case TAstNodeType::Less: {
                auto v1 = EvalImpl(expr->GetLeft(), params);
                if (!v1)
                    return false;
                auto v2 = EvalImpl(expr->GetRight(), params);
                return v2 ? v1 < v2 : false;
            }
            case TAstNodeType::GreaterEqual: {
                auto v1 = EvalImpl(expr->GetLeft(), params);
                if (!v1)
                    return false;
                auto v2 = EvalImpl(expr->GetRight(), params);
                return v2 ? v1 >= v2 : false;
            }
            case TAstNodeType::LessEqual: {
                auto v1 = EvalImpl(expr->GetLeft(), params);
                if (!v1)
                    return false;
                auto v2 = EvalImpl(expr->GetRight(), params);
                return v2 ? v1 <= v2 : false;
            }
            case TAstNodeType::Or: {
                auto v1 = EvalImpl(expr->GetLeft(), params);
                if (!v1)
                    return false;
                auto v2 = EvalImpl(expr->GetRight(), params);
                return v2 ? v1.value() || v2.value() : false;
            }
            case TAstNodeType::And: {
                auto v1 = EvalImpl(expr->GetLeft(), params);
                if (!v1)
                    return false;
                auto v2 = EvalImpl(expr->GetRight(), params);
                return v2 ? v1.value() && v2.value() : false;
            }
            case TAstNodeType::Ident: {
                return params.Get(expr->GetValue());
            }
            case TAstNodeType::Func: {
                return EvalFunction(expr, params);
            }
        }
        return nullopt;
    }

}

TToken::TToken(TTokenType type, size_t pos, const std::string& value): Type(type), Value(value), Pos(pos)
{}

TAstNode::TAstNode(const TToken& token)
{
    const std::unordered_map<TTokenType, TAstNodeType> types = {{TTokenType::Number, TAstNodeType::Number},
                                                                {TTokenType::Ident, TAstNodeType::Ident},
                                                                {TTokenType::Equal, TAstNodeType::Equal},
                                                                {TTokenType::NotEqual, TAstNodeType::NotEqual},
                                                                {TTokenType::Greater, TAstNodeType::Greater},
                                                                {TTokenType::Less, TAstNodeType::Less},
                                                                {TTokenType::GreaterEqual, TAstNodeType::GreaterEqual},
                                                                {TTokenType::LessEqual, TAstNodeType::LessEqual},
                                                                {TTokenType::Or, TAstNodeType::Or},
                                                                {TTokenType::And, TAstNodeType::And}};
    auto it = types.find(token.Type);
    if (it != types.end()) {
        Type = it->second;
    } else {
        throw std::runtime_error("Can't make AST node");
    }
    Value = token.Value;
}

TAstNode::TAstNode(const TAstNodeType& type, const std::string& value): Type(type), Value(value)
{}

void TAstNode::SetLeft(std::unique_ptr<TAstNode> node)
{
    Left.swap(node);
}

void TAstNode::SetRight(std::unique_ptr<TAstNode> node)
{
    Right.swap(node);
}

const TAstNode* TAstNode::GetLeft() const
{
    return Left.get();
}

const TAstNode* TAstNode::GetRight() const
{
    return Right.get();
}

const std::string& TAstNode::GetValue() const
{
    return Value;
}

TAstNodeType TAstNode::GetType() const
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

std::optional<TToken> TLexer::GetOpOrBracket()
{
    auto tokenStart = Pos - Start;
    switch (*Pos) {
        case '=': {
            ++Pos;
            if (CompareChar('=')) {
                ++Pos;
                return TToken(TTokenType::Equal, tokenStart);
            }
            ThrowUnexpected();
        }
        case '!': {
            ++Pos;
            if (CompareChar('=')) {
                ++Pos;
                return TToken(TTokenType::NotEqual, tokenStart);
            }
            ThrowUnexpected();
        }
        case '<': {
            ++Pos;
            if (CompareChar('=')) {
                ++Pos;
                return TToken(TTokenType::LessEqual, tokenStart);
            }
            return TToken(TTokenType::Less, tokenStart);
        }
        case '>': {
            ++Pos;
            if (CompareChar('=')) {
                ++Pos;
                return TToken(TTokenType::GreaterEqual, tokenStart);
            }
            return TToken(TTokenType::Greater, tokenStart);
        }
        case '|': {
            ++Pos;
            if (CompareChar('|')) {
                ++Pos;
                return TToken(TTokenType::Or, tokenStart);
            }
            ThrowUnexpected();
        }
        case '&': {
            ++Pos;
            if (CompareChar('&')) {
                ++Pos;
                return TToken(TTokenType::And, tokenStart);
            }
            ThrowUnexpected();
        }
        case '(': {
            ++Pos;
            return TToken(TTokenType::LeftBr, tokenStart);
        }
        case ')': {
            ++Pos;
            return TToken(TTokenType::RightBr, tokenStart);
        }
    }
    return nullopt;
}

std::optional<TToken> TLexer::GetNumber()
{
    std::string value;
    auto tokenStart = Pos - Start;
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
        return TToken(TTokenType::Number, tokenStart, value);
    }
    if (!value.empty()) {
        ThrowUnexpected();
    }
    return nullopt;
}

std::optional<TToken> TLexer::GetIdent()
{
    if (isalpha(*Pos)) {
        auto tokenStart = Pos - Start;
        std::string identifier;
        identifier += *Pos;
        ++Pos;
        for (; SomethingToParse() && (isalpha(*Pos) || isdigit(*Pos) || *Pos == '_'); ++Pos) {
            identifier += *Pos;
        }
        return TToken(TTokenType::Ident, tokenStart, identifier);
    }
    return nullopt;
}

std::vector<TToken> TLexer::GetTokens(const std::string& str)
{
    Start = str.begin();
    Pos = Start;
    End = str.end();

    std::vector<TToken> tokens;
    while (SomethingToParse()) {
        auto token = GetOpOrBracket();
        if (token) {
            tokens.emplace_back(*token);
            continue;
        }
        token = GetNumber();
        if (token) {
            tokens.emplace_back(*token);
            continue;
        }
        token = GetIdent();
        if (token) {
            tokens.emplace_back(*token);
        } else {
            ThrowUnexpected();
        }
    }
    tokens.emplace_back(TToken(TTokenType::EOL, Pos - Start));
    return tokens;
}

std::unique_ptr<TAstNode> TParser::ParseFunction()
{
    if (Token->Type != TTokenType::Ident) {
        return nullptr;
    }
    auto nextToken = Token + 1;
    if (nextToken->Type != TTokenType::LeftBr) {
        return nullptr;
    }
    ++nextToken;
    if (nextToken->Type != TTokenType::Ident) {
        ThrowMissingIdentifierError(nextToken->Pos);
    }
    if (Token->Value != "isDefined") {
        ThrowUnknownFunctionError(Token->Value, Token->Pos);
    }
    auto node = std::make_unique<TAstNode>(TAstNodeType::Func, Token->Value);
    node->SetRight(std::make_unique<TAstNode>(*nextToken));
    ++nextToken;
    if (nextToken->Type != TTokenType::RightBr) {
        ThrowMissingRightBracketError(nextToken->Pos);
    }
    Token = ++nextToken;
    return node;
}

std::unique_ptr<TAstNode> TParser::ParseOperand()
{
    auto fn = ParseFunction();
    if (fn) {
        return fn;
    }
    if (Token->Type == TTokenType::Number || Token->Type == TTokenType::Ident) {
        auto node = std::make_unique<TAstNode>(*Token);
        ++Token;
        return node;
    }
    auto node = ParseConditionWithBrackets();
    if (node) {
        return node;
    }
    ThrowMissingIdentifierOrNumberOrLeftBracketError(Token->Pos);
    return nullptr;
}

std::unique_ptr<TAstNode> TParser::ParseConditionWithBrackets()
{
    if (Token->Type != TTokenType::LeftBr) {
        return nullptr;
    }
    std::vector<std::unique_ptr<TAstNode>> tokens;
    ++Token;
    auto res = ParseExpression();
    if (Token->Type != TTokenType::RightBr) {
        ThrowMissingRightBracketError(Token->Pos);
    }
    ++Token;
    return res;
}

std::unique_ptr<TAstNode> TParser::ParseCondition()
{
    std::vector<std::unique_ptr<TAstNode>> tokens;
    tokens.emplace_back(ParseOperand());
    while (IsOperator(Token->Type)) {
        tokens.emplace_back(std::make_unique<TAstNode>(*Token));
        ++Token;
        tokens.emplace_back(ParseOperand());
    }
    return TreeByPriority(tokens.begin(), tokens.end());
}

std::unique_ptr<TAstNode> TParser::ParseExpression()
{
    auto root = ParseCondition();
    if (root) {
        return root;
    }
    root = ParseConditionWithBrackets();
    if (root) {
        return root;
    }
    ThrowMissingIdentifierOrNumberOrLeftBracketError(Token->Pos);
    return nullptr;
}

std::unique_ptr<TAstNode> TParser::Parse(const std::string& str)
{
    TLexer lexer;
    auto tokens = lexer.GetTokens(str);
    Token = tokens.cbegin();
    auto root = ParseExpression();
    if (Token->Type != TTokenType::EOL) {
        if (root) {
            ThrowMissingOperatorError(Token->Pos);
        }
        ThrowParserError("unexpected symbol at position ", Token->Pos);
    }
    return root;
}

bool Expressions::Eval(const Expressions::TAstNode* expr, const IParams& params)
{
    auto res = EvalImpl(expr, params);
    return res && res.value();
}

std::vector<std::string> Expressions::GetDependencies(const TAstNode* expression)
{
    std::set<std::string> dependencies;
    if (expression) {
        if (expression->GetType() == TAstNodeType::Ident) {
            dependencies.insert(expression->GetValue());
        } else {
            auto leftDeps = GetDependencies(expression->GetLeft());
            dependencies.insert(leftDeps.begin(), leftDeps.end());
            auto rightDeps = GetDependencies(expression->GetRight());
            dependencies.insert(rightDeps.begin(), rightDeps.end());
        }
    }
    return std::vector<std::string>(dependencies.begin(), dependencies.end());
}
