#include "qlog/config/property_value.hpp"

#include <cassert>
#include <cctype>
#include <cerrno>
#include <cmath>
#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <memory>
#include <utility>

#include "qlog/config/property.hpp"
#include "qlog/utility/string_utils.hpp"

namespace qlog::config {
namespace {
std::string serialize_recursive(std::string key, const PropertyValue& object) {
    std::string line;

    if (object.is_object()) {
        const auto keys = object.get_object_key_set();

        for (const auto& obj_key : keys) {
            std::string new_key = obj_key;

            if (!key.empty()) {
                new_key = key + "." + new_key;
            }

            const auto& value = object[obj_key];

            if (!value.is_object()) {
                line += new_key + "=";
            }

            line += serialize_recursive(new_key, value);

            if (!value.is_object()) {
                line += '\n';
            }
        }
    } else if (object.is_array()) {
        line += '[';

        for (PropertyValue::array_type::size_type i = 0; i < object.array_size(); ++i) {
            const auto& value = object[i];

            line += serialize_recursive("", value);

            if (object.array_size() - i > 1) {
                line += ',';
            }
        }

        line += ']';
    } else if (object.is_null()) {
        line += "null";
    } else if (object.is_bool()) {
        line += static_cast<bool>(object) ? "true" : "false";
    } else if (object.is_decimal()) {
        char fmt[32] = {};

        std::snprintf(fmt, sizeof(fmt), "%lf", static_cast<double>(object));

        line += fmt;
    } else if (object.is_integral()) {
        char fmt[32] = {};

        std::snprintf(fmt, sizeof(fmt), "%" PRId64, static_cast<std::int64_t>(object));

        line += fmt;
    } else if (object.is_string()) {
        const std::string value = static_cast<std::string>(object);

        for (const char c : value) {
            switch (c) {
                case '\n':
                    line += "\\n";
                    break;

                case '=':
                    line += "\\=";
                    break;

                case ':':
                    line += "\\:";
                    break;

                default:
                    line += c;
                    break;
            }
        }
    }

    return line;
}

bool parse_boolean_value(bool& out, const char* str) {
    constexpr std::size_t true_token_size = sizeof("true") - 1;
    constexpr std::size_t false_token_size = sizeof("false") - 1;

    if (std::strncmp("true", str, true_token_size) == 0) {
        out = true;
        return true;
    }

    if (std::strncmp("false", str, false_token_size) == 0) {
        out = false;
        return true;
    }

    return false;
}

bool is_decimal(const char* input_string, std::size_t length) {
    if (length > 1 && input_string[0] == '0' && input_string[1] != '.') {
        return false;
    }

    if (length > 2 && std::strncmp(input_string, "-0", 2) == 0 && input_string[2] != '.') {
        return false;
    }

    while (length--) {
        if (std::strchr("xX", input_string[length]) != nullptr) {
            return false;  // xX 是比较什么？
        }
    }

    return true;
}

bool parse_number_value(double& out, const char* str) {
    if (str == nullptr) {
        return false;
    }

    if (*str < '0' || *str > '9') {
        return false;
    }

    char* end = nullptr;
    errno = 0;

    const double number = std::strtod(str, &end);

    if (errno == ERANGE && (number <= -HUGE_VAL || number >= HUGE_VAL)) {
        return false;
    }

    if ((errno != 0 && errno != ERANGE) || !is_decimal(str, static_cast<std::size_t>(end - str))) {
        return false;
    }

    out = number;
    return true;
}

bool parse_null_value(const char* str) {
    constexpr std::size_t token_size = sizeof("null") - 1;

    return std::strncmp("null", str, token_size) == 0;
}

bool parse_array_key(std::string str) {
    return str.find("[]") != std::string::npos;
}

bool parse_array_value(std::string str) {
    if (str.size() < 2) {
        return false;
    }

    return str.front() == '[' && str.back() == ']';
}

bool try_convert_integral_double(std::int64_t& out, double dvalue) {
    if (std::fmod(dvalue, 1.0) != 0.0) {  // fmod 是什么？
        return false;
    }

    constexpr double int64_upper_bound = 0x1p63;

    if (dvalue < -int64_upper_bound || dvalue >= int64_upper_bound) {
        return false;
    }

    out = static_cast<std::int64_t>(dvalue);
    return true;
}

PropertyValue parse_to_property_value(PropertyValue& root, std::string key,
                                      const std::string& value) {
    PropertyValue pv;
    bool bvalue = false;
    double dvalue = 0.0;
    std::int64_t ivalue = 0;

    if (parse_array_key(key.c_str())) {
        key = key.substr(0, key.size() - 2);

        if (!root[key].is_array()) {
            root[key] = PropertyValue(PropertyValueType::array_type);
        }

        root[key].add_array_item(parse_to_property_value(root, "", value));
        pv = PropertyValue(PropertyValueType::array_type);
    } else if (parse_array_value(value.c_str())) {
        if (!root[key].is_array()) {
            root[key] = PropertyValue(PropertyValueType::array_type);
        }

        if (value.size() >= 3) {
            const std::string new_value = value.substr(1, value.size() - 2);
            const auto values = qlog::utility::split_nonempty(new_value, ',');

            for (const auto& cell_value : values) {
                root[key].add_array_item(
                    parse_to_property_value(root, "", qlog::utility::trim(cell_value)));
            }
        }

        pv = PropertyValue(PropertyValueType::array_type);
    } else if (parse_number_value(dvalue, value.c_str())) {
        if (try_convert_integral_double(ivalue, dvalue)) {
            pv = ivalue;
        } else {
            pv = dvalue;
        }
    } else if (parse_boolean_value(bvalue, value.c_str())) {
        pv = bvalue;
    } else if (parse_null_value(value.c_str())) {
        pv = PropertyValue(PropertyValueType::null_type);
    } else {
        pv = value;
    }
    return pv;
}

void trans_parson_to_property_value(PropertyValue& root, const std::string key,
                                    const std::string& value) {
    const auto keys = qlog::utility::split_nonempty(key, '.');

    switch (keys.size()) {
        case 0:
            return;

        case 1: {
            const std::string new_key = keys[0];
            PropertyValue pv = parse_to_property_value(root, key, value);

            if (!pv.is_array()) {
                root[new_key] = std::move(pv);
            }
            break;
        }

        default: {
            const std::string new_key = key.substr(keys[0].size() + 1);
            trans_parson_to_property_value(root[keys[0]], new_key, value);
            break;
        }
    }
}

}  // namespace

PropertyValue PropertyValue::create_from_string(const std::string& property_string) {
    PropertyValue pv_root;
    Property pbase;
    pbase.load(property_string);

    for (const auto& [key, value] : pbase.maps()) {
        trans_parson_to_property_value(pv_root, key, value);
    }

    return pv_root;
}

void PropertyValue::initialize_data(PropertyValueType value_type) {
    assert(type_ == PropertyValueType::null_type);

    switch ((value_type)) {
        case PropertyValueType::boolean_type:
            std::construct_at(std::addressof(data_.boolean), false);  // 这个是什么语法？
            break;

        case PropertyValueType::integral_type:
            std::construct_at(std::addressof(data_.integral), integral_type{0});
            break;

        case PropertyValueType::decimal_type:
            std::construct_at(std::addressof(data_.decimal), 0.0);
            break;

        case PropertyValueType::string_type:
            std::construct_at(std::addressof(data_.string));
            break;

        case PropertyValueType::array_type:
            std::construct_at(std::addressof(data_.array));
            break;

        case PropertyValueType::object_type:
            std::construct_at(std::addressof(data_.object));
            break;

        case PropertyValueType::null_type:
        case PropertyValueType::invalid_type:
            break;
    }

    type_ = value_type;
}

void PropertyValue::clear_data() {
    switch ((type_)) {
        case PropertyValueType::string_type:
            std::destroy_at(std::addressof(data_.string));
            break;

        case PropertyValueType::array_type:
            std::destroy_at(std::addressof(data_.array));
            break;

        case PropertyValueType::object_type:
            std::destroy_at(std::addressof(data_.object));
            break;

        default:
            break;
    }

    type_ = PropertyValueType::null_type;
}

PropertyValue::PropertyValue(PropertyValueType value_type) {
    initialize_data(value_type);
}

PropertyValue::~PropertyValue() {
    clear_data();
}

void PropertyValue::copy_data_from(const PropertyValue& rhs) {
    assert(type_ == PropertyValueType::null_type);

    switch (rhs.type_) {
        case PropertyValueType::boolean_type:
            std::construct_at(std::addressof(data_.boolean), rhs.data_.boolean);
            break;

        case PropertyValueType::decimal_type:
            std::construct_at(std::addressof(data_.decimal), rhs.data_.decimal);
            break;

        case PropertyValueType::integral_type:
            std::construct_at(std::addressof(data_.integral), rhs.data_.integral);
            break;

        case PropertyValueType::string_type:
            std::construct_at(std::addressof(data_.string), rhs.data_.string);
            break;

        case PropertyValueType::array_type: {
            array_type copied;
            copied.reserve(rhs.data_.array.size());

            for (const auto& item : rhs.data_.array) {
                copied.push_back(std::make_unique<PropertyValue>(*item));
            }

            std::construct_at(std::addressof(data_.array), std::move(copied));
            break;
        }

        case PropertyValueType::object_type: {
            object_type copied;
            copied.reserve(rhs.data_.object.size());

            for (const auto& [key, item] : rhs.data_.object) {
                copied.emplace(key, std::make_unique<PropertyValue>(*item));
            }

            std::construct_at(std::addressof(data_.object), std::move(copied));
            break;
        }

        case PropertyValueType::null_type:
        case PropertyValueType::invalid_type:
            break;
    }
    type_ = rhs.type_;
}

void PropertyValue::move_data_from(PropertyValue&& rhs) {
    assert(type_ == PropertyValueType::null_type);

    switch (rhs.type_) {
        case PropertyValueType::boolean_type:
            std::construct_at(std::addressof(data_.boolean), rhs.data_.boolean);
            break;

        case PropertyValueType::decimal_type:
            std::construct_at(std::addressof(data_.decimal), rhs.data_.decimal);
            break;

        case PropertyValueType::integral_type:
            std::construct_at(std::addressof(data_.integral), rhs.data_.integral);
            break;

        case PropertyValueType::string_type:
            std::construct_at(std::addressof(data_.string), std::move(rhs.data_.string));
            break;

        case PropertyValueType::array_type:
            std::construct_at(std::addressof(data_.array), std::move(rhs.data_.array));
            break;

        case PropertyValueType::object_type:
            std::construct_at(std::addressof(data_.object), std::move(rhs.data_.object));
            break;

        case PropertyValueType::null_type:
        case PropertyValueType::invalid_type:
            break;
    }

    type_ = rhs.type_;
}

PropertyValue::PropertyValue(const PropertyValue& rhs) {
    copy_data_from(rhs);
}

PropertyValue::PropertyValue(PropertyValue&& rhs) noexcept {
    move_data_from(std::move(rhs));
}

PropertyValue& PropertyValue::operator=(const PropertyValue& rhs) {
    if (this == &rhs) {
        return *this;
    }  // CR:如果不判断 都进行下一步 会怎么样？

    PropertyValue copied(rhs);
    clear_data();
    move_data_from(std::move(copied));
    return *this;
}

PropertyValue& PropertyValue::operator=(PropertyValue&& rhs) noexcept {
    if (this == &rhs) {
        return *this;
    }

    clear_data();
    move_data_from(std::move(rhs));
    return *this;
}

PropertyValue& PropertyValue::operator=(bool_type value) {
    if (is_bool()) {
        data_.boolean = value;
    } else {
        clear_data();
        std::construct_at(std::addressof(data_.boolean), value);
        type_ = PropertyValueType::boolean_type;
    }

    return *this;
}

PropertyValue& PropertyValue::operator=(decimal_type value) {
    if (is_decimal()) {
        data_.decimal = value;
    } else {
        clear_data();
        std::construct_at(std::addressof(data_.decimal), value);
        type_ = PropertyValueType::decimal_type;
    }

    return *this;
}

PropertyValue& PropertyValue::operator=(integral_type value) {
    if (is_integral()) {
        data_.integral = value;
    } else {
        clear_data();
        std::construct_at(std::addressof(data_.integral), value);
        type_ = PropertyValueType::integral_type;
    }

    return *this;
}

PropertyValue& PropertyValue::operator=(const string_type& value) {
    if (is_string()) {
        data_.string = value;
    } else {
        clear_data();
        std::construct_at(std::addressof(data_.string), value);
        type_ = PropertyValueType::string_type;
    }

    return *this;
}

PropertyValue& PropertyValue::operator=(string_type&& value) {
    if (is_string()) {
        data_.string = std::move(value);
    } else {
        clear_data();
        std::construct_at(std::addressof(data_.string), std::move(value));
        type_ = PropertyValueType::string_type;
    }

    return *this;
}

PropertyValue& PropertyValue::operator=(const char* value) {
    return *this = string_type(value);
}

PropertyValue& PropertyValue::operator=(std::nullptr_t value) {
    (void)value;
    clear_data();
    return *this;
}

PropertyValue::operator string_type() const {
    // CR: operator 是什么返回类型？
    assert(is_string());
    return data_.string;
}

PropertyValue::operator bool_type() const {
    assert(is_bool());
    return data_.boolean;
}

PropertyValue::operator decimal_type() const {
    assert(is_decimal());
    return data_.decimal;
}

PropertyValue::operator integral_type() const {
    assert(is_integral());
    return data_.integral;
}

PropertyValue::operator std::uint64_t() const {
    assert(is_integral());
    return static_cast<std::uint64_t>(data_.integral);
}

PropertyValue::operator std::int32_t() const {
    assert(is_integral());
    return static_cast<std::int32_t>(data_.integral);
}

PropertyValue::operator std::uint32_t() const {
    assert(is_integral());
    return static_cast<std::uint32_t>(data_.integral);
}
// CR: 为什么没有int64 u/int 16 /8
const PropertyValue& PropertyValue::get_null_value() {
    static const PropertyValue value;
    return value;
}

const PropertyValue& PropertyValue::null() {
    return get_null_value();
}

const PropertyValue& PropertyValue::operator[](const string_type& name) const {
    if (!is_object()) {
        return get_null_value();
    }

    const auto it = data_.object.find(name);
    if (it == data_.object.end()) {
        return get_null_value();
    }

    return *it->second;
}

PropertyValue& PropertyValue::operator[](const string_type& name) {
    if (!is_object()) {
        clear_data();
        initialize_data(PropertyValueType::object_type);
    }

    auto it = data_.object.find(name);
    if (it == data_.object.end()) {
        it = data_.object.emplace(name, std::make_unique<PropertyValue>()).first;
    }

    return *it->second;
}

const PropertyValue& PropertyValue::operator[](array_type::size_type idx) const {
    if (!is_array() || idx >= data_.array.size()) {
        return get_null_value();
    }

    return *data_.array[idx];
}

PropertyValue& PropertyValue::operator[](array_type::size_type idx) {
    if (!is_array()) {
        clear_data();
        initialize_data(PropertyValueType::array_type);
    }

    while (data_.array.size() <= idx) {
        data_.array.push_back(std::make_unique<PropertyValue>());
    }
    return *data_.array[idx];
}

PropertyValue& PropertyValue::add_null_array_item() {
    if (!is_array()) {
        return (*this)[array_type::size_type{0}];
    }

    return (*this)[data_.array.size()];
}

PropertyValue& PropertyValue::add_null_object_item(const string_type& key) {
    return (*this)[key];  // CR:如果this不加（）呢？
}

PropertyValue& PropertyValue::add_array_item(const string_type& key,
                                             std::vector<PropertyValue>& value) {
    auto& obj = (*this)[key];
    obj.set_array(value);
    return obj;
}

void PropertyValue::set_array(std::vector<PropertyValue>& items) {
    for (auto& item : items) {
        add_array_item(item);
    }
}

void PropertyValue::erase_object_item(const string_type& key) {
    assert(is_object());
    data_.object.erase(key);  // CR: bqlog如何做的 erase开销不小呢？
}

void PropertyValue::erase_array_item(array_type::size_type idx) {
    assert(is_array());

    if (idx == static_cast<array_type::size_type>(-1)) {
        data_.array.clear();
        return;
    }

    assert(idx < data_.array.size());

    data_.array.erase(data_.array.begin() +
                      static_cast<array_type::difference_type>(idx));  // difference type是什么
}

void PropertyValue::clear_array_item() {
    if (is_null()) {
        add_null_array_item();  // 这是什么 加入非数组 元素嘛？
    }  // 这个函数不是清除数组元素嘛

    assert(is_array());
    data_.array.clear();
}

bool PropertyValue::has_object_key(const string_type& name) const {
    if (!is_object()) {
        return false;
    }

    return !(*this)[name].is_null();
}

std::vector<PropertyValue::string_type> PropertyValue::get_object_key_set() const {
    std::vector<string_type> result;

    if (!is_object()) {
        return result;
    }

    result.reserve(data_.object.size());

    for (const auto& item : data_.object) {
        result.push_back(item.first);
    }

    return result;
}

PropertyValue::array_type::size_type PropertyValue::array_size() const {
    return is_array() ? data_.array.size() : 0;
}

PropertyValue::object_type::size_type PropertyValue::object_size() const {
    return is_object() ? data_.object.size() : 0;
}

std::string PropertyValue::serialize() const {
    return serialize_recursive("", *this);
}
}  // namespace qlog::config