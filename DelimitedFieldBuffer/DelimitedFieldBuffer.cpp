/**
 * @file DelimitedFieldBuffer.cpp
 */

#include "DelimitedFieldBuffer.h"
#include <limits>

DelimitedFieldBuffer::DelimitedFieldBuffer(char delimiter, bool keep_surrounding_space)
    : delimiter_(delimiter),
      keep_surrounding_space_(keep_surrounding_space) {}

void DelimitedFieldBuffer::clear() {
    raw_.clear();
    fields_.clear();
}

std::string_view DelimitedFieldBuffer::ltrim(std::string_view s) noexcept {
    std::size_t i = 0;
    while (i < s.size() && (s[i] == ' ' || s[i] == '\t')) ++i;
    return s.substr(i);
}

std::string_view DelimitedFieldBuffer::rtrim(std::string_view s) noexcept {
    std::size_t n = s.size();
    while (n > 0 && (s[n - 1] == ' ' || s[n - 1] == '\t')) --n;
    return s.substr(0, n);
}

bool DelimitedFieldBuffer::readRecord(std::istream& in) {
    clear();
    if (!in.good()) return false;

    raw_.reserve(256);
    bool in_quotes = false;
    bool any = false;

    for (;;) {
        int ch = in.get();
        if (ch == std::char_traits<char>::eof()) {
            if (!any) return false; // true EOF
            break;                  // finalize last record
        }
        any = true;
        char c = static_cast<char>(ch);

        if (c == '"') {
            in_quotes = !in_quotes;
            raw_.push_back(c);
            // Peek for doubled quote inside quoted field: "" -> keep both, stays inside quotes
            if (in_quotes) {
                // just entered quotes, nothing else to do right now
            } else {
                // just exited quotes, nothing else here; parsing will resolve doubled quotes
            }
            continue;
        }

        if ((c == '\n') || (c == '\r')) {
            if (in_quotes) {
                raw_.push_back(c); // newline inside quotes belongs to field
                continue;
            }
            // consume the other half of CRLF
            if (c == '\r' && in.peek() == '\n') (void)in.get();
            break; // end of logical record
        }

        raw_.push_back(c);
    }

    // Parse fields from raw_
    parseIntoFields();
    return true;
}

void DelimitedFieldBuffer::parseIntoFields() {
    fields_.clear();
    const char* data = raw_.data();
    const std::size_t N = raw_.size();

    std::size_t i = 0;
    while (i <= N) {
        bool quoted = false;
        std::size_t start = i;
        std::string field_accum;
        // Detect quoted field
        if (i < N && data[i] == '"') {
            quoted = true;
            ++i; // skip opening quote
            start = i;
            while (i < N) {
                if (data[i] == '"') {
                    if (i + 1 < N && data[i + 1] == '"') {
                        // doubled quote -> literal quote
                        field_accum.append(data + start, i - start);
                        field_accum.push_back('"');
                        i += 2;
                        start = i;
                    } else {
                        // closing quote
                        field_accum.append(data + start, i - start);
                        ++i; // skip closing quote
                        break;
                    }
                } else {
                    ++i;
                }
            }
            // After quoted field, next char must be delimiter or end
            while (i < N && data[i] != delimiter_) {
                // Allow spaces/tabs after closing quote before delimiter
                if (data[i] == ' ' || data[i] == '\t') { ++i; continue; }
                // Anything else is malformed
                // To keep it forgiving, include it as part of field
                field_accum.push_back(data[i]);
                ++i;
            }
            // Now at delimiter or end
            if (i < N && data[i] == delimiter_) ++i; // consume delimiter
            // Store accumulated (needs owning memory). We place into raw_ to keep string_view stable:
            std::size_t offset = raw_.size();
            raw_.append(field_accum);
            fields_.emplace_back(raw_.data() + offset, field_accum.size());
        } else {
            // Unquoted field: read until delimiter or end
            start = i;
            while (i < N && data[i] != delimiter_) ++i;
            std::size_t len = i - start;
            if (!keep_surrounding_space_) {
                std::string_view sv(data + start, len);
                sv = trim(sv);
                fields_.push_back(sv);
            } else {
                fields_.emplace_back(data + start, len);
            }
            if (i < N && data[i] == delimiter_) ++i; // consume delimiter
        }
    }

    // Corner case: if line ends with a delimiter, there is a trailing empty field.
    // The loop above already accounts for that by producing an empty token at the end.
}

std::string_view DelimitedFieldBuffer::get(std::size_t i) const {
    if (i >= fields_.size()) throw std::out_of_range("field index out of range");
    return fields_[i];
}

