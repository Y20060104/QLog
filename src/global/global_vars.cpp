#include "qlog/global/global_vars.hpp"

#include <cstddef>
#include <vector>

#include "qlog/platform/spin_lock.hpp"

namespace qlog {

static qlog::platform::spin_lock_zero_init destructor_mutex_;
static std::vector<global_var_destructiable*>* destructible_vars_;
static global_vars_destructor global_var_destructor_;

global_vars_destructor& get_global_var_destructor() {
    return global_var_destructor_;
}

void global_vars_destructor::register_destructible_var(global_var_destructiable* var) {
    qlog::platform::scoped_spin_lock lock(destructor_mutex_);

    if (!destructible_vars_) {
        destructible_vars_ = new std::vector<global_var_destructiable*>();
    }

    destructible_vars_->push_back(var);
}

global_vars_destructor::~global_vars_destructor() {
    qlog::platform::scoped_spin_lock lock(destructor_mutex_);

    if (destructible_vars_) {
        for (std::size_t i = destructible_vars_->size(); i > 0; --i) {
            (*destructible_vars_)[i - 1]->partial_destruct();
        }
    }

    delete destructible_vars_;
    destructible_vars_ = nullptr;
}

// CR：CRTP是什么？ 为什么线程是共享对象？
}  // namespace qlog