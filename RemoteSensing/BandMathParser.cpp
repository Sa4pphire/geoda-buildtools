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

#include "BandMathParser.h"
#include <cmath>
#include <cfloat>
#include <cctype>
#include <algorithm>

namespace {
// 递归下降最大嵌套深度：拒绝 crafted 深嵌套/长右结合链表达式，
// 防调用栈溢出；同时约束了后续 evalPixel 的递归深度
const int MAX_PARSE_DEPTH = 256;

// nodata 判定，与 RSIndexCalculator::IsNoData 保持同一口径：
// 容差随 nodata 量级缩放，避免 -9999 等大 nodata 处带单精舍入误差的
// 像素被精确 == 漏掉，保证两条计算路径的传播行为一致
inline bool IsNoDataValue(float v, float nodata)
{
    const float tol = std::max(1e-6f, std::fabs(nodata) * FLT_EPSILON * 4.0f);
    return std::fabs(v - nodata) <= tol;
}
}  // namespace

BandMathParser::BandMathParser() : m_tokenIndex(0), m_depth(0) {}

BandMath::TokenEnum BandMathParser::currentTokenType()
{
    if (m_tokenIndex >= m_tokens.size()) return BandMath::TOK_END;
    return m_tokens[m_tokenIndex].type;
}

const BandMathToken& BandMathParser::currentToken()
{
    // 边界守卫：索引越界时返回静态终止 token，
    // 避免对 m_tokens 的越界访问（未定义行为）
    static const BandMathToken s_endToken;
    if (m_tokenIndex >= m_tokens.size()) return s_endToken;
    return m_tokens[m_tokenIndex];
}

void BandMathParser::advanceToken()
{
    if (m_tokenIndex < m_tokens.size()) m_tokenIndex++;
}

std::unique_ptr<ASTNode> BandMathParser::parseExpression()
{
    // 深度守卫：RAII 计数，任意返回路径都会减回
    if (m_depth >= MAX_PARSE_DEPTH) {
        m_error = "Expression nesting too deep";
        return nullptr;
    }
    m_depth++;
    struct DepthGuard { int& d; ~DepthGuard() { d--; } } guard{m_depth};

    auto left = parseTerm();
    if (!left) return nullptr;

    while (currentTokenType() == BandMath::TOK_PLUS ||
           currentTokenType() == BandMath::TOK_MINUS) {
        auto node = std::make_unique<ASTNode>();
        node->type = AST_BINOP;
        node->op = currentTokenType();
        advanceToken();
        auto right = parseTerm();
        if (!right) {
            m_error = "Expected expression after operator";
            return nullptr;
        }
        node->children.push_back(std::move(left));
        node->children.push_back(std::move(right));
        left = std::move(node);
    }
    return left;
}

std::unique_ptr<ASTNode> BandMathParser::parseTerm()
{
    auto left = parsePower();
    if (!left) return nullptr;

    while (currentTokenType() == BandMath::TOK_MUL ||
           currentTokenType() == BandMath::TOK_DIV) {
        auto node = std::make_unique<ASTNode>();
        node->type = AST_BINOP;
        node->op = currentTokenType();
        advanceToken();
        auto right = parsePower();
        if (!right) {
            m_error = "Expected expression after operator";
            return nullptr;
        }
        node->children.push_back(std::move(left));
        node->children.push_back(std::move(right));
        left = std::move(node);
    }
    return left;
}

std::unique_ptr<ASTNode> BandMathParser::parsePower()
{
    // ^ 为右结合递归，长链同样会深递归，一并守卫
    if (m_depth >= MAX_PARSE_DEPTH) {
        m_error = "Expression nesting too deep";
        return nullptr;
    }
    m_depth++;
    struct DepthGuard { int& d; ~DepthGuard() { d--; } } guard{m_depth};

    auto base = parseFactor();
    if (!base) return nullptr;

    // 右结合：2^3^2 = 2^(3^2)
    if (currentTokenType() == BandMath::TOK_POW) {
        auto node = std::make_unique<ASTNode>();
        node->type = AST_BINOP;
        node->op = BandMath::TOK_POW;
        advanceToken();
        auto exp = parsePower();  // 递归实现右结合
        if (!exp) {
            m_error = "Expected exponent after ^";
            return nullptr;
        }
        node->children.push_back(std::move(base));
        node->children.push_back(std::move(exp));
        return node;
    }
    return base;
}

std::unique_ptr<ASTNode> BandMathParser::parseFactor()
{
    // 一元负号自我递归可形成长链，不经过 parseExpression/parsePower，
    // 在此一并守卫
    if (m_depth >= MAX_PARSE_DEPTH) {
        m_error = "Expression nesting too deep";
        return nullptr;
    }
    m_depth++;
    struct DepthGuard { int& d; ~DepthGuard() { d--; } } guard{m_depth};

    BandMath::TokenEnum tt = currentTokenType();

    if (tt == BandMath::TOK_NUMBER) {
        auto node = std::make_unique<ASTNode>();
        node->type = AST_NUMBER;
        node->number_value = currentToken().number_value;
        advanceToken();
        return node;
    }

    if (tt == BandMath::TOK_BAND) {
        auto node = std::make_unique<ASTNode>();
        node->type = AST_BAND;
        node->band_index = currentToken().band_index;
        advanceToken();
        return node;
    }

    if (tt == BandMath::TOK_MINUS) {
        advanceToken();
        auto node = std::make_unique<ASTNode>();
        node->type = AST_UNARYOP;
        node->op = BandMath::TOK_MINUS;
        auto child = parseFactor();
        if (!child) {
            m_error = "Expected expression after unary minus";
            return nullptr;
        }
        node->children.push_back(std::move(child));
        return node;
    }

    if (tt == BandMath::TOK_LPAREN) {
        advanceToken();
        auto node = parseExpression();
        if (!node) return nullptr;
        if (currentTokenType() != BandMath::TOK_RPAREN) {
            m_error = "Expected ')'";
            return nullptr;
        }
        advanceToken();
        return node;
    }

    if (tt == BandMath::TOK_FUNC) {
        auto node = std::make_unique<ASTNode>();
        node->type = AST_FUNC;
        node->func_name = currentToken().string_value;
        advanceToken();
        // 期望 '('
        if (currentTokenType() != BandMath::TOK_LPAREN) {
            m_error = "Expected '(' after function name";
            return nullptr;
        }
        advanceToken();
        // 解析参数列表
        if (currentTokenType() != BandMath::TOK_RPAREN) {
            while (true) {
                auto arg = parseExpression();
                if (!arg) return nullptr;
                node->children.push_back(std::move(arg));
                if (currentTokenType() == BandMath::TOK_COMMA) {
                    advanceToken();
                    continue;
                }
                break;
            }
        }
        if (currentTokenType() != BandMath::TOK_RPAREN) {
            m_error = "Expected ')' in function call";
            return nullptr;
        }
        advanceToken();
        return node;
    }

    m_error = "Unexpected token in expression";
    return nullptr;
}

void BandMathParser::collectBands(const ASTNode& node,
                                    std::vector<int>& bands) const
{
    switch (node.type) {
        case AST_BAND:
            bands.push_back(node.band_index);
            break;
        case AST_UNARYOP:
        case AST_BINOP:
            for (const auto& child : node.children) {
                collectBands(*child, bands);
            }
            break;
        case AST_FUNC:
            for (const auto& child : node.children) {
                collectBands(*child, bands);
            }
            break;
        default:
            break;
    }
}

bool BandMathParser::Parse(const std::string& expr,
                            std::unique_ptr<ASTNode>& ast)
{
    m_error.clear();
    m_referencedBands.clear();

    // 字符白名单预校验（PR 六轮审查阻断项修复：表达式注入防御）：
    // 进入词法/解析流程前先拒绝一切白名单之外的字符，
    // 仅允许数字、字母（波段变量/函数名/科学计数法）、下划线、
    // 小数点与数学运算符（含空白），杜绝任何意外字符被解析，
    // 与后续词法层的 Unexpected character 拒绝构成纵深防御
    for (char c : expr) {
        if (!(std::isalnum(static_cast<unsigned char>(c)) || c == '_' ||
              c == '.' || c == '+' || c == '-' || c == '*' || c == '/' ||
              c == '^' || c == '(' || c == ')' || c == ',' ||
              c == ' ' || c == '\t' || c == '\r' || c == '\n')) {
            m_error = std::string("Invalid character in expression: '") +
                      c + "'";
            return false;
        }
    }

    BandMathLexer lexer;
    if (!lexer.Tokenize(expr, m_tokens)) {
        m_error = lexer.GetError();
        return false;
    }

    m_tokenIndex = 0;
    m_depth = 0;
    ast = parseExpression();
    if (!ast) {
        if (m_error.empty()) m_error = "Parse error";
        return false;
    }

    if (currentTokenType() != BandMath::TOK_END) {
        m_error = "Unexpected tokens after expression";
        ast.reset();
        return false;
    }

    // 收集引用的波段号
    collectBands(*ast, m_referencedBands);
    // 去重
    std::sort(m_referencedBands.begin(), m_referencedBands.end());
    m_referencedBands.erase(
        std::unique(m_referencedBands.begin(), m_referencedBands.end()),
        m_referencedBands.end());

    return true;
}

bool BandMathParser::Validate(const std::string& expr)
{
    std::unique_ptr<ASTNode> ast;
    return Parse(expr, ast);
}

float BandMathParser::evalPixel(const ASTNode& node,
                                  const std::map<int, const float*>& band_data,
                                  size_t pixelIdx, float nodata)
{
    switch (node.type) {
        case AST_NUMBER:
            return (float)node.number_value;

        case AST_BAND: {
            auto it = band_data.find(node.band_index);
            if (it == band_data.end()) return nodata;
            return it->second[pixelIdx];
        }

        case AST_UNARYOP: {
            float v = evalPixel(*node.children[0], band_data, pixelIdx, nodata);
            if (IsNoDataValue(v, nodata)) return nodata;
            return (node.op == BandMath::TOK_MINUS) ? -v : v;
        }

        case AST_BINOP: {
            float a = evalPixel(*node.children[0], band_data, pixelIdx, nodata);
            float b = evalPixel(*node.children[1], band_data, pixelIdx, nodata);
            if (IsNoDataValue(a, nodata) || IsNoDataValue(b, nodata)) return nodata;
            switch (node.op) {
                case BandMath::TOK_PLUS:  return a + b;
                case BandMath::TOK_MINUS: return a - b;
                case BandMath::TOK_MUL:   return a * b;
                case BandMath::TOK_DIV:
                    return (std::fabs(b) < 1e-10f) ? nodata : a / b;
                case BandMath::TOK_POW:
                    // 负数底数 + 非整数指数时 std::pow 会静默返回 NaN，
                    // 输出 nodata 避免无效像元进入结果影像
                    return (a < 0 && std::floor(b) != b) ? nodata
                                                         : std::pow(a, b);
                default: return nodata;
            }
        }

        case AST_FUNC: {
            // 先求所有参数
            std::vector<float> args;
            for (const auto& child : node.children) {
                float v = evalPixel(*child, band_data, pixelIdx, nodata);
                if (IsNoDataValue(v, nodata)) return nodata;
                args.push_back(v);
            }

            const std::string& f = node.func_name;
            if (f == "abs" && args.size() == 1) return std::fabs(args[0]);
            if (f == "sqrt" && args.size() == 1) {
                return (args[0] < 0) ? nodata : std::sqrt(args[0]);
            }
            if (f == "pow" && args.size() == 2) {
                // 负数底数 + 非整数指数时 std::pow 会静默返回 NaN，输出 nodata
                return (args[0] < 0 && std::floor(args[1]) != args[1])
                       ? nodata : std::pow(args[0], args[1]);
            }
            if (f == "log" && args.size() == 1) {
                return (args[0] <= 0) ? nodata : std::log(args[0]);
            }
            if (f == "log10" && args.size() == 1) {
                return (args[0] <= 0) ? nodata : std::log10(args[0]);
            }
            if (f == "exp" && args.size() == 1) return std::exp(args[0]);
            if (f == "sin" && args.size() == 1) return std::sin(args[0]);
            if (f == "cos" && args.size() == 1) return std::cos(args[0]);
            if (f == "tan" && args.size() == 1) return std::tan(args[0]);
            if (f == "min" && args.size() == 2)
                return (args[0] < args[1]) ? args[0] : args[1];
            if (f == "max" && args.size() == 2)
                return (args[0] > args[1]) ? args[0] : args[1];
            return nodata;
        }
    }
    return nodata;
}

void BandMathParser::Evaluate(const ASTNode& ast,
                                const std::map<int, const float*>& band_data,
                                float* out, size_t count, float nodata)
{
    for (size_t i = 0; i < count; i++) {
        out[i] = evalPixel(ast, band_data, i, nodata);
    }
}
