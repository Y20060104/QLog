// Component timings deliberately exclude file I/O and asynchronous worker scheduling.
#include <algorithm>
#include <array>
#include <chrono>
#include <cstdint>
#include <cstring>
#include <iomanip>
#include <iostream>
#include <new>
#include <stdexcept>
#include <string>
#include <vector>
#ifdef QLOG_DIAG_BQLOG
#include "bq_log/bq_log.h"
#include "bq_log/log/layout.h"
#include "bq_log/types/buffer/siso_ring_buffer.h"
#include "bq_common/encryption/vernam.h"
#include "bq_common/utils/util.h"
using Layout = bq::layout;
using Zone = bq::time_zone;
using Head = bq::_log_entry_head_def;
using Entry = bq::log_entry_handle;
using Tag = bq::log_arg_type_enum;
using Ring = bq::siso_ring_buffer;
using BufferResult = bq::enum_buffer_result_code;
using Vernam = bq::vernam;
static auto hash_bytes(const void* src, std::size_t len) { return bq::util::get_hash_64(src,len); }
static auto copy_hash(void* dst,const void* src,std::size_t len) { return bq::util::bq_memcpy_with_hash(dst,src,len); }
#else
#include "qlog/layout/layout.hpp"
#include "qlog/record/argument_tag.hpp"
#include "qlog/buffer/siso_ring_buffer.hpp"
#include "qlog/encryption/vernam.hpp"
#include "qlog/utility/hash.hpp"
using Layout = qlog::layout::Layout;
using Zone = qlog::layout::TimeZone;
using Head = qlog::record::RecordHeader;
using Entry = qlog::record::LogEntryHandle;
using Tag = qlog::record::ArgumentTag;
using Ring = qlog::buffer::siso_ring_buffer;
using BufferResult = qlog::buffer::enum_buffer_result_code;
using Vernam = qlog::encryption::vernam;
static auto hash_bytes(const void* src, std::size_t len) { return qlog::utility::get_hash_64(src,len); }
static auto copy_hash(void* dst,const void* src,std::size_t len) { return qlog::utility::qlog_memcpy_with_hash(dst,src,len); }
#endif
using Clock = std::chrono::steady_clock;
void require(bool ok,const char* message) { if(!ok) throw std::runtime_error(message); }
void emit(const char* name,std::size_t length,std::uint64_t iterations,Clock::duration elapsed,std::uint64_t checksum) {
    const auto ns=static_cast<double>(std::chrono::duration_cast<std::chrono::nanoseconds>(elapsed).count());
    std::cout << std::fixed << std::setprecision(3) << "{\"component\":\"" << name << "\",\"bytes\":"<<length<<",\"iterations\":"<<iterations<<",\"ns_per_op\":"<<ns/static_cast<double>(iterations)<<",\"checksum\":"<<checksum<<"}\n";
}
void copy_case() {
    alignas(32) std::array<std::uint8_t,8256> src{},dst{};
    for(std::size_t i=0;i<src.size();++i) src[i]=static_cast<std::uint8_t>((i*37+19)%256);
    for(std::size_t len : {std::size_t{0},std::size_t{19},std::size_t{44},std::size_t{128},std::size_t{1024},std::size_t{4096}}) {
      const std::uint64_t count=std::max<std::uint64_t>(200000,64000000/(len+1));
      for(bool fused : {false,true}) {
        std::uint64_t sum=0;
        auto begin=Clock::now();
        for(std::uint64_t i=0;i<count;++i) {
          const auto* p=src.data()+(i&63U);auto* d=dst.data()+(i&31U);
          if(fused) sum+=copy_hash(d,p,len);
          else {std::memcpy(d,p,len);sum+=hash_bytes(p,len);}
        }
        auto end=Clock::now();
        require(std::memcmp(dst.data()+((count-1)&31U),src.data()+((count-1)&63U),len)==0,"copy output");
        emit(fused?"copy_hash_fused":"copy_then_hash",len,count,end-begin,sum);
      }
    }
}
void vernam_case() {
    alignas(32) std::array<std::uint8_t,65536> data{};
    alignas(32) std::array<std::uint8_t,4096> key{};
    for(std::size_t i=0;i<key.size();++i)key[i]=static_cast<std::uint8_t>((i*13+7)%256);
    for(std::size_t len : {std::size_t{32},std::size_t{128},std::size_t{1024},std::size_t{4096},std::size_t{65536}}) {
      for(std::size_t i=0;i<data.size();++i)data[i]=static_cast<std::uint8_t>(i%256);
      Vernam::vernam_encrypt_32bytes_aligned(data.data(),len,key.data(),key.size(),0);
      for(std::size_t i=0;i<len;++i) require(data[i]==static_cast<std::uint8_t>((i%256)^key[i%key.size()]),"Vernam independent XOR oracle");
      Vernam::vernam_encrypt_32bytes_aligned(data.data(),len,key.data(),key.size(),0);
      const std::uint64_t count=std::max<std::uint64_t>(4096,268435456/len);
      const auto begin=Clock::now();
      for(std::uint64_t i=0;i<count;++i)Vernam::vernam_encrypt_32bytes_aligned(data.data(),len,key.data(),key.size(),0);
      const auto end=Clock::now();
      for(std::size_t i=0;i<len;++i) require(data[i]==static_cast<std::uint8_t>(i%256),"Vernam even repetitions restore input");
      emit("vernam",len,count,end-begin,hash_bytes(data.data(),len));
    }
}
struct Memory {
    void* data;std::size_t size;
    explicit Memory(std::size_t n):data(::operator new(n,std::align_val_t{128})),size(n) {std::memset(data,0,n);}
    ~Memory(){::operator delete(data,std::align_val_t{128});}
};
void ring_case() {
    Memory mem(Ring::calculate_min_size_of_memory(65536));
    Ring ring(mem.data,static_cast<std::uint32_t>(mem.size),false);
    constexpr std::uint64_t count=3000000;std::uint64_t sum=0;
    auto begin=Clock::now();
    for(std::uint64_t i=0;i<count;++i) {
      auto w=ring.alloc_write_chunk(64); require(w.result==BufferResult::success,"ring allocation");
      std::memcpy(w.data_addr,&i,sizeof(i)); ring.commit_write_chunk(w);
      auto r=ring.read_chunk();require(r.result==BufferResult::success,"ring read");
      std::uint64_t actual;std::memcpy(&actual,r.data_addr,sizeof(actual));require(actual==i,"ring sequence");sum+=actual;ring.return_read_chunk(r);
    }
    emit("siso_same_thread_roundtrip",64,count,Clock::now()-begin,sum);
}
template<class T> void add_arg(std::vector<std::uint8_t>& args,Tag type,T value) {
    const auto start=args.size();const std::size_t offset=sizeof(T)<=2?2:4;
    args.resize(start+((offset+sizeof(T)+3)&~std::size_t{3}),0);
    args[start]=static_cast<std::uint8_t>(type);std::memcpy(args.data()+start+offset,&value,sizeof(value));
}
void layout_case(bool four) {
    const std::string format=four?"idx:{}, num:{}, This test, {}, {}":"counter:{}";
    std::vector<std::uint8_t> args;
    if(four) add_arg(args,Tag::int32_type,std::int32_t{0});
    const auto counter_slot=args.size()+4;
    add_arg(args,Tag::int32_type,std::int32_t{0});
    if(four){add_arg(args,Tag::float_type,2.4232F);add_arg(args,Tag::bool_type,true);}
    const auto format_size=(format.size()+3)&~std::size_t{3};const auto args_start=sizeof(Head)+format_size;
    const auto ext=args_start+args.size();Memory mem(ext+1+6);
    auto* raw=static_cast<std::uint8_t*>(mem.data);auto* head=::new(mem.data) Head{};
    head->timestamp_epoch=1123;head->ext_info_offset=static_cast<std::uint32_t>(ext);
    head->category_idx=0;head->log_thread_id=42;head->level=2;head->log_format_str_type=static_cast<std::uint8_t>(Tag::string_utf8_type);head->log_format_data_len=static_cast<std::uint32_t>(format.size());
    std::memcpy(raw+sizeof(Head),format.data(),format.size());std::memcpy(raw+args_start,args.data(),args.size());raw[ext]=6;std::memcpy(raw+ext+1,"worker",6);
    Entry entry(raw,static_cast<std::uint32_t>(mem.size));Layout layout;Zone zone(false,0,0,0,"UTC0");
#ifdef QLOG_DIAG_BQLOG
    bq::array<bq::string> categories; categories.push_back("core");
#else
    std::vector<std::string> categories{"core"};
#endif
    require(layout.do_layout(entry,zone,&categories)==Layout::enum_layout_result::finished,"layout warmup");
    constexpr std::uint64_t count=2000000;std::uint64_t sum=0;
    const auto begin=Clock::now();
    for(std::int32_t i=0;i<static_cast<std::int32_t>(count);++i) {
      std::memcpy(raw+args_start+counter_slot,&i,sizeof(i));
      require(layout.do_layout(entry,zone,&categories)==Layout::enum_layout_result::finished,"layout result");sum+=layout.get_formated_str_len();
    }
    emit(four?"layout_four_params":"layout_counter",0,count,Clock::now()-begin,sum);
    std::cout<<"OUTPUT "<<layout.get_formated_str()<<'\n';
}
int main(int argc,char** argv) {try { const std::string mode=argc>1?argv[1]:"all";if(mode=="copy"||mode=="all")copy_case();if(mode=="vernam"||mode=="all")vernam_case();if(mode=="ring"||mode=="all")ring_case();if(mode=="layout"||mode=="all"){layout_case(false);layout_case(true);}return 0;}catch(const std::exception& e){std::cerr<<e.what()<<'\n';return 1;}}
