// tgs-tool — inspect / validate / embed / preview .tgs stickers.
//
//   tgs-tool info   <file.tgs|dir>...                 print size, fps, frames, duration
//   tgs-tool check  <file.tgs|dir>...                 validate (exit code 1 on error) — use in CI / pre-build
//   tgs-tool embed  <out.cpp> <out.h> <id> <file|dir>...  generate the same sources as CMake tgs_embed()
//                                                     (a directory = every *.tgs inside it, sorted)
//                                                     (for plain Visual Studio projects: Pre-Build Event)
//   tgs-tool png    <file.tgs> <out.png> [frame] [size]
//   tgs-tool sheet  <file.tgs> <out.png> [cols] [cell] [frames]   contact sheet of evenly spaced frames
//   tgs-tool unpack <file.tgs> <out.json>             decompress to Lottie JSON
#include "tgs/tgs.h"
#include "tgs_inflate.h"

#include <cctype>
#include <cstdio>
#include <cstdlib>
#include <iterator>
#include <cstring>
#include <algorithm>
#include <filesystem>
#include <fstream>
#include <sstream>
#include <string>
#include <vector>

namespace {

bool ReadFile(const std::string& path, std::vector<unsigned char>& out)
{
    std::ifstream f(path, std::ios::binary);
    if (!f)
        return false;
    out.assign(std::istreambuf_iterator<char>(f), std::istreambuf_iterator<char>());
    return true;
}

bool WriteIfChanged(const std::string& path, const std::string& content)
{
    std::ifstream in(path, std::ios::binary);
    if (in)
    {
        std::string old((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
        if (old == content)
            return true;
    }
    std::ofstream f(path, std::ios::binary);
    f << content;
    return bool(f);
}

std::string Stem(const std::string& path)
{
    size_t s = path.find_last_of("/\\");
    std::string f = s == std::string::npos ? path : path.substr(s + 1);
    size_t d = f.find_last_of('.');
    return d == std::string::npos ? f : f.substr(0, d);
}

std::string CIdent(const std::string& s)
{
    std::string r;
    for (unsigned char c : s)
        r += (std::isalnum(c) || c == '_') ? char(c) : '_';
    if (r.empty() || std::isdigit((unsigned char)r[0]))
        r = "_" + r;
    return r;
}

std::string CLiteral(const std::string& s)
{
    bool plain = true;
    for (unsigned char c : s)
        if (!(std::isalnum(c) || std::strchr("_ ./+-", c)))
            plain = false;
    if (plain)
        return "\"" + s + "\"";
    std::string r = "\"";
    char buf[8];
    for (unsigned char c : s)
    {
        std::snprintf(buf, sizeof(buf), "\\x%02x", c);
        r += buf;
    }
    return r + "\"";
}

// --- minimal PNG writer (stored deflate, RGBA) --------------------------------
void Be32(std::string& s, uint32_t v)
{
    s += char(v >> 24); s += char(v >> 16); s += char(v >> 8); s += char(v);
}
void Chunk(std::string& png, const char* type, const std::string& data)
{
    Be32(png, uint32_t(data.size()));
    std::string td = std::string(type, 4) + data;
    png += td;
    Be32(png, tgs::detail::Crc32(reinterpret_cast<const uint8_t*>(td.data()), td.size()));
}
bool WritePng(const std::string& path, const std::vector<uint8_t>& rgba, int w, int h)
{
    std::string raw;
    raw.reserve(size_t(h) * (size_t(w) * 4 + 1));
    for (int y = 0; y < h; ++y)
    {
        raw += char(0);
        raw.append(reinterpret_cast<const char*>(&rgba[size_t(y) * w * 4]), size_t(w) * 4);
    }
    std::string z = "\x78\x01";
    size_t pos = 0;
    do
    {
        size_t n = raw.size() - pos < 65535 ? raw.size() - pos : 65535;
        bool last = pos + n == raw.size();
        z += char(last ? 1 : 0);
        z += char(n & 0xFF); z += char(n >> 8);
        z += char(~n & 0xFF); z += char((~n >> 8) & 0xFF);
        z.append(raw, pos, n);
        pos += n;
    } while (pos < raw.size());
    Be32(z, tgs::detail::Adler32(reinterpret_cast<const uint8_t*>(raw.data()), raw.size()));

    std::string png = "\x89PNG\r\n\x1a\n";
    std::string ihdr;
    Be32(ihdr, uint32_t(w)); Be32(ihdr, uint32_t(h));
    ihdr += char(8); ihdr += char(6); ihdr += char(0); ihdr += char(0); ihdr += char(0);
    Chunk(png, "IHDR", ihdr);
    Chunk(png, "IDAT", z);
    Chunk(png, "IEND", "");
    std::ofstream f(path, std::ios::binary);
    f << png;
    return bool(f);
}

tgs::Animation::Ptr Load(const std::string& path, bool verbose = true)
{
    std::string err;
    auto a = tgs::Animation::FromFile(path, &err);
    if (!a && verbose)
        std::fprintf(stderr, "error: %s: %s\n", path.c_str(), err.c_str());
    return a;
}

// files and directories (a directory = every *.tgs inside, sorted)
std::vector<std::string> ExpandInputs(int argc, char** argv)
{
    std::vector<std::string> files;
    for (int i = 0; i < argc; ++i)
    {
        std::error_code ec;
        if (std::filesystem::is_directory(argv[i], ec))
        {
            std::vector<std::string> found;
            for (auto& e : std::filesystem::directory_iterator(argv[i], ec))
                if (e.is_regular_file() && e.path().extension() == ".tgs")
                    found.push_back(e.path().string());
            std::sort(found.begin(), found.end());
            files.insert(files.end(), found.begin(), found.end());
        }
        else
            files.push_back(argv[i]);
    }
    return files;
}

int CmdInfo(int argc, char** argv, bool quiet)
{
    int fails = 0;
    const std::vector<std::string> files = ExpandInputs(argc, argv);
    for (const std::string& f : files)
    {
        const char* path = f.c_str();
        std::vector<unsigned char> raw;
        ReadFile(path, raw);
        auto a = Load(path);
        if (!a)
        {
            ++fails;
            continue;
        }
        const auto& in = a->Info();
        if (quiet)
            std::printf("ok   %s\n", path);
        else
            std::printf("%s\n  file: %zu B (%s)\n  json: %zu B\n  size: %dx%d\n  fps: %.2f\n  frames: %d\n  duration: %.3f s\n",
                        path, raw.size(), tgs::IsGzip(raw.data(), raw.size()) ? "gzip" : "plain/zlib",
                        a->Json().size(), in.width, in.height, in.fps, in.frame_count, in.duration);
        bool tgs_flag = a->Json().find("\"tgs\"") != std::string::npos;
        if (!quiet && (!tgs_flag || in.width != 512 || in.height != 512 || in.duration > 3.0 + 1e-6 || raw.size() > 64 * 1024))
            std::printf("  note: not a strict Telegram sticker (needs tgs:1, 512x512, <=3s, <=64KB) — still playable\n");
    }
    if (files.empty())
        std::fprintf(stderr, "no .tgs files\n");
    return (fails || files.empty()) ? 1 : 0;
}

int CmdEmbed(int argc, char** argv)
{
    if (argc < 4)
        return 2;
    std::string out_cpp = argv[0], out_h = argv[1], id = CIdent(argv[2]);
    std::string cpp = "// Auto-generated by tgs-tool embed. Do not edit.\n#include <tgs/tgs_embed.h>\n#include <cstddef>\n\nnamespace {\n\n";
    std::string table, names, all;
    std::vector<std::string> used;
    unsigned long long total = 0;
    int idx = 0;

    const std::vector<std::string> files = ExpandInputs(argc - 3, argv + 3);
    std::vector<std::string> names_seen;
    for (size_t fi = 0; fi < files.size(); ++fi, ++idx)
    {
        const char* path = files[fi].c_str();
        std::vector<unsigned char> raw;
        if (!ReadFile(path, raw) || raw.empty())
        {
            std::fprintf(stderr, "error: cannot read %s\n", path);
            return 1;
        }
        if (!Load(path))
            return 1;
        total += raw.size();
        std::string name = Stem(path);
        if (std::find(names_seen.begin(), names_seen.end(), name) != names_seen.end())
        {
            std::fprintf(stderr, "error: duplicate sticker name '%s' (%s)\n", name.c_str(), path);
            return 1;
        }
        names_seen.push_back(name);
        std::string lit = CLiteral(name);
        cpp += "// " + name + "  <-  " + files[fi] + "  (" + std::to_string(raw.size()) + " bytes)\n";
        cpp += "alignas(16) const unsigned char tgs_blob_" + std::to_string(idx) + "[" + std::to_string(raw.size()) + "] = {\n    ";
        char buf[8];
        for (size_t k = 0; k < raw.size(); ++k)
        {
            std::snprintf(buf, sizeof(buf), "0x%02x,", raw[k]);
            cpp += buf;
            if (k % 24 == 23)
                cpp += "\n    ";
        }
        cpp += "\n};\n\n";
        table += "    { " + lit + ", tgs_blob_" + std::to_string(idx) + ", " + std::to_string(raw.size()) + "u },\n";
        std::string ident = CIdent(name);
        for (auto& u : used)
            if (u == ident)
                ident += "_" + std::to_string(idx);
        used.push_back(ident);
        names += "    inline constexpr const char " + ident + "[] = " + lit + ";\n";
        all += "        " + lit + ",\n";
    }
    cpp += "const ::tgs::EmbeddedAsset tgs_table[] = {\n" + table + "};\n\n";
    cpp += "const ::tgs::EmbeddedRegistrar tgs_registrar(tgs_table, sizeof(tgs_table) / sizeof(tgs_table[0]));\n\n} // namespace\n\n";
    cpp += "void tgs_register_assets_" + id + "()\n{\n    ::tgs::RegisterEmbedded(tgs_table, sizeof(tgs_table) / sizeof(tgs_table[0])); // idempotent\n}\n";

    std::string h = "// Auto-generated by tgs-tool embed. Do not edit.\n#pragma once\n#include <tgs/tgs_embed.h>\n\n";
    h += "void tgs_register_assets_" + id + "();\n#ifndef TGS_REGISTER_ASSETS\n#define TGS_REGISTER_ASSETS() tgs_register_assets_" + id + "()\n#endif\n\n";
    h += "namespace tgs_assets {\n" + names + "\n    inline constexpr int count = " + std::to_string(idx) + ";\n";
    h += "    inline constexpr const char* all[] = {\n" + all + "    };\n";
    h += "    inline constexpr unsigned long long total_bytes = " + std::to_string(total) + "ull;\n} // namespace tgs_assets\n";

    if (!WriteIfChanged(out_cpp, cpp) || !WriteIfChanged(out_h, h))
    {
        std::fprintf(stderr, "error: cannot write output\n");
        return 1;
    }
    std::printf("tgs-tool: %d sticker(s), %llu bytes -> %s\n", idx, total, out_cpp.c_str());
    return 0;
}

int CmdPng(int argc, char** argv)
{
    if (argc < 2)
        return 2;
    auto a = Load(argv[0]);
    if (!a)
        return 1;
    int frame = argc > 2 ? std::atoi(argv[2]) : 0;
    int size = argc > 3 ? std::atoi(argv[3]) : 512;
    tgs::Renderer r(a, size, size);
    std::vector<uint8_t> px(size_t(size) * size * 4);
    if (!r.Render(frame, px.data(), size * 4))
        return 1;
    return WritePng(argv[1], px, size, size) ? 0 : 1;
}

int CmdSheet(int argc, char** argv)
{
    if (argc < 2)
        return 2;
    auto a = Load(argv[0]);
    if (!a)
        return 1;
    int cols = argc > 2 ? std::atoi(argv[2]) : 6;
    int cell = argc > 3 ? std::atoi(argv[3]) : 160;
    int count = argc > 4 ? std::atoi(argv[4]) : 12;
    int rows = (count + cols - 1) / cols;
    int W = cols * cell, H = rows * cell;
    std::vector<uint8_t> sheet(size_t(W) * H * 4);
    // checkerboard to see transparency
    for (int y = 0; y < H; ++y)
        for (int x = 0; x < W; ++x)
        {
            uint8_t c = ((x / 16 + y / 16) & 1) ? 235 : 205;
            uint8_t* p = &sheet[(size_t(y) * W + x) * 4];
            p[0] = p[1] = p[2] = c;
            p[3] = 255;
        }
    tgs::Renderer r(a, cell, cell);
    std::vector<uint8_t> px(size_t(cell) * cell * 4);
    for (int i = 0; i < count; ++i)
    {
        int frame = int((long long)i * a->Info().frame_count / count);
        r.Render(frame, px.data(), cell * 4);
        int ox = (i % cols) * cell, oy = (i / cols) * cell;
        for (int y = 0; y < cell; ++y)
            for (int x = 0; x < cell; ++x)
            {
                const uint8_t* s = &px[(size_t(y) * cell + x) * 4];
                uint8_t* d = &sheet[(size_t(oy + y) * W + ox + x) * 4];
                for (int c = 0; c < 3; ++c)
                    d[c] = uint8_t((s[c] * s[3] + d[c] * (255 - s[3]) + 127) / 255);
            }
    }
    return WritePng(argv[1], sheet, W, H) ? 0 : 1;
}

int CmdUnpack(int argc, char** argv)
{
    if (argc < 2)
        return 2;
    std::vector<unsigned char> raw;
    if (!ReadFile(argv[0], raw))
        return 1;
    std::string json, err;
    if (!tgs::Decompress(raw.data(), raw.size(), json, &err))
    {
        std::fprintf(stderr, "error: %s\n", err.c_str());
        return 1;
    }
    std::ofstream f(argv[1], std::ios::binary);
    f << json;
    return f ? 0 : 1;
}

int Usage()
{
    std::fprintf(stderr,
                 "tgs-tool " TGS_VERSION_STRING "\n"
                 "  info   <file.tgs|dir>...\n"
                 "  check  <file.tgs|dir>...\n"
                 "  embed  <out.cpp> <out.h> <id> <file.tgs|dir>...\n"
                 "  png    <file.tgs> <out.png> [frame=0] [size=512]\n"
                 "  sheet  <file.tgs> <out.png> [cols=6] [cell=160] [frames=12]\n"
                 "  unpack <file.tgs> <out.json>\n");
    return 2;
}

} // namespace

int main(int argc, char** argv)
{
    if (argc < 3)
        return Usage();
    std::string cmd = argv[1];
    int n = argc - 2;
    char** a = argv + 2;
    int rc = 2;
    if (cmd == "info") rc = CmdInfo(n, a, false);
    else if (cmd == "check") rc = CmdInfo(n, a, true);
    else if (cmd == "embed") rc = CmdEmbed(n, a);
    else if (cmd == "png") rc = CmdPng(n, a);
    else if (cmd == "sheet") rc = CmdSheet(n, a);
    else if (cmd == "unpack") rc = CmdUnpack(n, a);
    return rc == 2 ? Usage() : rc;
}
