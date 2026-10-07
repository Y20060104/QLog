#pragma once

#include <cinttypes>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <memory>
#include <string>
#include <unordered_map>
#include <utility>
#include <vector>

namespace qlog::config {
enum class PropertyValueType {
    boolean_type,
    decimal_type,
    integral_type,
    string_type,
    array_type,
    object_type,
    null_type,
    invalid_type,
};

class PropertyValue {
   public:
    using bool_type = bool;
    using decimal_type = double;
    using integral_type = std::int64_t;
    using string_type = std::string;

    using array_type = std::vector<std::unique_ptr<PropertyValue>>;

    using object_type = std::unordered_map<string_type, std::unique_ptr<PropertyValue>>;

    PropertyValue(PropertyValueType value_type = PropertyValueType::null_type);

    PropertyValue(const PropertyValue& rhs);
    PropertyValue(PropertyValue&& rhs) noexcept;
    ~PropertyValue();

    PropertyValue& operator=(const PropertyValue& rhs);
    PropertyValue& operator=(PropertyValue&& rhs) noexcept;

    PropertyValue& operator=(bool_type value);
    PropertyValue& operator=(decimal_type value);
    PropertyValue& operator=(integral_type value);
    PropertyValue& operator=(std::nullptr_t value);
    PropertyValue& operator=(const char* value);
    PropertyValue& operator=(const string_type& value);
    PropertyValue& operator=(string_type&& value);

    operator string_type() const;
    explicit operator bool_type() const;
    explicit operator decimal_type() const;
    explicit operator integral_type() const;
    explicit operator std::uint64_t() const;
    explicit operator std::int32_t() const;
    explicit operator std::uint32_t() const;

    static const PropertyValue& null();

    PropertyValueType get_type() const {
        return type_;
    }

    bool is_null() const {
        return type_ == PropertyValueType::null_type;
    }

    bool is_bool() const {
        return type_ == PropertyValueType::boolean_type;
    }

    bool is_decimal() const {
        return type_ == PropertyValueType::decimal_type;
    }

    bool is_integral() const {
        return type_ == PropertyValueType::integral_type;
    }

    bool is_string() const {
        return type_ == PropertyValueType::string_type;
    }

    bool is_array() const {
        return type_ == PropertyValueType::array_type;
    }

    bool is_object() const {
        return type_ == PropertyValueType::object_type;
    }

    const PropertyValue& operator[](const string_type& name) const;
    PropertyValue& operator[](const string_type& name);

    const PropertyValue& operator[](array_type::size_type idx) const;
    PropertyValue& operator[](array_type::size_type idx);

    template <typename T>
    PropertyValue& add_array_item(T&& value) {
        auto& new_item = add_null_array_item();
        new_item = std::forward<T>(value);
        return new_item;
    }

    template <typename T>
    PropertyValue& add_array_item(const string_type& key, T&& value) {
        auto& obj = (*this)[key];
        obj.add_array_item(std::forward<T>(value));
        return obj;
    }

    PropertyValue& add_array_item(const string_type& key, std::vector<PropertyValue>& value);

    void set_array(std::vector<PropertyValue>& items);
    void erase_array_item(array_type::size_type idx);
    void clear_array_item();

    std::string serialize() const;

    static PropertyValue create_from_string(const std::string& property_string);

    template <typename T>
    PropertyValue& add_object_item(const string_type& key, T&& value) {
        auto& new_item = add_null_object_item(key);
        new_item = std::forward<T>(value);
        return new_item;
    }

    void erase_object_item(const string_type& key);

    bool has_object_key(const string_type& name) const;
    std::vector<string_type> get_object_key_set() const;

    array_type::size_type array_size() const;
    object_type::size_type object_size() const;

   private:
    union Storage {
        bool_type boolean;
        decimal_type decimal;
        integral_type integral;
        string_type string;
        array_type array;
        object_type object;

        Storage() noexcept {}
        ~Storage() noexcept {}
    };

    void initialize_data(PropertyValueType value_type);
    void clear_data();
    void copy_data_from(const PropertyValue& rhs);
    void move_data_from(PropertyValue&& rhs);

    static const PropertyValue& get_null_value();

    PropertyValue& add_null_array_item();
    PropertyValue& add_null_object_item(const string_type& key);

    PropertyValueType type_ = PropertyValueType::null_type;
    Storage data_;
};
}  // namespace qlog::config