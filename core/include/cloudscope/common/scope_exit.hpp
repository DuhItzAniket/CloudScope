// Runs a function when the enclosing scope ends, however it ends (return, exception).
// For C handles that must be closed and for state that must be put back.
//
//   fitsfile* file = open();
//   const ScopeExit close_file([file] { close(file); });
#pragma once

#include <utility>

namespace cloudscope {

template <class Function>
class ScopeExit {
public:
    explicit ScopeExit(Function function) : function_(std::move(function)) {}
    ~ScopeExit() { function_(); }

    ScopeExit(const ScopeExit&) = delete;
    ScopeExit& operator=(const ScopeExit&) = delete;
    ScopeExit(ScopeExit&&) = delete;
    ScopeExit& operator=(ScopeExit&&) = delete;

private:
    Function function_;
};

}  // namespace cloudscope
