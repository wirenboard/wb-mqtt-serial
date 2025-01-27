#pragma once

#include <memory>
#include <optional>
#include <string>
#include <vector>

// EBNF:
//
// digit  = "0" | "1" | "2" | "3" | "4" | "5" | "6" | "7" | "8" | "9" ;
// operator = "==" | "!=" | ">" | "<" | ">=" | "<=" | "||" | "&&";
// letter = "A" | "B" | "C" | "D" | "E" | "F" | "G"
//        | "H" | "I" | "J" | "K" | "L" | "M" | "N"
//        | "O" | "P" | "Q" | "R" | "S" | "T" | "U"
//        | "V" | "W" | "X" | "Y" | "Z" | "a" | "b"
//        | "c" | "d" | "e" | "f" | "g" | "h" | "i"
//        | "j" | "k" | "l" | "m" | "n" | "o" | "p"
//        | "q" | "r" | "s" | "t" | "u" | "v" | "w"
//        | "x" | "y" | "z" ;
// number = [ "-" ], digit, { digit } ;
// identifier = letter , { letter | digit | "_" } ;
// function = identifier, "(", identifier, ")" ;
// operand = identifier | number | function | condition with brackets ;
// condition = operand , { operator, operand } ;
// condition with brackets = "(", expression, ")" ;
// expression = condition | condition with brackets ;

namespace Expressions
{
    enum class TTokenType
    {
        LeftBr,       // (
        RightBr,      // )
        Number,       // integer number
        Equal,        // ==
        NotEqual,     // !=
        Greater,      // >
        Less,         // <
        GreaterEqual, // >=
        LessEqual,    // <=
        Or,           // ||
        And,          // &&
        Ident,        // identifier
        EOL           // end of line
    };

    struct TToken
    {
        TTokenType Type;
        std::string Value;
        size_t Pos;

        TToken(TTokenType type, size_t pos, const std::string& value = std::string());
    };

    enum class TAstNodeType
    {
        Number,       // integer number
        Equal,        // ==
        NotEqual,     // !=
        Greater,      // >
        Less,         // <
        GreaterEqual, // >=
        LessEqual,    // <=
        Or,           // ||
        And,          // &&
        Ident,        // identifier
        Func          // function
    };

    //! AST tree node
    class TAstNode
    {
        std::unique_ptr<TAstNode> Left;
        std::unique_ptr<TAstNode> Right;
        TAstNodeType Type;
        std::string Value;

    public:
        TAstNode(const TToken& token);
        TAstNode(const TAstNodeType& type, const std::string& value);

        void SetLeft(std::unique_ptr<TAstNode> node);
        void SetRight(std::unique_ptr<TAstNode> node);

        const TAstNode* GetLeft() const;
        const TAstNode* GetRight() const;
        const std::string& GetValue() const;
        TAstNodeType GetType() const;
    };

    class TLexer
    {
        std::string::const_iterator Start;
        std::string::const_iterator TokenStart;
        std::string::const_iterator Pos;
        std::string::const_iterator End;

        void ThrowUnexpected() const;
        bool SomethingToParse() const;
        bool CompareChar(char c) const;

        std::optional<TToken> GetOpOrBracket();
        std::optional<TToken> GetNumber();
        std::optional<TToken> GetIdent();

    public:
        /**
         * @brief Read next token from string.
         *        Throw std::runtime_error on parsing error.
         */
        std::vector<TToken> GetTokens(const std::string& str);
    };

    class TParser
    {
        //! A token to analyze
        std::vector<TToken>::const_iterator Token;

        std::unique_ptr<TAstNode> ParseOperand();
        std::unique_ptr<TAstNode> ParseCondition();
        std::unique_ptr<TAstNode> ParseConditionWithBrackets();
        std::unique_ptr<TAstNode> ParseExpression();
        std::unique_ptr<TAstNode> ParseFunction();

    public:
        /**
         * @brief Parse expression and build AST for expression evaluation.
         *        Throw std::runtime_error on parsing error.
         *
         * @param str string containing expression to parse
         * @return resulting AST
         * @throw std::runtime_error on parsing error
         */
        std::unique_ptr<TAstNode> Parse(const std::string& str);
    };

    class IParams
    {
    public:
        virtual ~IParams() = default;

        virtual std::optional<int32_t> Get(const std::string& name) const = 0;
    };

    /**
     * @brief Evaluate expression.
     *        Throw std::runtime_error on evaluation error.
     *
     * @param expression AST of an expression to evaluate
     * @param params Parameters provider
     * @return result of expression evaluation
     */
    bool Eval(const TAstNode* expression, const IParams& params);
}
