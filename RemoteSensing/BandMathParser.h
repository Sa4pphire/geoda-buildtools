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

#ifndef __GEODA_CENTER_BAND_MATH_PARSER_H__
#define __GEODA_CENTER_BAND_MATH_PARSER_H__

#include "BandMathLexer.h"
#include <map>
#include <memory>
#include <exception>
#include <vector>

/** 解析器异常 */
class BandMathParserException : public std::exception {
public:
    BandMathParserException(const std::string& msg) : m_msg(msg) {}
    virtual const char* what() const throw() { return m_msg.c_str(); }
private:
    std::string m_msg;
};

/** AST 节点类型 */
enum ASTNodeType {
    AST_NUMBER,    /**< 数字常量 */
    AST_BAND,      /**< 波段引用 */
    AST_BINOP,     /**< 二元运算 +, -, *, /, ^ */
    AST_UNARYOP,   /**< 一元运算 - */
    AST_FUNC       /**< 函数调用 */
};

/** AST 节点 */
struct ASTNode {
    ASTNodeType type;
    double number_value;           /**< AST_NUMBER */
    int band_index;                /**< AST_BAND (1-based) */
    BandMath::TokenEnum op;        /**< AST_BINOP / AST_UNARYOP */
    std::string func_name;         /**< AST_FUNC */
    std::vector<std::unique_ptr<ASTNode>> children; /**< 子节点 */

    ASTNode() : type(AST_NUMBER), number_value(0), band_index(0),
                op(BandMath::TOK_END) {}
};

/**
 * 递归下降解析器。
 * 文法：
 *   expression: term (('+' | '-') term)*
 *   term:       power (('*' | '/') power)*
 *   power:      factor ('^' power)?        // 右结合
 *   factor:     NUMBER | BAND | '-' factor | '(' expression ')' | FUNC '(' expr (',' expr)* ')'
 */
class BandMathParser {
public:
    BandMathParser();

    /** 解析表达式，生成 AST */
    bool Parse(const std::string& expr, std::unique_ptr<ASTNode>& ast);

    /** 验证表达式语法（不生成 AST） */
    bool Validate(const std::string& expr);

    /** 获取表达式中引用的所有波段号 */
    std::vector<int> GetReferencedBands() const { return m_referencedBands; }

    std::string GetError() const { return m_error; }

    /**
     * 逐像元求值。
     * @param ast AST 根节点
     * @param band_data 波段号 -> 一维 float 数组的映射
     * @param out 输出数组（调用方分配，大小 = count）
     * @param count 像元总数
     * @param nodata 无效值
     */
    static void Evaluate(const ASTNode& ast,
                         const std::map<int, const float*>& band_data,
                         float* out, size_t count, float nodata);

private:
    std::unique_ptr<ASTNode> parseExpression();
    std::unique_ptr<ASTNode> parseTerm();
    std::unique_ptr<ASTNode> parsePower();
    std::unique_ptr<ASTNode> parseFactor();

    BandMath::TokenEnum currentTokenType();
    const BandMathToken& currentToken();
    void advanceToken();

    void collectBands(const ASTNode& node, std::vector<int>& bands) const;

    /** 递归求值单个像元 */
    static float evalPixel(const ASTNode& node,
                           const std::map<int, const float*>& band_data,
                           size_t pixelIdx, float nodata);

    std::vector<BandMathToken> m_tokens;
    size_t m_tokenIndex;
    int m_depth;  /**< 递归深度计数，防深嵌套表达式打爆调用栈 */
    std::string m_error;
    std::vector<int> m_referencedBands;
};

#endif // __GEODA_CENTER_BAND_MATH_PARSER_H__
