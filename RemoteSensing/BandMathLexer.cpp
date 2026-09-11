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

#include "BandMathLexer.h"
#include <cctype>
#include <cmath>
#include <set>

BandMathLexer::BandMathLexer() : m_pos(0) {}

bool BandMathLexer::isKnownFunction(const std::string& name)
{
    static const std::set<std::string> funcs = {
        "abs", "sqrt", "pow", "log", "log10", "exp",
        "sin", "cos", "tan", "min", "max"
    };
    return funcs.find(name) != funcs.end();
}

void BandMathLexer::skipWhitespace()
{
    while (m_pos < m_input.size() &&
           (m_input[m_pos] == ' ' || m_input[m_pos] == '\t' ||
            m_input[m_pos] == '\n' || m_input[m_pos] == '\r')) {
        m_pos++;
    }
}

BandMathToken BandMathLexer::getNextToken()
{
    skipWhitespace();
    BandMathToken tok;
    tok.start_pos = m_pos;

    if (m_pos >= m_input.size()) {
        tok.type = BandMath::TOK_END;
        tok.end_pos = m_pos;
        return tok;
    }

    char c = m_input[m_pos];

    // 数字：[0-9] 或 . 开头
    if (std::isdigit((unsigned char)c) || (c == '.' && m_pos + 1 < m_input.size()
        && std::isdigit((unsigned char)m_input[m_pos + 1]))) {
        size_t start = m_pos;
        while (m_pos < m_input.size() &&
               (std::isdigit((unsigned char)m_input[m_pos]) ||
                m_input[m_pos] == '.')) {
            m_pos++;
        }
        // 科学计数法：仅当指数部分确有数字时才消费，
        // 避免 "1e" / "1e+" 这类无效字面量进入 stod
        if (m_pos < m_input.size() &&
            (m_input[m_pos] == 'e' || m_input[m_pos] == 'E')) {
            size_t p = m_pos + 1;
            if (p < m_input.size() &&
                (m_input[p] == '+' || m_input[p] == '-')) {
                p++;
            }
            if (p < m_input.size() && std::isdigit((unsigned char)m_input[p])) {
                m_pos = p;
                while (m_pos < m_input.size() &&
                       std::isdigit((unsigned char)m_input[m_pos])) {
                    m_pos++;
                }
            }
        }
        tok.type = BandMath::TOK_NUMBER;
        // 安全转换：stod 失败（溢出等）走错误 token 约定，避免异常逸出崩溃
        try {
            tok.number_value = std::stod(m_input.substr(start, m_pos - start));
        } catch (const std::exception&) {
            m_error = "Invalid number literal: " + m_input.substr(start, m_pos - start);
            tok.is_error = true;
            tok.type = BandMath::TOK_END;
            tok.end_pos = m_pos;
            return tok;
        }
        tok.end_pos = m_pos;
        return tok;
    }

    // 波段引用：B 后跟数字
    if (c == 'B' || c == 'b') {
        if (m_pos + 1 < m_input.size() &&
            std::isdigit((unsigned char)m_input[m_pos + 1])) {
            size_t start = m_pos;
            m_pos++; // 跳过 B
            while (m_pos < m_input.size() &&
                   std::isdigit((unsigned char)m_input[m_pos])) {
                m_pos++;
            }
            tok.type = BandMath::TOK_BAND;
            tok.string_value = m_input.substr(start, m_pos - start);
            // 波段号过长可能使 stoi 溢出，同样走错误 token 约定
            try {
                tok.band_index = std::stoi(m_input.substr(start + 1, m_pos - start - 1));
            } catch (const std::exception&) {
                m_error = "Invalid band index: " + tok.string_value;
                tok.is_error = true;
                tok.type = BandMath::TOK_END;
                tok.end_pos = m_pos;
                return tok;
            }
            tok.end_pos = m_pos;
            return tok;
        }
    }

    // 标识符（可能是函数名）
    if (std::isalpha((unsigned char)c) || c == '_') {
        size_t start = m_pos;
        while (m_pos < m_input.size() &&
               (std::isalnum((unsigned char)m_input[m_pos]) ||
                m_input[m_pos] == '_')) {
            m_pos++;
        }
        std::string name = m_input.substr(start, m_pos - start);

        // 检查后面是否跟 '(' 来判断是否是函数调用
        skipWhitespace();
        if (m_pos < m_input.size() && m_input[m_pos] == '(') {
            if (isKnownFunction(name)) {
                tok.type = BandMath::TOK_FUNC;
                tok.string_value = name;
                tok.end_pos = m_pos;
                return tok;
            } else {
                m_error = "Unknown function: " + name;
                tok.is_error = true;
                tok.type = BandMath::TOK_END;
                tok.end_pos = m_pos;
                return tok;
            }
        }
        // 不是函数调用：标识符保持已消费状态直接报错误 token，
        // 不回退位置，保证即使被反复调用也始终前进终止
        m_error = "Unknown identifier: " + name;
        tok.is_error = true;
        tok.type = BandMath::TOK_END;
        tok.end_pos = m_pos;
        return tok;
    }

    // 运算符
    m_pos++;
    tok.end_pos = m_pos;
    switch (c) {
        case '+': tok.type = BandMath::TOK_PLUS;   return tok;
        case '-': tok.type = BandMath::TOK_MINUS;  return tok;
        case '*': tok.type = BandMath::TOK_MUL;    return tok;
        case '/': tok.type = BandMath::TOK_DIV;    return tok;
        case '^': tok.type = BandMath::TOK_POW;    return tok;
        case '(': tok.type = BandMath::TOK_LPAREN; return tok;
        case ')': tok.type = BandMath::TOK_RPAREN; return tok;
        case ',': tok.type = BandMath::TOK_COMMA;  return tok;
    }

    m_error = std::string("Unexpected character: '") + c + "'";
    tok.is_error = true;
    tok.type = BandMath::TOK_END;
    return tok;
}

bool BandMathLexer::Tokenize(const std::string& expr,
                              std::vector<BandMathToken>& tokens)
{
    m_input = expr;
    m_pos = 0;
    m_error.clear();
    tokens.clear();

    while (true) {
        BandMathToken tok = getNextToken();
        if (tok.is_error) {
            return false;
        }
        tokens.push_back(tok);
        if (tok.type == BandMath::TOK_END) {
            break;
        }
    }
    return true;
}

std::vector<int> BandMathLexer::GetReferencedBands(
    const std::vector<BandMathToken>& tokens)
{
    std::vector<int> bands;
    for (const auto& tok : tokens) {
        if (tok.type == BandMath::TOK_BAND) {
            bands.push_back(tok.band_index);
        }
    }
    return bands;
}
