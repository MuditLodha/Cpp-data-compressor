#ifndef HFT_TICKDATA_CODEC_HPP
#define HFT_TICKDATA_CODEC_HPP

#include <cstdint>
#include <cstdio>
#include <cstring>
#include <string>
#include <vector>

namespace tickdata {

constexpr int kDefaultPriceScalePow = 4;
constexpr char kMagicData[4] = {'H', 'F', 'T', 'D'};
constexpr char kMagicDepth[4] = {'H', 'F', 'T', 'B'};
constexpr std::uint64_t kVersion = 1;

struct FieldSpan {
    const char* start;
    const char* end;
};



inline std::int64_t pow10_int(int pow) {
    std::int64_t value = 1;
    for (int i = 0; i < pow; ++i) {
        value *= 10;
    }
    return value;
}

inline std::int64_t parse_int64(const char* start, const char* end) {
    if (start >= end) {
        return 0;
    }
    bool neg = (*start == '-');
    if (neg) {
        ++start;
    }
    std::int64_t value = 0;
    for (const char* p = start; p < end; ++p) {
        char c = *p;
        if (c < '0' || c > '9') {
            break;
        }
        value = value * 10 + (c - '0');
    }
    return neg ? -value : value;
}

inline std::int64_t parse_scaled_decimal(const char* start, const char* end, int scale_pow) {
    if (start >= end) {
        return 0;
    }
    bool neg = (*start == '-');
    if (neg) {
        ++start;
    }
    std::int64_t int_part = 0;
    std::int64_t frac_part = 0;
    int frac_digits = 0;
    bool in_frac = false;
    for (const char* p = start; p < end; ++p) {
        char c = *p;
        if (c == '.') {
            in_frac = true;
            continue;
        }
        if (c < '0' || c > '9') {
            break;
        }
        if (!in_frac) {
            int_part = int_part * 10 + (c - '0');
        } else if (frac_digits < scale_pow) {
            frac_part = frac_part * 10 + (c - '0');
            ++frac_digits;
        }
    }
    while (frac_digits < scale_pow) {
        frac_part *= 10;
        ++frac_digits;
    }
    std::int64_t scale = pow10_int(scale_pow);
    std::int64_t value = int_part * scale + frac_part;
    return neg ? -value : value;
}

inline void append_int(std::string& out, std::int64_t value) {
    char buffer[32];
    char* p = buffer + sizeof(buffer);
    std::uint64_t v = static_cast<std::uint64_t>(value < 0 ? -value : value);
    do {
        *--p = static_cast<char>('0' + (v % 10));
        v /= 10;
    } while (v);
    if (value < 0) {
        *--p = '-';
    }
    out.append(p, buffer + sizeof(buffer) - p);
}

inline void append_scaled(std::string& out, std::int64_t value, int scale_pow) {
    if (value == 0) {
        out.push_back('0');
        return;
    }
    bool neg = value < 0;
    if (neg) {
        value = -value;
        out.push_back('-');
    }
    std::int64_t scale = pow10_int(scale_pow);
    std::int64_t int_part = value / scale;
    std::int64_t frac = value % scale;
    append_int(out, int_part);
    if (frac == 0) {
        return;
    }
    out.push_back('.');
    char frac_buf[32];
    for (int i = scale_pow - 1; i >= 0; --i) {
        frac_buf[i] = static_cast<char>('0' + (frac % 10));
        frac /= 10;
    }
    int end = scale_pow;
    while (end > 0 && frac_buf[end - 1] == '0') {
        --end;
    }
    out.append(frac_buf, frac_buf + end);
}

class FastLineReader {
public:
// [Your CSV File on Disk - 10GB]
//     ↓
// [Read 1MB chunk into RAM Buffer]
//     ↓
// [Process data FROM Buffer] ← Much faster than disk!
//     ↓
// [When Buffer empty, read next 1MB]
    explicit FastLineReader(const char* path, std::size_t buf_size = 1 << 20)
        : file_(std::fopen(path, "rb")), buffer_(buf_size), pos_(0), len_(0) {}
    ~FastLineReader() {
        if (file_) {
            std::fclose(file_);
        }
    }
    bool ok() const { return file_ != nullptr; }
    bool read_line(std::string& out) {
        if (!file_) {
            return false;
        }
        out.clear();
        while (true) {
            if (pos_ >= len_) {
                len_ = std::fread(buffer_.data(), 1, buffer_.size(), file_);
                pos_ = 0;
                if (len_ == 0) {
                    return !out.empty();
                }
            }
            const char* base = buffer_.data() + pos_;
            const void* nl_ptr = std::memchr(base, '\n', len_ - pos_);
            if (nl_ptr) {
                std::size_t chunk = static_cast<const char*>(nl_ptr) - base;
                out.append(base, chunk);
                pos_ += chunk + 1;
                if (!out.empty() && out.back() == '\r') {
                    out.pop_back();
                }
                return true;
            }
            std::size_t chunk = len_ - pos_;
            out.append(base, chunk);
            pos_ = len_;
        }
    }

private:
    std::FILE* file_;
    std::vector<char> buffer_;
    std::size_t pos_;
    std::size_t len_;
};

class ByteWriter {
public:
    explicit ByteWriter(const char* path, std::size_t buf_size = 1 << 20)
        : file_(std::fopen(path, "wb")), buffer_(buf_size), pos_(0) {}
    ~ByteWriter() {
        flush();
        if (file_) {
            std::fclose(file_);
        }
    }
    bool ok() const { return file_ != nullptr; }
    void write_byte(std::uint8_t value) {
        buffer_[pos_++] = value;
        if (pos_ == buffer_.size()) {
            flush();
        }
    }
    void write_bytes(const void* data, std::size_t len) {
        if (len >= buffer_.size()) {
            flush();
            std::fwrite(data, 1, len, file_);
            return;
        }
        if (pos_ + len > buffer_.size()) {
            flush();
        }
        std::memcpy(buffer_.data() + pos_, data, len);
        pos_ += len;
    }
    void flush() {
        if (file_ && pos_ > 0) {
            std::fwrite(buffer_.data(), 1, pos_, file_);
            pos_ = 0;
        }
    }

private:
    std::FILE* file_;
    std::vector<std::uint8_t> buffer_;
    std::size_t pos_;
};

class ByteReader {
public:
    explicit ByteReader(const char* path, std::size_t buf_size = 1 << 20)
        : file_(std::fopen(path, "rb")), buffer_(buf_size), pos_(0), len_(0) {}
    ~ByteReader() {
        if (file_) {
            std::fclose(file_);
        }
    }
    bool ok() const { return file_ != nullptr; }
    int read_byte() {
        if (pos_ >= len_) {
            len_ = std::fread(buffer_.data(), 1, buffer_.size(), file_);
            pos_ = 0;
            if (len_ == 0) {
                return -1;
            }
        }
        return buffer_[pos_++];
    }
    bool read_bytes(void* out, std::size_t len) {
        std::size_t read_total = 0;
        auto* dst = static_cast<std::uint8_t*>(out);
        while (read_total < len) {
            if (pos_ >= len_) {
                len_ = std::fread(buffer_.data(), 1, buffer_.size(), file_);
                pos_ = 0;
                if (len_ == 0) {
                    return false;
                }
            }
            std::size_t chunk = len_ - pos_;
            std::size_t need = len - read_total;
            if (chunk > need) {
                chunk = need;
            }
            std::memcpy(dst + read_total, buffer_.data() + pos_, chunk);
            pos_ += chunk;
            read_total += chunk;
        }
        return true;
    }

private:
    std::FILE* file_;
    std::vector<std::uint8_t> buffer_;
    std::size_t pos_;
    std::size_t len_;
};

// 2️⃣ What zigzag encoding does (conceptually)

// Zigzag mapping:

// Signed	Zigzag
// 0	0
// -1	1
// +1	2
// -2	3
// +2	4
// -3	5
// +3	6

// Pattern:

// positives → even numbers

// negatives → odd numbers

inline std::uint64_t zigzag_encode(std::int64_t value) {
    return (static_cast<std::uint64_t>(value) << 1) ^ static_cast<std::uint64_t>(value >> 63);
}

inline std::int64_t zigzag_decode(std::uint64_t value) {
    return static_cast<std::int64_t>(value >> 1) ^ -static_cast<std::int64_t>(value & 1);
}

inline void write_varint(ByteWriter& out, std::uint64_t value) {
    while (value >= 0x80) {
        out.write_byte(static_cast<std::uint8_t>(value | 0x80));
        value >>= 7;
    }
    out.write_byte(static_cast<std::uint8_t>(value));
}

inline bool read_varint(ByteReader& in, std::uint64_t* value) {
    std::uint64_t result = 0;
    int shift = 0;
    while (shift < 64) {
        int byte = in.read_byte();
        if (byte < 0) {
            return false;
        }
        result |= static_cast<std::uint64_t>(byte & 0x7F) << shift;
        if ((byte & 0x80) == 0) {
            *value = result;
            return true;
        }
        shift += 7;
    }
    return false;
}

inline bool split_line(const std::string& line, FieldSpan* out, std::size_t expected_fields) {
    const char* data = line.data();
    std::size_t len = line.size();
    std::size_t field = 0;
    std::size_t start = 0;
    for (std::size_t i = 0; i <= len; ++i) {
        if (i == len || data[i] == ',') {
            if (field >= expected_fields) {
                return false;
            }
            out[field++] = FieldSpan{data + start, data + i};
            start = i + 1;
        }
    }
    return field == expected_fields;
}

inline int count_levels(const char* start, const char* end) {
    if (start >= end) {
        return 0;
    }
    int count = 1;
    for (const char* p = start; p < end; ++p) {
        if (*p == ';') {
            ++count;
        }
    }
    return count;
}

inline bool parse_level(const char*& p, const char* end,
                        std::int64_t* price, std::int64_t* qty) {
    if (p >= end) return false;

    const char* pipe = static_cast<const char*>(
        std::memchr(p, '|', end - p));
    if (!pipe) return false;

    // PRICE: integer, no scaling
    *price = parse_int64(p, pipe);

    const char* qty_start = pipe + 1;
    const char* qty_end = static_cast<const char*>(
        std::memchr(qty_start, ';', end - qty_start));
    if (!qty_end) qty_end = end;

    *qty = parse_int64(qty_start, qty_end);

    p = (qty_end < end) ? qty_end + 1 : end;
    return true;
}



}  // namespace tickdata

#endif  // HFT_TICKDATA_CODEC_HPP
