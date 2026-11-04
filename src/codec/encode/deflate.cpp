// The zlib compressor behind PNG and the public zlib API.
//
//   * LZ77 with a 4-byte hash chain (fixed arrays, no per-bucket allocation) and zlib-style
//     lazy matching; the effort sets how hard it searches.
//   * Dynamic Huffman blocks: every block gets its own length-limited canonical code, and the
//     cheapest of dynamic / fixed / stored is written.
//
// The output is a plain zlib stream that any inflater reads. Derived from the compressor in
// stb_image_write.h v1.16 (public domain / MIT, Sean Barrett and contributors).

#include <algorithm>
#include <array>
#include <cstring>

#include "codec/codec.hpp"

namespace stb::codec {

std::uint32_t adler32(const std::uint8_t* p, std::size_t n) {
    std::uint32_t a = 1, b = 0;
    while (n > 0) {
        const std::size_t k = std::min<std::size_t>(n, 5552);  // largest run that cannot overflow
        for (std::size_t i = 0; i < k; ++i) {
            a += p[i];
            b += a;
        }
        a %= 65521;
        b %= 65521;
        p += k;
        n -= k;
    }
    return (b << 16) | a;
}

bytes zlib_store(const std::uint8_t* data, std::size_t size) {
    bytes out;
    out.reserve(size + 6 + 5 * (size / 65535 + 1));
    byte_writer w(out);
    w.u8(0x78);
    w.u8(0x01);  // deflate, 32 KiB window, "fastest" flag; (0x78 << 8 | 0x01) % 31 == 0
    std::size_t pos = 0;
    do {
        const std::size_t n = std::min<std::size_t>(size - pos, 65535);
        const bool last = pos + n == size;
        w.u8(last ? 1 : 0);  // BFINAL, BTYPE = 00 (stored)
        w.u16_le(unsigned(n));
        w.u16_le(unsigned(~n & 0xffff));
        w.raw(data + pos, n);
        pos += n;
    } while (pos < size);
    w.u32_be(adler32(data, size));
    return out;
}

namespace {

constexpr int window_size = 32768;
constexpr int hash_bits = 16;
constexpr int hash_size = 1 << hash_bits;
constexpr int tokens_per_block = 65536;  // the Huffman codes are rebuilt for each block

constexpr int literal_symbols = 286;  // 0..255 literals, 256 end of block, 257..285 lengths
constexpr int distance_symbols = 30;
constexpr int length_code_symbols = 19;

constexpr std::uint16_t length_base[29] = {3,  4,  5,  6,  7,  8,  9,  10, 11,  13,  15,  17,  19,  23, 27,
                                           31, 35, 43, 51, 59, 67, 83, 99, 115, 131, 163, 195, 227, 258};
constexpr std::uint8_t length_extra[29] = {0, 0, 0, 0, 0, 0, 0, 0, 1, 1, 1, 1, 2, 2, 2,
                                           2, 3, 3, 3, 3, 4, 4, 4, 4, 5, 5, 5, 5, 0};
constexpr std::uint16_t dist_base[30] = {1,   2,   3,   4,   5,   7,    9,    13,   17,   25,
                                         33,  49,  65,  97,  129, 193,  257,  385,  513,  769,
                                         1025, 1537, 2049, 3073, 4097, 6145, 8193, 12289, 16385, 24577};
constexpr std::uint8_t dist_extra[30] = {0, 0, 0, 0, 1, 1, 2, 2,  3,  3,  4,  4,  5,  5,  6,
                                         6, 7, 7, 8, 8, 9, 9, 10, 10, 11, 11, 12, 12, 13, 13};
constexpr std::uint8_t code_length_order[19] = {16, 17, 18, 0, 8, 7, 9, 6, 10, 5, 11, 4, 12, 3, 13, 2, 14, 1, 15};

// Search effort by level: good length, max lazy length (0 = greedy), nice length, max chain.
struct effort {
    std::uint16_t good, lazy, nice, chain;
};
constexpr effort efforts[11] = {{4, 0, 8, 4},    {4, 0, 8, 4},     {4, 0, 16, 8},     {4, 0, 32, 32},
                                {4, 4, 16, 16},  {8, 16, 32, 32},  {8, 16, 64, 64},   {8, 16, 128, 128},
                                {8, 32, 128, 256}, {32, 128, 258, 1024}, {32, 258, 258, 4096}};

int reverse_bits(int code, int bits) {
    int r = 0;
    while (bits--) {
        r = (r << 1) | (code & 1);
        code >>= 1;
    }
    return r;
}

// A canonical Huffman code over N symbols. `code` is bit-reversed so it can be written LSB first.
template <std::size_t N>
struct huffman_code {
    std::array<std::uint8_t, N> length{};
    std::array<std::uint16_t, N> code{};

    // Lengths limited to max_length bits for the frequencies, which need at least two non-zero
    // entries. Huffman by the two-queue method; if the tree is too deep the frequencies are
    // flattened and it is rebuilt (slightly suboptimal, always valid).
    static huffman_code from_frequencies(const std::array<std::uint32_t, N>& freq, int max_length) {
        std::array<std::uint32_t, N> work = freq, key{};
        std::array<std::uint32_t, 2 * N> weight{};
        std::array<int, 2 * N> parent{}, depth{};
        int m = 0;
        for (;;) {
            m = 0;
            for (std::size_t i = 0; i < N; ++i)
                if (work[i]) key[m++] = (work[i] << 9) | std::uint32_t(i);
            std::sort(key.begin(), key.begin() + m);  // the symbol in the low bits makes every key unique
            for (int i = 0; i < m; ++i) weight[i] = key[i] >> 9;
            int leaf = 0, node = m, next = m;
            for (int k = 0; k < m - 1; ++k) {
                int a, b;
                if (leaf < m && (node >= next || weight[leaf] <= weight[node])) a = leaf++; else a = node++;
                if (leaf < m && (node >= next || weight[leaf] <= weight[node])) b = leaf++; else b = node++;
                weight[next] = weight[a] + weight[b];
                parent[a] = parent[b] = next;
                ++next;
            }
            depth[next - 1] = 0;
            int deepest = 0;
            for (int i = next - 2; i >= 0; --i) {
                depth[i] = depth[parent[i]] + 1;
                if (i < m && depth[i] > deepest) deepest = depth[i];
            }
            if (deepest <= max_length) break;
            for (std::size_t i = 0; i < N; ++i)
                if (work[i]) work[i] = (work[i] + 1) >> 1;
        }
        huffman_code h;
        for (int i = 0; i < m; ++i) h.length[key[i] & 511] = static_cast<std::uint8_t>(depth[i]);
        h.assign_codes();
        return h;
    }

    // The canonical codes of `length`.
    void assign_codes() {
        int count[16] = {}, next[16] = {}, c = 0;
        for (std::size_t i = 0; i < N; ++i) count[length[i]]++;
        count[0] = 0;
        for (int i = 1; i < 16; ++i) {
            c = (c + count[i - 1]) << 1;
            next[i] = c;
        }
        for (std::size_t i = 0; i < N; ++i)
            code[i] = length[i] ? static_cast<std::uint16_t>(reverse_bits(next[length[i]]++, length[i])) : 0;
    }
};

// A frequency table where at least two symbols are in use (a Huffman code needs two).
template <std::size_t N>
void ensure_two_symbols(std::array<std::uint32_t, N>& freq) {
    int used = int(std::count_if(freq.begin(), freq.end(), [](std::uint32_t f) { return f != 0; }));
    for (std::size_t i = 0; i < N && used < 2; ++i)
        if (!freq[i]) {
            freq[i] = 1;
            ++used;
        }
}

using literal_code = huffman_code<literal_symbols>;
using distance_code = huffman_code<distance_symbols>;
using length_code = huffman_code<length_code_symbols>;

// Match length / distance -> symbol, and the fixed Huffman code (RFC 1951 3.2.6). Built once.
struct symbol_tables {
    std::array<std::uint16_t, 259> length_symbol{};  // match length -> 257..285
    std::array<std::uint8_t, 512> distance_symbol{}; // distance -> symbol (see distance_of)
    huffman_code<288> fixed_literal;
    huffman_code<distance_symbols> fixed_distance;

    symbol_tables() {
        for (int i = 0; i < 3; ++i) length_symbol[i] = 257;  // never used: lengths start at 3
        for (int j = 0; j < 29; ++j) {
            const int hi = j < 28 ? length_base[j + 1] - 1 : 258;
            for (int i = length_base[j]; i <= hi; ++i) length_symbol[i] = static_cast<std::uint16_t>(257 + j);
        }
        for (int j = 0; j < 30; ++j) {
            const int lo = dist_base[j] - 1, hi = lo + (1 << dist_extra[j]) - 1;
            if (hi < 256) {
                for (int i = lo; i <= hi; ++i) distance_symbol[i] = static_cast<std::uint8_t>(j);
            } else {
                for (int i = lo >> 7; i <= hi >> 7; ++i) distance_symbol[256 + i] = static_cast<std::uint8_t>(j);
            }
            fixed_distance.length[j] = 5;
        }
        fixed_distance.assign_codes();
        for (int i = 0; i < 288; ++i)
            fixed_literal.length[i] = static_cast<std::uint8_t>(i < 144 ? 8 : i < 256 ? 9 : i < 280 ? 7 : 8);
        fixed_literal.assign_codes();
    }

    int distance_of(int d) const { return d <= 256 ? distance_symbol[d - 1] : distance_symbol[256 + ((d - 1) >> 7)]; }
};

const symbol_tables& tables() {
    static const symbol_tables t;
    return t;
}

// The zlib compressor for one input. Single use: construct, call compress().
class deflater {
public:
    deflater(const std::uint8_t* data, int size, int level)
        : data_(data), size_(size), effort_(efforts[std::clamp(level, 0, 10)]), tables_(tables()),
          tokens_(tokens_per_block), head_(hash_size, -1), prev_(window_size) {
        // Worst case: every block ends up stored (5 bytes of header per 64K) plus the zlib wrapper.
        out_.resize(std::size_t(size) + std::size_t(size) / 4096 + 1024);
    }

    bytes compress() {
        out_[n_++] = 0x78;  // zlib header: deflate, 32K window
        out_[n_++] = 0x5e;
        find_tokens();
        flush_block(true);
        align_to_byte();
        const std::uint32_t sum = adler32(data_, std::size_t(size_));
        for (int shift = 24; shift >= 0; shift -= 8) out_[n_++] = static_cast<std::uint8_t>(sum >> shift);
        out_.resize(n_);
        return std::move(out_);
    }

private:
    // ---- bits out (LSB first, four bytes at a time)

    void put(std::uint64_t value, int count) {
        bits_ |= value << nbits_;
        nbits_ += count;
        if (nbits_ >= 32) {
            std::uint8_t* o = out_.data() + n_;
            o[0] = std::uint8_t(bits_);
            o[1] = std::uint8_t(bits_ >> 8);
            o[2] = std::uint8_t(bits_ >> 16);
            o[3] = std::uint8_t(bits_ >> 24);
            n_ += 4;
            bits_ >>= 32;
            nbits_ -= 32;
        }
    }

    void align_to_byte() {
        while (nbits_ > 0) {
            out_[n_++] = std::uint8_t(bits_);
            bits_ >>= 8;
            nbits_ -= 8;
        }
        bits_ = 0;
        nbits_ = 0;
    }

    // ---- tokens: a literal is the byte itself, a match is (length << 16) | distance

    void add_literal(int byte) {
        tokens_[ntok_++] = static_cast<std::uint32_t>(byte);
        literal_freq_[byte]++;
        raw_len_++;
        if (ntok_ >= tokens_per_block) flush_block(false);
    }

    void add_match(int length, int dist) {
        const int ls = tables_.length_symbol[length], ds = tables_.distance_of(dist);
        tokens_[ntok_++] = (std::uint32_t(length) << 16) | std::uint32_t(dist);
        literal_freq_[ls]++;
        distance_freq_[ds]++;
        extra_bits_ += length_extra[ls - 257] + dist_extra[ds];
        raw_len_ += length;
        if (ntok_ >= tokens_per_block) flush_block(false);
    }

    // ---- LZ77 match finder (4-byte hash, chained through prev_)

    static std::uint32_t load32(const std::uint8_t* p) {
        std::uint32_t v;
        std::memcpy(&v, p, 4);
        return v;
    }

    static int hash4(const std::uint8_t* p) { return int(load32(p) * 0x9E3779B1u >> (32 - hash_bits)); }

    static int match_length(const std::uint8_t* a, const std::uint8_t* b, int max_len) {
        int l = 0;
        while (l + 8 <= max_len) {
            std::uint64_t x, y;
            std::memcpy(&x, a + l, 8);
            std::memcpy(&y, b + l, 8);
            if (x != y) break;
            l += 8;
        }
        while (l < max_len && a[l] == b[l]) ++l;
        return l;
    }

    void insert(int pos) {
        const int h = hash4(data_ + pos);
        prev_[pos & (window_size - 1)] = head_[h];
        head_[h] = pos;
    }

    void insert_if_room(int pos) {
        if (size_ - pos >= 4) insert(pos);
    }

    // The longest match for data_[pos..] that is longer than `best`; `dist` is set when one is found.
    int find_match(int pos, int best, int chain, int nice, int& dist) const {
        int max_len = std::min(size_ - pos, 258);
        const std::uint8_t* b = data_ + pos;
        nice = std::min(nice, max_len);
        if (best >= max_len) return best;
        int cand = head_[hash4(b)];
        while (cand >= 0 && pos - cand <= window_size && chain-- > 0) {
            const std::uint8_t* a = data_ + cand;
            if (a[best] == b[best] && load32(a) == load32(b)) {
                const int l = match_length(a, b, max_len);
                if (l > best) {
                    best = l;
                    dist = pos - cand;
                    if (l >= nice) break;
                }
            }
            const int next = prev_[cand & (window_size - 1)];
            if (next >= cand) break;
            cand = next;
        }
        return best;
    }

    // Walks the input once, turning it into tokens (flushed as blocks whenever 64K are collected).
    void find_tokens() {
        const int good = effort_.good, lazy = effort_.lazy, nice = effort_.nice, max_chain = effort_.chain;
        int pos = 0, pending_len = 0, pending_dist = 0;
        bool pending_literal = false;
        while (pos < size_) {
            int len = 0, dist = 0;
            const bool have = size_ - pos >= 4;
            if (have && pending_len < lazy + (lazy == 0)) {
                const int floor = std::max(pending_len, 3);
                len = find_match(pos, floor, pending_len >= good ? max_chain >> 2 : max_chain, nice, dist);
                if (len <= floor) len = 0;
            }
            if (lazy == 0) {  // greedy
                if (len >= 4) {
                    add_match(len, dist);
                    for (int i = 0; i < len; ++i, ++pos) insert_if_room(pos);
                } else {
                    add_literal(data_[pos]);
                    if (have) insert(pos);
                    ++pos;
                }
                continue;
            }
            if (have) insert(pos);
            if (pending_len >= 4 && len <= pending_len) {  // the match found one byte ago wins
                const int end = pos - 1 + pending_len;
                add_match(pending_len, pending_dist);
                for (int i = pos + 1; i < end; ++i) insert_if_room(i);
                pos = end;
                pending_len = 0;
                pending_literal = false;
            } else {
                if (pending_literal) add_literal(data_[pos - 1]);
                pending_len = len;
                pending_dist = dist;
                pending_literal = true;
                ++pos;
            }
        }
        if (pending_literal) add_literal(data_[pos - 1]);
    }

    // ---- blocks

    // The header of a dynamic block: the code lengths of the two alphabets, run-length coded and
    // themselves Huffman coded.
    struct dynamic_header {
        literal_code literal;
        distance_code distance;
        length_code lengths;
        int nlit = 0, ndist = 0, ncl = 0;
        std::array<std::uint8_t, 320> rle{}, rle_extra{};  // 16: repeat previous, 17 / 18: runs of zeros
        int nrle = 0;
    };

    dynamic_header plan_dynamic_header() const {
        dynamic_header h;
        std::array<std::uint32_t, literal_symbols> lf;
        std::array<std::uint32_t, distance_symbols> df;
        std::copy_n(literal_freq_.begin(), literal_symbols, lf.begin());
        std::copy_n(distance_freq_.begin(), distance_symbols, df.begin());
        ensure_two_symbols(lf);
        ensure_two_symbols(df);
        h.literal = literal_code::from_frequencies(lf, 15);
        h.distance = distance_code::from_frequencies(df, 15);

        h.nlit = literal_symbols;
        while (h.nlit > 257 && h.literal.length[h.nlit - 1] == 0) --h.nlit;
        h.ndist = distance_symbols;
        while (h.ndist > 1 && h.distance.length[h.ndist - 1] == 0) --h.ndist;

        // run-length code the code lengths
        const int total = h.nlit + h.ndist;
        std::array<std::uint8_t, 320> seq{};
        for (int i = 0; i < h.nlit; ++i) seq[i] = h.literal.length[i];
        for (int i = 0; i < h.ndist; ++i) seq[h.nlit + i] = h.distance.length[i];
        auto emit = [&h](int symbol, int extra) {
            h.rle[h.nrle] = static_cast<std::uint8_t>(symbol);
            h.rle_extra[h.nrle++] = static_cast<std::uint8_t>(extra);
        };
        for (int i = 0; i < total;) {
            const int v = seq[i];
            int run = 1;
            while (i + run < total && seq[i + run] == v) ++run;
            if (v == 0 && run >= 3) {
                i += run;
                while (run > 0) {
                    if (run >= 11) {
                        const int r = std::min(run, 138);
                        emit(18, r - 11);
                        run -= r;
                    } else if (run >= 3) {
                        emit(17, run - 3);
                        run = 0;
                    } else {
                        while (run--) emit(0, 0);
                        run = 0;
                    }
                }
            } else {
                emit(v, 0);
                ++i;
                --run;
                while (run >= 3) {
                    const int r = std::min(run, 6);
                    emit(16, r - 3);
                    run -= r;
                    i += r;
                }
                while (run > 0) {
                    emit(v, 0);
                    --run;
                    ++i;
                }
            }
        }
        std::array<std::uint32_t, length_code_symbols> cf{};
        for (int i = 0; i < h.nrle; ++i) cf[h.rle[i]]++;
        ensure_two_symbols(cf);
        h.lengths = length_code::from_frequencies(cf, 7);
        h.ncl = length_code_symbols;
        while (h.ncl > 4 && h.lengths.length[code_length_order[h.ncl - 1]] == 0) --h.ncl;
        return h;
    }

    static int rle_extra_bits(int symbol) { return symbol == 16 ? 2 : symbol == 17 ? 3 : symbol == 18 ? 7 : 0; }

    // The bits a block would take, per kind.
    std::uint32_t dynamic_cost(const dynamic_header& h) const {
        std::uint32_t bits = 3 + 14 + 3 * std::uint32_t(h.ncl) + std::uint32_t(extra_bits_);
        for (int i = 0; i < h.nrle; ++i) bits += h.lengths.length[h.rle[i]] + rle_extra_bits(h.rle[i]);
        for (int i = 0; i < literal_symbols; ++i) bits += literal_freq_[i] * h.literal.length[i];
        for (int i = 0; i < distance_symbols; ++i) bits += distance_freq_[i] * h.distance.length[i];
        return bits;
    }

    std::uint32_t fixed_cost() const {
        std::uint32_t bits = 3 + std::uint32_t(extra_bits_);
        for (int i = 0; i < literal_symbols; ++i) bits += literal_freq_[i] * tables_.fixed_literal.length[i];
        for (int i = 0; i < distance_symbols; ++i) bits += distance_freq_[i] * 5;
        return bits;
    }

    std::uint32_t stored_cost(int sub_blocks) const {
        return std::uint32_t(raw_len_) * 8 + std::uint32_t(sub_blocks) * 48;
    }

    template <std::size_t NL, std::size_t ND>
    void write_tokens(const huffman_code<NL>& lit, const huffman_code<ND>& dist) {
        for (int i = 0; i < ntok_; ++i) {
            const std::uint32_t t = tokens_[i];
            if (t < 256) {
                put(lit.code[t], lit.length[t]);
            } else {
                const int len = int(t >> 16), d = int(t & 0xffff);
                const int ls = tables_.length_symbol[len], ds = tables_.distance_of(d);
                put(lit.code[ls], lit.length[ls]);
                if (length_extra[ls - 257]) put(std::uint64_t(len - length_base[ls - 257]), length_extra[ls - 257]);
                put(dist.code[ds], dist.length[ds]);
                if (dist_extra[ds]) put(std::uint64_t(d - dist_base[ds]), dist_extra[ds]);
            }
        }
        put(lit.code[256], lit.length[256]);
    }

    void write_stored(bool final, int sub_blocks) {
        const std::uint8_t* p = data_ + raw_start_;
        int left = raw_len_;
        for (int i = 0; i < sub_blocks; ++i) {
            const int blk = std::min(left, 65535);
            put(final && i == sub_blocks - 1, 1);
            put(0, 2);
            align_to_byte();
            out_[n_++] = std::uint8_t(blk);
            out_[n_++] = std::uint8_t(blk >> 8);
            out_[n_++] = std::uint8_t(~blk);
            out_[n_++] = std::uint8_t(~blk >> 8);
            std::memcpy(out_.data() + n_, p, std::size_t(blk));
            n_ += std::size_t(blk);
            p += blk;
            left -= blk;
        }
    }

    void write_dynamic(bool final, const dynamic_header& h) {
        put(final, 1);
        put(2, 2);
        put(std::uint64_t(h.nlit - 257), 5);
        put(std::uint64_t(h.ndist - 1), 5);
        put(std::uint64_t(h.ncl - 4), 4);
        for (int i = 0; i < h.ncl; ++i) put(h.lengths.length[code_length_order[i]], 3);
        for (int i = 0; i < h.nrle; ++i) {
            const int s = h.rle[i];
            put(h.lengths.code[s], h.lengths.length[s]);
            if (const int extra = rle_extra_bits(s)) put(h.rle_extra[i], extra);
        }
        write_tokens(h.literal, h.distance);
    }

    // Writes the tokens collected so far as one block (the cheapest of dynamic, fixed, stored).
    void flush_block(bool final) {
        literal_freq_[256] = 1;  // end of block
        const dynamic_header header = plan_dynamic_header();
        const std::uint32_t dyn = dynamic_cost(header), fix = fixed_cost();
        const int sub_blocks = raw_len_ ? (raw_len_ + 65534) / 65535 : 1;
        const std::uint32_t sto = stored_cost(sub_blocks);

        if (sto <= dyn && sto <= fix) {
            write_stored(final, sub_blocks);
        } else if (fix <= dyn) {
            put(final, 1);
            put(1, 2);
            write_tokens(tables_.fixed_literal, tables_.fixed_distance);
        } else {
            write_dynamic(final, header);
        }

        raw_start_ += raw_len_;
        raw_len_ = 0;
        ntok_ = 0;
        extra_bits_ = 0;
        literal_freq_.fill(0);
        distance_freq_.fill(0);
    }

    const std::uint8_t* data_;
    int size_;
    effort effort_;
    const symbol_tables& tables_;

    bytes out_;  // capacity reserved up front, so bits can be stored without checks
    std::size_t n_ = 0;  // bytes written
    std::uint64_t bits_ = 0;
    int nbits_ = 0;

    std::vector<std::uint32_t> tokens_;
    int ntok_ = 0;
    int raw_start_ = 0, raw_len_ = 0;  // input bytes covered by the tokens of the current block
    int extra_bits_ = 0;               // length / distance extra bits of the current block
    std::array<std::uint32_t, 288> literal_freq_{};
    std::array<std::uint32_t, 32> distance_freq_{};

    std::vector<int> head_, prev_;
};

}  // namespace

bytes zlib_deflate(const std::uint8_t* data, std::size_t size, int effort_level) {
    return deflater(data, static_cast<int>(size), effort_level).compress();
}

}  // namespace stb::codec
