#pragma once

#include "qlog/serialization/entry_format.h"
#include "qlog/serialization/serializer.h"
#include<cstdint>
#include <string_view>

namespace qlog::layout{

    // ─────────────────────────────────────────────────────────────────────────────
// format_info: 解析 Python-style 格式说明符的结果
//
// 对标 BqLog layout.h:format_info
// 格式: [[fill]align][sign][#][0][width][grouping_option][.precision][type]
//
// 示例: {0:>10.2f}
//       fill=' ', align='>', width=10, precision=2, type='f'
// ─────────────────────────────────────────────────────────────────────────────

struct format_info
{
    // 对齐方式
    enum class align_type:uint8_t
    {
        none=0,
        left='<',// 左对齐 字符串默认
        right='>', // 右对齐 数字默认
        center='^', //居中
        pad_after_sign='=', // 符号填充 数字专用
    };

    //符号控制
    enum class sign_type:uint8_t
{
    none=0,
    plus='+',// 正数加 +
    minus='-', // （默认）只有负数加 -
    space=' ',// 正数 ' ' 负数 '-'
};
 
    // 类型标志
    enum class fmt_type:uint8_t
    {
        none=0,
        // 正数
        binary='b',
        octal='0',
        decimal='d',
        hex_lower='x',
        hex_upper='X',
        // 浮点
        fixed='f',// 固定小数点
        fixed_ipper='F',
        scientific='e',
        scientific_upper='E',
        general='g', // 通用（自动选f/e）
        general_upper='G',
        // 字符串
        string='s',
        // 字符
        char_type='c',
        // 指针
        pointer='p',

    };

    char fill_char=' ';// 填充字符 默认空格
    align_type align=align_type::none;//对齐方式
    sign_type sign=sign_type::minus;// 符号控制
    bool alt_form=false;//'#'替代形式 （0x 前缀等）
    bool zero_pad=false;// '0'补零
    int  width=0;// 最小宽度 0=不限制
    int precision=-1; // 小数精度 -1=默认
    fmt_type type=fmt_type::none;// 类型标识符

    bool has_format()const noexcept
    {
        return align!=align_type::none||sign!=sign_type::minus||alt_form||zero_pad||width>0||precision>=0||type!=fmt_type::none;
    }
};

enum class layout_result:uint8_t 
{
    success=0,
    buffer_insufficient, // 缓冲区不足
    entry_invalid, // Entery header 损坏
    fmt_str_missing, // 格式字符串未提供
};

class layout
{
    public:
    static  constexpr size_t INIT_BUFFER_SIZE=1024;// 缓冲区初始大小
    layout();
    ~layout();

    layout(const layout&)=delete;
    layout&  operator=(const layout& )=delete;

    // ─── 主接口 ───────────────────────────────────────────────────────────
    // 将二进制 entry 格式化为文本
    // 参数：
    //   entry_data  : entry_header 起始地址（由 M3 log_buffer::read_chunk 返回）
    //   entry_size  : 整个 entry 的字节数（含 header 和参数）
    //   fmt_str     : 格式字符串（如 "User {0} logged in from {1}"）
    //   categories  : 分类名称数组（用于输出分类字段，可为 nullptr）
    //
    // 返回：格式化后的文本（有效期至下次 do_layout 调用）

    [[nodiscard]]std::string_view dolayout(const uint8_t* entry_data,uint32_t entry_size,std::string_view fmt_str,
    const char* const* categories=nullptr,
uint32_t category_count=0)noexcept;

        // 获取缓冲区内容
        std::string_view result()const noexcept;

        // 预分配缓冲区
        void reserve(size_t capacity);

        private:
        uint8_t* buf_begin_=nullptr; // 缓冲区起始
        uint8_t* buf_cur_=nullptr;// 当前写入位置
        uint8_t* buf_end_=nullptr;//缓冲区结束位置（不含
        size_t buf_capacity_=0;

        // 重置到缓冲区起点（不释放内存）
        void reset_buffer()noexcept;
        // 确保还有n个字节可写 不足就realloc
        bool ensure_space(size_t n)noexcept;
        // 追加n字节
        void push_bytes(const void* src,size_t n)noexcept;

        // 格式字符串解析
           // 解析 "{index:format_spec}" 的 format_spec 部分
    // 返回解析消耗的字符数
    static int parse_format_cpec(std::string_view spec,format_info& out)noexcept;

    // 类型转换函数
    // 均返回实际写入缓冲区的字节数

    // 无符号整数：base=2/8/10/16（默认 10）
    size_t insert_integral_unsigned(uint64_t val,const format_info& fi)noexcept;
    // 有符号整数
    size_t insert_intefral_signed(int64_t val,const format_info& fi)noexcept;

    // float/double /fixed/scientific/general
    size_t insert_decimal(double val,const format_info& fi,bool is_float)noexcept;

    // bool 
    size_t insert_bool(bool val,const format_info& fi)noexcept;
    
    //UTF-8字符串
    size_t insert_str_utf8(std::string_view  sv,const format_info& fi)noexcept;

    // 指针
    size_t insert_pointer(uintptr_t ptr,const format_info& fi)noexcept;

    // 填充辅助
    // 在目标字符串两侧/某侧插入 fill_char 使总宽度达到width
    void fill_and_alignment(const char* content,size_t content_len,
    const format_info& fi)noexcept;
    

    // 时间戳格式化
    size_t insert_timestamp(uint64_t timestamp_ms)noexcept;

    // 参数分发 
    // 根据param_type 调用对应的inset_* 函数
    // 返回该参数在params_buf占用的字节数（用于skip）
    size_t insert_param(const uint8_t* param_ptr,
    const format_info& fi)noexcept;


    // entry 解析状态 在dolayout生命周期内有效
    const uint8_t* params_begin_=nullptr;
    const uint8_t* params_end_=nullptr;

    // 参数索引 ： 记录每个参数在param_buf的偏移
    static constexpr int MAX_PARAMS=32;
    const uint8_t* param_offsets_[MAX_PARAMS]{};
    int param_count_=0;
};
}//namespace qlog::layout