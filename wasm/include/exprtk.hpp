// Minimal exprtk stub for WASM builds
// Provides a tiny subset of the real exprtk API used by the project
#pragma once
#include <string>

namespace exprtk {

template<typename T>
struct expression {
    T value() const { return T(0); }
    void register_symbol_table(void*) {}
};

template<typename T>
struct symbol_table {
    void add_variable(const std::string&, T&) {}
    void add_constants() {}
};

template<typename T>
struct parser {
    bool compile(const std::string&, expression<T>&) { return true; }
};

} // namespace exprtk
