#include "tgs_inflate.h"

#include "tgs/tgs.h"

#include <cstring>

namespace tgs {
namespace detail {
namespace {

// ---------------------------------------------------------------------------
// Bit reader (LSB-first). Reads past the end produce zero bits and are detected
// through Overrun() — keeps the hot path branch-free.
// ---------------------------------------------------------------------------
struct BitReader
{
    const uint8_t* p;
    size_t         n;
    size_t         pos = 0;
    size_t         pad = 0;  // zero bytes injected after the end
    uint64_t       buf = 0;
    int            cnt = 0;

    BitReader(const uint8_t* data, size_t len) : p(data), n(len) {}

    inline void Refill()
    {
        while (cnt <= 56)
        {
            if (pos < n)
                buf |= uint64_t(p[pos++]) << cnt;
            else
                ++pad;
            cnt += 8;
        }
    }
    inline uint32_t Peek(int k)
    {
        if (cnt < k)
            Refill();
        return uint32_t(buf & ((uint64_t(1) << k) - 1));
    }
    inline void Drop(int k)
    {
        buf >>= k;
        cnt -= k;
    }
    inline uint32_t Bits(int k) // k <= 32
    {
        if (k == 0)
            return 0;
        uint32_t v = Peek(k);
        Drop(k);
        return v;
    }
    inline void AlignToByte() { Drop(cnt & 7); }
    inline uint64_t ConsumedBits() const { return uint64_t(pos + pad) * 8 - uint64_t(cnt); }
    inline bool Overrun() const { return ConsumedBits() > uint64_t(n) * 8; }
};

// ---------------------------------------------------------------------------
// Canonical Huffman decoder with a 9-bit fast lookup table.
// ---------------------------------------------------------------------------
constexpr int kMaxBits  = 15;
constexpr int kFastBits = 9;
constexpr int kMaxLit   = 288;
constexpr int kMaxDist  = 32;

struct Huffman
{
    uint16_t count[kMaxBits + 1];
    uint16_t symbol[kMaxLit];
    uint32_t fast[1 << kFastBits]; // (len << 16) | sym, 0 = use slow path
};

inline uint32_t ReverseBits(uint32_t code, int len)
{
    uint32_t r = 0;
    for (int i = 0; i < len; ++i)
    {
        r = (r << 1) | (code & 1);
        code >>= 1;
    }
    return r;
}

// returns: 0 complete, >0 incomplete, <0 over-subscribed
int Build(Huffman& h, const uint8_t* lengths, int n)
{
    std::memset(h.count, 0, sizeof(h.count));
    std::memset(h.fast, 0, sizeof(h.fast));
    for (int s = 0; s < n; ++s)
        h.count[lengths[s]]++;
    if (h.count[0] == n)
        return 0; // no codes: complete but decoding will fail

    int left = 1;
    for (int len = 1; len <= kMaxBits; ++len)
    {
        left <<= 1;
        left -= h.count[len];
        if (left < 0)
            return left;
    }

    uint16_t offs[kMaxBits + 1];
    offs[1] = 0;
    for (int len = 1; len < kMaxBits; ++len)
        offs[len + 1] = uint16_t(offs[len] + h.count[len]);
    for (int s = 0; s < n; ++s)
        if (lengths[s] != 0)
            h.symbol[offs[lengths[s]]++] = uint16_t(s);

    // fast table
    uint32_t next_code[kMaxBits + 2];
    uint32_t code = 0;
    next_code[1] = 0;
    for (int len = 1; len <= kMaxBits; ++len)
    {
        code = (code + (len > 1 ? h.count[len - 1] : 0)) << (len > 1 ? 1 : 0);
        next_code[len] = code;
    }
    for (int s = 0; s < n; ++s)
    {
        int len = lengths[s];
        if (len == 0)
            continue;
        uint32_t c = next_code[len]++;
        if (len <= kFastBits)
        {
            uint32_t rev = ReverseBits(c, len);
            for (uint32_t k = rev; k < (1u << kFastBits); k += (1u << len))
                h.fast[k] = (uint32_t(len) << 16) | uint32_t(s);
        }
    }
    return left;
}

inline int Decode(BitReader& br, const Huffman& h)
{
    uint32_t e = h.fast[br.Peek(kFastBits)];
    if (e)
    {
        br.Drop(int(e >> 16));
        return int(e & 0xFFFF);
    }
    // slow path (codes longer than kFastBits) — canonical decode bit by bit
    int code = 0, first = 0, index = 0;
    for (int len = 1; len <= kMaxBits; ++len)
    {
        code |= int(br.Bits(1));
        int count = h.count[len];
        if (code - count < first)
            return h.symbol[index + (code - first)];
        index += count;
        first += count;
        first <<= 1;
        code <<= 1;
    }
    return -1;
}

const uint16_t kLenBase[29]  = {3, 4, 5, 6, 7, 8, 9, 10, 11, 13, 15, 17, 19, 23, 27, 31,
                                35, 43, 51, 59, 67, 83, 99, 115, 131, 163, 195, 227, 258};
const uint8_t  kLenExtra[29] = {0, 0, 0, 0, 0, 0, 0, 0, 1, 1, 1, 1, 2, 2, 2, 2,
                                3, 3, 3, 3, 4, 4, 4, 4, 5, 5, 5, 5, 0};
const uint16_t kDistBase[30] = {1, 2, 3, 4, 5, 7, 9, 13, 17, 25, 33, 49, 65, 97, 129,
                                193, 257, 385, 513, 769, 1025, 1537, 2049, 3073, 4097,
                                6145, 8193, 12289, 16385, 24577};
const uint8_t  kDistExtra[30] = {0, 0, 0, 0, 1, 1, 2, 2, 3, 3, 4, 4, 5, 5, 6,
                                 6, 7, 7, 8, 8, 9, 9, 10, 10, 11, 11, 12, 12, 13, 13};

struct FixedTables
{
    Huffman lit, dist;
    FixedTables()
    {
        uint8_t l[kMaxLit];
        int s = 0;
        for (; s < 144; ++s) l[s] = 8;
        for (; s < 256; ++s) l[s] = 9;
        for (; s < 280; ++s) l[s] = 7;
        for (; s < 288; ++s) l[s] = 8;
        Build(lit, l, 288);
        uint8_t d[30];
        for (int i = 0; i < 30; ++i) d[i] = 5;
        Build(dist, d, 30);
    }
};

const FixedTables& GetFixed()
{
    static const FixedTables t;
    return t;
}

struct Inflater
{
    BitReader    br;
    std::string& out;
    size_t       max_out;
    const char*  err = nullptr;

    Inflater(const uint8_t* src, size_t len, std::string& o, size_t mx) : br(src, len), out(o), max_out(mx) {}

    InflateStatus Fail(InflateStatus s, const char* text)
    {
        err = text;
        return s;
    }

    InflateStatus Stored()
    {
        br.AlignToByte();
        uint32_t len = br.Bits(16);
        uint32_t nlen = br.Bits(16);
        if (br.Overrun())
            return Fail(InflateStatus::Truncated, "truncated stored block header");
        if ((len ^ 0xFFFFu) != nlen)
            return Fail(InflateStatus::BadData, "stored block length mismatch");
        if (out.size() + len > max_out)
            return Fail(InflateStatus::TooLarge, "output exceeds limit");
        // bytes still sitting in the bit buffer first, then straight memcpy
        while (len && br.cnt >= 8)
        {
            out.push_back(char(br.Bits(8)));
            --len;
        }
        if (br.Overrun())
            return Fail(InflateStatus::Truncated, "truncated stored block");
        if (len)
        {
            if (br.n - br.pos < len)
                return Fail(InflateStatus::Truncated, "truncated stored block");
            out.append(reinterpret_cast<const char*>(br.p + br.pos), len);
            br.pos += len;
        }
        return InflateStatus::Ok;
    }

    InflateStatus Codes(const Huffman& lit, const Huffman& dist)
    {
        for (;;)
        {
            int sym = Decode(br, lit);
            if (br.Overrun())
                return Fail(InflateStatus::Truncated, "unexpected end of deflate data");
            if (sym < 0)
                return Fail(InflateStatus::BadData, "invalid literal/length code");
            if (sym < 256)
            {
                if (out.size() >= max_out)
                    return Fail(InflateStatus::TooLarge, "output exceeds limit");
                out.push_back(char(sym));
                continue;
            }
            if (sym == 256)
                return InflateStatus::Ok;

            sym -= 257;
            if (sym >= 29)
                return Fail(InflateStatus::BadData, "invalid length symbol");
            size_t len = kLenBase[sym] + br.Bits(kLenExtra[sym]);

            int dsym = Decode(br, dist);
            if (dsym < 0 || dsym >= 30)
                return Fail(InflateStatus::BadData, "invalid distance symbol");
            size_t d = kDistBase[dsym] + br.Bits(kDistExtra[dsym]);
            if (br.Overrun())
                return Fail(InflateStatus::Truncated, "unexpected end of deflate data");
            if (d > out.size())
                return Fail(InflateStatus::BadData, "distance too far back");
            if (out.size() + len > max_out)
                return Fail(InflateStatus::TooLarge, "output exceeds limit");

            size_t from = out.size() - d;
            size_t old = out.size();
            out.resize(old + len);
            char* o = &out[0];
            for (size_t i = 0; i < len; ++i) // overlapping copy is intended
                o[old + i] = o[from + i];
        }
    }

    InflateStatus Dynamic()
    {
        static const uint8_t order[19] = {16, 17, 18, 0, 8, 7, 9, 6, 10, 5, 11, 4, 12, 3, 13, 2, 14, 1, 15};
        int nlen = int(br.Bits(5)) + 257;
        int ndist = int(br.Bits(5)) + 1;
        int ncode = int(br.Bits(4)) + 4;
        if (nlen > 286 || ndist > 30)
            return Fail(InflateStatus::BadData, "bad dynamic block counts");

        uint8_t lengths[kMaxLit + kMaxDist];
        std::memset(lengths, 0, sizeof(lengths));
        for (int i = 0; i < ncode; ++i)
            lengths[order[i]] = uint8_t(br.Bits(3));
        if (br.Overrun())
            return Fail(InflateStatus::Truncated, "truncated dynamic header");

        Huffman lencode;
        if (Build(lencode, lengths, 19) != 0)
            return Fail(InflateStatus::BadData, "incomplete code-length code");

        int index = 0;
        while (index < nlen + ndist)
        {
            int sym = Decode(br, lencode);
            if (br.Overrun())
                return Fail(InflateStatus::Truncated, "truncated code lengths");
            if (sym < 0)
                return Fail(InflateStatus::BadData, "bad code-length symbol");
            if (sym < 16)
            {
                lengths[index++] = uint8_t(sym);
                continue;
            }
            uint8_t val = 0;
            int rep;
            if (sym == 16)
            {
                if (index == 0)
                    return Fail(InflateStatus::BadData, "repeat with no previous length");
                val = lengths[index - 1];
                rep = 3 + int(br.Bits(2));
            }
            else if (sym == 17)
                rep = 3 + int(br.Bits(3));
            else
                rep = 11 + int(br.Bits(7));
            if (index + rep > nlen + ndist)
                return Fail(InflateStatus::BadData, "too many code lengths");
            while (rep--)
                lengths[index++] = val;
        }
        if (lengths[256] == 0)
            return Fail(InflateStatus::BadData, "missing end-of-block code");

        Huffman lit, dist;
        int e = Build(lit, lengths, nlen);
        if (e < 0 || (e > 0 && nlen - lit.count[0] != 1))
            return Fail(InflateStatus::BadData, "bad literal/length code");
        e = Build(dist, lengths + nlen, ndist);
        if (e < 0 || (e > 0 && ndist - dist.count[0] != 1))
            return Fail(InflateStatus::BadData, "bad distance code");

        return Codes(lit, dist);
    }

    InflateStatus Run()
    {
        int last;
        do
        {
            last = int(br.Bits(1));
            int type = int(br.Bits(2));
            if (br.Overrun())
                return Fail(InflateStatus::Truncated, "unexpected end of deflate data");
            InflateStatus s;
            switch (type)
            {
            case 0: s = Stored(); break;
            case 1: s = Codes(GetFixed().lit, GetFixed().dist); break;
            case 2: s = Dynamic(); break;
            default: return Fail(InflateStatus::BadData, "invalid block type");
            }
            if (s != InflateStatus::Ok)
                return s;
        } while (!last);
        return InflateStatus::Ok;
    }
};

struct Crc32Table
{
    uint32_t t[256];
    Crc32Table()
    {
        for (uint32_t i = 0; i < 256; ++i)
        {
            uint32_t c = i;
            for (int k = 0; k < 8; ++k)
                c = (c & 1) ? (0xEDB88320u ^ (c >> 1)) : (c >> 1);
            t[i] = c;
        }
    }
};

inline uint32_t ReadLE32(const uint8_t* p) { return uint32_t(p[0]) | (uint32_t(p[1]) << 8) | (uint32_t(p[2]) << 16) | (uint32_t(p[3]) << 24); }
inline uint32_t ReadBE32(const uint8_t* p) { return (uint32_t(p[0]) << 24) | (uint32_t(p[1]) << 16) | (uint32_t(p[2]) << 8) | uint32_t(p[3]); }

void SetError(std::string* error, const char* text)
{
    if (error)
        *error = text;
}

} // namespace

InflateStatus InflateRaw(const uint8_t* src, size_t src_len, std::string& out, size_t max_output,
                         size_t* consumed, const char** error_text)
{
    Inflater inf(src, src_len, out, max_output);
    InflateStatus s = inf.Run();
    if (consumed)
        *consumed = size_t((inf.br.ConsumedBits() + 7) / 8);
    if (error_text)
        *error_text = inf.err;
    return s;
}

uint32_t Crc32(const uint8_t* data, size_t len, uint32_t crc)
{
    static const Crc32Table table;
    crc = ~crc;
    for (size_t i = 0; i < len; ++i)
        crc = table.t[(crc ^ data[i]) & 0xFF] ^ (crc >> 8);
    return ~crc;
}

uint32_t Adler32(const uint8_t* data, size_t len, uint32_t adler)
{
    uint32_t a = adler & 0xFFFF, b = adler >> 16;
    while (len)
    {
        size_t chunk = len < 5552 ? len : 5552;
        len -= chunk;
        while (chunk--)
        {
            a += *data++;
            b += a;
        }
        a %= 65521;
        b %= 65521;
    }
    return (b << 16) | a;
}

} // namespace detail

// ---------------------------------------------------------------------------
// Public: Decompress / IsGzip
// ---------------------------------------------------------------------------
using namespace detail;

bool IsGzip(const void* data, size_t size)
{
    const uint8_t* p = static_cast<const uint8_t*>(data);
    return p && size >= 2 && p[0] == 0x1F && p[1] == 0x8B;
}

static bool InflateChecked(const uint8_t* src, size_t len, std::string& out, size_t max_output,
                           size_t* consumed, std::string* error)
{
    const char* text = nullptr;
    InflateStatus s = InflateRaw(src, len, out, max_output, consumed, &text);
    if (s == InflateStatus::Ok)
        return true;
    if (error)
        *error = std::string("deflate: ") + (text ? text : "error");
    return false;
}

bool Decompress(const void* data, size_t size, std::string& out_json, std::string* error, size_t max_output)
{
    out_json.clear();
    const uint8_t* p = static_cast<const uint8_t*>(data);
    if (!p || size == 0)
    {
        SetError(error, "empty input");
        return false;
    }

    // --- gzip (.tgs) ---------------------------------------------------------
    if (IsGzip(p, size))
    {
        if (size < 18)
        {
            SetError(error, "gzip: file too short");
            return false;
        }
        if (p[2] != 8)
        {
            SetError(error, "gzip: unsupported compression method");
            return false;
        }
        const uint8_t flg = p[3];
        if (flg & 0xE0)
        {
            SetError(error, "gzip: reserved flags set");
            return false;
        }
        size_t pos = 10;
        if (flg & 0x04) // FEXTRA
        {
            if (pos + 2 > size) { SetError(error, "gzip: truncated header"); return false; }
            size_t xlen = size_t(p[pos]) | (size_t(p[pos + 1]) << 8);
            pos += 2 + xlen;
        }
        if (flg & 0x08) // FNAME
        {
            while (pos < size && p[pos]) ++pos;
            ++pos;
        }
        if (flg & 0x10) // FCOMMENT
        {
            while (pos < size && p[pos]) ++pos;
            ++pos;
        }
        if (flg & 0x02) // FHCRC
            pos += 2;
        if (pos + 8 > size)
        {
            SetError(error, "gzip: truncated header");
            return false;
        }

        // ISIZE is a hint (mod 2^32) — use it to reserve, but never trust it above the limit
        const uint32_t isize = ReadLE32(p + size - 4);
        if (isize > max_output)
        {
            SetError(error, "gzip: declared size exceeds limit");
            return false;
        }
        out_json.reserve(isize);

        size_t used = 0;
        if (!InflateChecked(p + pos, size - pos - 8, out_json, max_output, &used, error))
            return false;
        const uint8_t* trailer = p + pos + used;
        if (trailer + 8 > p + size)
        {
            SetError(error, "gzip: missing trailer");
            return false;
        }
        if (ReadLE32(trailer) != Crc32(reinterpret_cast<const uint8_t*>(out_json.data()), out_json.size()))
        {
            SetError(error, "gzip: CRC32 mismatch (corrupted file)");
            return false;
        }
        if (ReadLE32(trailer + 4) != uint32_t(out_json.size()))
        {
            SetError(error, "gzip: size mismatch");
            return false;
        }
        return true;
    }

    // --- zlib ------------------------------------------------------------------
    if (size >= 6 && (p[0] & 0x0F) == 8 && (p[0] >> 4) <= 7 && ((uint32_t(p[0]) << 8) | p[1]) % 31 == 0)
    {
        if (p[1] & 0x20)
        {
            SetError(error, "zlib: preset dictionary not supported");
            return false;
        }
        size_t used = 0;
        if (!InflateChecked(p + 2, size - 2, out_json, max_output, &used, error))
            return false;
        if (2 + used + 4 > size)
        {
            SetError(error, "zlib: missing Adler-32");
            return false;
        }
        if (ReadBE32(p + 2 + used) != Adler32(reinterpret_cast<const uint8_t*>(out_json.data()), out_json.size()))
        {
            SetError(error, "zlib: Adler-32 mismatch");
            return false;
        }
        return true;
    }

    // --- plain JSON ------------------------------------------------------------
    size_t i = 0;
    if (size >= 3 && p[0] == 0xEF && p[1] == 0xBB && p[2] == 0xBF)
        i = 3; // UTF-8 BOM
    while (i < size && (p[i] == ' ' || p[i] == '\t' || p[i] == '\r' || p[i] == '\n'))
        ++i;
    if (i < size && p[i] == '{')
    {
        if (size - i > max_output)
        {
            SetError(error, "json: exceeds limit");
            return false;
        }
        out_json.assign(reinterpret_cast<const char*>(p + i), size - i);
        return true;
    }

    SetError(error, "unknown format (expected .tgs/gzip, zlib or Lottie JSON)");
    return false;
}

} // namespace tgs
