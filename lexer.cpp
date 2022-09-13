#include "lexer.h"
#include "table.h"

Lexer::Lexer(const std::string &file) {
    std::ifstream in(file);
    in.exceptions(std::ifstream::badbit);
    DfaState prev_state = DfaState::Whitespace;
    DfaState state = DfaState::Whitespace;
    std::string str;
    this->token_count = 0;
    while (true) {
        auto pos = in.tellg();
        std::uint8_t c = 0;
        in >> std::noskipws >> c;
        // Figure out what we've got
        if (STATE_TBL[c][static_cast<std::uint64_t>(state)] ==
                DfaState::Accept ||
            !c) {
            in.seekg(pos);
            if (!trim(str).empty()) {
                Token token;
                switch (state) {
                case DfaState::Letter: {
                    if (std::find(RESERVED_WORDS.cbegin(),
                                  RESERVED_WORDS.cend(),
                                  trim(str)) != RESERVED_WORDS.cend()) {
                        token.emplace<4>(trim(str));
                    } else {
                        token.emplace<0>(trim(str));
                    }
                } break;
                case DfaState::Integer: {
                    token.emplace<1>(trim(str));
                } break;
                case DfaState::RealRational:
                case DfaState::RealThirdExpDigit: {
                    token.emplace<2>(trim(str));
                } break;
                case DfaState::Special:
                case DfaState::Dot:
                case DfaState::Colon: {
                    token.emplace<3>(trim(str));
                } break;
                default: {
                    std::stringstream ss;
                    ss << "Lexer entered unknown state "
                       << unsigned(STATE_TBL[unsigned(c)][unsigned(state)])
                       << " from state " << unsigned(state) << std::endl
                       << "Character code found: " << unsigned(c);
                    throw std::runtime_error(ss.str());
                } break;
                }
                this->push_token(token);
            }
            if (c == 0) {
                break;
            }
            str.clear();
            prev_state = DfaState::Whitespace;
            state = DfaState::Whitespace;
            continue;
        }
        if (STATE_TBL[c][static_cast<std::uint64_t>(state)] ==
            DfaState::Error) {
            std::stringstream ss;
            ss << "Invalid token at position " << in.tellg()
               << ": was parsing char " << unsigned(c) << " in state "
               << unsigned(state) << "; got " << str
               << "\nTransitional state: " << unsigned(c)
               << ", transitions to state "
               << unsigned(STATE_TBL[c][static_cast<std::uint64_t>(state)])
               << " from state " << unsigned(prev_state) << " and "
               << unsigned(state);
            throw std::runtime_error(ss.str());
        } else {
            str += static_cast<unsigned char>(c);
            prev_state = state;
            state = STATE_TBL[c][static_cast<std::uint64_t>(state)];
        }
    }
}

auto Lexer::get_token() -> std::optional<Token> {
    if (this->tokens.empty()) {
        return std::nullopt;
    }

    auto tok = this->tokens.front();
    this->tokens.pop_front();
    return tok;
}

auto Lexer::number_of_tokens() const -> std::tuple<std::size_t, std::size_t> {
    return {this->token_count, this->tokens.size()};
}

void Lexer::push_token(const Token &tok) {
    this->tokens.push_back(tok);
    this->token_count = this->tokens.size();
}
