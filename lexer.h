#pragma once
#include <algorithm>
#include <array>
#include <cctype>
#include <cstdint>
#include <deque>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <optional>
#include <sstream>
#include <stdexcept>
#include <string>
#include <tuple>
#include <variant>

using Token = std::variant<std::string, std::string, std::string, std::string,
                           std::string>;

enum class DfaState : std::uint64_t {
    Whitespace,
    Letter,
    Integer,
    RealInit,
    RealRational,
    RealExp,
    RealExpOp,
    RealFirstExpDigit,
    RealSecondExpDigit,
    RealThirdExpDigit,
    Special,
    Dot,
    Colon,
    Accept,
    Error
};

class Lexer {
  private:
    std::deque<Token> tokens;
    const std::string WHITESPACE = " \n\r\t\f\v";
    std::size_t token_count;

    inline std::string ltrim(const std::string &s) {
        size_t start = s.find_first_not_of(WHITESPACE);
        return (start == std::string::npos) ? "" : s.substr(start);
    }

    inline std::string rtrim(const std::string &s) {
        size_t end = s.find_last_not_of(WHITESPACE);
        return (end == std::string::npos) ? "" : s.substr(0, end + 1);
    }

    inline std::string trim(const std::string &s) { return rtrim(ltrim(s)); }

  public:
    Lexer(const std::string &file);
    auto get_token() -> std::optional<Token>;
    auto number_of_tokens() const -> std::tuple<std::size_t, std::size_t>;
    void push_token(const Token &tok);
};
