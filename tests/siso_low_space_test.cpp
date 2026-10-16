#include <cstdint>
#include <cstring>
#include <iostream>
#include <memory>
#include <new>
#include <stdexcept>
#include "qlog/buffer/siso_ring_buffer.hpp"
using qlog::buffer::siso_ring_buffer;
using qlog::buffer::enum_buffer_result_code;
namespace {
void require(bool ok,const char* message) { if (!ok) throw std::runtime_error(message); }
struct Storage {
    std::size_t size = siso_ring_buffer::calculate_min_size_of_memory(8192);
    void* data = ::operator new(size, std::align_val_t{128});
    ~Storage() { ::operator delete(data,std::align_val_t{128}); }
};
void crossing_case() {
    Storage storage;
    siso_ring_buffer ring(storage.data,storage.size,false);
    const auto blocks=ring.get_total_blocks_count();
    require(blocks>=8 && blocks%4==0,"test ring geometry");
    unsigned notifications=0;
    for (std::uint32_t i=0; i<blocks/2; ++i) {
        auto handle=ring.alloc_write_chunk(sizeof(std::uint64_t));
        require(handle.result==enum_buffer_result_code::success,"allocate until full");
        const bool crossing=i+1==blocks/4;
        require(handle.low_space_flag==crossing,"only allocation crossing half-full notifies");
        notifications+=handle.low_space_flag ? 1U : 0U;
        const std::uint64_t sequence=i;
        std::memcpy(handle.data_addr,&sequence,sizeof(sequence));
        ring.commit_write_chunk(handle);
    }
    require(notifications==1,"one notification per crossing");
    auto full=ring.alloc_write_chunk(sizeof(std::uint64_t));
    require(full.result==enum_buffer_result_code::err_not_enough_space,"full still reports failure");
    ring.commit_write_chunk(full);
    for (std::uint32_t i=0;i<blocks/2;++i) {
        auto handle=ring.read_chunk();
        require(handle.result==enum_buffer_result_code::success,"read full ring");
        std::uint64_t value=0;std::memcpy(&value,handle.data_addr,sizeof(value));
        require(value==i,"notification change preserves data order");
        ring.return_read_chunk(handle);
    }
    require(ring.read_chunk().result==enum_buffer_result_code::err_empty_log_buffer,"drained ring");
}
void consumed_case() {
    Storage storage;
    siso_ring_buffer ring(storage.data,storage.size,false);
    const auto rounds=ring.get_total_blocks_count()*5;
    for (std::uint32_t i=0;i<rounds;++i) {
        auto write=ring.alloc_write_chunk(sizeof(std::uint64_t));
        require(write.result==enum_buffer_result_code::success,"allocate immediately consumed record");
        require(!write.low_space_flag,"refresh read cursor suppresses stale low-space hint");
        const std::uint64_t value=i; std::memcpy(write.data_addr,&value,sizeof(value));
        ring.commit_write_chunk(write);
        auto read=ring.read_chunk();
        require(read.result==enum_buffer_result_code::success,"read immediately consumed record");
        std::uint64_t actual=0;std::memcpy(&actual,read.data_addr,sizeof(actual));
        require(actual==value,"payload preserved across wraps");
        ring.return_read_chunk(read);
    }
}
}
int main(){try{crossing_case();consumed_case();std::cout<<"SISO half-full crossing and consumed-cursor regressions passed\n";return 0;}catch(const std::exception& e){std::cerr<<e.what()<<'\n';return 1;}}
