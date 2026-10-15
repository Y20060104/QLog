/* Copyright (C) 2025 Tencent.
 * BQLOG is licensed under the Apache License, Version 2.0.
 * You may obtain a copy of the License at
 *
 *     http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 */

#include "qlog/runtime/log_manager.hpp"

#include "qlog/global/log_vars.hpp"
#include "qlog/runtime/log_worker.hpp"

namespace qlog::runtime {
log_manager::log_manager() {
    phase_ = phase::invalid;
    automatic_log_name_seq_ = 0;
    assert((std::endian::native == std::endian::little) && "Only Little-Endian is Supported!");
    public_worker_.init(log_thread_mode::async, nullptr);
    public_worker_.start();
    phase_ = phase::working;
#if !defined(NDEBUG)
    qlog::utility::util::log_device_console(qlog::LogLevel::info, "log_manager is constructed");
#endif
}

log_manager& log_manager::instance() {
    return log_global_vars::get().log_manager_inst_;
}

log_manager::~log_manager() {
    uninit();
    qlog::utility::util::log_device_console(qlog::LogLevel::info, "log_manager is destructed");
}

uint64_t log_manager::create_log(std::string log_name, const std::string& config_content,
                                 const std::vector<std::string>& category_names) {
    auto config_obj = qlog::runtime::property_value::create_from_string(config_content);
    if (config_obj.is_null()) {
        qlog::utility::util::log_device_console(
            qlog::LogLevel::error, "create_log parse property failed, config content:%s",
            config_content.c_str());
        return 0;
    }

    qlog::platform::scoped_spin_lock_write_crazy scoped_lock(logs_lock_);
    auto current_phase = phase_.load();

    switch (current_phase) {
        case phase::invalid:
            qlog::utility::util::log_device_console(qlog::LogLevel::error,
                                                    "you can not create log before BqLog initing!");
            return 0;
            break;
        case phase::working:
            break;
        case phase::uninitialized:
            qlog::utility::util::log_device_console(
                qlog::LogLevel::error, "you can not create log after BqLog is uninited!");
            return 0;
            break;
        default:
            break;
    }

    if (!log_name.empty()) {
        decltype(log_imp_list_)::iterator exist_iter = log_imp_list_.end();
        for (decltype(log_imp_list_)::iterator it = log_imp_list_.begin();
             it != log_imp_list_.end(); ++it) {
            if ((*it) && (*it)->get_name() == log_name) {
                exist_iter = it;
                break;
            }
        }
        if (exist_iter != log_imp_list_.end()) {
            // check category change
            bool categories_changed = false;
            if (category_names.size() != (*exist_iter)->get_categories_count()) {
                categories_changed = true;
            } else {
                auto count = category_names.size();
                for (size_t i = 0; i < count; ++i) {
                    if (category_names[i] !=
                        (*exist_iter)->get_category_name_by_index((uint32_t)i)) {
                        categories_changed = true;
                        break;
                    }
                }
            }
            if (categories_changed) {
                assert(false &&
                       "categories can not been changed during runtime, please check the generated "
                       "categories file for all the wrappers");
            }
            (*exist_iter)->reset_config(config_obj);
            return (*exist_iter).get()->id();
        }
    }

    if (log_name.empty()) {
        // assign a new log_name
        while (true) {
            auto new_seq = automatic_log_name_seq_.add_fetch_relaxed(1);
            char tmp[64];
            snprintf(tmp, sizeof(tmp), "AutoBqLog_%" PRId32, new_seq);
            std::string random_log_name = tmp;
            for (decltype(log_imp_list_)::iterator it = log_imp_list_.begin();
                 it != log_imp_list_.end(); ++it) {
                if ((*it) && (*it)->get_name() == random_log_name) {
                    continue;
                }
            }
            log_name = std::move(random_log_name);
            break;
        }
    }

    std::unique_ptr<log_imp> log = std::make_unique<log_imp>();
    if (!log->init(log_name, config_obj, category_names)) {
        return 0;
    }
    log->set_config(config_content);
    log_imp_list_.push_back(std::move(log));
    return log_imp_list_[log_imp_list_.size() - 1].get()->id();
}

bool log_manager::reset_config(const std::string& log_name, const std::string& config_content) {
    auto config_obj = qlog::runtime::property_value::create_from_string(config_content);
    if (config_obj.is_null()) {
        qlog::utility::util::log_device_console(
            qlog::LogLevel::error, "create_log parse property failed, config content:%s",
            config_content.c_str());
        return 0;
    }
    if (log_name.empty()) {
        qlog::utility::util::log_device_console(qlog::LogLevel::error,
                                                "create_log parse property failed, valid log name "
                                                "is not found, log_name:%s, config content:%s",
                                                log_name.c_str(), config_content.c_str());
        return 0;
    }

    qlog::platform::scoped_spin_lock_write_crazy scoped_lock(logs_lock_);
    auto current_phase = phase_.load();

    switch (current_phase) {
        case phase::invalid:
            qlog::utility::util::log_device_console(
                qlog::LogLevel::error, "you can not reset log config before BqLog initing!");
            return 0;
            break;
        case phase::working:
            break;
        case phase::uninitialized:
            qlog::utility::util::log_device_console(
                qlog::LogLevel::error, "you can not reset log config after BqLog is uninited!");
            return 0;
            break;
        default:
            break;
    }
    for (decltype(log_imp_list_)::iterator it = log_imp_list_.begin(); it != log_imp_list_.end();
         ++it) {
        if ((*it) && (*it)->get_name() == log_name) {
            if ((*it)->reset_config(config_obj)) {
                (*it)->set_config(config_content);
            }
            return true;
        }
    }
    return false;
}

bool log_manager::process_by_worker(log_imp* target_log, bool is_force_flush) {
    qlog::platform::scoped_spin_lock_read_crazy scoped_lock(logs_lock_);
    if (phase::working != phase_.load(qlog::platform::memory_order::relaxed)) {
        return false;
    }
    bool did_work = false;
    if (target_log) {
        did_work = target_log->process(is_force_flush);
    } else {
        for (decltype(log_imp_list_)::size_type i = 0; i < log_imp_list_.size(); ++i) {
            auto& log_impl = log_imp_list_[i];
            if (log_impl->get_thread_mode() == log_thread_mode::async) {
                did_work = log_imp_list_[i]->process(is_force_flush) || did_work;
            }
        }
    }
    return did_work;
}

void log_manager::force_flush_all() {
    qlog::platform::scoped_spin_lock_write_crazy scoped_lock(logs_lock_);
    qlog::scoped_thread_check_disable disable_helper;
    (void)disable_helper;
    for (decltype(log_imp_list_)::size_type i = 0; i < log_imp_list_.size(); ++i) {
        auto& log_impl = log_imp_list_[i];
        if (log_impl->get_thread_mode() != log_thread_mode::sync) {
            log_impl->process(true);
        }
    }
}

bool log_manager::try_flush_all() {
    qlog::platform::scoped_try_spin_lock_write_crazy scoped_lock(logs_lock_);
    if (!scoped_lock.owns_lock()) {
        return false;
    }
    qlog::scoped_thread_check_disable disable_helper;
    (void)disable_helper;
    for (decltype(log_imp_list_)::size_type i = 0; i < log_imp_list_.size(); ++i) {
        auto& log_impl = log_imp_list_[i];
        if (log_impl->get_thread_mode() != log_thread_mode::sync) {
            log_impl->process(true);
        }
    }
    return true;
}

void log_manager::force_flush(uint64_t log_id) {
    log_imp* log = get_log_by_id(log_id);
    if (!log) {
        return;
    }
    switch (log->get_thread_mode()) {
        case log_thread_mode::sync:
            // do nothing
            break;
        case log_thread_mode::async:
        case log_thread_mode::independent: {
            qlog::platform::scoped_spin_lock_write_crazy scoped_lock(logs_lock_);
            qlog::scoped_thread_check_disable disable_helper;
            (void)disable_helper;
            log->process(true);
        } break;
        default:
            break;
    }
}

void log_manager::uninit() {
    qlog::platform::scoped_spin_lock_read_crazy scoped_lock(logs_lock_);
    qlog::platform::scoped_spin_lock scoped_lock_uninit(uninit_lock_);
    // avoid worker thread call console callback which may cause dead lock when main thread is
    // calling this method(for example, in Unity)
    appender_console::register_console_callback(nullptr);
    phase expected_phase = phase::working;
    if (!phase_.compare_exchange_strong(expected_phase, phase::uninitialized)) {
        return;
    }
    public_worker_.cancel();
    public_worker_.awake();
    public_worker_.join();
    for (auto& log_imp : log_imp_list_) {
        if (log_imp->get_thread_mode() == log_thread_mode::independent) {
            log_imp->worker_.cancel();
            log_imp->worker_.awake();
        }
    }
    for (auto& log_imp : log_imp_list_) {
        if (log_imp->get_thread_mode() == log_thread_mode::independent) {
            log_imp->worker_.join();
        }
    }
    qlog::utility::util::log_device_console(qlog::LogLevel::info, "BqLog is uninited!");
}

qlog::runtime::layout& log_manager::get_public_layout() {
    return public_layout_;
}

void log_manager::try_restart_worker(log_worker* worker_ptr) {
    if (phase_.load(qlog::platform::memory_order::acquire) != phase::working) {
        return;
    }
    qlog::platform::scoped_spin_lock scoped_lock(uninit_lock_);
    if (phase_.load(qlog::platform::memory_order::acquire) != phase::working) {
        return;
    }
    qlog::utility::util::log_device_console(
        qlog::LogLevel::warning, "thread id:%" PRIu64 ", name:%s was terminated, try restart it!",
        worker_ptr->get_thread_id(), worker_ptr->get_thread_name().c_str());
    auto thread_mode = worker_ptr->get_thread_mode();
    auto target_log = worker_ptr->get_log_target();
    qlog::utility::object_destructor<log_worker>::destruct(worker_ptr);
    qlog::utility::object_constructor<log_worker>::construct(worker_ptr);
    worker_ptr->init(thread_mode, target_log);
    worker_ptr->start();
}

uint32_t log_manager::get_logs_count() const {
    return (uint32_t)log_imp_list_.size();
}

log_imp* log_manager::get_log_by_index(uint32_t index) {
    qlog::platform::scoped_spin_lock_read_crazy scoped_lock(logs_lock_);
    if (index < (uint32_t)log_imp_list_.size()) {
        return log_imp_list_[index].get();
    }
    return nullptr;
}
}  // namespace qlog::runtime
