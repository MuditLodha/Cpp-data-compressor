#include "tickdata_codec.hpp"

#include <cstring>
#include <iostream>
#include <string>

namespace {

// Assumptions:
// side is either 1 or -1 for trade messages
// ask is increasing order
// bids is either increasing or decreasing order
// update_id_end never missing

void print_usage() {
    std::cerr << "Usage:\n"
              << "  decode_tickdata data <input.bin> <output.csv>\n"
              << "  decode_tickdata depth <input.bin> <output.csv>\n";
}

class TextWriter {
public:
    explicit TextWriter(const char* path, std::size_t buf_size = 1 << 20)
        : file_(std::fopen(path, "wb")) {
        if (file_) {
            std::setvbuf(file_, nullptr, _IOFBF, buf_size);
        }
    }
    ~TextWriter() {
        if (file_) {
            std::fclose(file_);
        }
    }
    bool ok() const { return file_ != nullptr; }
    void write_line(const std::string& line) {
        std::fwrite(line.data(), 1, line.size(), file_);
    }
    void write_literal(const char* text) {
        std::fwrite(text, 1, std::strlen(text), file_);
    }

private:
    std::FILE* file_;
};

bool decode_data_bin(const char* bin_path, const char* csv_path) {
    tickdata::ByteReader reader(bin_path);
    if (!reader.ok()) {
        std::cerr << "Failed to open BIN: " << bin_path << "\n";
        return false;
    }
    char magic[4];
    if (!reader.read_bytes(magic, sizeof(magic)) ||
        std::memcmp(magic, tickdata::kMagicData, sizeof(magic)) != 0) {
        std::cerr << "Bad magic for data bin: " << bin_path << "\n";
        return false;
    }
    std::uint64_t version = 0;
    std::uint64_t scale_pow = 0;
    if (!tickdata::read_varint(reader, &version) || !tickdata::read_varint(reader, &scale_pow)) {
        std::cerr << "Failed to read header\n";
        return false;
    }

    TextWriter writer(csv_path);
    if (!writer.ok()) {
        std::cerr << "Failed to open CSV: " << csv_path << "\n";
        return false;
    }

    std::int64_t prev_ts = 0;
    std::int64_t prev_bid = 0;
    std::int64_t prev_bid_qty = 0;
    std::int64_t prev_ask = 0;
    std::int64_t prev_ask_qty = 0;
    std::int64_t prev_evt = 0;
    std::int64_t prev_recv = 0;

    std::int64_t prev_trade_price = 0;
    std::int64_t prev_trade_qty = 0;
    std::int64_t prev_trade_evt = 0;
    std::int64_t prev_trade_recv = 0;

    std::string line;
    line.reserve(256);
    while (true) {
        std::uint64_t raw = 0;
        if (!tickdata::read_varint(reader, &raw)) {
            break;
        }
        std::int64_t ts = prev_ts + tickdata::zigzag_decode(raw);
        prev_ts = ts;

        std::uint64_t msg_type = 0;
        if (!tickdata::read_varint(reader, &msg_type)) {
            break;
        }

        line.clear();
        tickdata::append_int(line, ts);
        line.push_back(',');
        msg_type = tickdata::zigzag_decode(msg_type);
        if (msg_type == 1 || msg_type == static_cast<std::uint64_t>(-1)) {
            std::uint64_t v = 0;
            tickdata::read_varint(reader, &v);
            std::int64_t price = prev_trade_price + tickdata::zigzag_decode(v);
            tickdata::read_varint(reader, &v);
            std::int64_t qty = prev_trade_qty + tickdata::zigzag_decode(v);
            
            int side = msg_type == 1 ? 1 : -1;
            tickdata::read_varint(reader, &v);
            std::int64_t evt = static_cast<std::int64_t>(v);
            tickdata::read_varint(reader, &v);
            std::int64_t recv = static_cast<std::int64_t>(v);

            prev_trade_price = price;
            prev_trade_qty = qty;
            prev_trade_evt = evt;
            prev_trade_recv = recv;

            line.push_back('0');
            line.push_back(',');
            tickdata::append_scaled(line, price, static_cast<int>(scale_pow));
            line.push_back(',');
            tickdata::append_int(line, qty);
            line.push_back(',');
            tickdata::append_int(line, side);
            line.push_back(',');
            tickdata::append_int(line, evt);
            line.push_back(',');
            tickdata::append_int(line, recv);
            line.push_back('\n');
        } else {
            std::uint64_t v = 0;
            tickdata::read_varint(reader, &v);
            std::int64_t bid = prev_bid + tickdata::zigzag_decode(v);
            tickdata::read_varint(reader, &v);
            std::int64_t bid_qty = prev_bid_qty + tickdata::zigzag_decode(v);
            tickdata::read_varint(reader, &v);
            std::int64_t ask = prev_ask + tickdata::zigzag_decode(v);
            tickdata::read_varint(reader, &v);
            std::int64_t ask_qty = prev_ask_qty + tickdata::zigzag_decode(v);
            tickdata::read_varint(reader, &v);
            std::int64_t evt = static_cast<std::int64_t>(v);
            tickdata::read_varint(reader, &v);
            std::int64_t recv = static_cast<std::int64_t>(v);

            prev_bid = bid;
            prev_bid_qty = bid_qty;
            prev_ask = ask;
            prev_ask_qty = ask_qty;
            prev_evt = evt;
            prev_recv = recv;

            tickdata::append_scaled(line, bid, static_cast<int>(scale_pow));
            line.push_back(',');
            tickdata::append_int(line, bid_qty);
            line.push_back(',');
            tickdata::append_scaled(line, ask, static_cast<int>(scale_pow));
            line.push_back(',');
            tickdata::append_int(line, ask_qty);
            line.push_back(',');
            tickdata::append_int(line, evt);
            line.push_back(',');
            tickdata::append_int(line, recv);
            line.push_back('\n');
        }
        writer.write_line(line);
    }
    return true;
}



bool decode_depth_bin(const char* bin_path, const char* csv_path) {
    tickdata::ByteReader reader(bin_path);
    if (!reader.ok()) {
        std::cerr << "Failed to open BIN: " << bin_path << "\n";
        return false;
    }

    char magic[4];
    if (!reader.read_bytes(magic, sizeof(magic)) ||
        std::memcmp(magic, tickdata::kMagicDepth, sizeof(magic)) != 0) {
        std::cerr << "Bad magic for depth bin\n";
        return false;
    }

    std::uint64_t version = 0, scale_pow = 0;
    tickdata::read_varint(reader, &version);
    tickdata::read_varint(reader, &scale_pow);

    TextWriter writer(csv_path);
    if (!writer.ok()) {
        std::cerr << "Failed to open CSV\n";
        return false;
    }

    writer.write_literal(
        "transaction_ts,event_ts,receive_ts,"
        "bids,asks,update_id_start,update_id_end,prev_update_id\n"
    );

    std::int64_t prev_ts = 0, prev_event = 0, prev_receive = 0;
    std::int64_t prev_update_end = 0;

    std::string line;
    line.reserve(2048);

    while (true) {
        std::uint64_t v;

        /* timestamps */
        if (!tickdata::read_varint(reader, &v)) break;
        std::int64_t ts = prev_ts + tickdata::zigzag_decode(v);

        tickdata::read_varint(reader, &v);
        std::int64_t event_ts = static_cast<std::int64_t>(v);

        tickdata::read_varint(reader, &v);
        std::int64_t receive_ts = static_cast<std::int64_t>(v);

        prev_ts = ts;
        prev_event = event_ts;
        prev_receive = receive_ts;

        /* update header */
        tickdata::read_varint(reader, &v);
        bool has_update_start = v & 1;
        bool has_prev_update  = v & 2;

        std::int64_t update_end =
            prev_update_end + static_cast<std::int64_t>(v >> 2);
       

        std::int64_t update_start = 0, prev_update = 0;

        if (has_update_start) {
            tickdata::read_varint(reader, &v);
            update_start = update_end - static_cast<std::int64_t>(v);
        }
        if (has_prev_update) {
            tickdata::read_varint(reader, &v);
            prev_update = prev_update_end - tickdata::zigzag_decode(v);
        }
        prev_update_end = update_end;
        /* read counts */
        std::uint64_t packed_bid_count = 0;
        std::uint64_t ask_count = 0;

        tickdata::read_varint(reader, &packed_bid_count);
        tickdata::read_varint(reader, &ask_count);

        bool bids_decreasing = packed_bid_count & 1;
        std::uint64_t bid_count = packed_bid_count >> 1;


        line.clear();
        tickdata::append_int(line, ts); line.push_back(',');
        tickdata::append_int(line, event_ts); line.push_back(',');
        tickdata::append_int(line, receive_ts); line.push_back(',');

        /* ===================== BIDS ===================== */
        uint64_t row_mask;
        tickdata::read_varint(reader, &row_mask);
        std::int64_t prev_price = 0;
        for (std::uint64_t i = 0; i < bid_count; ++i) {
            tickdata::read_varint(reader, &v);
            std::int64_t price;
            if (i == 0) {
                price = static_cast<std::int64_t>(v);
            } else if (bids_decreasing) {
                price = prev_price - static_cast<std::int64_t>(v);
            } else {
                price = prev_price + static_cast<std::int64_t>(v);
            }


            tickdata::read_varint(reader, &v);
            std::int64_t qty = static_cast<std::int64_t>(v);

            if (i > 0) line.push_back(';');

            if (row_mask & 1)
                tickdata::append_scaled(line, price, scale_pow);
            else
                tickdata::append_int(line, price);

            line.push_back('|');
            tickdata::append_int(line, qty);

            prev_price = price;
        }

        line.push_back(',');

        /* ===================== ASKS ===================== */

        prev_price = 0;
        for (std::uint64_t i = 0; i < ask_count; ++i) {

            tickdata::read_varint(reader, &v);
            std::int64_t price =
                prev_price + static_cast<std::int64_t>(v);

            tickdata::read_varint(reader, &v);
            std::int64_t qty = static_cast<std::int64_t>(v);

            if (i > 0) line.push_back(';');

            if (row_mask & 1)
                tickdata::append_scaled(line, price, scale_pow);
            else
                tickdata::append_int(line, price);

            line.push_back('|');
            tickdata::append_int(line, qty);

            prev_price = price;
        }

        /* update columns */
        line.push_back(',');
        if (has_update_start) tickdata::append_int(line, update_start);
        line.push_back(',');
        tickdata::append_int(line, update_end);
        line.push_back(',');
        if (has_prev_update) tickdata::append_int(line, prev_update);
        line.push_back('\n');

        writer.write_line(line);
    }

    return true;
}


}  // namespace


int main(int argc, char** argv) {
    if (argc < 4) {
        print_usage();
        return 1;
    }
    const char* kind = argv[1];
    const char* bin_path = argv[2];
    const char* csv_path = argv[3];

    if (std::strcmp(kind, "data") == 0) {
        return decode_data_bin(bin_path, csv_path) ? 0 : 1;
    }

    if (std::strcmp(kind, "depth") == 0) {
        return decode_depth_bin(bin_path, csv_path) ? 0 : 1;
    }

    print_usage();
    return 1;
}
