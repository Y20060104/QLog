#pragma once

namespace qlog::platform {

class mutex {
    friend class condition_variable;

   public:
    mutex();
    mutex(bool reentrant);

    mutex(const mutex& rhs) = delete;
    mutex(mutex&& rhs) = delete;

    ~mutex();

    mutex& operator=(const mutex& rhs) = delete;
    mutex& operator=(mutex&& rhs) = delete;

    void lock();
    bool try_lock();
    bool unlock();

   private:
    void* get_platform_handle();

   private:
    bool reentrant_;
    struct mutex_platform_def* platform_data_;
};

class scoped_mutex {
   private:
    mutex& mutex_;

   public:
    scoped_mutex() = delete;

    explicit scoped_mutex(mutex& mutex) : mutex_(mutex) {
        mutex_.lock();
    }

    ~scoped_mutex() {
        mutex_.unlock();
    }
};
// CR:默认可重入锁是什么意思？
}  // namespace qlog::platform