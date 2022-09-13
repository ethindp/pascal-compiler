#pragma once
#include "lexer.h"
#include "symtab.hpp"
#include <functional>
#include <sstream>
#include <stack>
#include <string_view>
#include <vector>

class Parser {
  private:
    std::optional<Token> token = std::nullopt;
    std::uint16_t grouping_depth = 0;
    std::uint16_t block_depth = 0;
    std::uint64_t index = 0;
    struct VarValue {
        VarType type;
        std::optional<std::variant<std::int32_t, float, bool>> literal;
    };
    std::stack<VarValue> values;
    SymbolTable symtab;
    std::vector<std::string> temporaries;
    std::vector<std::string> gprs{"EAX", "EBX", "ECX", "EDX"};
    std::uint8_t gpr_index = 0;
    std::string filename;
    std::uint64_t offset = 0;
    bool or_used = false;
    bool for_while = false;
    char8_t last_comparison = 0;
    std::uint64_t if_count = 0;
    std::uint64_t while_count = 0;
    std::uint64_t or_count = 0;
    std::stack<std::uint64_t> conditional_stack;
    std::stack<std::uint64_t> loop_stack;

    enum TypeValue { Word, Integer, Real, Special, ReservedWord };

  public:
    std::unique_ptr<Lexer> lexer = nullptr;
    std::ofstream asm_output;

    explicit Parser(const std::string_view filename);

    [[nodiscard]] inline auto get_grouping_depth() const -> std::uint16_t {
        return grouping_depth;
    }

    [[nodiscard]] inline auto get_block_depth() const -> std::uint16_t {
        return block_depth;
    }

    [[nodiscard]] inline auto get_index() -> std::uint64_t { return index; }

    Parser() = delete;

    Parser(Parser &) = delete;

    Parser(const Parser &) = delete;

    ~Parser() = default;

  private:
    void program();
    void block();
    void statement();
    void if_prime();
    void mstatement();
    void
    expression(std::optional<std::reference_wrapper<std::stringstream>> stream);
    void s_expression(
        std::optional<std::reference_wrapper<std::stringstream>> stream);
    void s_expression_r(
        std::optional<std::reference_wrapper<std::stringstream>> stream);
    void s_expression_prime(
        std::optional<std::reference_wrapper<std::stringstream>> stream);
    void term(std::optional<std::reference_wrapper<std::stringstream>> stream);
    void
    term_r(std::optional<std::reference_wrapper<std::stringstream>> stream);
    void
    term_prime(std::optional<std::reference_wrapper<std::stringstream>> stream);
    void fact(std::optional<std::reference_wrapper<std::stringstream>> stream);
    void
    fact_prime(std::optional<std::reference_wrapper<std::stringstream>> stream);
    void
    fact_r(std::optional<std::reference_wrapper<std::stringstream>> stream);
    void handle_if();
    void handle_while();
    void end_program();
    void pfv();
    void varlist();
    void datatype();
    void mvar();
    void param();
    void mparam();
    void consume_params(const FuncData func);
    void consume_params(const ProcData proc);
    void dim();
    void mdim();
};
