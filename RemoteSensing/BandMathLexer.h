/**
 * GeoDa TM, Copyright (C) 2011-2015 by Luc Anselin - all rights reserved
 *
 * This file is part of GeoDa.
 *
 * GeoDa is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 *
 * GeoDa is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program.  If not, see <http://www.gnu.org/licenses/>.
 */

#ifndef __GEODA_CENTER_BAND_MATH_LEXER_H__
#define __GEODA_CENTER_BAND_MATH_LEXER_H__

#include <string>
#include <vector>
#include <exception>

/** 波段运算 Token 类型 */
namespace BandMath {
enum TokenEnum {
    TOK_END    = 0,  /**< 输入结束 */
    TOK_NUMBER,      /**< 数字常量 */
    TOK_BAND,        /**< 波段引用 B1, B2, ... */
    TOK_PLUS,        /**< + */
    TOK_MINUS,       /**< - */
    TOK_MUL,         /**< * */
    TOK_DIV,         /**< / */
    TOK_POW,         /**< ^ */
    TOK_LPAREN,      /**< ( */
    TOK_RPAREN,      /**< ) */
    TOK_COMMA,       /**< , */
    TOK_FUNC         /**< 函数名: abs, sqrt, log, ... */
};
}

/** Token 详情 */
struct BandMathToken {
    BandMath::TokenEnum type;
    double number_value;      /**< TOK_NUMBER 时的值 */
    std::string string_value; /**< TOK_BAND 时的 "B1"，TOK_FUNC 时的 "sqrt" */
    int band_index;           /**< TOK_BAND 时的波段号 (1-based) */
    size_t start_pos;         /**< 在原始字符串中的起始位置 */
    size_t end_pos;           /**< 结束位置 */
    bool is_error;            /**< 是否为错误 token */

    BandMathToken()
        : type(BandMath::TOK_END), number_value(0), band_index(0),
          start_pos(0), end_pos(0), is_error(false) {}
};

/** 词法分析异常 */
class BandMathLexerException : public std::exception {
public:
    BandMathLexerException(const std::string& msg) : m_msg(msg) {}
    virtual const char* what() const noexcept { return m_msg.c_str(); }
private:
    std::string m_msg;
};

/**
 * 波段运算表达式词法分析器。
 * 将表达式字符串分解为 token 序列。
 */
class BandMathLexer {
public:
    BandMathLexer();

    /**
     * 词法分析：将表达式字符串转为 token 列表。
     * @return true 成功，false 有错误（通过 GetError 获取）
     */
    bool Tokenize(const std::string& expr,
                  std::vector<BandMathToken>& tokens);

    std::string GetError() const { return m_error; }

    /** 从 token 列表获取所有引用的波段号（1-based） */
    static std::vector<int> GetReferencedBands(
        const std::vector<BandMathToken>& tokens);

private:
    BandMathToken getNextToken();
    void skipWhitespace();

    std::string m_input;
    size_t m_pos;
    std::string m_error;

    /** 判断是否为已知函数名 */
    static bool isKnownFunction(const std::string& name);
};

#endif // __GEODA_CENTER_BAND_MATH_LEXER_H__
