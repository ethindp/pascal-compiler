#pragma once
#include <cstdint>
#include <optional>
#include <string>
#include <tuple>
#include <unordered_map>
#include <variant>
#include <vector>

enum class VarType { Integer, Boolean, Character, Real };

enum class FindType { Variable, Procedure, Function };

struct Scope;

struct VarData {
    VarType type;
    std::string name;
    std::uint64_t size;
    std::uint64_t offset;
    bool pass_by_ref;
    bool is_param;
    Scope *next;
};

struct ProcData {
    std::string name;
    Scope *next;
};

struct FuncData {
    std::string name;
    Scope *next;
};

struct Scope {
    std::unordered_map<std::string, std::variant<VarData, ProcData, FuncData>>
        table;
    std::uint64_t param_offset;
    std::uint64_t var_offset;
    std::string name;
    Scope *previous;
};

class SymbolTable {
  public:
    mutable Scope *cur_scope;
    explicit SymbolTable();
    ~SymbolTable();
    [[nodiscard]] auto add_variable(const std::string name, const VarType type,
                                    const std::uint64_t size,
                                    const bool pass_by_ref = false,
                                    const bool is_param = false) const -> bool;
    [[nodiscard]] auto enter_proc_scope(const std::string name) const -> bool;
    [[nodiscard]] auto enter_func_scope(const std::string name) const -> bool;
    [[nodiscard]] auto find(const std::string name,
                            const FindType type = FindType::Variable) const
        -> std::optional<std::variant<VarData, ProcData, FuncData>>;
    void leave_scope();
    [[nodiscard]] auto get_var_info(const std::string name) const
        -> std::optional<VarData>;
    [[nodiscard]] auto get_func_info(const std::string name) const
        -> std::optional<FuncData>;
    [[nodiscard]] auto get_proc_info(const std::string name) const
        -> std::optional<ProcData>;
};
