#include "src/qlog/buffer/log_buffer.h"
#include <iostream>
#include <thread>
#include <atomic>

int main() {
    constexpr uint64_t k_lp_force_time = 5000;  // Force LP path
    constexpr uint32_t kPayload = 64;
    
    qlog::log_buffer buf(16 * 1024, 4 * 1024, 100'000);
    
    std::atomic<int> success_count{0};
    
    // Writer thread
    std::thread writer([&]() {
        int iter = 0;
        while (true) {
            void* ptr = buf.alloc_write_chunk(kPayload, k_lp_force_time);
            if (ptr == nullptr) {
                std::cout << "Write failed at iteration " << iter << std::endl;
                break;
            }
            buf.commit_write_chunk(ptr);
            success_count.fetch_add(1, std::memory_order_relaxed);
            iter++;
            if (success_count.load() > 10000) break;
        }
    });
    writer.join();
    
    std::cout << "Total written: " << success_count.load() << std::endl;
    
    // Try to read one entry
    uint32_t out_size = 0;
    const void* rptr = buf.read_chunk(out_size);
    std::cout << "Read returned: " << (rptr != nullptr ? "SUCCESS" : "NULLPTR") << std::endl;
    if (rptr) {
        std::cout << "Out size: " << out_size << std::endl;
        buf.commit_read_chunk(rptr);
        std::cout << "Commit done" << std::endl;
    }
    
    // Try to write again
    void* ptr2 = buf.alloc_write_chunk(kPayload, k_lp_force_time);
    std::cout << "Write after read: " << (ptr2 != nullptr ? "SUCCESS" : "NULLPTR") << std::endl;
    if (ptr2) buf.commit_write_chunk(ptr2);
    
    return 0;
}
