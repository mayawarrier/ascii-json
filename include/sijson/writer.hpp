
#ifndef SIJSON_WRITER_HPP
#define SIJSON_WRITER_HPP

#include <cstddef>
#include <cstdint>
#include <cassert>
#include <cmath>
#include <limits>
#include <ios>
#include <ostream>
#include <locale>
#include <string>
#include <utility>
#include <stdexcept>

#include "internal/util.hpp"
#include "internal/buffers.hpp"
#include "internal/impl_rw.hpp"

#include "common.hpp"
#include "number.hpp"
#include "stringstream.hpp"
#include "stdstream.hpp"


namespace sijson {

// Low-level ASCII JSON writer.
template <typename Ostream>
class raw_ascii_writer
{
private:
    using JsonOstream = wrap_std_ostream_t<Ostream>;

public:
    using stream_type = JsonOstream;

public:
    raw_ascii_writer(Ostream& stream) :
        m_stream(stream)
    {}

    inline void write_start_object(void) { m_stream.put('{'); }
    inline void write_end_object(void) { m_stream.put('}'); }
    inline void write_start_array(void) { m_stream.put('['); }
    inline void write_end_array(void) { m_stream.put(']'); }
    inline void write_key_separator(void) { m_stream.put(':'); }
    inline void write_item_separator(void) { m_stream.put(','); }

    inline void write_int32(std::int_least32_t value) { write_int_impl(m_stream, value); }
    inline void write_int64(std::int_least64_t value) { write_int_impl(m_stream, value); }
    inline void write_uint32(std::uint_least32_t value) { write_uint_impl(m_stream, value); }
    inline void write_uint64(std::uint_least64_t value) { write_uint_impl(m_stream, value); }

    inline void write_float(float value) { write_floating_impl(m_stream, value); }
    inline void write_double(double value) { write_floating_impl(m_stream, value); }

    inline void write_number(number value) { write_number_impl(m_stream, value); }

    inline void write_bool(bool value)
    {
        if (value) 
            m_stream.putn("true", sizeof("true") - 1);
        else m_stream.putn("false", sizeof("false") - 1);
    }

    inline void write_null(void) { m_stream.putn("null", sizeof("null") - 1); }

    // Write string from input stream.
    // String is escaped.
    // If quoted is false, the string is 
    // not delimited by quotes.
    template <typename Istream>
    void write_string_from(Istream& is, bool quoted = true);

    // Write string or null.
    inline void write_string(const char* value)
    {
        if (value)
        {
            icsstream is(value);
            write_string_from(is);
        }
        else write_null();
    }

    // Write string or null.
    inline void write_string(const char* value, std::size_t length)
    {
        if (value)
        {
            isstream is(value, length);
            write_string_from(is);
        }
        else write_null();
    }

    // Write string.
    template <typename ...Ts>
    inline void write_string(const std::basic_string<char, Ts...>& value)
    {
        isstream is(value.c_str(), value.length());
        write_string_from(is);
    }

    // Write value of type T.
    //
    // Supports all types with named write functions in this class
    // (write_float(), write_double(), etc.), and all remaining integral types.
    // Char types (e.g. char16_t, char32_t etc.) are written as integers.
    //
    template <typename T>
    void write(const T& value)
    {
        write_t_impl::write(*this, value);
    }

    inline void write_newline(void) { m_stream.put('\n'); }

    inline void write_whitespace(std::size_t num_spaces)
    {
        m_stream.put(' ', num_spaces);
    }

    // Get stream.
    inline stream_type& stream(void) noexcept { return m_stream; }

    ~raw_ascii_writer(void) {
        try { m_stream.flush(); } catch (...) {}
    }

private:
    template <typename UintT>
    static inline void write_uint_impl(JsonOstream& stream, UintT value);
    template <typename IntT>
    static inline void write_int_impl(JsonOstream& stream, IntT value);
    template <typename FloatT>
    static inline void write_floating_impl(JsonOstream& stream, FloatT value);
    static inline void write_number_impl(JsonOstream& stream, number value);

    struct write_t_impl
    {
        template <typename T, iutil::require_t<iutil::is_nb_signed_integral<T>::value> = 0>
        static inline void write(raw_ascii_writer& w, T val) { write_int_impl(w.m_stream, val); }

        template <typename T, iutil::require_t<iutil::is_nb_unsigned_integral<T>::value> = 0>
        static inline void write(raw_ascii_writer& w, T val) { write_uint_impl(w.m_stream, val); }

        template <typename ...Ts>
        static inline void write(raw_ascii_writer& w, const std::basic_string<char, Ts...>& val) { w.write_string(val); }

        static inline void write(raw_ascii_writer& w, float val) { w.write_float(val); }
        static inline void write(raw_ascii_writer& w, double val) { w.write_double(val); }
        static inline void write(raw_ascii_writer& w, number val) { w.write_number(val); }
        static inline void write(raw_ascii_writer& w, bool val) { w.write_bool(val); }
        static inline void write(raw_ascii_writer& w, const char* val) { w.write_string(val); }
        static inline void write(raw_ascii_writer& w, std::nullptr_t) { w.write_null(); }
    };

private:
    wrap_std_ostream_t<Ostream&> m_stream;
};



// ASCII JSON writer.
template <typename Ostream, 
    // Allocator type used for internal purposes/book-keeping.
    typename AllocatorPolicy = std::allocator<void>>
class ascii_writer : public internal::rw_base<AllocatorPolicy>
{
public:
    ascii_writer(Ostream& stream) :
        m_rw{ stream }
    {
        this->m_nodes.push({ DOCNODE_root });
    }

    // Get parent node.
    // For eg. if you call start_object(), parent_node()
    // returns DOCNODE_object until the next call to 
    // end_object() or start_array().
    inline doc_node_t parent_node(void) const { return this->m_nodes.top().type; }

    // Start writing object.
    inline void start_object(void)
    {
        this->template start_node<DOCNODE_object>([&] {
            write_separator();
            m_rw.write_start_object();
        });
    }

    // Start writing array.
    inline void start_array(void)
    {
        this->template start_node<DOCNODE_array>([&] {
            write_separator();
            m_rw.write_start_array();
        });
    }

    // End writing object.
    inline void end_object(void)
    {
        this->template end_node<DOCNODE_object>([&] 
        { m_rw.write_end_object(); });
    }

    // End writing array.
    inline void end_array(void)
    {
        this->template end_node<DOCNODE_array>([&] 
        { m_rw.write_end_array(); });
    }

    // Write object key.
    inline void write_key(const char* key)
    {
        write_key_impl([&] { m_rw.write_string(key); }, !key);
    }

    // Write object key.
    inline void write_key(const char* key, std::size_t length)
    {
        write_key_impl([&] { m_rw.write_string(key, length); }, !key);
    }

    // Write object key.
    template <typename ...Ts>
    inline void write_key(const std::basic_string<char, Ts...>& key)
    {
        write_key(key.c_str(), key.length());
    }

    // Write value.
    template <typename Value>
    inline void write_value(const Value& value)
    {
        write_value_impl<iutil::decay_array_to_constptr_t<Value>>(value);
    }

    // Write object key-value pair.
    template <typename Value>
    inline void write_key_value(const char* key, const Value& value)
    {
        write_key_value_impl<iutil::decay_array_to_constptr_t<Value>>(
            [&] { m_rw.write_string(key); }, !key, value);
    }

    // Write object key-value pair.
    template <typename Value>
    inline void write_key_value(const char* key, std::size_t key_length, const Value& value)
    {
        write_key_value_impl<iutil::decay_array_to_constptr_t<Value>>(
            [&] { m_rw.write_string(key, key_length); }, !key, value);
    }

    // Write object key-value pair.
    template <typename Value, typename ...Ts>
    inline void write_key_value(const std::basic_string<char, Ts...>& key, const Value& value)
    {
        write_key_value(key.c_str(), key.length(), value);
    }

    // Write object key-value pair.
    // Key must be a std::basic_string.
    template <typename Key, typename Value>
    inline void write_key_value(const std::pair<Key, Value>& kv)
    {
        static_assert(iutil::is_instance_of_basic_string<Key, char>::value,
            "Key must be a std::basic_string.");

        write_key_value(kv.first.c_str(), kv.first.length(), kv.second);
    }

    // Write a new line.
    inline void write_newline(void) { m_rw.write_newline(); }

    // Write indentation.
    inline void write_whitespace(std::size_t num_spaces) { m_rw.write_whitespace(num_spaces); }

    // Get stream position.
    inline std::size_t outpos(void) { return m_rw.stream().outpos(); }

private:
    inline void write_separator(void);

    template <typename Func>
    inline void write_key_impl(Func do_write, bool is_null);

    template <typename Value>
    inline void write_value_impl(const Value& value);

    template <typename Value, typename Func>
    inline void write_key_value_impl(Func do_write_key, bool is_key_null, const Value& value);

private:
    raw_ascii_writer<Ostream> m_rw;
};



template <typename Ostream>
template <typename UintT>
inline void raw_ascii_writer<Ostream>::write_uint_impl(JsonOstream& stream, UintT value)
{
    char strbuf[iutil::max_chars10<UintT>::value];
    const auto strbuf_end = strbuf + sizeof(strbuf);

    auto strp = strbuf_end;
    do {
        // units first
        *(--strp) = '0' + (char)(value % 10);
        value /= 10;
    } while (value != 0);

    stream.putn(strp, strbuf_end - strp);
}

template <typename Ostream>
template <typename IntT>
inline void raw_ascii_writer<Ostream>::write_int_impl(JsonOstream& stream, IntT value)
{
    char strbuf[iutil::max_chars10<IntT>::value];
    const auto strbuf_end = strbuf + sizeof(strbuf);

    auto abs_value = iutil::absu(value);
    auto strp = strbuf_end;
    do {
        // units first
        *(--strp) = '0' + (char)(abs_value % 10);
        abs_value /= 10;
    } while (abs_value != 0);

    if (value < 0)
        *(--strp) = '-';

    stream.putn(strp, strbuf_end - strp);
}

template <typename Ostream>
template <typename FloatT>
inline void raw_ascii_writer<Ostream>::write_floating_impl(JsonOstream& stream, FloatT value)
{
    if (!std::isfinite(value))
        throw std::invalid_argument("Value is NAN or infinity.");

    char strbuf[iutil::max_chars10<FloatT>::value];
    internal::memspanbuf streambuf(strbuf, std::ios_base::out);

    std::ostream sstream(&streambuf);
    sstream.imbue(std::locale::classic()); // make decimal point '.'
    sstream.precision(std::numeric_limits<FloatT>::max_digits10);
    sstream << value;

    auto strdata = streambuf.data();
    stream.putn(strdata.begin, strdata.size());
}

template <typename Ostream>
inline void raw_ascii_writer<Ostream>::write_number_impl(JsonOstream& stream, number value)
{
    switch (value.type())
    {
        case number::TYPE_float:
            write_floating_impl(stream, value.get_nothrow<float>());
            break;
        case number::TYPE_double:
            write_floating_impl(stream, value.get_nothrow<double>());
            break;
        case number::TYPE_intmax_t:
            write_int_impl(stream, value.get_nothrow<std::intmax_t>());
            break;
        case number::TYPE_uintmax_t:
            write_uint_impl(stream, value.get_nothrow<std::uintmax_t>());
            break;
        default:
            assert(false); break;
    }
}

template <typename Ostream>
template <typename Istream>
inline void raw_ascii_writer<Ostream>::write_string_from(Istream& is, bool quoted)
{
    if (quoted)
        m_stream.put('"');
    while (!is.end())
    {
        char c = is.take();
        switch (c)
        {
            case '\b': m_stream.putn("\\b", 2); break;
            case '\f': m_stream.putn("\\f", 2); break;
            case '\n': m_stream.putn("\\n", 2); break;
            case '\r': m_stream.putn("\\r", 2); break;
            case '\t': m_stream.putn("\\t", 2); break;
            case '"':  m_stream.putn("\\\"", 2); break;
            case '\\': m_stream.putn("\\\\", 2); break;

            default: m_stream.put(c); break;
        }
    }
    if (quoted)
        m_stream.put('"');
}

template <typename Ostream, typename AllocatorPolicy>
inline void ascii_writer<Ostream, AllocatorPolicy>::write_separator(void)
{
    if (this->m_nodes.top().has_children)
    {
        switch (this->m_nodes.top().type)
        {
            case DOCNODE_object:
            case DOCNODE_array: m_rw.write_item_separator(); break;
            case DOCNODE_root: throw std::runtime_error(internal::EXSTR_multi_root);
            default: assert(false);
        }
    }
    // key node is popped before it can have children
    else if (this->m_nodes.top().type == DOCNODE_key)
        m_rw.write_key_separator();
}

template <typename Ostream, typename AllocatorPolicy>
template <typename Func>
inline void ascii_writer<Ostream, AllocatorPolicy>::write_key_impl(Func do_write, bool is_null)
{
    if (is_null) 
        throw std::invalid_argument("Key is null.");

    this->template assert_rule<DOCNODE_key>();

    write_separator();
    do_write();

    this->m_nodes.push({ DOCNODE_key });
    // don't end_child_node(), key-value pair is incomplete
}

template <typename Ostream, typename AllocatorPolicy>
template <typename Value>
inline void ascii_writer<Ostream, AllocatorPolicy>::write_value_impl(const Value& value)
{
    this->template assert_rule<DOCNODE_value>();

    write_separator();
    m_rw.write(value);

    this->end_child_node();
}

template <typename Ostream, typename AllocatorPolicy>
template <typename Value, typename Func>
inline void ascii_writer<Ostream, AllocatorPolicy>::write_key_value_impl(
    Func do_write_key, bool is_key_null, const Value& value)
{
    if (is_key_null) 
        throw std::invalid_argument("Key is null.");

    this->template assert_rule<DOCNODE_key, DOCNODE_value>();

    if (this->m_nodes.top().has_children)
        m_rw.write_item_separator();

    do_write_key();
    m_rw.write_key_separator();
    m_rw.write(value);

    this->end_child_node();
}
}
#endif
