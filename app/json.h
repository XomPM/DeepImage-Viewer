// DeepImage Viewer — a small JSON reader / writer helpers and base64 (GitHub API, 3D notes)
// Copyright (c) DMIND <dmind114@gmail.com>. MIT License.
#pragma once
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <algorithm>
#include <string>
#include <utility>
#include <vector>

namespace dv::json {

struct J {
    enum T { Null, Bool, Num, Str, Arr, Obj } t = Null;
    std::string s;
    double n = 0;
    std::vector<J> a;
    std::vector<std::pair<std::string, J>> o;
    const J* get(const char* k) const {
        for (auto& [key, v] : o) if (key == k) return &v;
        return nullptr;
    }
    std::string str(const char* k) const { auto* v = get(k); return v && v->t == Str ? v->s : std::string(); }
};

struct Parser {
    const char* p;
    const char* e;
    int depth = 0;
    void ws() { while (p < e && (*p == ' ' || *p == '\n' || *p == '\r' || *p == '\t')) ++p; }
    static void utf8(std::string& out, uint32_t c) {
        if (c < 0x80) out += (char)c;
        else if (c < 0x800) { out += (char)(0xC0 | (c >> 6)); out += (char)(0x80 | (c & 63)); }
        else if (c < 0x10000) { out += (char)(0xE0 | (c >> 12)); out += (char)(0x80 | ((c >> 6) & 63)); out += (char)(0x80 | (c & 63)); }
        else { out += (char)(0xF0 | (c >> 18)); out += (char)(0x80 | ((c >> 12) & 63)); out += (char)(0x80 | ((c >> 6) & 63)); out += (char)(0x80 | (c & 63)); }
    }
    bool hex4(uint32_t& v) {
        if (e - p < 4) return false;
        v = 0;
        for (int i = 0; i < 4; ++i) {
            char c = *p++;
            v <<= 4;
            if (c >= '0' && c <= '9') v |= c - '0';
            else if (c >= 'a' && c <= 'f') v |= c - 'a' + 10;
            else if (c >= 'A' && c <= 'F') v |= c - 'A' + 10;
            else return false;
        }
        return true;
    }
    bool string(std::string& out) {
        if (p >= e || *p != '"') return false;
        ++p;
        while (p < e && *p != '"') {
            if (*p != '\\') { out += *p++; continue; }
            if (++p >= e) return false;
            char c = *p++;
            switch (c) {
            case 'n': out += '\n'; break;
            case 't': out += '\t'; break;
            case 'r': out += '\r'; break;
            case 'b': out += '\b'; break;
            case 'f': out += '\f'; break;
            case 'u': {
                uint32_t v;
                if (!hex4(v)) return false;
                if (v >= 0xD800 && v < 0xDC00 && e - p >= 6 && p[0] == '\\' && p[1] == 'u') {
                    p += 2;
                    uint32_t lo;
                    if (!hex4(lo)) return false;
                    v = 0x10000 + ((v - 0xD800) << 10) + (lo - 0xDC00);
                }
                utf8(out, v);
                break;
            }
            default: out += c;
            }
        }
        if (p >= e) return false;
        ++p;
        return true;
    }
    bool value(J& v) {
        if (++depth > 64) return false;
        ws();
        if (p >= e) return false;
        bool ok = true;
        if (*p == '{') {
            v.t = J::Obj;
            ++p;
            ws();
            if (p < e && *p == '}') ++p;
            else
                for (;;) {
                    std::string k;
                    ws();
                    if (!string(k)) { ok = false; break; }
                    ws();
                    if (p >= e || *p++ != ':') { ok = false; break; }
                    J c;
                    if (!value(c)) { ok = false; break; }
                    v.o.emplace_back(std::move(k), std::move(c));
                    ws();
                    if (p < e && *p == ',') { ++p; continue; }
                    if (p < e && *p == '}') { ++p; break; }
                    ok = false;
                    break;
                }
        } else if (*p == '[') {
            v.t = J::Arr;
            ++p;
            ws();
            if (p < e && *p == ']') ++p;
            else
                for (;;) {
                    J c;
                    if (!value(c)) { ok = false; break; }
                    v.a.push_back(std::move(c));
                    ws();
                    if (p < e && *p == ',') { ++p; continue; }
                    if (p < e && *p == ']') { ++p; break; }
                    ok = false;
                    break;
                }
        } else if (*p == '"') {
            v.t = J::Str;
            ok = string(v.s);
        } else if (!strncmp(p, "true", std::min<size_t>(4, e - p))) { v.t = J::Bool; v.n = 1; p += 4; }
        else if (!strncmp(p, "false", std::min<size_t>(5, e - p))) { v.t = J::Bool; p += 5; }
        else if (!strncmp(p, "null", std::min<size_t>(4, e - p))) { v.t = J::Null; p += 4; }
        else {
            char* end = nullptr;
            v.t = J::Num;
            v.n = strtod(p, &end);
            if (end == p) ok = false;
            else p = end;
        }
        --depth;
        return ok && p <= e;
    }
};

inline bool parse(const std::string& text, J& out) {
    Parser ps{text.data(), text.data() + text.size()};
    return ps.value(out);
}

inline double num(const J& o, const char* k, double def = 0) {
    auto* v = o.get(k);
    return v && (v->t == J::Num || v->t == J::Bool) ? v->n : def;
}

// "..." with JSON escapes (input UTF-8)
inline std::string quote(const std::string& s) {
    std::string o = "\"";
    for (unsigned char c : s) {
        switch (c) {
        case '"': o += "\\\""; break;
        case '\\': o += "\\\\"; break;
        case '\n': o += "\\n"; break;
        case '\r': o += "\\r"; break;
        case '\t': o += "\\t"; break;
        default:
            if (c < 0x20) { char b[8]; snprintf(b, sizeof b, "\\u%04x", c); o += b; }
            else o += (char)c;
        }
    }
    return o + "\"";
}

inline std::string base64(const std::string& in) {
    static const char* T = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
    std::string o;
    o.reserve((in.size() + 2) / 3 * 4);
    size_t i = 0;
    for (; i + 2 < in.size(); i += 3) {
        uint32_t v = (uint8_t)in[i] << 16 | (uint8_t)in[i + 1] << 8 | (uint8_t)in[i + 2];
        o += T[v >> 18]; o += T[(v >> 12) & 63]; o += T[(v >> 6) & 63]; o += T[v & 63];
    }
    if (i + 1 == in.size()) { uint32_t v = (uint8_t)in[i] << 16; o += T[v >> 18]; o += T[(v >> 12) & 63]; o += "=="; }
    else if (i + 2 == in.size()) { uint32_t v = (uint8_t)in[i] << 16 | (uint8_t)in[i + 1] << 8; o += T[v >> 18]; o += T[(v >> 12) & 63]; o += T[(v >> 6) & 63]; o += '='; }
    return o;
}

inline std::string unbase64(const std::string& in) {
    auto val = [](char c) -> int {
        if (c >= 'A' && c <= 'Z') return c - 'A';
        if (c >= 'a' && c <= 'z') return c - 'a' + 26;
        if (c >= '0' && c <= '9') return c - '0' + 52;
        if (c == '+') return 62;
        if (c == '/') return 63;
        return -1;
    };
    std::string o;
    uint32_t acc = 0;
    int bits = 0;
    for (char c : in) {
        int v = val(c);
        if (v < 0) continue;
        acc = acc << 6 | (uint32_t)v;
        bits += 6;
        if (bits >= 8) { bits -= 8; o += (char)((acc >> bits) & 255); }
    }
    return o;
}

} // namespace dv::json
