#include "tickdata_codec.hpp"

#include <cstdlib>
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
              << "  encode_tickdata data <input.csv> <output.bin> [price_scale_pow]\n"
              << "  encode_tickdata depth <input.csv> <output.bin> [price_scale_pow]\n";
}


bool has_decimal(const char* start, const char* end) {
    for (const char* p = start; p < end; ++p) {
        if (*p == '.') {
            return true;
        }
    }
    return false;
}

inline bool has_decimal_in_row(const char* start, const char* end) {
    const char* p = start;
    while (p < end) {
        const char* pipe = (const char*)std::memchr(p, '|', end - p);
        if (!pipe) break;

        if (has_decimal(p, pipe))
            return true;

        const char* semi =
            (const char*)std::memchr(pipe + 1, ';', end - (pipe + 1));
        if (!semi)
            break;

        p = semi + 1;
    }
    return false;
}

bool encode_data_csv(const char* csv_path, const char* bin_path, int scale_pow) {
    tickdata::FastLineReader reader(csv_path);
    if (!reader.ok()) {
        std::cerr << "Failed to open CSV: " << csv_path << "\n";
        return false;
    }
    tickdata::ByteWriter writer(bin_path);
    if (!writer.ok()) {
        std::cerr << "Failed to open BIN: " << bin_path << "\n";
        return false;
    }

    writer.write_bytes(tickdata::kMagicData, sizeof(tickdata::kMagicData));
    tickdata::write_varint(writer, tickdata::kVersion);
    tickdata::write_varint(writer, static_cast<std::uint64_t>(scale_pow));

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
    tickdata::FieldSpan fields[7];
    while (reader.read_line(line)) {
        if (line.empty()) {
            continue;
        }
        if (!tickdata::split_line(line, fields, 7)) {
            continue;
        }

        std::int64_t ts = tickdata::parse_int64(fields[0].start, fields[0].end);
        tickdata::write_varint(writer, tickdata::zigzag_encode(ts - prev_ts));
        prev_ts = ts;

        bool is_trade = (fields[1].end - fields[1].start == 1 && fields[1].start[0] == '0');

        if (is_trade) {
            std::int64_t price = tickdata::parse_scaled_decimal(fields[2].start, fields[2].end, scale_pow);
            std::int64_t qty = tickdata::parse_int64(fields[3].start, fields[3].end);
            std::int64_t side = tickdata::parse_int64(fields[4].start, fields[4].end);
            std::int64_t evt = tickdata::parse_int64(fields[5].start, fields[5].end);
            std::int64_t recv = tickdata::parse_int64(fields[6].start, fields[6].end);
            tickdata::write_varint(writer, tickdata::zigzag_encode(side > 0 ? 1 : -1));
            tickdata::write_varint(writer, tickdata::zigzag_encode(price - prev_trade_price));
            tickdata::write_varint(writer, tickdata::zigzag_encode(qty - prev_trade_qty));
            tickdata::write_varint(writer, static_cast<std::int64_t>(evt));
            tickdata::write_varint(writer, static_cast<std::int64_t>(recv));

            prev_trade_price = price;
            prev_trade_qty = qty;
            prev_trade_evt = evt;
            prev_trade_recv = recv;
        } else {
            std::int64_t bid = tickdata::parse_scaled_decimal(fields[1].start, fields[1].end, scale_pow);
            std::int64_t bid_qty = tickdata::parse_int64(fields[2].start, fields[2].end);
            std::int64_t ask = tickdata::parse_scaled_decimal(fields[3].start, fields[3].end, scale_pow);
            std::int64_t ask_qty = tickdata::parse_int64(fields[4].start, fields[4].end);
            std::int64_t evt = tickdata::parse_int64(fields[5].start, fields[5].end);
            std::int64_t recv = tickdata::parse_int64(fields[6].start, fields[6].end);
            tickdata::write_varint(writer, 0);  // msg_type = 0 for quote
            tickdata::write_varint(writer, tickdata::zigzag_encode(bid - prev_bid));
            tickdata::write_varint(writer, tickdata::zigzag_encode(bid_qty - prev_bid_qty));
            tickdata::write_varint(writer, tickdata::zigzag_encode(ask - prev_ask));
            tickdata::write_varint(writer, tickdata::zigzag_encode(ask_qty - prev_ask_qty));
            tickdata::write_varint(writer, static_cast<std::int64_t>(evt));
            tickdata::write_varint(writer, static_cast<std::int64_t>(recv));

            prev_bid = bid;
            prev_bid_qty = bid_qty;
            prev_ask = ask;
            prev_ask_qty = ask_qty;
            prev_evt = evt;
            prev_recv = recv;
        }
    }
    writer.flush();
    return true;
}


bool encode_depth_csv(const char* csv_path, const char* bin_path, int scale_pow) {
    tickdata::FastLineReader reader(csv_path);
    if (!reader.ok()) {
        std::cerr << "Failed to open CSV: " << csv_path << "\n";
        return false;
    }
    tickdata::ByteWriter writer(bin_path);
    if (!writer.ok()) {
        std::cerr << "Failed to open BIN: " << bin_path << "\n";
        return false;
    }

    writer.write_bytes(tickdata::kMagicDepth, sizeof(tickdata::kMagicDepth));
    tickdata::write_varint(writer, tickdata::kVersion);
    tickdata::write_varint(writer, static_cast<std::uint64_t>(scale_pow));

    std::int64_t prev_ts = 0;
    std::int64_t prev_event = 0;
    std::int64_t prev_receive = 0;
    std::int64_t prev_update_end = 0;

    std::string line;
    line.reserve(1024);
    tickdata::FieldSpan fields[8];
    while (reader.read_line(line)) {
        if (line.empty()) {
            continue;
        }
        if (line.rfind("transaction_ts", 0) == 0) {
            continue;
        }
        if (!tickdata::split_line(line, fields, 8)) {
            continue;
        }

        std::int64_t ts = tickdata::parse_int64(fields[0].start, fields[0].end);
        std::int64_t event_delta = tickdata::parse_int64(fields[1].start, fields[1].end);
        std::int64_t receive_delta = tickdata::parse_int64(fields[2].start, fields[2].end);
        const char* bids_start = fields[3].start;
        const char* bids_end = fields[3].end;
        const char* asks_start = fields[4].start;
        const char* asks_end = fields[4].end;
        bool has_update_start = (fields[5].start < fields[5].end);
        bool has_prev_update = (fields[7].start < fields[7].end);
        std::int64_t update_start = has_update_start
                                        ? tickdata::parse_int64(fields[5].start, fields[5].end)
                                        : 0;
        std::int64_t update_end = tickdata::parse_int64(fields[6].start, fields[6].end);
        std::int64_t prev_update = has_prev_update
                                       ? tickdata::parse_int64(fields[7].start, fields[7].end)
                                       : 0;


        tickdata::write_varint(writer, tickdata::zigzag_encode(ts - prev_ts));
        tickdata::write_varint(writer, static_cast<std::int64_t>(event_delta));
        tickdata::write_varint(writer, static_cast<std::int64_t>(receive_delta));
        prev_ts = ts;
        prev_event = event_delta;
        prev_receive = receive_delta;

        // 1) update_end delta (signed → zigzag)
        uint64_t mask = (has_update_start ? 1 : 0)
              | (has_prev_update  ? 2 : 0);

        uint64_t packed =
            (uint64_t(update_end - prev_update_end) << 2) | mask;

        tickdata::write_varint(writer, packed);

        // Only write extras if present
        if (has_update_start) {
            tickdata::write_varint(
                writer,
                static_cast<std::int64_t>(update_end - update_start)
            );
        }
        if (has_prev_update) {
            tickdata::write_varint(
                writer,
                tickdata::zigzag_encode(prev_update_end - prev_update)
            );
        }
        prev_update_end = update_end;

        int bid_count = tickdata::count_levels(bids_start, bids_end);
        int ask_count = tickdata::count_levels(asks_start, asks_end);
    
        std::int64_t prev_price = 0;
        
        const char* p = bids_start;
        bool bids_dec = (bid_count > 0)
            ? has_decimal_in_row(bids_start, bids_end)
            : false;

        bool asks_dec = (ask_count > 0)
            ? has_decimal_in_row(asks_start, asks_end)
            : false;


        uint64_t row_mask = 0;
        if (bids_dec || asks_dec) row_mask |= 1ULL << 0;

        bool bid_dec_dir = false;
        if (bid_count >= 2) {
            const char* p0 = bids_start;
            const char* pipe0 = (const char*)std::memchr(p0, '|', bids_end - p0);
            const char* q0 = (const char*)std::memchr(pipe0 + 1, ';', bids_end - pipe0);
            if (q0) {
                const char* p1 = q0 + 1;
                const char* pipe1 = (const char*)std::memchr(p1, '|', bids_end - p1);
                if (pipe1) {
                    std::int64_t price0 = (bids_dec)
                        ? tickdata::parse_scaled_decimal(p0, pipe0, scale_pow)
                        : tickdata::parse_int64(p0, pipe0);

                    std::int64_t price1 = (bids_dec)
                        ? tickdata::parse_scaled_decimal(p1, pipe1, scale_pow)
                        : tickdata::parse_int64(p1, pipe1);

                    bid_dec_dir = (price1 < price0);
                }
            }
        }
        uint64_t packed_bid_count = (uint64_t(bid_count) << 1) | (bid_dec_dir ? 1ULL : 0ULL);

        tickdata::write_varint(writer, packed_bid_count);
        tickdata::write_varint(writer, static_cast<uint64_t>(ask_count));
        tickdata::write_varint(writer, row_mask);

        // bids_decreasing = 1 -> prices go down
        bool bids_decreasing = (packed_bid_count & 1);


        for (int i = 0; i < bid_count; ++i) {

            const char* price_start = p;
            const char* pipe = (const char*)std::memchr(p, '|', bids_end - p);
            const char* qty_start = pipe + 1;
            const char* qty_end =
                (const char*)std::memchr(qty_start, ';', bids_end - qty_start);
            if (!qty_end) qty_end = bids_end;

            std::int64_t price = row_mask
                ? tickdata::parse_scaled_decimal(price_start, pipe, scale_pow)
                : tickdata::parse_int64(price_start, pipe);

            std::int64_t qty = tickdata::parse_int64(qty_start, qty_end);

            // ---------- UNSIGNED DELTA ----------
            uint64_t delta;
            if (i == 0) {
                // first price is absolute
                delta = static_cast<uint64_t>(price);
            } else if (bids_decreasing) {
                // prev_price > price
                delta = static_cast<uint64_t>(prev_price - price);
            } else {
                // price > prev_price
                delta = static_cast<uint64_t>(price - prev_price);
            }

            tickdata::write_varint(writer, delta);
            tickdata::write_varint(
                writer,
                static_cast<int64_t>(qty)
            );


            prev_price = price;
            p = (qty_end < bids_end) ? qty_end + 1 : bids_end;
        }

        prev_price = 0;
        p = asks_start;
        for (int i = 0; i < ask_count; ++i) {
            const char* price_start = p;
            const char* pipe = (const char*)std::memchr(p, '|', asks_end - p);
            const char* qty_start = pipe + 1;
            const char* qty_end = (const char*)std::memchr(qty_start, ';', asks_end - qty_start);
            if (!qty_end) qty_end = asks_end;

            std::int64_t price = row_mask
                ? tickdata::parse_scaled_decimal(price_start, pipe, scale_pow)
                : tickdata::parse_int64(price_start, pipe);
            
            std::int64_t qty = tickdata::parse_int64(qty_start, qty_end);

            tickdata::write_varint(
                writer,
                static_cast<std::uint64_t>(price - prev_price)
            );
            
            tickdata::write_varint(
                writer,
                static_cast<int64_t>(qty)
            );


            prev_price = price;
            p = (qty_end < asks_end) ? qty_end + 1 : asks_end;
        }
    }
    writer.flush();
    return true;
}

}  // namespace

int main(int argc, char** argv) {
    if (argc < 4) {
        print_usage();
        return 1;
    }
    const char* kind = argv[1];
    const char* csv_path = argv[2];
    const char* bin_path = argv[3];
    int scale_pow = (argc >= 5) ? std::atoi(argv[4]) : tickdata::kDefaultPriceScalePow;

    if (std::strcmp(kind, "data") == 0) {
        return encode_data_csv(csv_path, bin_path, scale_pow) ? 0 : 1;
    }
    if (std::strcmp(kind, "depth") == 0) {
        return encode_depth_csv(csv_path, bin_path, scale_pow) ? 0 : 1;
    }

    print_usage();
    return 1;
}
