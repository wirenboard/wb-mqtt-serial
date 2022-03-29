#pragma once
#include <wblib/json/json.h>

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
// right operand = identifier | number | "(", expression, ")" ;
// condition = identifier | number , operator, right operand, { operator, right operand } ;
// condition with brackets = "(", expression, ")" , [ { operator, right operand } ] ;
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

    //! AST tree node
    class TToken
    {
        std::unique_ptr<TToken> Left;
        std::unique_ptr<TToken> Right;
        TTokenType Type;
        std::string Value;

    public:
        TToken(TTokenType type, const std::string& value = std::string());

        void SetLeft(std::unique_ptr<TToken> node);
        void SetRight(std::unique_ptr<TToken> node);

        const TToken* GetLeft() const;
        const TToken* GetRight() const;
        const std::string& GetValue() const;
        TTokenType GetType() const;
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

        std::unique_ptr<TToken> GetOpOrBracket();
        std::unique_ptr<TToken> GetNumber();
        std::unique_ptr<TToken> GetIdent();

    public:
        TLexer(const std::string& str);

        /**
         * @brief Read next token from string.
         *        Throw std::runtime_error on parsing error.
         */
        std::unique_ptr<TToken> GetNextToken();

        //! Get the last read token position in chars starting from 1
        size_t GetLastTokenPosition() const;
    };

    class TParser
    {
        std::unique_ptr<TToken> Token;

        std::unique_ptr<TToken> ParseRightOperand(TLexer& lexer);
        std::unique_ptr<TToken> ParseCondition(TLexer& lexer);
        std::unique_ptr<TToken> ParseConditionWithBrackets(TLexer& lexer);
        std::unique_ptr<TToken> ParseExpression(TLexer& lexer);

    public:
        /**
         * @brief Parse expression and build AST for expression evaluation.
         *        Throw std::runtime_error on parsing error.
         *
         * @param str string containing expression to parse
         * @return std::unique_ptr<TToken> resulting AST
         */
        std::unique_ptr<TToken> Parse(const std::string& str);
    };

    /**
     * @brief Evaluate expression.
     *        Throw std::runtime_error on evaluation error.
     *
     * @param expression AST of an expression to evaluate
     * @param params JSON with parameters. Values must be integers.
     * @return result of espression evaluation
     */
    bool Eval(const TToken* expression, const Json::Value& params);
}
