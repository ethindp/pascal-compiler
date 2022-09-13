#include "inja.hpp"
#include "json.hpp"
#include "lexer.h"
#include "parser.h"
#include "popl.hpp"
#include <algorithm>
#include <charconv>
#include <cstdint>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <functional>
#include <iostream>
#include <limits>
#include <memory>
#include <numeric>
#include <optional>
#include <system_error>
#include <tuple>

Parser::Parser(const std::string_view filename) {
    lexer = std::make_unique<Lexer>(filename.data());
    this->filename = filename.data();
    std::filesystem::path p = filename;
    p.replace_extension(".lst");
    asm_output.open(p.string());
    asm_output.exceptions(std::ofstream::badbit | std::ofstream::failbit);
    asm_output << "char data_segment[65536] = {0};" << std::endl
               << "int main() {" << std::endl
               << "_asm {" << std::endl
               << "PUSHAD" << std::endl
               << "LEA EBP, data_segment" << std::endl
               << "JMP kmain" << std::endl;
    program();
}

void Parser::program() {
    index++;
    token = lexer->get_token();
    if (token->index() != ReservedWord ||
        (token->index() == ReservedWord && std::get<4>(*token) != "program"))
        throw std::runtime_error(
            "Bad code: program keyword required to declare program");
    index++;
    token = lexer->get_token();
    if (token->index() != Word) {
        throw std::runtime_error("Bad code: expected word");
    }
    token = lexer->get_token();
    index++;
    if (token->index() != Special)
        throw std::runtime_error("Bad code: expected ';'");
    if (std::get<3>(*token) != ";")
        throw std::runtime_error("Bad code: expected ';'");
    token = lexer->get_token();
    block();
    end_program();
}

void Parser::block() {
    pfv();
    if (!symtab.cur_scope->name.empty()) {
        std::vector<std::uint64_t> variables;
        for (const auto &[_, v] : symtab.cur_scope->table) {
            if (std::holds_alternative<VarData>(v)) {
                const auto vdata = std::get<VarData>(v);
                if (!vdata.is_param) {
                    variables.push_back(vdata.size);
                }
            }
        }
        asm_output << "PUSH EDI" << std::endl << "MOV EDI, ESP" << std::endl;
        if (const auto all_variables_size =
                std::accumulate(variables.cbegin(), variables.cend(), 0);
            all_variables_size != 0) {
            asm_output << "SUB ESP, " << all_variables_size << std::endl;
        }
        asm_output << "PUSHAD" << std::endl;
    } else {
        asm_output << "kmain:" << std::endl;
    }
    if (token->index() == ReservedWord && std::get<4>(*token) == "begin") {
        index++;
        block_depth++;
        token = lexer->get_token();
        statement();
        mstatement();
        if (token->index() == ReservedWord && std::get<4>(*token) == "end") {
            index++;
            block_depth--;
            token = lexer->get_token();
        } else {
            throw std::runtime_error("Bad code: unterminated block");
        }
    } else {
        throw std::runtime_error("Bad code: expected a block");
    }
}

void Parser::statement() {
    if (token->index() == ReservedWord) {
        if (auto tok = std::get<4>(*token); tok == "begin") {
            index++;
            block_depth++;
            token = lexer->get_token();
            statement();
            mstatement();
            if (token->index() == ReservedWord &&
                std::get<4>(*token) == "end") {
                index++;
                block_depth--;
                token = lexer->get_token();
            } else {
                throw std::runtime_error("Bad code: unterminated block");
            }
        } else if (tok == "if") {
            index++;
            token = lexer->get_token();
            conditional_stack.push(if_count);
            if_count++;
            expression(std::nullopt);
            handle_if();
        } else if (tok == "while") {
            index++;
            token = lexer->get_token();
            loop_stack.push(while_count);
            while_count++;
            asm_output << "while" << loop_stack.top() << ":" << std::endl;
            for_while = true;
            expression(std::nullopt);
            for_while = false;
            handle_while();
        }
    } else if (token->index() == Word) {
        const auto name = std::get<0>(*token);
        if (const auto var_info = symtab.get_var_info(name); var_info) {
            values.push({var_info->type, std::nullopt});
            index++;
            token = lexer->get_token();
            if (token->index() != Special || std::get<3>(*token) != ":=") {
                throw std::runtime_error(
                    "Bad code: expected ':=' for variable assignment");
            }
            index++;
            token = lexer->get_token();
            expression(std::nullopt);
            const auto rhs = values.top();
            values.pop();
            const auto lhs = values.top();
            values.pop();
            if (rhs.type != lhs.type) {
                throw std::runtime_error("Bad code: type mismatch");
            }
            if (!symtab.cur_scope->name.empty()) {
                if (!var_info->is_param) {
                    asm_output << "MOV [EDI - " << var_info->offset << "], "
                               << gprs[gpr_index - 1] << std::endl;
                    gpr_index--;
                } else {
                    if (var_info->pass_by_ref) {
                        asm_output << "MOV ESI, [EDI + " << var_info->offset
                                   << "]" << std::endl
                                   << "MOV [ESI], " << gprs[gpr_index - 1]
                                   << std::endl;
                        gpr_index--;
                    } else {
                        asm_output << "MOV [EDI + " << var_info->offset << "], "
                                   << gprs[gpr_index - 1] << std::endl;
                        gpr_index--;
                    }
                }
            } else {
                asm_output << "MOV [EBP + " << var_info->offset << "], "
                           << gprs[gpr_index - 1] << std::endl;
                gpr_index--;
            }
        } else if (const auto global_var_info = symtab.find(name);
                   global_var_info) {
            const auto info = std::get<VarData>(*global_var_info);
            values.push({info.type, std::nullopt});
            index++;
            token = lexer->get_token();
            if (token->index() != Special || std::get<3>(*token) != ":=") {
                throw std::runtime_error(
                    "Bad code: expected ':=' for variable assignment");
            }
            index++;
            token = lexer->get_token();
            expression(std::nullopt);
            const auto rhs = values.top();
            values.pop();
            const auto lhs = values.top();
            values.pop();
            if (rhs.type != lhs.type) {
                throw std::runtime_error("Bad code: type mismatch");
            }
            asm_output << "MOV [EBP + " << info.offset << "], "
                       << gprs[gpr_index - 1] << std::endl;
            gpr_index--;
        } else if (const auto proc_info = symtab.get_proc_info(name);
                   proc_info) {
            index++;
            token = lexer->get_token();
            if (token->index() != Special || std::get<3>(*token) != "(") {
                throw std::runtime_error(
                    "Bad code: procedure requires a call expression");
            }
            index++;
            token = lexer->get_token();
            consume_params(*proc_info);
            if (token->index() != Special || std::get<3>(*token) != ")") {
                throw std::runtime_error(
                    "Bad code: call expression requires termination");
            }
            asm_output << "CALL " << proc_info->name << std::endl;
            index++;
            token = lexer->get_token();
        } else if (const auto global_proc_info =
                       symtab.find(name, FindType::Procedure);
                   global_proc_info) {
            const auto info = std::get<ProcData>(*global_proc_info);
            index++;
            token = lexer->get_token();
            if (token->index() != Special || std::get<3>(*token) != "(") {
                throw std::runtime_error(
                    "Bad code: procedure requires a call expression");
            }
            index++;
            token = lexer->get_token();
            consume_params(info);
            if (token->index() != Special || std::get<3>(*token) != ")") {
                throw std::runtime_error(
                    "Bad code: call expression requires termination");
            }
            asm_output << "CALL " << proc_info->name << std::endl;
            index++;
            token = lexer->get_token();
        } else if (const auto func_info = symtab.get_func_info(name);
                   func_info) {
            index++;
            token = lexer->get_token();
            if (token->index() != Special || std::get<3>(*token) != "(") {
                throw std::runtime_error(
                    "Bad code: procedure requires a call expression");
            }
            index++;
            token = lexer->get_token();
            consume_params(*func_info);
            if (token->index() != Special || std::get<3>(*token) != ")") {
                throw std::runtime_error(
                    "Bad code: call expression requires termination");
            }
            asm_output << "CALL " << proc_info->name << std::endl;
            index++;
            token = lexer->get_token();
        } else if (const auto global_func_info =
                       symtab.find(name, FindType::Function);
                   global_func_info) {
            const auto info = std::get<FuncData>(*global_func_info);
            index++;
            token = lexer->get_token();
            if (token->index() != Special || std::get<3>(*token) != "(") {
                throw std::runtime_error(
                    "Bad code: procedure requires a call expression");
            }
            index++;
            token = lexer->get_token();
            consume_params(info);
            if (token->index() != Special || std::get<3>(*token) != ")") {
                throw std::runtime_error(
                    "Bad code: call expression requires termination");
            }
            asm_output << "CALL " << proc_info->name << std::endl;
            index++;
            token = lexer->get_token();
        }
    }
}

void Parser::if_prime() {
    if (token->index() == ReservedWord) {
        if (auto tok = std::get<4>(*token); tok == "else") {
            index++;
            token = lexer->get_token();
            statement();
        }
    }
}

void Parser::mstatement() {
    if (token->index() == Special) {
        if (std::get<3>(*token) == ";") {
            index++;
            token = lexer->get_token();
            statement();
            mstatement();
        }
    }
}

void Parser::handle_if() {
    if (token->index() == ReservedWord) {
        if (std::get<4>(*token) == "then") {
            if (last_comparison == '<') {
                asm_output << "JL if" << conditional_stack.top() << std::endl;
            } else if (last_comparison == '>') {
                asm_output << "JG if" << conditional_stack.top() << std::endl;
            } else if (last_comparison == '=') {
                asm_output << "JE if" << conditional_stack.top() << std::endl;
            }
            if (or_used) {
                asm_output << "or" << or_count << ":" << std::endl;
                or_used = false;
                or_count++;
            }
            asm_output << "JMP else" << conditional_stack.top() << std::endl
                       << "if" << conditional_stack.top() << ":" << std::endl;
            index++;
            token = lexer->get_token();
            statement();
            asm_output << "JMP endif" << conditional_stack.top() << std::endl
                       << "else" << conditional_stack.top() << ":" << std::endl;
            if_prime();
            asm_output << "JMP endif" << conditional_stack.top() << std::endl;
            asm_output << "endif" << conditional_stack.top() << ":"
                       << std::endl;
            conditional_stack.pop();
        } else {
            throw std::runtime_error("Bad code: missing required keyword "
                                     "'then' after conditional expression");
        }
    } else {
        throw std::runtime_error("Bad code: missing required keyword 'then' "
                                 "after conditional expression");
    }
}

void Parser::handle_while() {
    if (token->index() == ReservedWord) {
        if (std::get<4>(*token) == "do") {
            if (last_comparison == '<') {
                asm_output << "JL while" << loop_stack.top() << "inner"
                           << std::endl;
            } else if (last_comparison == '>') {
                asm_output << "JG while" << loop_stack.top() << "inner"
                           << std::endl;
            } else if (last_comparison == '=') {
                asm_output << "JE while" << loop_stack.top() << "inner"
                           << std::endl;
            }
            if (or_used) {
                asm_output << "or" << or_count << ":" << std::endl;
                or_used = false;
                or_count++;
            }
            asm_output << "JMP endwhile" << loop_stack.top() << std::endl;
            asm_output << "while" << loop_stack.top() << "inner:" << std::endl;
            index++;
            token = lexer->get_token();
            statement();
            asm_output << "JMP while" << loop_stack.top() << std::endl
                       << "endwhile" << loop_stack.top() << ":" << std::endl;
            loop_stack.pop();
        } else {
            throw std::runtime_error("Bad code: missing required keyword 'do' "
                                     "after conditional expression");
        }
    } else {
        throw std::runtime_error("Bad code: missing required keyword 'do' "
                                 "after conditional expression");
    }
}

void Parser::end_program() {
    if (token->index() == Special) {
        if (std::get<3>(*token) == ".") {
            index++;
            asm_output << "POPAD" << std::endl
                       << "}" << std::endl
                       << "return 0;" << std::endl
                       << "}" << std::endl;
        } else {
            throw std::runtime_error("Bad code: program must be terminated "
                                     "with a full stop ('.')");
        }
    } else {
        throw std::runtime_error(
            "Bad code: program must be terminated with a full stop ('.')");
    }
}

void Parser::expression(
    std::optional<std::reference_wrapper<std::stringstream>> stream) {
    s_expression(stream);
}

void Parser::s_expression(
    std::optional<std::reference_wrapper<std::stringstream>> stream) {
    s_expression_r(stream);
    s_expression_prime(stream);
}

void Parser::s_expression_r(
    std::optional<std::reference_wrapper<std::stringstream>> stream) {
    term(stream);
}

void Parser::s_expression_prime(
    std::optional<std::reference_wrapper<std::stringstream>> stream) {
    if (token->index() == Special) {
        if (auto tok = std::get<3>(*token);
            tok == "<" || tok == ">" || tok == "=") {
            if (tok == "<") {
                last_comparison = '<';
            } else if (tok == ">") {
                last_comparison = '>';
            } else if (tok == "=") {
                last_comparison = '=';
            }
            index++;
            token = lexer->get_token();
            s_expression_r(stream);
            const auto rhs = values.top();
            values.pop();
            const auto lhs = values.top();
            values.pop();
            if (tok == "<" || tok == ">") {
                // You can only perform this comparison on integers or reals
                if ((lhs.type == VarType::Integer &&
                     rhs.type == VarType::Integer) ||
                    (lhs.type == VarType::Character &&
                     rhs.type == VarType::Character) ||
                    (lhs.type == VarType::Real && rhs.type == VarType::Real)) {
                    values.push({VarType::Boolean, std::nullopt});
                } else {
                    throw std::runtime_error(
                        "Bad code: invalid comparison in expression");
                }
            } else {
                // All types bar reals can be converted via `=` operator. We
                // eliminate `=` comparison to reals (which violates the Pascal
                // language specification) because floating-point comparison
                // with such an operator is unreliable and can have major
                // problems. See
                // https://docs.oracle.com/cd/E19957-01/806-3568/ncg_goldberg.html
                // and https://bitbashing.io/comparing-floats.html for more
                // info.
                if (lhs.type == VarType::Real || rhs.type == VarType::Real) {
                    throw std::runtime_error("Bad code: equivalence comparison "
                                             "cannot be performed on reals");
                }
                values.push({VarType::Boolean, std::nullopt});
            }
            if (!stream) {
                asm_output << "CMP " << gprs[gpr_index - 2] << ", "
                           << gprs[gpr_index - 1] << std::endl;
            } else {
                stream->get() << "CMP " << gprs[gpr_index - 2] << ", "
                              << gprs[gpr_index - 1] << std::endl;
            }
            gpr_index -= 2;
            s_expression_prime(stream);
        }
    }
}

void Parser::term(
    std::optional<std::reference_wrapper<std::stringstream>> stream) {
    term_r(stream);
    term_prime(stream);
}

void Parser::term_r(
    std::optional<std::reference_wrapper<std::stringstream>> stream) {
    fact(stream);
}

void Parser::term_prime(
    std::optional<std::reference_wrapper<std::stringstream>> stream) {
    if (token->index() == Special) {
        if (auto tok = std::get<3>(*token); tok == "+" || tok == "-") {
            index++;
            token = lexer->get_token();
            term_r(stream);
            const auto rhs = values.top();
            values.pop();
            const auto lhs = values.top();
            values.pop();
            if ((lhs.type == VarType::Integer &&
                 rhs.type == VarType::Integer) ||
                (lhs.type == VarType::Character &&
                 rhs.type == VarType::Character)) {
                if (lhs.literal && rhs.literal) {
                    if (tok == "+") {
                        auto res = std::get<std::int32_t>(*lhs.literal) +
                                   std::get<std::int32_t>(*rhs.literal);
                        values.push({VarType::Integer, res});
                    } else if (tok == "-") {
                        auto res = std::get<std::int32_t>(*lhs.literal) -
                                   std::get<std::int32_t>(*rhs.literal);
                        values.push({VarType::Integer, res});
                    }
                } else {
                    if (gpr_index > gprs.size() - 1) {
                        throw std::runtime_error(
                            "Bad code: expression is too complicated");
                    }
                    if (tok == "+") {
                        if (!stream) {
                            asm_output << "ADD " << gprs[gpr_index - 2] << ", "
                                       << gprs[gpr_index - 2] << ", "
                                       << gprs[gpr_index - 1] << std::endl;
                        } else {
                            stream->get() << "ADD " << gprs[gpr_index - 2]
                                          << ", " << gprs[gpr_index - 2] << ", "
                                          << gprs[gpr_index - 1] << std::endl;
                        }
                    } else {
                        if (!stream) {
                            asm_output << "SUB " << gprs[gpr_index - 2] << ", "
                                       << gprs[gpr_index - 2] << ", "
                                       << gprs[gpr_index - 1] << std::endl;
                        } else {
                            stream->get() << "SUB " << gprs[gpr_index - 2]
                                          << ", " << gprs[gpr_index - 2] << ", "
                                          << gprs[gpr_index - 1] << std::endl;
                        }
                    }
                    gpr_index--;
                    values.push({VarType::Integer, std::nullopt});
                }
            } else if (lhs.type == VarType::Real && rhs.type == VarType::Real) {
                if (lhs.literal && rhs.literal) {
                    if (tok == "+") {
                        auto res = std::get<float>(*lhs.literal) +
                                   std::get<std::int32_t>(*rhs.literal);
                        values.push({VarType::Real, res});
                    } else if (tok == "-") {
                        auto res = std::get<float>(*lhs.literal) -
                                   std::get<std::int32_t>(*rhs.literal);
                        values.push({VarType::Real, res});
                    }
                } else {
                    if (gpr_index > gprs.size() - 1) {
                        throw std::runtime_error(
                            "Bad code: expression is too complicated");
                    }
                    if (tok == "+") {
                        if (!stream) {
                            asm_output << "ADD " << gprs[gpr_index - 2] << ", "
                                       << gprs[gpr_index - 2] << ", "
                                       << gprs[gpr_index - 1] << std::endl;
                        } else {
                            stream->get() << "ADD " << gprs[gpr_index - 2]
                                          << ", " << gprs[gpr_index - 2] << ", "
                                          << gprs[gpr_index - 1] << std::endl;
                        }
                    } else {
                        if (!stream) {
                            asm_output << "SUB " << gprs[gpr_index - 2] << ", "
                                       << gprs[gpr_index - 2] << ", "
                                       << gprs[gpr_index - 1] << std::endl;
                        } else {
                            stream->get() << "ADD " << gprs[gpr_index - 2]
                                          << ", " << gprs[gpr_index - 2] << ", "
                                          << gprs[gpr_index - 1] << std::endl;
                        }
                    }
                    gpr_index--;
                    values.push({VarType::Real, std::nullopt});
                }
            } else {
                throw std::runtime_error(
                    "Bad code: invalid type on left-or right-hand side of "
                    "expression");
            }
            term_prime(stream);
        }
    } else if (token->index() == ReservedWord) {
        if (auto tok = std::get<4>(*token); tok == "or") {
            index++;
            token = lexer->get_token();
            if (!for_while) {
                if (last_comparison == '<') {
                    if (!stream) {
                        asm_output << "JL if" << conditional_stack.top()
                                   << std::endl;
                    } else {
                        stream->get()
                            << "JL if" << conditional_stack.top() << std::endl;
                    }
                } else if (last_comparison == '>') {
                    if (!stream) {
                        asm_output << "JG if" << conditional_stack.top()
                                   << std::endl;
                    } else {
                        stream->get()
                            << "JG if" << conditional_stack.top() << std::endl;
                    }
                } else if (last_comparison == '=') {
                    if (!stream) {
                        asm_output << "JE if" << conditional_stack.top()
                                   << std::endl;
                    } else {
                        stream->get()
                            << "JE if" << conditional_stack.top() << std::endl;
                    }
                }
            } else {
                if (last_comparison == '<') {
                    if (!stream) {
                        asm_output << "JL while" << loop_stack.top() << "inner"
                                   << std::endl;
                    } else {
                        stream->get() << "JL while" << loop_stack.top()
                                      << "inner" << std::endl;
                    }
                } else if (last_comparison == '>') {
                    if (!stream) {
                        asm_output << "JG while" << loop_stack.top() << "inner"
                                   << std::endl;
                    } else {
                        stream->get() << "JG while" << loop_stack.top()
                                      << "inner" << std::endl;
                    }
                } else if (last_comparison == '=') {
                    if (!stream) {
                        asm_output << "JE while" << loop_stack.top() << "inner"
                                   << std::endl;
                    } else {
                        stream->get() << "JE while" << loop_stack.top()
                                      << "inner" << std::endl;
                    }
                }
            }
            if (or_used) {
                if (!stream) {
                    asm_output << "or" << or_count << ":" << std::endl;
                } else {
                    stream->get() << "or" << or_count << ":" << std::endl;
                }
                or_used = false;
                or_count++;
            }
            term_r(stream);
            const auto lhs = values.top();
            values.pop();
            const auto rhs = values.top();
            values.pop();
            if (lhs.type == VarType::Boolean && rhs.type == VarType::Boolean) {
                values.push({VarType::Boolean, std::nullopt});
            } else {
                throw std::runtime_error("Bad code: expected type boolean "
                                         "for conjunctive 'or'");
            }
            term_prime(stream);
        }
    }
}

void Parser::fact(
    std::optional<std::reference_wrapper<std::stringstream>> stream) {
    fact_r(stream);
    fact_prime(stream);
}

void Parser::fact_r(
    std::optional<std::reference_wrapper<std::stringstream>> stream) {
    if (token->index() == Special) {
        if (auto tok = std::get<3>(*token); tok == "(") {
            grouping_depth++;
            index++;
            token = lexer->get_token();
            expression(stream);
            if (token->index() == Special) {
                if (auto tok = std::get<3>(*token); tok == ")") {
                    grouping_depth--;
                    index++;
                    token = lexer->get_token();
                } else {
                    throw std::runtime_error("Bad code: expected ')'");
                }
            } else {
                throw std::runtime_error("Bad code: expected ')'");
            }
        } else if (tok == "+" || tok == "-") {
            if (tok == "-") {
                // Do we use the current GPR?
                if (!stream) {
                    asm_output << "NEG " << gprs[gpr_index - 1] << std::endl;
                } else {
                    stream->get() << "NEG " << gprs[gpr_index - 1] << std::endl;
                }
            }
            index++;
            token = lexer->get_token();
            term_r(stream);
        }
    } else if (token->index() == Integer || token->index() == Real) {
        if (token->index() == Integer) {
            std::int32_t out;
            std::string integer = std::get<1>(*token);
            auto [ptr, ec] = std::from_chars(
                integer.data(), integer.data() + integer.size(), out, 10);
            if (ec != std::errc()) {
                throw std::runtime_error("Bad code: integer is not valid");
            }
            values.push({VarType::Integer, out});
            if (gpr_index > gprs.size() - 1) {
                throw std::runtime_error(
                    "Bad code: expression is too complicated");
            }
            if (!stream) {
                asm_output << "mov " << gprs[gpr_index] << ", " << out
                           << std::endl;
            } else {
                stream->get()
                    << "mov " << gprs[gpr_index] << ", " << out << std::endl;
            }
            gpr_index++;
        } else if (token->index() == Real) {
            std::string decimal = std::get<2>(*token);
            char *str = decimal.data();
            char *end = str;
            float out = std::strtof(str, &end);
            if (end == str) {
                throw std::runtime_error("Bad code: decimal is not valid");
            }
            values.push({VarType::Real, out});
            if (gpr_index > gprs.size() - 1) {
                throw std::runtime_error(
                    "Bad code: expression is too complicated");
            }
            if (!stream) {
                asm_output << "mov " << gprs[gpr_index] << ", " << out
                           << std::endl;
            } else {
                stream->get()
                    << "mov " << gprs[gpr_index] << ", " << out << std::endl;
            }
            gpr_index++;
        }
        index++;
        token = lexer->get_token();
    } else if (token->index() == Word) {
        if (const auto local_var_data =
                symtab.get_var_info(std::get<0>(*token));
            local_var_data) {
            if (gpr_index > gprs.size() - 1) {
                throw std::runtime_error(
                    "Bad code: exceeded available registers");
            }
            if (!symtab.cur_scope->name.empty()) {
                if (!local_var_data->is_param) {
                    if (!stream) {
                        asm_output << "MOV " << gprs[gpr_index] << ", [EDI - "
                                   << local_var_data->offset << "]"
                                   << std::endl;
                    } else {
                        stream->get()
                            << "MOV " << gprs[gpr_index] << ", [EDI - "
                            << local_var_data->offset << "]" << std::endl;
                    }
                    gpr_index++;
                } else {
                    if (!local_var_data->pass_by_ref) {
                        if (!stream) {
                            asm_output << "MOV " << gprs[gpr_index]
                                       << ", [EDI + " << local_var_data->offset
                                       << "]" << std::endl;
                        } else {
                            stream->get()
                                << "MOV " << gprs[gpr_index] << ", [EDI + "
                                << local_var_data->offset << "]" << std::endl;
                        }
                        gpr_index++;
                    } else {
                        if (!stream) {
                            asm_output << "MOV ESI, [EDI - "
                                       << local_var_data->offset << "]"
                                       << std::endl
                                       << "MOV " << gprs[gpr_index] << ", [ESI]"
                                       << std::endl;
                        } else {
                            stream->get()
                                << "MOV ESI, [EDI - " << local_var_data->offset
                                << "]" << std::endl
                                << "MOV " << gprs[gpr_index] << ", [ESI]"
                                << std::endl;
                        }
                        gpr_index++;
                    }
                }
            } else {
                if (!stream) {
                    asm_output << "MOV " << gprs[gpr_index] << ", [EBP + "
                               << local_var_data->offset << "]" << std::endl;
                } else {
                    stream->get() << "MOV " << gprs[gpr_index] << ", [EBP + "
                                  << local_var_data->offset << "]" << std::endl;
                }
                gpr_index++;
            }
            values.push({local_var_data->type, std::nullopt});
            index++;
            token = lexer->get_token();
        } else if (const auto global_var_data =
                       symtab.find(std::get<0>(*token));
                   global_var_data) {
            const auto vdata = std::get<VarData>(*global_var_data);
            index++;
            token = lexer->get_token();
            values.push({vdata.type, std::nullopt});
            if (gpr_index > gprs.size() - 1) {
                throw std::runtime_error(
                    "Bad code: exceeded available registers");
            }
            if (!stream) {
                asm_output << "MOV " << gprs[gpr_index] << ", [EBP + "
                           << vdata.offset << "]" << std::endl;
            } else {
                stream->get() << "MOV " << gprs[gpr_index] << ", [EBP + "
                              << vdata.offset << "]" << std::endl;
            }
            gpr_index++;
        } else if (const auto funcdata =
                       symtab.find(std::get<0>(*token), FindType::Function);
                   funcdata) {
            index++;
            token = lexer->get_token();
            if (token->index() != Special || std::get<3>(*token) != "(") {
                throw std::runtime_error(
                    "Bad code: procedure requires a call expression");
            }
            index++;
            token = lexer->get_token();
            consume_params(std::get<FuncData>(*funcdata));
            if (token->index() != Special || std::get<3>(*token) != ")") {
                throw std::runtime_error(
                    "Bad code: call expression requires termination");
            }
            index++;
            token = lexer->get_token();
            const auto rhs = values.top();
            values.pop();
            const auto lhs = values.top();
            values.pop();
            if (lhs.type != rhs.type) {
                throw std::runtime_error(
                    "Bad code: return type doesn't match variable type");
            }
        }
    } else {
        throw std::runtime_error("Bad code: expected grouped expression, "
                                 "additive or subtractive "
                                 "operator, integer, real, or word");
    }
}

void Parser::fact_prime(
    std::optional<std::reference_wrapper<std::stringstream>> stream) {
    if (token->index() == Special) {
        if (auto tok = std::get<3>(*token); tok == "*" || tok == "/") {
            index++;
            token = lexer->get_token();
            fact_r(stream);
            const auto rhs = values.top();
            values.pop();
            const auto lhs = values.top();
            values.pop();
            if ((lhs.type == VarType::Integer &&
                 rhs.type == VarType::Integer) ||
                (lhs.type == VarType::Character &&
                 rhs.type == VarType::Character)) {
                if (gpr_index > gprs.size() - 1) {
                    throw std::runtime_error(
                        "Bad code: exceeded available registers");
                }
                if (lhs.literal && rhs.literal) {
                    if (tok == "*") {
                        auto res = std::get<std::int32_t>(*lhs.literal) *
                                   std::get<std::int32_t>(*rhs.literal);
                        values.push({VarType::Integer, res});
                    } else if (tok == "/") {
                        auto res = std::get<std::int32_t>(*lhs.literal) /
                                   std::get<std::int32_t>(*rhs.literal);
                        values.push({VarType::Integer, res});
                    }
                } else {
                    if (gpr_index > gprs.size() - 1) {
                        throw std::runtime_error(
                            "Bad code: expression is too complicated");
                    }
                    if (tok == "*") {
                        if (!stream) {
                            asm_output << "IMUL " << gprs[gpr_index - 2] << ", "
                                       << gprs[gpr_index - 2] << ", "
                                       << gprs[gpr_index - 1] << std::endl;
                        } else {
                            stream->get() << "IMUL " << gprs[gpr_index - 2]
                                          << ", " << gprs[gpr_index - 2] << ", "
                                          << gprs[gpr_index - 1] << std::endl;
                        }
                        gpr_index--;
                    } else if (tok == "/") {
                        bool do_pop = false;
                        if (gprs[gpr_index - 2] != "EAX") {
                            if (!stream) {
                                asm_output << "PUSH EAX" << std::endl
                                           << "PUSH EDX" << std::endl
                                           << "MOV EAX, " << gprs[gpr_index - 2]
                                           << std::endl;
                            } else {
                                stream->get()
                                    << "PUSH EAX" << std::endl
                                    << "PUSH EDX" << std::endl
                                    << "MOV EAX, " << gprs[gpr_index - 2]
                                    << std::endl;
                            }
                            do_pop = !do_pop;
                        }
                        if (!stream) {
                            asm_output << "CDQ" << std::endl
                                       << "IDIV " << gprs[gpr_index - 1]
                                       << std::endl;
                        } else {
                            stream->get()
                                << "CDQ" << std::endl
                                << "IDIV " << gprs[gpr_index - 1] << std::endl;
                        }
                        gpr_index--;
                        if (do_pop) {
                            if (!stream) {
                                asm_output << "POP EDX" << std::endl
                                           << "POP EAX" << std::endl;
                            } else {
                                stream->get() << "POP EDX" << std::endl
                                              << "POP EAX" << std::endl;
                            }
                        }
                    }
                    values.push({VarType::Integer, std::nullopt});
                }
            } else if (lhs.type == VarType::Real && rhs.type == VarType::Real) {
                if (lhs.literal && rhs.literal) {
                    if (tok == "*") {
                        auto res = std::get<float>(*lhs.literal) *
                                   std::get<float>(*rhs.literal);
                        values.push({VarType::Real, res});
                    } else if (tok == "/") {
                        auto res = std::get<float>(*lhs.literal) /
                                   std::get<float>(*rhs.literal);
                        values.push({VarType::Real, res});
                    }
                } else {
                    if (gpr_index > gprs.size() - 1) {
                        throw std::runtime_error(
                            "Bad code: expression is too complicated");
                    }
                    if (tok == "*") {
                        if (!stream) {
                            asm_output << "IMUL " << gprs[gpr_index - 2] << ", "
                                       << gprs[gpr_index - 2] << ", "
                                       << gprs[gpr_index - 1] << std::endl;
                        } else {
                            stream->get() << "IMUL " << gprs[gpr_index - 2]
                                          << ", " << gprs[gpr_index - 2] << ", "
                                          << gprs[gpr_index - 1] << std::endl;
                        }
                        gpr_index--;
                    } else if (tok == "/") {
                        bool do_pop = false;
                        if (gprs[gpr_index - 2] != "EAX") {
                            if (!stream) {
                                asm_output << "PUSH EAX" << std::endl
                                           << "PUSH EDX" << std::endl
                                           << "MOV EAX, " << gprs[gpr_index - 2]
                                           << std::endl;
                            } else {
                                stream->get()
                                    << "PUSH EAX" << std::endl
                                    << "PUSH EDX" << std::endl
                                    << "MOV EAX, " << gprs[gpr_index - 2]
                                    << std::endl;
                            }
                            do_pop = !do_pop;
                        }
                        if (!stream) {
                            asm_output << "CDQ" << std::endl
                                       << "IDIV " << gprs[gpr_index - 1]
                                       << std::endl;
                        } else {
                            stream->get()
                                << "CDQ" << std::endl
                                << "IDIV " << gprs[gpr_index - 1] << std::endl;
                        }
                        gpr_index--;
                        if (do_pop) {
                            if (!stream) {
                                asm_output << "POP EDX" << std::endl
                                           << "POP EAX" << std::endl;
                            } else {
                                stream->get() << "POP EDX" << std::endl
                                              << "POP EAX" << std::endl;
                            }
                        }
                    }
                    values.push({VarType::Real, std::nullopt});
                }
            } else {
                throw std::runtime_error(
                    "Bad code: invalid type on left-or right-hand side of "
                    "expression");
            }
            fact_prime(stream);
        }
    } else if (token->index() == ReservedWord) {
        if (auto tok = std::get<4>(*token); tok == "and") {
            index++;
            token = lexer->get_token();
            if (!stream) {
                if (last_comparison == '<') {
                    asm_output << "JGE or" << or_count << std::endl;
                } else if (last_comparison == '>') {
                    asm_output << "JLE or" << or_count << std::endl;
                } else if (last_comparison == '=') {
                    asm_output << "JNE or" << or_count << std::endl;
                }
            } else {
                if (last_comparison == '<') {
                    stream->get() << "JGE or" << or_count << std::endl;
                } else if (last_comparison == '>') {
                    stream->get() << "JLE or" << or_count << std::endl;
                } else if (last_comparison == '=') {
                    stream->get() << "JNE or" << or_count << std::endl;
                }
            }
            or_used = true;
            fact_r(stream);
            const auto lhs = values.top();
            values.pop();
            const auto rhs = values.top();
            values.pop();
            if (lhs.type == VarType::Boolean && rhs.type == VarType::Boolean) {
                values.push({VarType::Boolean, std::nullopt});
            } else {
                throw std::runtime_error("Bad code: expected type boolean "
                                         "for conjunctive 'and'");
            }
            fact_prime(stream);
        }
    }
}

void Parser::pfv() {
    if (token->index() == ReservedWord) {
        if (auto tok = std::get<4>(*token); tok == "var") {
            index++;
            token = lexer->get_token();
            const auto var = std::get<0>(*token);
            if (token->index() != Word) {
                throw std::runtime_error(
                    "Bad code: variable has invalid identifier");
            }
            temporaries.push_back(var);
            index++;
            token = lexer->get_token();
            varlist();
            if (token->index() != Special || std::get<3>(*token) != ":") {
                throw std::runtime_error(
                    "Bad code: variable must have datatype-specifier");
            }
            index++;
            token = lexer->get_token();
            datatype();
            for (const auto &temporary : temporaries) {
                std::uint64_t size = 0;
                VarType vtype;
                if (std::get<0>(*token) == "integer") {
                    vtype = VarType::Integer;
                    size = 4;
                } else if (std::get<0>(*token) == "boolean") {
                    vtype = VarType::Boolean;
                    size = 4;
                } else if (std::get<0>(*token) == "char") {
                    vtype = VarType::Character;
                    size = 4;
                } else if (std::get<0>(*token) == "real") {
                    vtype = VarType::Real;
                    size = 4;
                } else {
                    nlohmann::json data;
                    data["type"] = std::get<0>(*token);
                    throw std::runtime_error(inja::render(
                        "Bad code: type {{type}} is not valid", data));
                }
                if (!symtab.add_variable(temporary, vtype, size)) {
                    nlohmann::json data;
                    data["temporary"] = temporary;
                    throw std::runtime_error(inja::render(
                        "Bad code: variable {{temporary}} already defined",
                        data));
                }
            }
            temporaries.clear();
            index++;
            token = lexer->get_token();
            if (token->index() != Special || std::get<3>(*token) != ";") {
                throw std::runtime_error("Bad code: expected ';' to "
                                         "terminate variable declaration");
            }
            index++;
            token = lexer->get_token();
            mvar();
            pfv();
        } else if (tok == "procedure") {
            index++;
            token = lexer->get_token();
            if (token->index() != Word) {
                throw std::runtime_error(
                    "Bad code: procedure has invalid identifier");
            }
            if (!symtab.enter_proc_scope(std::get<0>(*token))) {
                throw std::runtime_error("Bad code: cannot redeclare a "
                                         "procedure that already exists");
            }
            asm_output << std::get<0>(*token) << ":" << std::endl;
            index++;
            token = lexer->get_token();
            if (token->index() != Special || std::get<3>(*token) != "(") {
                throw std::runtime_error("Bad code: missing required "
                                         "parameter list for procedure");
            }
            index++;
            token = lexer->get_token();
            param();
            if (token->index() != Special || std::get<3>(*token) != ")") {
                throw std::runtime_error(
                    "Bad code: parameter list must be terminated with ')'");
            }
            index++;
            token = lexer->get_token();
            if (token->index() != Special || std::get<3>(*token) != ";") {
                throw std::runtime_error("Bad code: procedure declaration must "
                                         "be terminated with ';'");
            }
            index++;
            token = lexer->get_token();
            block();
            if (token->index() != Special || std::get<3>(*token) != ";") {
                throw std::runtime_error("Bad code: procedure definition must "
                                         "be terminated with ';'");
            }
            std::vector<std::uint64_t> parameters;
            std::vector<std::uint64_t> variables;
            for (const auto &[_, v] : symtab.cur_scope->table) {
                if (std::holds_alternative<VarData>(v)) {
                    const auto vdata = std::get<VarData>(v);
                    if (vdata.is_param) {
                        parameters.push_back(vdata.size);
                    } else {
                        variables.push_back(vdata.size);
                    }
                }
            }
            asm_output << "POPAD" << std::endl;
            if (const auto all_variables_size =
                    std::accumulate(variables.cbegin(), variables.cend(), 0);
                all_variables_size != 0) {
                asm_output << "ADD ESP, " << all_variables_size << std::endl;
            }
            asm_output << "POP EDI" << std::endl;
            if (const auto all_parameters_size =
                    std::accumulate(parameters.cbegin(), parameters.cend(), 0);
                all_parameters_size != 0) {
                asm_output << "RET " << all_parameters_size << std::endl;
            } else {
                asm_output << "RET" << std::endl;
            }
            symtab.leave_scope();
            index++;
            token = lexer->get_token();
            pfv();
        } else if (tok == "function") {
            index++;
            token = lexer->get_token();
            if (token->index() != Word) {
                throw std::runtime_error(
                    "Bad code: function has invalid identifier");
            }
            const auto func_name = std::get<0>(*token);
            if (!symtab.enter_func_scope(func_name)) {
                throw std::runtime_error(
                    "Bad code: cannot redeclare a function");
            }
            index++;
            token = lexer->get_token();
            if (token->index() != Special || std::get<3>(*token) != "(") {
                throw std::runtime_error("Bad code: missing required "
                                         "parameter list for procedure");
            }
            index++;
            token = lexer->get_token();
            param();
            if (token->index() != Special || std::get<3>(*token) != ")") {
                throw std::runtime_error(
                    "Bad code: parameter list must be terminated with ')'");
            }
            index++;
            token = lexer->get_token();
            if (token->index() != Special || std::get<3>(*token) != ":") {
                throw std::runtime_error("Bad code: missing datatype "
                                         "specification indicator ':'");
            }
            index++;
            token = lexer->get_token();
            datatype();
            bool success = false;
            if (auto dtype = std::get<0>(*token); dtype == "integer") {
                success = symtab.add_variable(func_name, VarType::Integer, 4);
            } else if (dtype == "boolean") {
                success = symtab.add_variable(func_name, VarType::Boolean, 1);
            } else if (dtype == "char") {
                success = symtab.add_variable(func_name, VarType::Character, 1);
            } else if (dtype == "real") {
                success = symtab.add_variable(func_name, VarType::Real, 8);
            }
            // This should never, ever happen.
            if (!success) {
                nlohmann::json data;
                data["func_name"] = func_name;
                throw std::runtime_error(inja::render(
                    "Bad code: function {{func_name}} already defined", data));
            }
            index++;
            token = lexer->get_token();
            if (token->index() != Special || std::get<3>(*token) != ";") {
                throw std::runtime_error("Bad code: function declaration must "
                                         "be terminated with ';'");
            }
            index++;
            token = lexer->get_token();
            block();
            if (token->index() != Special || std::get<3>(*token) != ";") {
                throw std::runtime_error("Bad code: function definition must "
                                         "be terminated with ';'");
            }
            symtab.leave_scope();
            index++;
            token = lexer->get_token();
            pfv();
        }
    }
}

void Parser::varlist() {
    if (token->index() == Special && std::get<3>(*token) == ",") {
        index++;
        token = lexer->get_token();
        const auto var = std::get<0>(*token);
        if (token->index() != Word) {
            throw std::runtime_error(
                "Bad code: variable has invalid identifier");
        }
        temporaries.push_back(var);
        index++;
        token = lexer->get_token();
        varlist();
    }
}

void Parser::datatype() {
    if (token->index() == Word) {
        if (auto dtype = std::get<0>(*token);
            dtype != "integer" && dtype != "char" && dtype != "boolean" &&
            dtype != "real") {
            throw std::runtime_error("Bad code: unknown data type");
        }
    } else if (token->index() == ReservedWord) {
    if (std::get<4>(*token) != "array") {
throw std::runtime_error("Bad code: expected 'array' keyword or a valid data type");
}
index++;
token = lexer->get_token();
if (token->index() != Special || std::get<3>(*token) != "[") {
throw std::runtime_error("Bad code: expected '[' for array specification");
}
index++;
token = lexer->get_token();
dim();
if (token->index() != Special || std::get<3>(*token) != "]") {
throw std::runtime_error("Bad code: expected ']' to end array specification");
}
index++;
token = lexer->get_token();
if (token->index() != ReservedWord || std::get<4>(*token) == "of") {
throw std::runtime_error("Bad code: expected 'of' keyword to separate array length specification from data type");
}
index++;
token = lexer->get_token();
datatype();
} else {
throw std::runtime_error("Bad code: expected valid data type or array specification");
}
}

void Parser::mvar() {
    if (token->index() == Word) {
        const auto var = std::get<0>(*token);
        temporaries.push_back(var);
        index++;
        token = lexer->get_token();
        varlist();
        if (token->index() != Special || std::get<3>(*token) != ":") {
            throw std::runtime_error(
                "Bad code: missing datatype specifier ':'");
        }
        index++;
        token = lexer->get_token();
        datatype();
        for (const auto &temporary : temporaries) {
            std::uint64_t size = 0;
            VarType vtype;
            if (std::get<0>(*token) == "integer") {
                vtype = VarType::Integer;
                size = 4;
            } else if (std::get<0>(*token) == "boolean") {
                vtype = VarType::Boolean;
                size = 4;
            } else if (std::get<0>(*token) == "char") {
                vtype = VarType::Character;
                size = 4;
            } else if (std::get<0>(*token) == "real") {
                vtype = VarType::Real;
                size = 4;
            } else {
                nlohmann::json data;
                data["type"] = std::get<0>(*token);
                throw std::runtime_error(
                    inja::render("Bad code: type {{type}} is not valid", data));
            }
            if (!symtab.add_variable(temporary, vtype, size)) {
                nlohmann::json data;
                data["temporary"] = temporary;
                throw std::runtime_error(inja::render(
                    "Bad code: variable {{temporary}} already defined", data));
            }
        }
        temporaries.clear();
        index++;
        token = lexer->get_token();
        if (token->index() != Special || std::get<3>(*token) != ";") {
            throw std::runtime_error(
                "Bad code: variable declaration must end with ';'");
        }
        index++;
        token = lexer->get_token();
        mvar();
    }
}

void Parser::param() {
    auto pass_by_reference = false;
    if (token->index() == ReservedWord && std::get<4>(*token) == "var") {
        pass_by_reference = !pass_by_reference;
        index++;
        token = lexer->get_token();
    }
    if (token->index() == Word) {
        const auto var = std::get<0>(*token);
        temporaries.push_back(var);
        index++;
        token = lexer->get_token();
        varlist();
        if (token->index() != Special || std::get<3>(*token) != ":") {
            throw std::runtime_error(
                "Bad code: parameter declarations and parameter type "
                "specifications must be separated by ':'");
        }
        index++;
        token = lexer->get_token();
        datatype();
        for (const auto &temporary : temporaries) {
            std::uint64_t size = 0;
            VarType vtype;
            if (std::get<0>(*token) == "integer") {
                vtype = VarType::Integer;
                size = 4;
            } else if (std::get<0>(*token) == "boolean") {
                vtype = VarType::Boolean;
                size = 4;
            } else if (std::get<0>(*token) == "char") {
                vtype = VarType::Character;
                size = 4;
            } else if (std::get<0>(*token) == "real") {
                vtype = VarType::Real;
                size = 4;
            } else {
                nlohmann::json data;
                data["type"] = std::get<0>(*token);
                throw std::runtime_error(
                    inja::render("Bad code: type {{type}} is not valid", data));
            }
            if (!symtab.add_variable(temporary, vtype, size, pass_by_reference,
                                     true)) {
                nlohmann::json data;
                data["temporary"] = temporary;
                throw std::runtime_error(inja::render(
                    "Bad code: variable {{temporary}} already defined", data));
            }
        }
        temporaries.clear();
        index++;
        token = lexer->get_token();
        mparam();
    }
}

void Parser::mparam() {
    auto pass_by_reference = false;
    if (token->index() == Special && std::get<3>(*token) == ";") {
        index++;
        token = lexer->get_token();
        if (token->index() == ReservedWord && std::get<4>(*token) == "var") {
            pass_by_reference = !pass_by_reference;
            index++;
            token = lexer->get_token();
        }
        if (token->index() != Word) {
            throw std::runtime_error(
                "Bad code: parameter has invalid identifier");
        }
        const auto var = std::get<0>(*token);
        temporaries.push_back(var);
        index++;
        token = lexer->get_token();
        varlist();
        if (token->index() != Special || std::get<3>(*token) != ":") {
            throw std::runtime_error(
                "Bad code: parameter declarations and parameter type "
                "specifications must be separated by ':'");
        }
        index++;
        token = lexer->get_token();
        datatype();
        for (const auto &temporary : temporaries) {
            std::uint64_t size = 0;
            VarType vtype;
            if (std::get<0>(*token) == "integer") {
                vtype = VarType::Integer;
                size = 4;
            } else if (std::get<0>(*token) == "boolean") {
                vtype = VarType::Boolean;
                size = 1;
            } else if (std::get<0>(*token) == "char") {
                vtype = VarType::Character;
                size = 1;
            } else if (std::get<0>(*token) == "real") {
                vtype = VarType::Real;
                size = 4;
            } else {
                nlohmann::json data;
                data["type"] = std::get<0>(*token);
                throw std::runtime_error(
                    inja::render("Bad code: type {{type}} is not valid", data));
            }
            if (!symtab.add_variable(temporary, vtype, size, pass_by_reference,
                                     true)) {
                nlohmann::json data;
                data["temporary"] = temporary;
                throw std::runtime_error(inja::render(
                    "Bad code: variable {{temporary}} already defined", data));
            }
        }
        temporaries.clear();
        index++;
        token = lexer->get_token();
        mparam();
    }
}

void Parser::consume_params(const ProcData proc) {
    const auto scope = proc.next;
    std::vector<VarData> parameters;
    for (const auto &[name, el] : scope->table) {
        auto var = std::get<VarData>(el);
        if (var.is_param) {
            var.name = name;
            parameters.push_back(var);
        }
    }
    std::vector<std::string> assembly;
    std::size_t current_param = 0;
    while (current_param < parameters.size()) {
        const auto parameter = parameters[current_param];
        if (parameter.pass_by_ref) {
            if (token->index() == Word) {
                if (const auto variable =
                        std::get<VarData>(*symtab.find(std::get<0>(*token)));
                    parameter.type != variable.type) {
                    throw std::runtime_error(
                        "Bad code: parameter and variable type are invalid");
                } else {
                    std::stringstream ss;
                    ss << "MOV EAX, " << variable.offset << std::endl
                       << "ADD EAX, EBP" << std::endl
                       << "PUSH EAX";
                    assembly.push_back(ss.str());
                    index++;
                    token = lexer->get_token();
                }
            } else {
                throw std::runtime_error(
                    "Bad code: parameter expected pass-by-reference variable");
            }
        } else {
            std::stringstream ss;
            expression(std::ref(ss));
            const auto rhs = values.top();
            values.pop();
            if (parameter.type != rhs.type) {
                throw std::runtime_error(
                    "Bad code: expression did not match expected data type");
            }
            ss << "PUSH " << gprs[gpr_index - 1] << std::endl;
            assembly.push_back(ss.str());
            gpr_index--;
        }
        current_param++;
        if (current_param < parameters.size()) {
            if (token->index() == Special && std::get<3>(*token) == ",") {
                index++;
                token = lexer->get_token();
            } else {
                throw std::runtime_error(
                    "Bad code: got wrong number of parameters; expected ','");
            }
        }
    }
    // Generate assembly in reverse order
    for (std::size_t i = 0; i < assembly.size(); ++i) {
        if (assembly[i].ends_with("\n")) {
            asm_output << assembly[i];
        } else {
            asm_output << assembly[i] << std::endl;
        }
    }
}

void Parser::consume_params(const FuncData func) {
    const auto scope = func.next;
    std::vector<VarData> parameters;
    for (const auto &[_, var] : scope->table) {
        parameters.push_back(std::get<VarData>(var));
    }
    std::size_t current_param = 0;
    while (current_param < parameters.size()) {
        const auto parameter = parameters[current_param];
        if (parameter.pass_by_ref) {
            if (token->index() == Word) {
                const auto varinfo = symtab.find(std::get<0>(*token));
                if (!varinfo) {
                    nlohmann::json data;
                    data["name"] = std::get<0>(*token);
                    throw std::runtime_error(inja::render(
                        "Bad code: identifier {{name}} is not a variable",
                        data));
                }
                const auto var = std::get<VarData>(*varinfo);
                if (var.type != parameter.type) {
                    nlohmann::json data;
                    if (var.type == VarType::Integer) {
                        data["vtype"] = "integer";
                    } else if (var.type == VarType::Boolean) {
                        data["vtype"] = "boolean";
                    } else if (var.type == VarType::Character) {
                        data["vtype"] = "char";
                    } else {
                        data["vtype"] = "real";
                    }
                    if (parameter.type == VarType::Integer) {
                        data["ptype"] = "integer";
                    } else if (parameter.type == VarType::Boolean) {
                        data["ptype"] = "boolean";
                    } else if (parameter.type == VarType::Character) {
                        data["ptype"] = "char";
                    } else {
                        data["ptype"] = "real";
                    }
                    data["funcname"] = func.name;
                    throw std::runtime_error(
                        inja::render("Bad code: type of variable ({{vtype}}) "
                                     "does not match type "
                                     " of parameter ({{ptype}}) within "
                                     "function declaration {{funcname}}",
                                     data));
                }
                index++;
                token = lexer->get_token();
            } else {
                nlohmann::json data;
                data["pname"] = parameter.name;
                throw std::runtime_error(inja::render(
                    "Bad code: parameter {{pname}} expects reference", data));
            }
        } else {
            expression(std::nullopt);
            const auto rhs = values.top();
            values.pop();
            if (parameter.type != rhs.type) {
                nlohmann::json data;
                data["pname"] = parameter.name;
                if (parameter.type == VarType::Integer) {
                    data["vtype"] = "integer";
                } else if (parameter.type == VarType::Boolean) {
                    data["vtype"] = "boolean";
                } else if (parameter.type == VarType::Character) {
                    data["vtype"] = "char";
                } else {
                    data["vtype"] = "real";
                }
                if (rhs.type == VarType::Integer) {
                    data["ptype"] = "integer";
                } else if (rhs.type == VarType::Boolean) {
                    data["ptype"] = "boolean";
                } else if (rhs.type == VarType::Character) {
                    data["ptype"] = "char";
                } else {
                    data["ptype"] = "real";
                }
                throw std::runtime_error(
                    inja::render("Bad code: parameter {{pname}} got datatype "
                                 "{{vtype}}, but expected {{ptype}}",
                                 data));
            }
        }
        current_param += 1;
        if (current_param < parameters.size()) {
            if (token->index() == Special && std::get<3>(*token) == ",") {
                index++;
                token = lexer->get_token();
            } else {
                nlohmann::json data;
                data["funcname"] = func.name;
                data["current_param"] = current_param;
                data["total_params"] = parameters.size();
                throw std::runtime_error(inja::render(
                    "Bad code: function {{funcname}} got {{current_param}} "
                    "parameters, but expected {{total_params}}",
                    data));
            }
        }
    }
}

void Parser::dim() {
if (token->index() != Integer) {
throw std::runtime_error("Bad code: expected integer for array bounds");
}
index++;
token = lexer->get_token();
if (token->index() != Special || std::get<3>(*token) != ".") {
throw std::runtime_error("Bad code: expected '..' for array range specifier");
}
index++;
token = lexer->get_token();
if (token->index() != Special || std::get<3>(*token) != ".") {
throw std::runtime_error("Bad code: expected '..' for array range specifier");
}
index++;
token = lexer->get_token();
if (token->index() != Integer) {
throw std::runtime_error("Bad code: expected integer for array bounds");
}
index++;
token = lexer->get_token();
mdim();
}

void Parser::mdim() {
if (token->index() == Special && std::get<3>(*token) == ",") {
index++;
token = lexer->get_token();
dim();
}
}

auto main(int argc, char **argv) -> int {
    try {
        popl::OptionParser op;
        op.parse(argc, argv);
        if (op.non_option_args().size() == 0) {
            try {
                Parser p("code.txt");
                const auto [total, remaining] = p.lexer->number_of_tokens();
                if (p.get_index() != total || p.get_grouping_depth() > 0 ||
                    p.get_block_depth() > 0) {
                    std::cerr << "code.txt: Bad code (parsed " << p.get_index()
                              << "/" << total << " tokens)" << std::endl;
                    return 1;
                } else {
                    std::cout << "code.txt: Good code (parsed " << p.get_index()
                              << "/" << total << " tokens)" << std::endl;
                    return 0;
                }
            } catch (std::exception &e) {
                std::cerr << "code.txt: error: " << e.what() << std::endl;
                return 1;
            } catch (...) {
                std::cerr << "code.txt: unknown error" << std::endl;
                return 1;
            }
        }
        for (const auto &arg : op.non_option_args()) {
            try {
                Parser p(arg);
                const auto [total, remaining] = p.lexer->number_of_tokens();
                if (p.get_index() != total || p.get_grouping_depth() > 0 ||
                    p.get_block_depth() > 0) {
                    std::cerr << arg << ": Bad code (parsed " << p.get_index()
                              << "/" << total << " tokens)" << std::endl;
                } else {
                    std::cout << arg << ": Good code (parsed " << p.get_index()
                              << "/" << total << " tokens)" << std::endl;
                }
            } catch (std::exception &e) {
                std::cerr << arg << ": error: " << e.what() << std::endl;
            } catch (...) {
                std::cerr << arg << ": unknown error" << std::endl;
            }
        }
        return 0;
    } catch (std::exception &e) {
        std::cerr << "Internal error: " << e.what() << std::endl;
        return 1;
    } catch (...) {
        std::cerr << "Unknown error";
        return 1;
    }
}
