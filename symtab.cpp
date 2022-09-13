#include "symtab.hpp"

SymbolTable::SymbolTable() {
    cur_scope = new Scope;
    cur_scope->param_offset = 0;
    cur_scope->var_offset = 0;
    cur_scope->name = "";
    cur_scope->previous = nullptr;
}

SymbolTable::~SymbolTable() {}

[[nodiscard]] auto
SymbolTable::add_variable(const std::string name, const VarType type,
                          const std::uint64_t size, const bool pass_by_ref,
                          const bool is_param) const -> bool {
    if (cur_scope->table.contains(name)) {
        return false;
    }
    if (is_param) {
        cur_scope->table[name] = VarData{.type = type,
                                         .size = size,
                                         .offset = 8 + cur_scope->param_offset,
                                         .pass_by_ref = pass_by_ref,
                                         .is_param = is_param,
                                         .next = nullptr};
        cur_scope->param_offset += size;
    } else {
        cur_scope->table[name] = VarData{.type = type,
                                         .size = size,
                                         .offset = cur_scope->var_offset,
                                         .pass_by_ref = pass_by_ref,
                                         .is_param = is_param,
                                         .next = nullptr};
        cur_scope->var_offset += size;
    }
    return true;
}

[[nodiscard]] auto SymbolTable::find(const std::string name,
                                     const FindType type) const
    -> std::optional<std::variant<VarData, ProcData, FuncData>> {
    auto trav_scope = cur_scope;
    while (trav_scope) {
        if (!trav_scope->table.contains(name)) {
            trav_scope = trav_scope->previous;
        } else {
            if ((type == FindType::Variable &&
                 std::holds_alternative<VarData>(trav_scope->table[name])) ||
                (type == FindType::Procedure &&
                 std::holds_alternative<ProcData>(trav_scope->table[name])) ||
                (type == FindType::Function &&
                 std::holds_alternative<FuncData>(trav_scope->table[name]))) {
                return trav_scope->table[name];
            } else {
                return std::nullopt;
            }
        }
    }
    return std::nullopt;
}

[[nodiscard]] auto SymbolTable::enter_proc_scope(const std::string name) const
    -> bool {
    if (cur_scope->table.contains(name)) {
        return false;
    }
    cur_scope->table[name] = ProcData{.name = name, .next = new Scope};
    auto old_scope = cur_scope;
    cur_scope = std::get<ProcData>(old_scope->table[name]).next;
    cur_scope->param_offset = 0;
    cur_scope->var_offset = 0;
    cur_scope->name = name;
    cur_scope->previous = old_scope;
    return true;
}

[[nodiscard]] auto SymbolTable::enter_func_scope(const std::string name) const
    -> bool {
    if (cur_scope->table.contains(name)) {
        return false;
    }
    cur_scope->table[name] = FuncData{.name = name, .next = new Scope};
    auto old_scope = cur_scope;
    cur_scope = std::get<FuncData>(old_scope->table[name]).next;
    cur_scope->param_offset = 0;
    cur_scope->var_offset = 0;
    cur_scope->name = name;
    cur_scope->previous = old_scope;
    return true;
}

void SymbolTable::leave_scope() {
    if (cur_scope->previous) {
        cur_scope = cur_scope->previous;
    }
}

[[nodiscard]] auto SymbolTable::get_var_info(const std::string name) const
    -> std::optional<VarData> {
    if (cur_scope->table.contains(name) &&
        std::holds_alternative<VarData>(cur_scope->table[name])) {
        return std::get<VarData>(cur_scope->table[name]);
    }
    return std::nullopt;
}

[[nodiscard]] auto SymbolTable::get_func_info(const std::string name) const
    -> std::optional<FuncData> {
    if (cur_scope->table.contains(name) &&
        std::holds_alternative<FuncData>(cur_scope->table[name])) {
        return std::get<FuncData>(cur_scope->table[name]);
    }
    return std::nullopt;
}

[[nodiscard]] auto SymbolTable::get_proc_info(const std::string name) const
    -> std::optional<ProcData> {
    if (cur_scope->table.contains(name) &&
        std::holds_alternative<ProcData>(cur_scope->table[name])) {
        return std::get<ProcData>(cur_scope->table[name]);
    }
    return std::nullopt;
}
