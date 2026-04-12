
inline void cpu_relax()
{
#ifdef __x86_64__
    asm volatile("pause");
#elif defined(__aarch64__)
    asm volatile("yield");
#else
    std::this_thread::yield(); // 备选，会真的让出 CPU
#endif
}