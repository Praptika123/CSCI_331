/**
 * @file DelimitedFieldBuffer.h
 * @brief Lightweight CSV/DSV reader that tokenizes one record at a time from an input stream.
 *
 * The buffer supports RFC-4180–style quoted fields:
 *   - Fields may be enclosed in double quotes.
 *   - Inside quoted fields, a literal quote is escaped as "" (two quotes).
 *   - Delimiters and newlines inside quoted fields are preserved.
 *
 * Typical usage:
 * @code
 *   DelimitedFieldBuffer buf(',');           // or '\t'
 *   std::string raw;
 *   while (buf.readRecord(in)) {
 *       // access by index
 *       std::string_view state = buf.get( header["State"] );
 *       // convert to number
 *       double lat = buf.getAs<double>( header["Latitude"] );
 *   }
 * @endcode
 */

#ifndef DELIMITEDFIELDBUFFER_H
#define DELIMITEDFIELDBUFFER_H

#include <istream>
#include <string>
#include <string_view>
#include <vector>
#include <stdexcept>
#include <type_traits>
#include <charconv>

/// @class DelimitedFieldBuffer
/// @brief Parses a single delimited line into fields and exposes zero-copy string_view access.
class DelimitedFieldBuffer {
public:
    /// Construct a buffer.
    /// @param delimiter Field separator (default: comma).
    /// @param keep_surrounding_space If true, do not trim unquoted field edges.
    explicit DelimitedFieldBuffer(char delimiter = ',', bool keep_surrounding_space = false);

    /// Change the delimiter at runtime (e.g., ',' to '\t').
    void setDelimiter(char d) noexcept { delimiter_ = d; }

    /// @return Current delimiter.
    char delimiter() const noexcept { return delimiter_; }

    /// @return Number of fields in the most recent record.
    std::size_t size() const noexcept { return fields_.size(); }

    /// @return True if no record is loaded.
    bool empty() const noexcept { return fields_.empty(); }

    /// Clear current record and internal buffers.
    void clear();

    /**
     * @brief Read and parse the next record from @p in.
     *
     * Reads until a logical record terminator (LF/CRLF) that is **not inside a quoted field**.
     * Supports Windows (CRLF), Unix (LF), and old Mac (CR) line endings.
     *
     * @param in Input stream.
     * @return true if a record was read; false on EOF with no data read.
     * @throws std::runtime_error on malformed quote sequences.
     */
    bool readRecord(std::istream& in);

    /**
     * @brief Get field by index as a string_view.
     * @param i Zero-based field index.
     * @return Field view; empty view for empty cell.
     * @throws std::out_of_range if i >= size().
     */
    std::string_view get(std::size_t i) const;

    /**
     * @brief Convert field to a numeric type using std::from_chars.
     * @tparam T arithmetic type (integral or floating).
     * @param i field index.
     * @param fallback value to return if field is empty (default constructs if not provided).
     * @return Parsed value or @p fallback if the field is empty.
     * @throws std::invalid_argument on parse failure; std::out_of_range on index error.
     */
    template <typename T>
    T getAs(std::size_t i, std::optional<T> fallback = std::nullopt) const {
        static_assert(std::is_arithmetic_v<T>, "getAs<T> requires arithmetic type");
        auto sv = get(i);
        if (sv.empty()) {
            if (fallback) return *fallback;
            return T{};
        }
        // from_chars does not accept leading/trailing spaces—trim if we didn't keep spaces
        if (!keep_surrounding_space_) {
            sv = trim(sv);
        }
        T value{};
        auto* first = sv.data();
        auto* last  = sv.data() + sv.size();
        std::from_chars_result res{};
        if constexpr (std::is_floating_point_v<T>) {
            // fallback to stod/stof for portability; from_chars(float) fully supported C++17+ but varies by lib.
            try {
                std::string tmp(sv);
                if constexpr (std::is_same_v<T, float>)      value = std::stof(tmp);
                else if constexpr (std::is_same_v<T, double>) value = std::stod(tmp);
                else                                          value = std::stold(tmp);
            } catch (...) {
                throw std::invalid_argument("Failed to parse floating field at index");
            }
        } else {
            res = std::from_chars(first, last, value);
            if (res.ec != std::errc{} || res.ptr != last) {
                throw std::invalid_argument("Failed to parse integer field at index");
            }
        }
        return value;
    }

private:
    // --- helpers ---
    static std::string_view ltrim(std::string_view s) noexcept;
    static std::string_view rtrim(std::string_view s) noexcept;
    static std::string_view trim(std::string_view s) noexcept { return rtrim(ltrim(s)); }

    void parseIntoFields();          // tokenize current raw_ into fields_
    void pushField(std::size_t beg, std::size_t len, bool was_quoted);

    char delimiter_;
    bool keep_surrounding_space_;

    std::string   raw_;              // raw line buffer (owns memory)
    std::vector<std::string_view> fields_; // views into raw_
};

#endif // DELIMITEDFIELDBUFFER_H
