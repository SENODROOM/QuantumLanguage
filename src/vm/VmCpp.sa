// Runtime for the typed C/C++ front-end (src/cpp/): the __cpp_* natives its
// lowered code calls. Representation (see src/cpp/CppLower.cpp):
//   class object  [descriptor dict, field...]      pair/tuple  [a, b, ...]
//   vector/array  array                            optional    nil | [v]
//   map           dict: encoded key → [key, value] variant     [index, value]
//   set           dict: encoded key → key
//   iterator      [array, pos]  (maps/sets: [snapshot, pos, dict])
//   Ref (T* / T& to a scalar)  [container, key]
//   stream        dict {"__stream": kind, ...state}
#include "Vm.h"
#include "Error.h"

#include <algorithm>
#include <cerrno>
#include <cctype>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <ctime>
#include <fstream>
#include <functional>
#include <iostream>
#include <map>
#include <memory>
#include <sstream>
#include <string>
#include <unordered_map>
#include <vector>

extern std::vector<std::string> g_scriptArgv;
extern bool g_testMode;

namespace
{
using QV = QuantumValue;
using ArrP = std::shared_ptr<Array>;
using DictP = std::shared_ptr<Dict>;

QV arg(const std::vector<QV> &a, size_t i) { return i < a.size() ? a[i] : QV(); }
double num(const QV &v)
{
    if (v.isNumber())
        return v.asNumber();
    if (v.isBool())
        return v.asBool() ? 1 : 0;
    if (v.isNil())
        return 0;
    if (v.isString())
    {
        const std::string &s = v.asString();
        return s.size() == 1 ? (unsigned char)s[0] : 0;
    }
    return 0;
}
QV N(double d) { return QV(d); }
QV S(const std::string &s) { return QV(s); }
QV B(bool b) { return QV(b); }
ArrP newArr(size_t n = 0) { return std::make_shared<Array>(n); }
QV A(ArrP a) { return QV(std::move(a)); }
QV A(std::initializer_list<QV> l) { return QV(std::make_shared<Array>(l)); }
std::string str(const QV &v)
{
    if (v.isString())
        return v.asString();
    if (v.isNil())
        return "";
    return v.toString();
}

const double NPOS = 18446744073709551615.0;

// ─── Thrown C++ exceptions ─────────────────────────────────────────────────

std::unordered_map<std::string, QV> *g_excDesc = nullptr; // std exception descriptors

QV makeExc(const std::string &cls, const std::string &msg)
{
    auto it = g_excDesc->find(cls);
    QV desc = it != g_excDesc->end() ? it->second : QV();
    return A({desc, S(msg)});
}
[[noreturn]] void throwExc(const std::string &cls, const std::string &msg) { throw CppThrow(makeExc(cls, msg)); }

// ─── Comparison / equality / keys ──────────────────────────────────────────

int cmpValues(const QV &a, const QV &b)
{
    if (a.isNumber() && b.isNumber())
    {
        double x = a.asNumber(), y = b.asNumber();
        return x < y ? -1 : x > y ? 1 : 0;
    }
    if (a.isString() && b.isString())
    {
        const std::string &x = a.asString(), &y = b.asString();
        size_t n = std::min(x.size(), y.size());
        int r = n ? std::memcmp(x.data(), y.data(), n) : 0;
        if (r)
            return r < 0 ? -1 : 1;
        return x.size() < y.size() ? -1 : x.size() > y.size() ? 1 : 0;
    }
    if (a.isNil() || b.isNil())
        return a.isNil() == b.isNil() ? 0 : (a.isNil() ? -1 : 1);
    if (a.isBool() || b.isBool())
    {
        double x = num(a), y = num(b);
        return x < y ? -1 : x > y ? 1 : 0;
    }
    if (a.isArray() && b.isArray())
    {
        const Array &x = *a.asArray(), &y = *b.asArray();
        size_t start = (!x.empty() && x[0].isDict()) ? 1 : 0; // class objects: skip the descriptor
        for (size_t i = start; i < x.size() && i < y.size(); ++i)
        {
            int c = cmpValues(x[i], y[i]);
            if (c)
                return c;
        }
        return x.size() < y.size() ? -1 : x.size() > y.size() ? 1 : 0;
    }
    if (a.isNumber() || b.isNumber())
    {
        double x = num(a), y = num(b);
        return x < y ? -1 : x > y ? 1 : 0;
    }
    return 0;
}

bool eqValues(const QV &a, const QV &b)
{
    if (a.isArray() && b.isArray())
    {
        const Array &x = *a.asArray(), &y = *b.asArray();
        if (&x == &y)
            return true;
        if (x.size() != y.size())
            return false;
        for (size_t i = 0; i < x.size(); ++i)
        {
            if (i == 0 && x[0].isDict() && y[0].isDict())
                continue;
            if (!eqValues(x[i], y[i]))
                return false;
        }
        return true;
    }
    if (a.isDict() && b.isDict())
    {
        const Dict &x = *a.asDict(), &y = *b.asDict();
        if (&x == &y)
            return true;
        if (x.size() != y.size())
            return false;
        for (auto &kv : x)
        {
            auto it = y.find(kv.first);
            if (it == y.end() || !eqValues(kv.second, it->second))
                return false;
        }
        return true;
    }
    if (a.isNumber() && b.isNumber())
        return a.asNumber() == b.asNumber();
    if (a.isString() && b.isString())
        return a.asString() == b.asString();
    if (a.isNil() || b.isNil())
        return a.isNil() && b.isNil();
    if (a.isBool() && b.isBool())
        return a.asBool() == b.asBool();
    return num(a) == num(b);
}

void encodeKey(const QV &v, std::string &out)
{
    if (v.isNumber())
    {
        char buf[40];
        double d = v.asNumber();
        if (d == 0)
            d = 0; // -0 and 0 are one key
        std::snprintf(buf, sizeof buf, "n%.17g;", d);
        out += buf;
    }
    else if (v.isString())
    {
        out += 's';
        out += std::to_string(v.asString().size());
        out += ':';
        out += v.asString();
    }
    else if (v.isBool())
        out += v.asBool() ? "b1;" : "b0;";
    else if (v.isNil())
        out += "z;";
    else if (v.isArray())
    {
        const Array &a = *v.asArray();
        out += 'a';
        out += std::to_string(a.size());
        out += '(';
        for (size_t i = 0; i < a.size(); ++i)
        {
            if (i == 0 && a[0].isDict())
                continue;
            encodeKey(a[i], out);
        }
        out += ')';
    }
    else
        out += "?" + v.toString() + ";";
}
std::string keyOf(const QV &v)
{
    std::string k;
    encodeKey(v, k);
    return k;
}

// ─── Map / set snapshots (cached per dict until it changes) ────────────────

struct Snap
{
    ArrP items; // pairs (maps) or keys (sets), in iteration order
    bool sorted;
    bool isMap;
};
std::unordered_map<const Dict *, Snap> g_snaps;

void invalidate(const DictP &d) { g_snaps.erase(d.get()); }

bool dictIsMap(const Dict &d)
{
    for (auto &kv : d)
        return kv.second.isArray() && kv.second.asArray()->size() == 2 && !kv.first.empty() && false;
    return false;
}

ArrP snapshot(const DictP &d, bool sorted, bool isMap)
{
    auto it = g_snaps.find(d.get());
    if (it != g_snaps.end() && it->second.sorted == sorted && it->second.isMap == isMap &&
        it->second.items->size() == d->size())
        return it->second.items;
    auto items = newArr();
    items->reserve(d->size());
    for (auto &kv : *d)
        items->push_back(kv.second);
    if (sorted)
        std::stable_sort(items->begin(), items->end(), [&](const QV &a, const QV &b)
                         {
                             if (isMap)
                                 return cmpValues(a.asArray()->at(0), b.asArray()->at(0)) < 0;
                             return cmpValues(a, b) < 0; });
    g_snaps[d.get()] = {items, sorted, isMap};
    return items;
}

QV entryKey(const QV &e, bool isMap) { return isMap ? e.asArray()->at(0) : e; }

// ─── Copies ────────────────────────────────────────────────────────────────

QV copyValue(const QV &v, const QV &plan)
{
    if (!plan.isArray() || v.isNil())
        return v;
    const Array &p = *plan.asArray();
    if (p.empty())
        return v;
    int kind = (int)num(p[0]);
    switch (kind)
    {
    case 1: // sequence
    {
        if (!v.isArray())
            return v;
        const Array &src = *v.asArray();
        auto out = newArr();
        out->reserve(src.size());
        QV ep = p.size() > 1 ? p[1] : QV();
        bool deep = ep.isArray();
        for (const auto &x : src)
            out->push_back(deep ? copyValue(x, ep) : x);
        return A(out);
    }
    case 2: // map
    {
        if (!v.isDict())
            return v;
        auto out = std::make_shared<Dict>();
        QV kp = p.size() > 1 ? p[1] : QV(), vp = p.size() > 2 ? p[2] : QV();
        for (auto &kv : *v.asDict())
        {
            const Array &pr = *kv.second.asArray();
            (*out)[kv.first] = A({copyValue(pr[0], kp), copyValue(pr[1], vp)});
        }
        return QV(out);
    }
    case 7: // set
    {
        if (!v.isDict())
            return v;
        auto out = std::make_shared<Dict>();
        for (auto &kv : *v.asDict())
            (*out)[kv.first] = kv.second;
        return QV(out);
    }
    case 3: // class object
    {
        if (!v.isArray())
            return v;
        const Array &src = *v.asArray();
        const Array &slots = *p[1].asArray();
        auto out = newArr(src.size());
        for (size_t i = 0; i < src.size(); ++i)
            (*out)[i] = (i > 0 && i < slots.size()) ? copyValue(src[i], slots[i]) : src[i];
        return A(out);
    }
    case 4: // optional box
    {
        if (!v.isArray() || v.asArray()->empty())
            return v;
        return A({copyValue(v.asArray()->at(0), p.size() > 1 ? p[1] : QV())});
    }
    case 5: // variant
    {
        if (!v.isArray() || v.asArray()->size() < 2)
            return v;
        const Array &src = *v.asArray();
        const Array &alts = *p[1].asArray();
        size_t idx = (size_t)num(src[0]);
        return A({src[0], idx < alts.size() ? copyValue(src[1], alts[idx]) : src[1]});
    }
    case 6: // pair / tuple
    {
        if (!v.isArray())
            return v;
        const Array &src = *v.asArray();
        const Array &el = *p[1].asArray();
        auto out = newArr(src.size());
        for (size_t i = 0; i < src.size(); ++i)
            (*out)[i] = i < el.size() ? copyValue(src[i], el[i]) : src[i];
        return A(out);
    }
    default:
        return v;
    }
}

// ─── Strings ───────────────────────────────────────────────────────────────

std::string chr(double c) { return std::string(1, (char)(long long)c); }

std::string fmtDouble(double d, int prec, bool fixed, bool sci, bool showpoint, bool upper)
{
    if (std::isnan(d))
        return std::signbit(d) ? (upper ? "-NAN" : "-nan") : (upper ? "NAN" : "nan");
    if (std::isinf(d))
        return d < 0 ? (upper ? "-INF" : "-inf") : (upper ? "INF" : "inf");
    char buf[512];
    if (fixed)
        std::snprintf(buf, sizeof buf, "%.*f", prec, d);
    else if (sci)
        std::snprintf(buf, sizeof buf, upper ? "%.*E" : "%.*e", prec, d);
    else
        std::snprintf(buf, sizeof buf, showpoint ? (upper ? "%#.*G" : "%#.*g") : (upper ? "%.*G" : "%.*g"),
                      prec == 0 ? 1 : prec, d);
    return buf;
}

std::string fmtInt(double v, int base, bool upper, bool showbase, bool showpos)
{
    long long x = (long long)v;
    char buf[80];
    if (base == 16)
    {
        std::snprintf(buf, sizeof buf, upper ? "%llX" : "%llx", (unsigned long long)x);
        return (showbase ? (upper ? "0X" : "0x") : "") + std::string(buf);
    }
    if (base == 8)
    {
        std::snprintf(buf, sizeof buf, "%llo", (unsigned long long)x);
        return (showbase && x ? "0" : "") + std::string(buf);
    }
    if (v >= 9.2233720368547758e18)
    {
        std::snprintf(buf, sizeof buf, "%llu", (unsigned long long)v);
        return buf;
    }
    std::snprintf(buf, sizeof buf, "%lld", x);
    return (showpos && x >= 0 ? "+" : "") + std::string(buf);
}

// ─── Streams ───────────────────────────────────────────────────────────────

struct OutFile
{
    std::ofstream f;
};
std::unordered_map<const Dict *, std::shared_ptr<OutFile>> g_files;

DictP stream(const QV &v)
{
    if (!v.isDict())
        throw RuntimeError("not a stream");
    return v.asDict();
}
std::string sget(const DictP &d, const char *k)
{
    auto it = d->find(k);
    return it == d->end() ? "" : str(it->second);
}
double nget(const DictP &d, const char *k, double def = 0)
{
    auto it = d->find(k);
    return it == d->end() || it->second.isNil() ? def : num(it->second);
}
bool bget(const DictP &d, const char *k)
{
    auto it = d->find(k);
    return it != d->end() && it->second.isTruthy();
}

DictP newStream(const std::string &kind)
{
    auto d = std::make_shared<Dict>();
    (*d)["__stream"] = S(kind);
    (*d)["buf"] = S("");
    (*d)["pos"] = N(0);
    (*d)["fail"] = B(false);
    (*d)["eof"] = B(false);
    (*d)["precision"] = N(6);
    (*d)["width"] = N(0);
    (*d)["fill"] = S(" ");
    (*d)["adjust"] = S("right");
    (*d)["float"] = S("");
    (*d)["base"] = N(10);
    return d;
}

void streamWrite(const DictP &d, const std::string &text)
{
    std::string kind = sget(d, "__stream");
    if (kind == "cout")
        std::cout << text;
    else if (kind == "cerr")
    {
        std::cout.flush();
        std::cerr << text;
    }
    else if (kind == "ofstream" || kind == "fstream")
    {
        auto it = g_files.find(d.get());
        if (it != g_files.end())
            it->second->f << text;
    }
    else
    {
        // string streams write at the put position (append)
        (*d)["buf"] = S(sget(d, "buf") + text);
    }
}

// Characters still to read, refilled from stdin for cin.
bool fill(const DictP &d)
{
    std::string kind = sget(d, "__stream");
    size_t pos = (size_t)nget(d, "pos");
    std::string buf = sget(d, "buf");
    if (pos < buf.size())
        return true;
    if (kind != "cin")
        return false;
    std::string line;
    if (g_testMode || !std::getline(std::cin, line))
        return false;
    (*d)["buf"] = S(line + "\n");
    (*d)["pos"] = N(0);
    return true;
}

int peekChar(const DictP &d)
{
    if (!fill(d))
        return -1;
    std::string buf = sget(d, "buf");
    return (unsigned char)buf[(size_t)nget(d, "pos")];
}
int getChar(const DictP &d)
{
    int c = peekChar(d);
    if (c >= 0)
        (*d)["pos"] = N(nget(d, "pos") + 1);
    return c;
}
void skipWs(const DictP &d)
{
    while (true)
    {
        int c = peekChar(d);
        if (c < 0 || !std::isspace(c))
            return;
        getChar(d);
    }
}
void setFail(const DictP &d, bool eof)
{
    (*d)["fail"] = B(true);
    if (eof)
        (*d)["eof"] = B(true);
}

// Formats `v` (kind i u d f c b s p ?) with the stream's state.
std::string formatFor(const DictP &d, const QV &v, const std::string &kind)
{
    int prec = (int)nget(d, "precision", 6);
    std::string fl = sget(d, "float");
    if (kind == "i" || kind == "u")
        return fmtInt(num(v), (int)nget(d, "base", 10), bget(d, "uppercase"), bget(d, "showbase"), bget(d, "showpos"));
    if (kind == "d" || kind == "f")
    {
        std::string s = fmtDouble(num(v), prec, fl == "fixed", fl == "scientific", bget(d, "showpoint"), bget(d, "uppercase"));
        if (bget(d, "showpos") && num(v) >= 0)
            s = "+" + s;
        return s;
    }
    if (kind == "c")
        return v.isString() ? v.asString() : chr(num(v));
    if (kind == "b")
        return bget(d, "boolalpha") ? (v.isTruthy() ? "true" : "false") : (v.isTruthy() ? "1" : "0");
    if (kind == "s")
        return str(v);
    if (kind == "p")
    {
        if (v.isNil())
            return "0";
        char buf[40];
        std::snprintf(buf, sizeof buf, "0x%llx", (unsigned long long)(uintptr_t)(v.isArray() ? (void *)v.asArray().get() : (void *)&v));
        return buf;
    }
    if (v.isNumber())
    {
        double x = v.asNumber();
        if (x == std::floor(x) && std::fabs(x) < 1e15)
            return fmtInt(x, 10, false, false, false);
        return fmtDouble(x, prec, fl == "fixed", fl == "scientific", false, false);
    }
    if (v.isBool())
        return v.asBool() ? "1" : "0";
    return str(v);
}

void applyManip(const DictP &d, const DictP &m)
{
    std::string n = str(m->at("__manip"));
    QV a = m->count("n") ? m->at("n") : QV();
    if (n == "endl")
    {
        streamWrite(d, "\n");
        std::string k = sget(d, "__stream");
        if (k == "cout")
            std::cout.flush();
        else if (k == "ofstream" && g_files.count(d.get()))
            g_files[d.get()]->f.flush();
    }
    else if (n == "flush")
    {
        if (sget(d, "__stream") == "cout")
            std::cout.flush();
    }
    else if (n == "ends")
        streamWrite(d, std::string(1, '\0'));
    else if (n == "fixed" || n == "scientific")
        (*d)["float"] = S(n);
    else if (n == "defaultfloat")
        (*d)["float"] = S("");
    else if (n == "left" || n == "right" || n == "internal")
        (*d)["adjust"] = S(n);
    else if (n == "boolalpha" || n == "showpoint" || n == "showpos" || n == "uppercase" || n == "showbase")
        (*d)[n] = B(true);
    else if (n == "noboolalpha" || n == "noshowpoint" || n == "noshowpos" || n == "nouppercase" || n == "noshowbase")
        (*d)[n.substr(2)] = B(false);
    else if (n == "hex")
        (*d)["base"] = N(16);
    else if (n == "oct")
        (*d)["base"] = N(8);
    else if (n == "dec")
        (*d)["base"] = N(10);
    else if (n == "setw")
        (*d)["width"] = N(num(a));
    else if (n == "setprecision")
        (*d)["precision"] = N(num(a));
    else if (n == "setfill")
        (*d)["fill"] = S(a.isString() ? a.asString() : chr(num(a)));
    else if (n == "setbase")
        (*d)["base"] = N(num(a));
    else if (n == "ws")
        skipWs(d);
}

void pad(const DictP &d, std::string &s)
{
    size_t w = (size_t)nget(d, "width");
    if (w > s.size())
    {
        std::string f = sget(d, "fill");
        char fc = f.empty() ? ' ' : f[0];
        std::string adj = sget(d, "adjust");
        if (adj == "left")
            s += std::string(w - s.size(), fc);
        else if (adj == "internal" && !s.empty() && (s[0] == '-' || s[0] == '+'))
            s = s.substr(0, 1) + std::string(w - s.size(), fc) + s.substr(1);
        else
            s = std::string(w - s.size(), fc) + s;
    }
    (*d)["width"] = N(0);
}

std::string readWord(const DictP &d)
{
    skipWs(d);
    std::string w;
    while (true)
    {
        int c = peekChar(d);
        if (c < 0 || std::isspace(c))
            break;
        w += (char)getChar(d);
    }
    return w;
}

// ─── printf ────────────────────────────────────────────────────────────────

std::string cppPrintf(const std::string &fmt, const Array &kinds, const Array &vals)
{
    std::string out;
    size_t vi = 0;
    for (size_t i = 0; i < fmt.size(); ++i)
    {
        char c = fmt[i];
        if (c != '%')
        {
            out += c;
            continue;
        }
        if (i + 1 < fmt.size() && fmt[i + 1] == '%')
        {
            out += '%';
            ++i;
            continue;
        }
        size_t j = i + 1;
        std::string spec = "%";
        while (j < fmt.size() && std::strchr("-+ #0", fmt[j]))
            spec += fmt[j++];
        auto starArg = [&]() -> int
        { return vi < vals.size() ? (int)num(vals[vi++]) : 0; };
        if (j < fmt.size() && fmt[j] == '*')
        {
            spec += std::to_string(starArg());
            ++j;
        }
        while (j < fmt.size() && std::isdigit((unsigned char)fmt[j]))
            spec += fmt[j++];
        if (j < fmt.size() && fmt[j] == '.')
        {
            spec += fmt[j++];
            if (j < fmt.size() && fmt[j] == '*')
            {
                spec += std::to_string(starArg());
                ++j;
            }
            while (j < fmt.size() && std::isdigit((unsigned char)fmt[j]))
                spec += fmt[j++];
        }
        while (j < fmt.size() && std::strchr("hlLqjzt", fmt[j]))
            ++j; // length modifiers: values are printed by their converted kind
        if (j >= fmt.size())
            break;
        char conv = fmt[j];
        i = j;
        QV v = vi < vals.size() ? vals[vi] : QV();
        std::string k = vi < kinds.size() ? str(kinds[vi]) : "?";
        ++vi;
        char buf[1024];
        switch (conv)
        {
        case 'd': case 'i':
            std::snprintf(buf, sizeof buf, (spec + "lld").c_str(), (long long)num(v));
            break;
        case 'u': case 'x': case 'X': case 'o':
            std::snprintf(buf, sizeof buf, (spec + "ll" + conv).c_str(), (unsigned long long)(long long)num(v));
            break;
        case 'f': case 'F': case 'e': case 'E': case 'g': case 'G': case 'a': case 'A':
            std::snprintf(buf, sizeof buf, (spec + conv).c_str(), num(v));
            break;
        case 'c':
            std::snprintf(buf, sizeof buf, (spec + "c").c_str(), v.isString() ? (v.asString().empty() ? 0 : v.asString()[0]) : (int)num(v));
            break;
        case 's':
        {
            std::string s = str(v);
            std::snprintf(buf, sizeof buf, (spec + "s").c_str(), s.c_str());
            if (s.size() > 900)
            {
                out += s;
                continue;
            }
            break;
        }
        case 'p':
            std::snprintf(buf, sizeof buf, "%p", v.isArray() ? (void *)v.asArray().get() : nullptr);
            break;
        default:
            buf[0] = 0;
        }
        out += buf;
    }
    return out;
}

// ─── Iterator / ref helpers ────────────────────────────────────────────────

struct It
{
    QV cont; // array, string, or snapshot array
    long pos = 0;
};
It itOf(const QV &v)
{
    It it;
    if (v.isArray())
    {
        const Array &a = *v.asArray();
        if (!a.empty())
            it.cont = a[0];
        if (a.size() > 1)
            it.pos = (long)num(a[1]);
    }
    return it;
}
QV itGet(const It &it)
{
    if (it.cont.isArray())
    {
        const Array &a = *it.cont.asArray();
        return it.pos >= 0 && it.pos < (long)a.size() ? a[it.pos] : QV();
    }
    if (it.cont.isString())
    {
        const std::string &s = it.cont.asString();
        return N(it.pos >= 0 && it.pos < (long)s.size() ? (unsigned char)s[it.pos] : 0);
    }
    return QV();
}
void itSet(const It &it, const QV &v)
{
    if (it.cont.isArray())
    {
        Array &a = *it.cont.asArray();
        if (it.pos >= 0 && it.pos < (long)a.size())
            a[it.pos] = v;
        else if (it.pos == (long)a.size())
            a.push_back(v);
    }
}
QV mkIt(const QV &cont, long pos, const QV &extra = QV())
{
    if (extra.isNil())
        return A({cont, N((double)pos)});
    return A({cont, N((double)pos), extra});
}

} // namespace

// ─── Registration ──────────────────────────────────────────────────────────

void VM::registerCppNatives()
{
    auto reg = [this](const std::string &name, QuantumNativeFunc fn)
    {
        auto n = std::make_shared<QuantumNative>();
        n->name = name;
        n->fn = std::move(fn);
        globals->define(name, QuantumValue(n));
    };
    auto call = [this](const QV &f, std::vector<QV> args) -> QV
    { return invokeCallable(f, args); };
    // comparator: nil → <, "greater" → >, closure → its result
    auto less = [this](const QV &cmp, const QV &a, const QV &b) -> bool
    {
        if (cmp.isNil() || (cmp.isString() && cmp.asString() == "less"))
            return cmpValues(a, b) < 0;
        if (cmp.isString() && cmp.asString() == "greater")
            return cmpValues(b, a) < 0;
        return invokeCallable(cmp, {a, b}).isTruthy();
    };

    // std exception descriptors
    static std::unordered_map<std::string, QV> descs;
    g_excDesc = &descs;
    struct E
    {
        const char *name, *base;
    };
    static const E exc[] = {{"exception", nullptr},
                            {"bad_alloc", "exception"},
                            {"bad_cast", "exception"},
                            {"bad_variant_access", "exception"},
                            {"bad_optional_access", "exception"},
                            {"bad_function_call", "exception"},
                            {"logic_error", "exception"},
                            {"invalid_argument", "logic_error"},
                            {"out_of_range", "logic_error"},
                            {"length_error", "logic_error"},
                            {"domain_error", "logic_error"},
                            {"runtime_error", "exception"},
                            {"range_error", "runtime_error"},
                            {"overflow_error", "runtime_error"},
                            {"underflow_error", "runtime_error"},
                            {"system_error", "runtime_error"}};
    std::map<std::string, std::string> baseOf;
    for (const E &e : exc)
    {
        std::string q = std::string("std::") + e.name;
        auto d = std::make_shared<Dict>();
        (*d)["name"] = S(q);
        auto bases = newArr();
        for (std::string n = e.name; !n.empty(); n = baseOf.count(n) ? baseOf[n] : "")
            bases->push_back(S("std::" + n));
        if (e.base)
        {
            baseOf[e.name] = e.base;
            bases->clear();
            for (std::string n = e.name; !n.empty(); n = baseOf.count(n) ? baseOf[n] : "")
                bases->push_back(S("std::" + n));
        }
        (*d)["bases"] = A(bases);
        (*d)["vt"] = QV(std::make_shared<Dict>());
        descs[e.name] = QV(d);
        globals->define(q + "#desc", QV(d));
    }

    // streams
    {
        auto out = newStream("cout");
        globals->define("__cpp_cout", QV(out));
        auto err = newStream("cerr");
        globals->define("__cpp_cerr", QV(err));
        auto in = newStream("cin");
        globals->define("__cpp_cin", QV(in));
    }

    // ── program ──
    reg("__cpp_argc__", [](std::vector<QV>) -> QV { return N((double)std::max<size_t>(1, g_scriptArgv.size())); });
    reg("__cpp_argv__", [](std::vector<QV>) -> QV
        {
        auto a = newArr();
        for (const auto &s : g_scriptArgv) a->push_back(S(s));
        if (a->empty()) a->push_back(S("program"));
        return A(a); });
    reg("__cpp_exit__", [](std::vector<QV> a) -> QV
        {
        std::cout.flush();
        std::cerr.flush();
        for (auto &f : g_files) f.second->f.flush();
        std::exit((int)num(arg(a, 0))); });
    reg("__cpp_terminate__", [this](std::vector<QV> a) -> QV
        {
        std::cout.flush();
        QV e = arg(a, 0);
        if (e.isArray() && !e.asArray()->empty() && e.asArray()->at(0).isDict())
        {
            DictP d = e.asArray()->at(0).asDict();
            std::string name = d->count("name") ? str(d->at("name")) : "?";
            std::string what;
            auto vt = d->count("vt") ? d->at("vt") : QV();
            if (vt.isDict() && vt.asDict()->count("what/0"))
                what = str(invokeCallable(vt.asDict()->at("what/0"), {e}));
            else if (e.asArray()->size() > 1)
                what = str(e.asArray()->at(1));
            bool isClass = name.rfind("std::", 0) == 0 || (d->count("bases") && d->at("bases").isArray() &&
                                                            d->at("bases").asArray()->size() > 0 && !(d->count("boxed")));
            if (d->count("boxed"))
                std::cerr << "terminate called after throwing an instance of '" << name << "'\n";
            else
            {
                std::cerr << "terminate called after throwing an instance of '" << name << "'\n";
                if (isClass)
                    std::cerr << "  what():  " << what << "\n";
            }
        }
        else
            std::cerr << "\x1b[31m[RuntimeError]\x1b[0m " << str(e) << "\n";
        std::cerr.flush();
        std::exit(3); });
    reg("__cpp_assert__", [](std::vector<QV> a) -> QV
        {
        if (!arg(a, 0).isTruthy())
        {
            std::cout.flush();
            std::cerr << "Assertion failed: " << str(arg(a, 1)) << "\n";
            std::exit(3);
        }
        return QV(); });

    // ── numbers ──
    reg("__cpp_idiv__", [](std::vector<QV> a) -> QV
        {
        double x = num(arg(a, 0)), y = num(arg(a, 1));
        if (y == 0) throw RuntimeError("integer division by zero");
        if (std::fabs(x) < 9.2e18 && std::fabs(y) < 9.2e18)
            return N((double)((long long)x / (long long)y));
        return N(std::trunc(x / y)); });
    reg("__cpp_imod__", [](std::vector<QV> a) -> QV
        {
        double x = num(arg(a, 0)), y = num(arg(a, 1));
        if (y == 0) throw RuntimeError("integer division by zero");
        if (std::fabs(x) < 9.2e18 && std::fabs(y) < 9.2e18)
            return N((double)((long long)x % (long long)y));
        return N(std::fmod(x, y)); });
    reg("__cpp_fmod__", [](std::vector<QV> a) -> QV { return N(std::fmod(num(arg(a, 0)), num(arg(a, 1)))); });
    reg("__cpp_trunc__", [](std::vector<QV> a) -> QV
        {
        double x = num(arg(a, 0));
        if (std::isnan(x)) return N(0);
        return N(std::trunc(x)); });
    reg("__cpp_b2i__", [](std::vector<QV> a) -> QV { return N(num(arg(a, 0))); });
    reg("__cpp_wrap__", [](std::vector<QV> a) -> QV
        {
        double m = num(arg(a, 1));
        double x = std::fmod(std::trunc(num(arg(a, 0))), m);
        if (x < 0) x += m;
        return N(x); });
    reg("__cpp_uwrap__", [](std::vector<QV> a) -> QV
        {
        double x = num(arg(a, 0));
        std::string t = str(arg(a, 1));
        if (x >= 0) return N(x);
        double m = (t == "unsigned int" || t == "unsigned long") ? 4294967296.0 : t == "unsigned short" ? 65536.0 : 18446744073709551616.0;
        return N(x + m); });
    reg("__cpp_math__", [](std::vector<QV> a) -> QV
        {
        std::string f = str(arg(a, 0));
        double x = num(arg(a, 1)), y = num(arg(a, 2));
        if (!f.empty() && f.back() == 'f' && f != "erf" && f != "erfc") f.pop_back();
        if (f == "sqrt") return N(std::sqrt(x));
        if (f == "cbrt") return N(std::cbrt(x));
        if (f == "exp") return N(std::exp(x));
        if (f == "exp2") return N(std::exp2(x));
        if (f == "expm1") return N(std::expm1(x));
        if (f == "log") return N(std::log(x));
        if (f == "log2") return N(std::log2(x));
        if (f == "log10") return N(std::log10(x));
        if (f == "log1p") return N(std::log1p(x));
        if (f == "sin") return N(std::sin(x));
        if (f == "cos") return N(std::cos(x));
        if (f == "tan") return N(std::tan(x));
        if (f == "asin") return N(std::asin(x));
        if (f == "acos") return N(std::acos(x));
        if (f == "atan") return N(std::atan(x));
        if (f == "sinh") return N(std::sinh(x));
        if (f == "cosh") return N(std::cosh(x));
        if (f == "tanh") return N(std::tanh(x));
        if (f == "asinh") return N(std::asinh(x));
        if (f == "acosh") return N(std::acosh(x));
        if (f == "atanh") return N(std::atanh(x));
        if (f == "floor") return N(std::floor(x));
        if (f == "ceil") return N(std::ceil(x));
        if (f == "round") return N(std::round(x));
        if (f == "trunc") return N(std::trunc(x));
        if (f == "fabs") return N(std::fabs(x));
        if (f == "nearbyint" || f == "rint") return N(std::nearbyint(x));
        if (f == "erf") return N(std::erf(x));
        if (f == "erfc") return N(std::erfc(x));
        if (f == "tgamma") return N(std::tgamma(x));
        if (f == "lgamma") return N(std::lgamma(x));
        if (f == "pow") return N(std::pow(x, y));
        if (f == "atan2") return N(std::atan2(x, y));
        if (f == "fmod") return N(std::fmod(x, y));
        if (f == "hypot") return N(std::hypot(x, y));
        if (f == "fmax") return N(std::fmax(x, y));
        if (f == "fmin") return N(std::fmin(x, y));
        if (f == "copysign") return N(std::copysign(x, y));
        if (f == "remainder") return N(std::remainder(x, y));
        if (f == "fdim") return N(std::fdim(x, y));
        if (f == "isnan") return B(std::isnan(x));
        if (f == "isinf") return B(std::isinf(x));
        if (f == "isfinite") return B(std::isfinite(x));
        if (f == "isnormal") return B(std::isnormal(x));
        if (f == "signbit") return B(std::signbit(x));
        throw RuntimeError("math function " + f); });
    reg("__cpp_min__", [less](std::vector<QV> a) -> QV { return less(arg(a, 2), arg(a, 1), arg(a, 0)) ? arg(a, 1) : arg(a, 0); });
    reg("__cpp_max__", [less](std::vector<QV> a) -> QV { return less(arg(a, 2), arg(a, 0), arg(a, 1)) ? arg(a, 1) : arg(a, 0); });
    reg("__cpp_minmax_list__", [](std::vector<QV> a) -> QV
        {
        const Array &v = *arg(a, 0).asArray();
        bool mx = arg(a, 1).isTruthy();
        QV best = v.empty() ? QV() : v[0];
        for (auto &x : v)
            if (mx ? cmpValues(best, x) < 0 : cmpValues(x, best) < 0) best = x;
        return best; });
    reg("__cpp_clamp__", [](std::vector<QV> a) -> QV
        {
        QV v = arg(a, 0), lo = arg(a, 1), hi = arg(a, 2);
        if (cmpValues(v, lo) < 0) return lo;
        if (cmpValues(hi, v) < 0) return hi;
        return v; });
    reg("__cpp_rand__", [](std::vector<QV>) -> QV { return N(std::rand()); });
    reg("__cpp_srand__", [](std::vector<QV> a) -> QV { std::srand((unsigned)(long long)num(arg(a, 0))); return QV(); });
    reg("__cpp_time__", [](std::vector<QV>) -> QV { return N((double)std::time(nullptr)); });
    reg("__cpp_clock__", [](std::vector<QV>) -> QV { return N((double)std::clock()); });
    reg("__cpp_system__", [](std::vector<QV> a) -> QV { std::cout.flush(); return N(std::system(str(arg(a, 0)).c_str())); });

    // ── values ──
    reg("__cpp_seq__", [](std::vector<QV> a) -> QV { return arg(a, a.empty() ? 0 : a.size() - 1); });
    reg("__cpp_seq3__", [](std::vector<QV> a) -> QV { return arg(a, 2); });
    reg("__cpp_in_ok__", [](std::vector<QV>) -> QV { return B(true); });
    reg("__cpp_isnull__", [](std::vector<QV> a) -> QV { return B(arg(a, 0).isNil()); });
    reg("__cpp_same__", [](std::vector<QV> a) -> QV
        {
        QV x = arg(a, 0), y = arg(a, 1);
        if (x.isNil() || y.isNil()) return B(x.isNil() && y.isNil());
        if (x.isArray() && y.isArray())
        {
            if (x.asArray() == y.asArray()) return B(true);
            // two refs to the same element
            const Array &p = *x.asArray(), &q = *y.asArray();
            if (p.size() == 2 && q.size() == 2 && p[0].isArray() && q[0].isArray() && p[0].asArray() == q[0].asArray())
                return B(eqValues(p[1], q[1]));
            return B(false);
        }
        if (x.isDict() && y.isDict()) return B(x.asDict() == y.asDict());
        if (x.isClosure() && y.isClosure()) return B(x.asFunction() == y.asFunction());
        return B(eqValues(x, y)); });
    reg("__cpp_eq__", [](std::vector<QV> a) -> QV { return B(eqValues(arg(a, 0), arg(a, 1))); });
    reg("__cpp_cmp__", [](std::vector<QV> a) -> QV { return N(cmpValues(arg(a, 0), arg(a, 1))); });
    reg("__cpp_copy__", [](std::vector<QV> a) -> QV { return copyValue(arg(a, 0), arg(a, 1)); });
    reg("__cpp_plan_set__", [](std::vector<QV> a) -> QV
        {
        ArrP p = arg(a, 0).asArray();
        p->clear();
        p->push_back(arg(a, 1));
        p->push_back(arg(a, 2));
        return QV(); });
    reg("__cpp_assign__", [](std::vector<QV> a) -> QV
        {
        QV dst = arg(a, 0), src = arg(a, 1);
        if (dst.isArray() && src.isArray())
        {
            ArrP d = dst.asArray(), s = src.asArray();
            if (d == s) return dst;
            if (!d->empty() && (*d)[0].isDict() && !s->empty() && (*s)[0].isDict())
            {
                // objects keep their own descriptor (and size)
                QV desc = (*d)[0];
                size_t n = d->size();
                Array copy = *s;
                copy.resize(std::max(n, (size_t)1));
                copy[0] = desc;
                *d = std::move(copy);
                return dst;
            }
            *d = *s;
            return dst;
        }
        if (dst.isDict() && src.isDict())
        {
            DictP d = dst.asDict(), s = src.asDict();
            if (d == s) return dst;
            invalidate(d);
            Dict fresh;
            for (auto &kv : *s) fresh[kv.first] = kv.second;
            *d = std::move(fresh);
            return dst;
        }
        return src; });
    reg("__cpp_alloc__", [](std::vector<QV> a) -> QV
        {
        auto o = newArr((size_t)num(arg(a, 1)));
        (*o)[0] = arg(a, 0);
        return A(o); });
    reg("__cpp_class__", [](std::vector<QV> a) -> QV
        {
        auto d = std::make_shared<Dict>();
        (*d)["name"] = arg(a, 0);
        (*d)["bases"] = arg(a, 1);
        auto vt = std::make_shared<Dict>();
        QV base = arg(a, 2);
        if (base.isDict() && base.asDict()->count("vt"))
            for (auto &kv : *base.asDict()->at("vt").asDict()) (*vt)[kv.first] = kv.second;
        (*d)["vt"] = QV(vt);
        return QV(d); });
    reg("__cpp_vt_set__", [](std::vector<QV> a) -> QV
        {
        (*arg(a, 0).asDict()->at("vt").asDict())[str(arg(a, 1))] = arg(a, 2);
        return QV(); });
    auto isa = [](const QV &v, const std::string &name) -> bool
    {
        if (!v.isArray() || v.asArray()->empty() || !v.asArray()->at(0).isDict())
            return false;
        DictP d = v.asArray()->at(0).asDict();
        auto it = d->find("bases");
        if (it == d->end() || !it->second.isArray())
            return false;
        for (auto &b : *it->second.asArray())
            if (b.isString() && b.asString() == name)
                return true;
        return false;
    };
    reg("__cpp_isa__", [isa](std::vector<QV> a) -> QV
        {
        QV v = arg(a, 0);
        if (std::getenv("QUANTUM_DEBUG_CPPFE") && !(v.isArray() && !v.asArray()->empty() && v.asArray()->at(0).isDict()))
            std::cerr << "[cppfe] caught a non-C++ exception: " << str(v) << "\n";
        return B(isa(v, str(arg(a, 1)))); });
    reg("__cpp_dyncast__", [isa](std::vector<QV> a) -> QV
        {
        QV p = arg(a, 0);
        if (p.isNil()) return p;
        if (isa(p, str(arg(a, 1)))) return p;
        if (arg(a, 2).isTruthy()) throwExc("bad_cast", "std::bad_cast");
        return QV(); });
    reg("__cpp_exc__", [](std::vector<QV> a) -> QV { return A({arg(a, 0), S(str(arg(a, 1)))}); });
    reg("__cpp_exc_init__", [](std::vector<QV> a) -> QV
        {
        ArrP o = arg(a, 0).asArray();
        if (o->size() < 2) o->resize(2);
        (*o)[1] = S(str(arg(a, 1)));
        return arg(a, 0); });
    reg("__cpp_boxexc__", [](std::vector<QV> a) -> QV
        {
        auto d = std::make_shared<Dict>();
        std::string t = str(arg(a, 0));
        (*d)["name"] = S(t);
        (*d)["bases"] = A({S(t)});
        (*d)["boxed"] = B(true);
        return A({QV(d), arg(a, 1)}); });
    reg("__cpp_throw__", [](std::vector<QV> a) -> QV { throw CppThrow(arg(a, 0)); });
    reg("__cpp_what__", [this](std::vector<QV> a) -> QV
        {
        QV e = arg(a, 0);
        if (e.isArray() && !e.asArray()->empty() && e.asArray()->at(0).isDict())
        {
            DictP d = e.asArray()->at(0).asDict();
            if (d->count("vt") && d->at("vt").asDict()->count("what/0"))
                return invokeCallable(d->at("vt").asDict()->at("what/0"), {e});
            return e.asArray()->size() > 1 ? e.asArray()->at(1) : S("");
        }
        return S(str(e)); });

    // ── strings ──
    reg("__cpp_chr__", [](std::vector<QV> a) -> QV
        {
        QV c = arg(a, 0);
        if (c.isString()) return c;
        return S(chr(num(c))); });
    reg("__cpp_str_at__", [](std::vector<QV> a) -> QV
        {
        QV s = arg(a, 0);
        long i = (long)num(arg(a, 1));
        if (!s.isString()) return N(0);
        const std::string &x = s.asString();
        return N(i >= 0 && i < (long)x.size() ? (unsigned char)x[i] : 0); });
    reg("__cpp_str_set__", [](std::vector<QV> a) -> QV
        {
        std::string s = str(arg(a, 0));
        long i = (long)num(arg(a, 1));
        if (i >= 0 && i < (long)s.size()) s[i] = (char)(long long)num(arg(a, 2));
        return S(s); });
    reg("__cpp_str_check__", [](std::vector<QV> a) -> QV
        {
        std::string s = str(arg(a, 0));
        double i = num(arg(a, 1));
        if (i < 0 || i >= (double)s.size())
            throwExc("out_of_range", "basic_string::at: __n (which is " + fmtInt(i, 10, false, false, false) +
                                         ") >= this->size() (which is " + std::to_string(s.size()) + ")");
        return N(i); });
    reg("__cpp_size__", [](std::vector<QV> a) -> QV
        {
        QV v = arg(a, 0);
        if (v.isString()) return N((double)v.asString().size());
        if (v.isArray()) return N((double)v.asArray()->size());
        if (v.isDict()) return N((double)v.asDict()->size());
        return N(0); });
    reg("__cpp_substr__", [](std::vector<QV> a) -> QV
        {
        std::string s = str(arg(a, 0));
        double pos = num(arg(a, 1)), len = num(arg(a, 2));
        if (pos > (double)s.size())
            throwExc("out_of_range", "basic_string::substr: __pos (which is " + fmtInt(pos, 10, false, false, false) +
                                         ") > this->size() (which is " + std::to_string(s.size()) + ")");
        if (len < 0 || len > 1e18) return S(s.substr((size_t)pos));
        return S(s.substr((size_t)pos, (size_t)len)); });
    reg("__cpp_str_from__", [](std::vector<QV> a) -> QV
        {
        std::string s = str(arg(a, 0));
        long n = (long)num(arg(a, 1));
        if (n < 0) n = 0;
        return S(n < (long)s.size() ? s.substr(n) : ""); });
    reg("__cpp_str_repeat__", [](std::vector<QV> a) -> QV
        {
        long n = (long)num(arg(a, 0));
        QV c = arg(a, 1);
        std::string ch = c.isString() ? c.asString() : chr(num(c));
        std::string out;
        for (long i = 0; i < n; ++i) out += ch;
        return S(out); });
    reg("__cpp_str_find__", [](std::vector<QV> a) -> QV
        {
        std::string op = str(arg(a, 0)), s = str(arg(a, 1)), nd = str(arg(a, 2));
        QV posv = arg(a, 3);
        bool hasPos = !posv.isNil();
        double posd = hasPos ? num(posv) : 0;
        size_t pos = hasPos ? (posd >= 1e18 ? std::string::npos : (size_t)posd) : 0;
        if (a.size() > 4 && !arg(a, 4).isNil()) nd = nd.substr(0, (size_t)num(arg(a, 4)));
        size_t r;
        if (op == "find") r = s.find(nd, pos);
        else if (op == "rfind") r = s.rfind(nd, hasPos ? pos : std::string::npos);
        else if (op == "find_first_of") r = s.find_first_of(nd, pos);
        else if (op == "find_last_of") r = s.find_last_of(nd, hasPos ? pos : std::string::npos);
        else if (op == "find_first_not_of") r = s.find_first_not_of(nd, pos);
        else if (op == "find_last_not_of") r = s.find_last_not_of(nd, hasPos ? pos : std::string::npos);
        else if (op == "contains") return B(s.find(nd) != std::string::npos);
        else if (op == "starts_with") return B(s.compare(0, nd.size(), nd) == 0 && s.size() >= nd.size());
        else if (op == "ends_with") return B(s.size() >= nd.size() && s.compare(s.size() - nd.size(), nd.size(), nd) == 0);
        else r = std::string::npos;
        return N(r == std::string::npos ? NPOS : (double)r); });
    reg("__cpp_str_compare__", [](std::vector<QV> a) -> QV
        {
        std::string x = str(arg(a, 0)), y = str(arg(a, 1));
        size_t n = std::min(x.size(), y.size());
        for (size_t i = 0; i < n; ++i)
            if (x[i] != y[i]) return N((double)((int)(unsigned char)x[i] - (int)(unsigned char)y[i]));
        long long d = (long long)x.size() - (long long)y.size();
        return N((double)(d > 2147483647 ? 2147483647 : d < -2147483648LL ? -2147483648LL : d)); });
    reg("__cpp_str_reversed__", [](std::vector<QV> a) -> QV
        {
        std::string s = str(arg(a, 0));
        std::reverse(s.begin(), s.end());
        return S(s); });
    reg("__cpp_str_pop__", [](std::vector<QV> a) -> QV
        {
        std::string s = str(arg(a, 0));
        if (!s.empty()) s.pop_back();
        return S(s); });
    reg("__cpp_str_range__", [](std::vector<QV> a) -> QV
        {
        It f = itOf(arg(a, 0)), l = itOf(arg(a, 1));
        std::string out;
        if (f.cont.isString())
        {
            const std::string &s = f.cont.asString();
            long e = std::min<long>(l.pos, (long)s.size());
            for (long i = f.pos; i < e; ++i) out += s[i];
            return S(out);
        }
        for (long i = f.pos; i < l.pos; ++i)
        {
            QV v = itGet({f.cont, i});
            out += v.isString() ? v.asString() : chr(num(v));
        }
        return S(out); });
    reg("__cpp_str_edit__", [](std::vector<QV> a) -> QV
        {
        std::string op = str(arg(a, 0));
        std::string s = str(arg(a, 1));
        const Array &args = *arg(a, 2).asArray();
        const Array &kinds = *arg(a, 3).asArray();
        auto k = [&](size_t i) { return i < kinds.size() ? str(kinds[i]) : ""; };
        auto text = [&](size_t i) -> std::string
        {
            QV v = args[i];
            return k(i) == "c" ? chr(num(v)) : str(v);
        };
        auto idx = [&](size_t i) -> size_t
        {
            double d = num(args[i]);
            return d < 0 ? 0 : d > 1e18 ? std::string::npos : (size_t)d;
        };
        if (op == "insert")
        {
            if (args.size() == 2)
            {
                size_t p = std::min(idx(0), s.size());
                s.insert(p, text(1));
            }
            else if (args.size() == 3 && (k(2) == "c"))
            {
                size_t p = std::min(idx(0), s.size());
                s.insert(p, (size_t)num(args[1]), (char)(long long)num(args[2]));
            }
            else if (args.size() >= 3)
            {
                size_t p = std::min(idx(0), s.size());
                std::string t = str(args[1]);
                s.insert(p, t.substr(idx(2) < t.size() ? idx(2) : t.size(), args.size() > 3 ? idx(3) : std::string::npos));
            }
        }
        else if (op == "erase")
        {
            if (args.empty()) s.clear();
            else if (args.size() == 1 && k(0) == "I") { if (idx(0) < s.size()) s.erase(idx(0), 1); }
            else if (args.size() == 2 && k(0) == "I") s.erase(idx(0), idx(1) - idx(0));
            else
            {
                size_t p = idx(0);
                if (p > s.size()) throwExc("out_of_range", "basic_string::erase");
                s.erase(p, args.size() > 1 ? idx(1) : std::string::npos);
            }
        }
        else if (op == "replace")
        {
            size_t p = idx(0), n = idx(1);
            if (k(0) == "I") n = idx(1) - idx(0);
            if (p > s.size()) throwExc("out_of_range", "basic_string::replace");
            if (args.size() == 4 && k(3) == "c")
                s.replace(p, n, (size_t)num(args[2]), (char)(long long)num(args[3]));
            else
                s.replace(p, n, text(2));
        }
        else if (op == "resize")
        {
            size_t n = idx(0);
            char c = args.size() > 1 ? (char)(long long)num(args[1]) : '\0';
            s.resize(n, c);
        }
        else if (op == "assign")
        {
            if (args.size() == 2 && k(1) == "c") s.assign((size_t)num(args[0]), (char)(long long)num(args[1]));
            else s = text(0);
        }
        return S(s); });
    reg("__cpp_str_algo__", [this, less](std::vector<QV> a) -> QV
        {
        std::string op = str(arg(a, 0));
        std::string s = str(arg(a, 1));
        if (op == "sort" || op == "stable_sort" || op == "partial_sort")
        {
            QV cmp = arg(a, op == "partial_sort" ? 3 : 2);
            std::vector<int> c(s.begin(), s.end());
            for (auto &x : c) x = (unsigned char)x;
            std::stable_sort(c.begin(), c.end(), [&](int x, int y) { return less(cmp, N(x), N(y)); });
            std::string out;
            for (int x : c) out += (char)x;
            return S(out);
        }
        if (op == "reverse") { std::reverse(s.begin(), s.end()); return S(s); }
        if (op == "transform")
        {
            QV f = arg(a, 2);
            for (auto &ch : s) ch = (char)(long long)num(invokeCallable(f, {N((unsigned char)ch)}));
            return S(s);
        }
        if (op == "fill") { char c = (char)(long long)num(arg(a, 2)); for (auto &ch : s) ch = c; return S(s); }
        if (op == "remove") { char c = (char)(long long)num(arg(a, 2)); s.erase(std::remove(s.begin(), s.end(), c), s.end()); return S(s); }
        if (op == "remove_if")
        {
            QV f = arg(a, 2);
            std::string out;
            for (char ch : s) if (!invokeCallable(f, {N((unsigned char)ch)}).isTruthy()) out += ch;
            return S(out);
        }
        if (op == "unique") { s.erase(std::unique(s.begin(), s.end()), s.end()); return S(s); }
        if (op == "replace")
        {
            char o = (char)(long long)num(arg(a, 2)), n = (char)(long long)num(arg(a, 3));
            for (auto &ch : s) if (ch == o) ch = n;
            return S(s);
        }
        if (op == "rotate") return S(s);
        if (op == "shuffle" || op == "random_shuffle") { std::random_shuffle(s.begin(), s.end()); return S(s); }
        return S(s); });
    reg("__cpp_str_perm__", [](std::vector<QV> a) -> QV
        {
        // next/prev_permutation through a Ref to the string
        ArrP r = arg(a, 0).asArray();
        ArrP cont = (*r)[0].asArray();
        size_t key = (size_t)num((*r)[1]);
        std::string s = str((*cont)[key]);
        bool ok = arg(a, 1).isTruthy() ? std::next_permutation(s.begin(), s.end()) : std::prev_permutation(s.begin(), s.end());
        (*cont)[key] = S(s);
        return B(ok); });
    reg("__cpp_strcmp__", [](std::vector<QV> a) -> QV
        {
        std::string x = str(arg(a, 0)), y = str(arg(a, 1));
        double n = num(arg(a, 2));
        if (n >= 0) { x = x.substr(0, (size_t)n); y = y.substr(0, (size_t)n); }
        if (arg(a, 3).isTruthy())
        {
            for (auto &c : x) c = (char)std::tolower((unsigned char)c);
            for (auto &c : y) c = (char)std::tolower((unsigned char)c);
        }
        int r = x.compare(y);
        return N(r < 0 ? -1 : r > 0 ? 1 : 0); });
    reg("__cpp_strfind__", [](std::vector<QV> a) -> QV
        {
        std::string s = str(arg(a, 0)), n = str(arg(a, 1));
        size_t p = s.find(n);
        return p == std::string::npos ? QV() : S(s.substr(p)); });
    reg("__cpp_ctype__", [](std::vector<QV> a) -> QV
        {
        std::string f = str(arg(a, 0));
        int c = (int)num(arg(a, 1));
        if (c < -1 || c > 255) c = 0;
        if (f == "isalpha") return N(std::isalpha(c));
        if (f == "isdigit") return N(std::isdigit(c));
        if (f == "isalnum") return N(std::isalnum(c));
        if (f == "isspace") return N(std::isspace(c));
        if (f == "isupper") return N(std::isupper(c));
        if (f == "islower") return N(std::islower(c));
        if (f == "ispunct") return N(std::ispunct(c));
        if (f == "isxdigit") return N(std::isxdigit(c));
        if (f == "iscntrl") return N(std::iscntrl(c));
        if (f == "isprint") return N(std::isprint(c));
        if (f == "isgraph") return N(std::isgraph(c));
        if (f == "isblank") return N(std::isblank(c));
        if (f == "tolower") return N(std::tolower(c));
        if (f == "toupper") return N(std::toupper(c));
        return N(0); });
    reg("__cpp_to_string__", [](std::vector<QV> a) -> QV
        {
        QV v = arg(a, 0);
        std::string k = str(arg(a, 1));
        if (k == "d" || k == "f")
        {
            char buf[512];
            std::snprintf(buf, sizeof buf, "%f", num(v));
            return S(buf);
        }
        if (k == "b") return S(v.isTruthy() ? "1" : "0");
        if (k == "s") return S(str(v));
        return S(fmtInt(num(v), 10, false, false, false)); });
    reg("__cpp_sto__", [](std::vector<QV> a) -> QV
        {
        std::string s = str(arg(a, 0));
        QV posRef = arg(a, 1);
        int base = (int)num(arg(a, 2));
        std::string fn = str(arg(a, 3));
        bool ato = fn.rfind("ato", 0) == 0 || fn.rfind("strto", 0) == 0;
        bool fl = fn == "stof" || fn == "stod" || fn == "stold" || fn == "atof" || fn == "strtod";
        const char *b = s.c_str();
        char *end = nullptr;
        errno = 0;
        double r = 0;
        if (fl)
            r = std::strtod(b, &end);
        else if (fn == "stoul" || fn == "stoull" || fn == "strtoul")
            r = (double)std::strtoull(b, &end, base);
        else
        {
            long long v = std::strtoll(b, &end, base);
            r = (double)v;
            if (errno != ERANGE && (fn == "stoi" || fn == "stol" || fn == "atoi" || fn == "atol") &&
                (v > 2147483647LL || v < -2147483648LL))
                errno = ERANGE;
        }
        if (end == b)
        {
            if (ato) return N(0);
            throwExc("invalid_argument", fn);
        }
        if (errno == ERANGE)
        {
            if (ato) return N(r);
            throwExc("out_of_range", fn);
        }
        if (posRef.isArray())
        {
            ArrP ref = posRef.asArray();
            if (ref->size() >= 2 && (*ref)[0].isArray())
                (*(*ref)[0].asArray())[(size_t)num((*ref)[1])] = N((double)(end - b));
        }
        return N(r); });

    // ── printf family ──
    reg("__cpp_printf__", [](std::vector<QV> a) -> QV
        {
        std::string target = str(arg(a, 0));
        std::string out = cppPrintf(str(arg(a, 1)), *arg(a, 2).asArray(), *arg(a, 3).asArray());
        if (target == "string") return S(out);
        if (target == "stderr") { std::cout.flush(); std::cerr << out; }
        else std::cout << out;
        return N((double)out.size()); });
    reg("__cpp_puts__", [](std::vector<QV> a) -> QV { std::cout << str(arg(a, 0)) << "\n"; return N(1); });
    reg("__cpp_putchar__", [](std::vector<QV> a) -> QV { std::cout << (char)(long long)num(arg(a, 0)); return arg(a, 0); });
    reg("__cpp_getchar__", [this](std::vector<QV>) -> QV
        {
        DictP in = globals->get("__cpp_cin").asDict();
        return N(getChar(in)); });

    // ── streams ──
    reg("__cpp_manip__", [](std::vector<QV> a) -> QV
        {
        auto d = std::make_shared<Dict>();
        (*d)["__manip"] = arg(a, 0);
        if (a.size() > 1) (*d)["n"] = arg(a, 1);
        return QV(d); });
    reg("__cpp_stream_new__", [](std::vector<QV> a) -> QV
        {
        std::string kind = str(arg(a, 0));
        auto d = newStream(kind);
        if (kind == "ifstream" || kind == "fstream" && false)
        {
            std::ifstream f(str(arg(a, 1)), std::ios::binary);
            if (!f) (*d)["fail"] = B(true);
            else
            {
                std::ostringstream ss;
                ss << f.rdbuf();
                (*d)["buf"] = S(ss.str());
            }
            (*d)["open"] = B((bool)f);
        }
        else if (kind == "ofstream" || kind == "fstream")
        {
            auto of = std::make_shared<OutFile>();
            bool app = num(arg(a, 2)) == 1;
            if (a.size() > 1)
            {
                of->f.open(str(arg(a, 1)), app ? std::ios::app : std::ios::trunc);
                if (!of->f) (*d)["fail"] = B(true);
                (*d)["open"] = B((bool)of->f);
            }
            g_files[d.get()] = of;
        }
        else if (a.size() > 1)
            (*d)["buf"] = S(str(arg(a, 1)));
        return QV(d); });
    reg("__cpp_out__", [](std::vector<QV> a) -> QV
        {
        QV sv = arg(a, 0);
        DictP d = stream(sv);
        QV v = arg(a, 1);
        std::string k = str(arg(a, 2));
        if (k == "m" && v.isDict())
        {
            applyManip(d, v.asDict());
            return sv;
        }
        if (k == "r" && v.isDict() && v.asDict()->count("__rdbuf"))
        {
            DictP src = v.asDict()->at("__rdbuf").asDict();
            std::string buf = sget(src, "buf");
            size_t pos = (size_t)nget(src, "pos");
            streamWrite(d, pos < buf.size() ? buf.substr(pos) : "");
            (*src)["pos"] = N((double)buf.size());
            return sv;
        }
        std::string text = formatFor(d, v, k);
        pad(d, text);
        streamWrite(d, text);
        return sv; });
    reg("__cpp_in__", [](std::vector<QV> a) -> QV
        {
        DictP d = stream(arg(a, 0));
        std::string k = str(arg(a, 1));
        // Test mode feeds cin nothing, so every read fails. A failed numeric
        // read yields 1 there rather than C++'s 0 (as __cin__ does) so
        // arithmetic on the value doesn't divide by zero; the stream still
        // fails, so `while (cin >> x)` loops end.
        QV failedNum = N(g_testMode && sget(d, "__stream") == "cin" ? 1 : 0);
        if (bget(d, "fail")) return k == "s" ? S("") : failedNum;
        if (k == "c")
        {
            skipWs(d);
            int c = getChar(d);
            if (c < 0) { setFail(d, true); return N(0); }
            return N(c);
        }
        std::string w = readWord(d);
        if (w.empty()) { setFail(d, true); return k == "s" ? S("") : failedNum; }
        if (k == "s" || k == "?") return S(w);
        char *end = nullptr;
        double r = (k == "d" || k == "f") ? std::strtod(w.c_str(), &end) : (double)std::strtoll(w.c_str(), &end, 10);
        if (end == w.c_str()) { setFail(d, false); return N(0); }
        // unread the rest of the token (e.g. "12abc")
        size_t used = (size_t)(end - w.c_str());
        if (used < w.size()) (*d)["pos"] = N(nget(d, "pos") - (double)(w.size() - used));
        if (k == "b") return B(r != 0);
        return N(r); });
    reg("__cpp_getline__", [](std::vector<QV> a) -> QV
        {
        DictP d = stream(arg(a, 0));
        char delim = a.size() > 1 ? (char)(long long)num(arg(a, 1)) : '\n';
        if (bget(d, "fail")) return S("");
        if (!fill(d)) { setFail(d, true); return S(""); }
        std::string line;
        while (true)
        {
            int c = getChar(d);
            if (c < 0) { (*d)["eof"] = B(true); break; }
            if (c == (unsigned char)delim) break;
            line += (char)c;
        }
        return S(line); });
    reg("__cpp_stream_good__", [](std::vector<QV> a) -> QV
        {
        QV v = arg(a, 0);
        if (!v.isDict()) return B(v.isTruthy());
        return B(!bget(v.asDict(), "fail")); });
    reg("__cpp_ss_str__", [](std::vector<QV> a) -> QV { return S(sget(stream(arg(a, 0)), "buf")); });
    reg("__cpp_ss_setstr__", [](std::vector<QV> a) -> QV
        {
        DictP d = stream(arg(a, 0));
        (*d)["buf"] = S(str(arg(a, 1)));
        (*d)["pos"] = N(0);
        (*d)["fail"] = B(false);
        (*d)["eof"] = B(false);
        return QV(); });
    reg("__cpp_stream_call__", [](std::vector<QV> a) -> QV
        {
        QV sv = arg(a, 0);
        DictP d = stream(sv);
        std::string m = str(arg(a, 1));
        const Array &args = *arg(a, 2).asArray();
        if (m == "good") return B(!bget(d, "fail") && !bget(d, "eof"));
        if (m == "fail" || m == "bad") return B(bget(d, "fail"));
        if (m == "eof") { if (!bget(d, "eof") && peekChar(d) < 0 && sget(d, "__stream") != "cin") (*d)["eof"] = B(true); return B(bget(d, "eof")); }
        if (m == "clear") { (*d)["fail"] = B(false); (*d)["eof"] = B(false); return QV(); }
        if (m == "is_open") return B(bget(d, "open"));
        if (m == "close") { auto it = g_files.find(d.get()); if (it != g_files.end()) { it->second->f.close(); } (*d)["open"] = B(false); return QV(); }
        if (m == "flush") { if (sget(d, "__stream") == "cout") std::cout.flush(); auto it = g_files.find(d.get()); if (it != g_files.end()) it->second->f.flush(); return sv; }
        if (m == "get") { int c = getChar(d); if (c < 0) setFail(d, true); return N(c); }
        if (m == "peek") return N(peekChar(d));
        if (m == "unget" || m == "putback") { (*d)["pos"] = N(std::max(0.0, nget(d, "pos") - 1)); return sv; }
        if (m == "ignore")
        {
            double n = args.empty() ? 1 : num(args[0]);
            int delim = args.size() > 1 ? (int)num(args[1]) : -1;
            for (double i = 0; i < n; ++i) { int c = getChar(d); if (c < 0 || c == delim) break; }
            return sv;
        }
        if (m == "put") { streamWrite(d, chr(args.empty() ? 0 : num(args[0]))); return sv; }
        if (m == "write") { std::string s = args.empty() ? "" : str(args[0]); if (args.size() > 1) s = s.substr(0, (size_t)num(args[1])); streamWrite(d, s); return sv; }
        if (m == "precision") { double old = nget(d, "precision", 6); if (!args.empty()) (*d)["precision"] = N(num(args[0])); return N(old); }
        if (m == "width") { double old = nget(d, "width"); if (!args.empty()) (*d)["width"] = N(num(args[0])); return N(old); }
        if (m == "fill") { if (!args.empty()) (*d)["fill"] = S(chr(num(args[0]))); return QV(); }
        if (m == "tellg") return N(nget(d, "pos"));
        if (m == "seekg") { if (!args.empty()) (*d)["pos"] = N(num(args[0])); (*d)["eof"] = B(false); (*d)["fail"] = B(false); return sv; }
        if (m == "rdbuf") { auto r = std::make_shared<Dict>(); (*r)["__rdbuf"] = sv; return QV(r); }
        return QV(); });

    // ── containers ──
    reg("__cpp_dict__", [](std::vector<QV>) -> QV { return QV(std::make_shared<Dict>()); });
    reg("__cpp_fill__", [](std::vector<QV> a) -> QV
        {
        long n = (long)num(arg(a, 0));
        QV v = arg(a, 1), plan = arg(a, 2);
        auto out = newArr();
        out->reserve(std::max(0L, n));
        for (long i = 0; i < n; ++i) out->push_back(plan.isArray() ? copyValue(v, plan) : v);
        return A(out); });
    reg("__cpp_fill_to__", [](std::vector<QV> a) -> QV
        {
        ArrP arr = arg(a, 0).asArray();
        long n = (long)num(arg(a, 1));
        QV v = arg(a, 2), plan = arg(a, 3);
        while ((long)arr->size() < n) arr->push_back(plan.isArray() ? copyValue(v, plan) : v);
        return arg(a, 0); });
    reg("__cpp_fill_prefix__", [](std::vector<QV> a) -> QV
        {
        ArrP arr = arg(a, 0).asArray();
        const Array &init = *arg(a, 1).asArray();
        for (size_t i = 0; i < init.size() && i < arr->size(); ++i) (*arr)[i] = init[i];
        return arg(a, 0); });
    reg("__cpp_fill_all__", [](std::vector<QV> a) -> QV
        {
        ArrP arr = arg(a, 0).asArray();
        QV v = arg(a, 1), plan = arg(a, 2);
        for (auto &x : *arr) x = plan.isArray() ? copyValue(v, plan) : v;
        return QV(); });
    reg("__cpp_push__", [](std::vector<QV> a) -> QV
        {
        QV c = arg(a, 0);
        if (c.isArray()) c.asArray()->push_back(arg(a, 1));
        return QV(); });
    reg("__cpp_push_front__", [](std::vector<QV> a) -> QV
        {
        ArrP c = arg(a, 0).asArray();
        c->insert(c->begin(), arg(a, 1));
        return QV(); });
    reg("__cpp_pop__", [](std::vector<QV> a) -> QV
        {
        ArrP c = arg(a, 0).asArray();
        if (!c->empty()) c->pop_back();
        return QV(); });
    reg("__cpp_pop_front__", [](std::vector<QV> a) -> QV
        {
        ArrP c = arg(a, 0).asArray();
        if (!c->empty()) c->erase(c->begin());
        return QV(); });
    reg("__cpp_clear__", [](std::vector<QV> a) -> QV
        {
        QV c = arg(a, 0);
        if (c.isArray()) c.asArray()->clear();
        else if (c.isDict()) { invalidate(c.asDict()); *c.asDict() = Dict(); }
        return QV(); });
    reg("__cpp_check_index__", [](std::vector<QV> a) -> QV
        {
        double n = (double)arg(a, 0).asArray()->size();
        double i = num(arg(a, 1));
        if (i < 0 || i >= n)
            throwExc("out_of_range", "vector::_M_range_check: __n (which is " + fmtInt(i, 10, false, false, false) +
                                         ") >= this->size() (which is " + fmtInt(n, 10, false, false, false) + ")");
        return N(i); });
    reg("__cpp_reversed__", [](std::vector<QV> a) -> QV
        {
        auto out = std::make_shared<Array>(*arg(a, 0).asArray());
        std::reverse(out->begin(), out->end());
        return A(out); });
    reg("__cpp_resize__", [](std::vector<QV> a) -> QV
        {
        ArrP v = arg(a, 0).asArray();
        long n = (long)num(arg(a, 1));
        QV val = arg(a, 2), plan = arg(a, 3);
        if (n < (long)v->size()) v->resize(n);
        while ((long)v->size() < n) v->push_back(plan.isArray() ? copyValue(val, plan) : val);
        return QV(); });
    reg("__cpp_insert__", [](std::vector<QV> a) -> QV
        {
        ArrP v = arg(a, 0).asArray();
        long p = std::clamp<long>(itOf(arg(a, 1)).pos, 0, (long)v->size());
        v->insert(v->begin() + p, arg(a, 2));
        return mkIt(arg(a, 0), p); });
    reg("__cpp_insert_n__", [](std::vector<QV> a) -> QV
        {
        ArrP v = arg(a, 0).asArray();
        long p = std::clamp<long>(itOf(arg(a, 1)).pos, 0, (long)v->size());
        long n = (long)num(arg(a, 2));
        QV val = arg(a, 3), plan = arg(a, 4);
        for (long i = 0; i < n; ++i) v->insert(v->begin() + p, plan.isArray() ? copyValue(val, plan) : val);
        return mkIt(arg(a, 0), p); });
    reg("__cpp_insert_range__", [](std::vector<QV> a) -> QV
        {
        ArrP v = arg(a, 0).asArray();
        long p = std::clamp<long>(itOf(arg(a, 1)).pos, 0, (long)v->size());
        It src = itOf(arg(a, 2));
        Array items;
        if (src.cont.isArray()) items.assign(src.cont.asArray()->begin() + std::min<long>(src.pos, (long)src.cont.asArray()->size()), src.cont.asArray()->end());
        v->insert(v->begin() + p, items.begin(), items.end());
        return mkIt(arg(a, 0), p); });
    reg("__cpp_insert_range2__", [](std::vector<QV> a) -> QV
        {
        ArrP v = arg(a, 0).asArray();
        long p = std::clamp<long>(itOf(arg(a, 1)).pos, 0, (long)v->size());
        It f = itOf(arg(a, 2)), l = itOf(arg(a, 3));
        Array items;
        for (long i = f.pos; i < l.pos; ++i) items.push_back(itGet({f.cont, i}));
        v->insert(v->begin() + p, items.begin(), items.end());
        return mkIt(arg(a, 0), p); });
    reg("__cpp_erase__", [](std::vector<QV> a) -> QV
        {
        ArrP v = arg(a, 0).asArray();
        long f = std::clamp<long>(itOf(arg(a, 1)).pos, 0, (long)v->size());
        long l = arg(a, 2).isNil() ? f + 1 : std::clamp<long>(itOf(arg(a, 2)).pos, f, (long)v->size());
        if (f < (long)v->size()) v->erase(v->begin() + f, v->begin() + std::min<long>(l, (long)v->size()));
        return mkIt(arg(a, 0), f); });
    reg("__cpp_range_array__", [](std::vector<QV> a) -> QV
        {
        It f = itOf(arg(a, 0)), l = itOf(arg(a, 1));
        auto out = newArr();
        if (f.cont.isString())
        {
            const std::string &s = f.cont.asString();
            for (long i = f.pos; i < l.pos && i < (long)s.size(); ++i) out->push_back(N((unsigned char)s[i]));
            return A(out);
        }
        for (long i = f.pos; i < l.pos; ++i) out->push_back(itGet({f.cont, i}));
        return A(out); });
    reg("__cpp_pq_push__", [less](std::vector<QV> a) -> QV
        {
        ArrP v = arg(a, 0).asArray();
        bool minHeap = arg(a, 2).isTruthy();
        v->push_back(arg(a, 1));
        auto lt = [&](const QV &x, const QV &y) { return minHeap ? cmpValues(y, x) < 0 : cmpValues(x, y) < 0; };
        std::push_heap(v->begin(), v->end(), lt);
        return QV(); });
    reg("__cpp_pq_pop__", [](std::vector<QV> a) -> QV
        {
        ArrP v = arg(a, 0).asArray();
        bool minHeap = arg(a, 1).isTruthy();
        auto lt = [&](const QV &x, const QV &y) { return minHeap ? cmpValues(y, x) < 0 : cmpValues(x, y) < 0; };
        if (!v->empty()) { std::pop_heap(v->begin(), v->end(), lt); v->pop_back(); }
        return QV(); });
    reg("__cpp_swap_obj__", [](std::vector<QV> a) -> QV
        {
        QV x = arg(a, 0), y = arg(a, 1);
        if (x.isArray() && y.isArray()) std::swap(*x.asArray(), *y.asArray());
        else if (x.isDict() && y.isDict()) { invalidate(x.asDict()); invalidate(y.asDict()); std::swap(*x.asDict(), *y.asDict()); }
        return QV(); });
    reg("__cpp_swap__", [](std::vector<QV> a) -> QV
        {
        ArrP r1 = arg(a, 0).asArray(), r2 = arg(a, 1).asArray();
        ArrP c1 = (*r1)[0].asArray(), c2 = (*r2)[0].asArray();
        size_t k1 = (size_t)num((*r1)[1]), k2 = (size_t)num((*r2)[1]);
        std::swap((*c1)[k1], (*c2)[k2]);
        return QV(); });
    reg("__cpp_exchange__", [](std::vector<QV> a) -> QV
        {
        ArrP r = arg(a, 0).asArray();
        ArrP c = (*r)[0].asArray();
        size_t k = (size_t)num((*r)[1]);
        QV old = (*c)[k];
        (*c)[k] = arg(a, 1);
        return old; });
    reg("__cpp_tie__", [](std::vector<QV> a) -> QV { return arg(a, 0); });
    reg("__cpp_tie_assign__", [](std::vector<QV> a) -> QV
        {
        const Array &refs = *arg(a, 0).asArray();
        const Array &vals = *arg(a, 1).asArray();
        for (size_t i = 0; i < refs.size() && i < vals.size(); ++i)
        {
            if (refs[i].isNil()) continue; // std::ignore
            ArrP r = refs[i].asArray();
            (*(*r)[0].asArray())[(size_t)num((*r)[1])] = vals[i];
        }
        return arg(a, 1); });
    reg("__cpp_ptr_index__", [](std::vector<QV> a) -> QV
        {
        QV p = arg(a, 0);
        long i = (long)num(arg(a, 1));
        if (p.isArray() && p.asArray()->size() == 2 && p.asArray()->at(0).isArray() && !p.asArray()->at(0).asArray()->empty() &&
            !p.asArray()->at(0).asArray()->at(0).isDict())
        {
            ArrP c = p.asArray()->at(0).asArray();
            long k = (long)num(p.asArray()->at(1)) + i;
            return k >= 0 && k < (long)c->size() ? (*c)[k] : QV();
        }
        if (p.isArray() && p.asArray()->size() == 2 && p.asArray()->at(0).isArray())
        {
            ArrP c = p.asArray()->at(0).asArray();
            long k = (long)num(p.asArray()->at(1)) + i;
            return k >= 0 && k < (long)c->size() ? (*c)[k] : QV();
        }
        return p; });
    reg("__cpp_ptr_first__", [](std::vector<QV> a) -> QV
        {
        ArrP r = arg(a, 0).asArray();
        ArrP c = (*r)[0].asArray();
        return c->empty() ? QV() : (*c)[0]; });
    reg("__cpp_deref__", [](std::vector<QV> a) -> QV
        {
        QV p = arg(a, 0);
        if (p.isArray() && p.asArray()->size() == 2 && p.asArray()->at(0).isArray())
        {
            ArrP c = p.asArray()->at(0).asArray();
            size_t k = (size_t)num(p.asArray()->at(1));
            return k < c->size() ? (*c)[k] : QV();
        }
        if (p.isPointer()) return p.asPointer()->deref();
        return p; });

    // iterators
    reg("__cpp_it_add__", [](std::vector<QV> a) -> QV
        {
        QV it = arg(a, 0);
        if (!it.isArray()) return it;
        auto out = std::make_shared<Array>(*it.asArray());
        if (out->size() > 1) (*out)[1] = N(num((*out)[1]) + num(arg(a, 1)));
        return A(out); });
    reg("__cpp_ref_add__", [](std::vector<QV> a) -> QV
        {
        QV it = arg(a, 0);
        if (!it.isArray()) return it;
        auto out = std::make_shared<Array>(*it.asArray());
        if (out->size() > 1) (*out)[1] = N(num((*out)[1]) + num(arg(a, 1)));
        return A(out); });
    reg("__cpp_it_diff__", [](std::vector<QV> a) -> QV { return N((double)(itOf(arg(a, 0)).pos - itOf(arg(a, 1)).pos)); });
    reg("__cpp_it_eq__", [](std::vector<QV> a) -> QV
        {
        QV x = arg(a, 0), y = arg(a, 1);
        if (!x.isArray() || !y.isArray()) return B(x.isNil() && y.isNil());
        const Array &p = *x.asArray(), &q = *y.asArray();
        long pp = p.size() > 1 ? (long)num(p[1]) : 0, qp = q.size() > 1 ? (long)num(q[1]) : 0;
        if (p.size() > 2 || q.size() > 2)
        {
            // map / set iterators: both at the end, or the same entry
            long pn = p[0].isArray() ? (long)p[0].asArray()->size() : 0;
            long qn = q[0].isArray() ? (long)q[0].asArray()->size() : 0;
            bool pe = pp >= pn, qe = qp >= qn;
            if (pe || qe) return B(pe && qe);
            QV ea = p[0].asArray()->at(pp), eb = q[0].asArray()->at(qp);
            if (ea.isArray() && eb.isArray()) return B(ea.asArray() == eb.asArray());
            return B(eqValues(ea, eb));
        }
        if (p[0].isArray() && q[0].isArray()) return B(p[0].asArray() == q[0].asArray() && pp == qp);
        return B(pp == qp && eqValues(p[0], q[0])); });

    // map / set
    auto mapGet = [](const DictP &d, const QV &k) -> QV
    {
        std::string key = keyOf(k);
        auto it = d->find(key);
        return it == d->end() ? QV() : it->second;
    };
    reg("__cpp_map_get__", [](std::vector<QV> a) -> QV
        {
        DictP d = arg(a, 0).asDict();
        std::string key = keyOf(arg(a, 1));
        auto it = d->find(key);
        if (it != d->end()) return it->second;
        invalidate(d);
        QV entry = A({arg(a, 1), arg(a, 2)});
        (*d)[key] = entry;
        return entry; });
    reg("__cpp_map_at__", [mapGet](std::vector<QV> a) -> QV
        {
        QV e = mapGet(arg(a, 0).asDict(), arg(a, 1));
        if (e.isNil()) throwExc("out_of_range", "map::at");
        return e; });
    reg("__cpp_dict_count__", [mapGet](std::vector<QV> a) -> QV { return N(mapGet(arg(a, 0).asDict(), arg(a, 1)).isNil() ? 0 : 1); });
    auto isMapDict = [](const DictP &d) -> bool
    {
        // map entries are [key, value] arrays; set entries are the keys
        for (auto &kv : *d)
        {
            if (!kv.second.isArray() || kv.second.asArray()->size() != 2)
                return false;
            return keyOf(kv.second.asArray()->at(0)) == kv.first;
        }
        return false;
    };
    reg("__cpp_map_items__", [](std::vector<QV> a) -> QV
        {
        QV m = arg(a, 0);
        if (!m.isDict()) return A(newArr());
        return A(snapshot(m.asDict(), arg(a, 1).isTruthy(), true)); });
    reg("__cpp_set_items__", [](std::vector<QV> a) -> QV
        {
        QV m = arg(a, 0);
        if (!m.isDict()) return A(newArr());
        return A(snapshot(m.asDict(), arg(a, 1).isTruthy(), false)); });
    reg("__cpp_dict_find__", [isMapDict](std::vector<QV> a) -> QV
        {
        std::string op = str(arg(a, 0));
        DictP d = arg(a, 1).asDict();
        QV k = arg(a, 2);
        bool sorted = arg(a, 3).isTruthy();
        bool isMap = isMapDict(d);
        ArrP snap = snapshot(d, sorted, isMap);
        long n = (long)snap->size();
        if (op == "find")
        {
            std::string key = keyOf(k);
            auto it = d->find(key);
            if (it == d->end()) return mkIt(A(snap), n, arg(a, 1));
            for (long i = 0; i < n; ++i)
                if ((isMap && (*snap)[i].asArray() == it->second.asArray()) || (!isMap && eqValues((*snap)[i], it->second)))
                    return mkIt(A(snap), i, arg(a, 1));
            return mkIt(A(snap), n, arg(a, 1));
        }
        // lower_bound / upper_bound on the sorted order
        long i = 0;
        for (; i < n; ++i)
        {
            int c = cmpValues(entryKey((*snap)[i], isMap), k);
            if (op == "lower_bound" ? c >= 0 : c > 0) break;
        }
        return mkIt(A(snap), i, arg(a, 1)); });
    reg("__cpp_dict_iter__", [isMapDict](std::vector<QV> a) -> QV
        {
        std::string op = str(arg(a, 0));
        DictP d = arg(a, 1).asDict();
        bool isMap = isMapDict(d) || d->empty();
        ArrP snap = snapshot(d, arg(a, 2).isTruthy(), isMap);
        if (op == "rbegin" || op == "rend")
        {
            auto rev = std::make_shared<Array>(*snap);
            std::reverse(rev->begin(), rev->end());
            return mkIt(A(rev), op == "rbegin" ? 0 : (long)rev->size(), arg(a, 1));
        }
        bool begin = op == "begin" || op == "cbegin";
        return mkIt(A(snap), begin ? 0 : (long)snap->size(), arg(a, 1)); });
    reg("__cpp_dict_erase__", [](std::vector<QV> a) -> QV
        {
        DictP d = arg(a, 0).asDict();
        std::string key = keyOf(arg(a, 1));
        auto it = d->find(key);
        if (it == d->end()) return N(0);
        invalidate(d);
        Dict fresh;
        for (auto &kv : *d) if (kv.first != key) fresh[kv.first] = kv.second;
        *d = std::move(fresh);
        return N(1); });
    reg("__cpp_dict_erase_it__", [isMapDict](std::vector<QV> a) -> QV
        {
        DictP d = arg(a, 0).asDict();
        It it = itOf(arg(a, 1));
        bool sorted = arg(a, 2).isTruthy();
        bool isMap = isMapDict(d);
        if (!it.cont.isArray() || it.pos < 0 || it.pos >= (long)it.cont.asArray()->size()) return arg(a, 1);
        QV entry = it.cont.asArray()->at(it.pos);
        std::string key = keyOf(entryKey(entry, isMap));
        Dict fresh;
        for (auto &kv : *d) if (kv.first != key) fresh[kv.first] = kv.second;
        invalidate(d);
        *d = std::move(fresh);
        ArrP snap = snapshot(d, sorted, isMap);
        return mkIt(A(snap), it.pos, arg(a, 0)); });
    auto insertEntry = [isMapDict](std::vector<QV> &a, bool isMap) -> QV
    {
        DictP d = arg(a, 0).asDict();
        QV k = arg(a, 1), v = arg(a, 2);
        bool assignIfExists = arg(a, 4).isTruthy();
        bool sorted = arg(a, 5).isTruthy();
        std::string key = keyOf(k);
        auto it = d->find(key);
        bool inserted = false;
        if (it == d->end())
        {
            invalidate(d);
            (*d)[key] = isMap ? A({k, v}) : k;
            inserted = true;
        }
        else if (assignIfExists && isMap)
            it->second.asArray()->at(1) = v;
        ArrP snap = snapshot(d, sorted, isMap);
        QV entry = d->find(key)->second;
        long pos = 0;
        for (; pos < (long)snap->size(); ++pos)
            if ((isMap && (*snap)[pos].isArray() && (*snap)[pos].asArray() == entry.asArray()) || (!isMap && eqValues((*snap)[pos], entry)))
                break;
        return A({mkIt(A(snap), pos, arg(a, 0)), B(inserted)});
    };
    reg("__cpp_map_insert__", [insertEntry](std::vector<QV> a) -> QV { return insertEntry(a, true); });
    reg("__cpp_set_insert__", [insertEntry](std::vector<QV> a) -> QV { return insertEntry(a, false); });
    reg("__cpp_map_from__", [](std::vector<QV> a) -> QV
        {
        auto d = std::make_shared<Dict>();
        for (auto &p : *arg(a, 0).asArray())
        {
            const Array &pr = *p.asArray();
            std::string key = keyOf(pr[0]);
            if (!d->count(key)) (*d)[key] = A({pr[0], pr[1]});
        }
        return QV(d); });
    reg("__cpp_set_from__", [](std::vector<QV> a) -> QV
        {
        auto d = std::make_shared<Dict>();
        for (auto &k : *arg(a, 0).asArray())
        {
            std::string key = keyOf(k);
            if (!d->count(key)) (*d)[key] = k;
        }
        return QV(d); });

    // optional / variant
    reg("__cpp_opt_check__", [](std::vector<QV> a) -> QV
        {
        if (arg(a, 0).isNil()) throwExc("bad_optional_access", "bad optional access");
        return arg(a, 0); });
    reg("__cpp_opt_or__", [](std::vector<QV> a) -> QV
        {
        QV o = arg(a, 0);
        return o.isNil() ? arg(a, 1) : o.asArray()->at(0); });
    reg("__cpp_vcheck__", [](std::vector<QV> a) -> QV
        {
        QV v = arg(a, 0);
        if (!v.isArray() || num(v.asArray()->at(0)) != num(arg(a, 1)))
            throwExc("bad_variant_access", "std::get: wrong index for variant");
        return v; });
    reg("__cpp_vget_if__", [](std::vector<QV> a) -> QV
        {
        QV v = arg(a, 0);
        if (!v.isArray() || num(v.asArray()->at(0)) != num(arg(a, 1))) return QV();
        return A({v, N(1)}); });
    reg("__cpp_vget_obj_if__", [](std::vector<QV> a) -> QV
        {
        QV v = arg(a, 0);
        if (!v.isArray() || num(v.asArray()->at(0)) != num(arg(a, 1))) return QV();
        return v.asArray()->at(1); });
    reg("__cpp_visit__", [call](std::vector<QV> a) -> QV
        {
        QV v = arg(a, 1);
        return call(arg(a, 0), {v.isArray() && v.asArray()->size() > 1 ? v.asArray()->at(1) : v}); });

    // algorithms over [first, last) iterator ranges
    reg("__cpp_sort__", [less](std::vector<QV> a) -> QV
        {
        It f = itOf(arg(a, 0)), l = itOf(arg(a, 1));
        if (!f.cont.isArray()) return QV();
        Array &v = *f.cont.asArray();
        long b = std::clamp<long>(f.pos, 0, (long)v.size()), e = std::clamp<long>(l.pos, b, (long)v.size());
        QV cmp = arg(a, 2);
        std::stable_sort(v.begin() + b, v.begin() + e, [&](const QV &x, const QV &y) { return less(cmp, x, y); });
        return QV(); });
    reg("__cpp_reverse__", [](std::vector<QV> a) -> QV
        {
        It f = itOf(arg(a, 0)), l = itOf(arg(a, 1));
        if (!f.cont.isArray()) return QV();
        Array &v = *f.cont.asArray();
        long b = std::clamp<long>(f.pos, 0, (long)v.size()), e = std::clamp<long>(l.pos, b, (long)v.size());
        std::reverse(v.begin() + b, v.begin() + e);
        return QV(); });
    reg("__cpp_shuffle__", [](std::vector<QV> a) -> QV
        {
        It f = itOf(arg(a, 0)), l = itOf(arg(a, 1));
        if (!f.cont.isArray()) return QV();
        Array &v = *f.cont.asArray();
        long b = std::clamp<long>(f.pos, 0, (long)v.size()), e = std::clamp<long>(l.pos, b, (long)v.size());
        for (long i = e - 1; i > b; --i) std::swap(v[i], v[b + std::rand() % (i - b + 1)]);
        return QV(); });
    reg("__cpp_transform__", [call](std::vector<QV> a) -> QV
        {
        It f = itOf(arg(a, 0)), l = itOf(arg(a, 1));
        bool binary = a.size() >= 5;
        It f2 = binary ? itOf(arg(a, 2)) : It();
        It d = itOf(arg(a, binary ? 3 : 2));
        QV fn = arg(a, binary ? 4 : 3);
        long n = l.pos - f.pos;
        for (long i = 0; i < n; ++i)
        {
            QV r = binary ? call(fn, {itGet({f.cont, f.pos + i}), itGet({f2.cont, f2.pos + i})}) : call(fn, {itGet({f.cont, f.pos + i})});
            itSet({d.cont, d.pos + i}, r);
        }
        return mkIt(d.cont, d.pos + n); });
    reg("__cpp_algo__", [call, less](std::vector<QV> a) -> QV
        {
        std::string op = str(arg(a, 0));
        It f = itOf(arg(a, 1)), l = itOf(arg(a, 2));
        QV x = arg(a, 3), y = arg(a, 4), z = arg(a, 5);
        long b = f.pos, e = l.pos;
        auto at = [&](long i) { return itGet({f.cont, i}); };
        auto res = [&](long i) { return mkIt(f.cont, i, arg(a, 1).isArray() && arg(a, 1).asArray()->size() > 2 ? arg(a, 1).asArray()->at(2) : QV()); };
        auto truthy = [&](const QV &fn, const QV &v) { return call(fn, {v}).isTruthy(); };
        if (op == "count") { long c = 0; for (long i = b; i < e; ++i) if (eqValues(at(i), x)) ++c; return N((double)c); }
        if (op == "count_if") { long c = 0; for (long i = b; i < e; ++i) if (truthy(x, at(i))) ++c; return N((double)c); }
        if (op == "find") { for (long i = b; i < e; ++i) if (eqValues(at(i), x)) return res(i); return res(e); }
        if (op == "find_if") { for (long i = b; i < e; ++i) if (truthy(x, at(i))) return res(i); return res(e); }
        if (op == "find_if_not") { for (long i = b; i < e; ++i) if (!truthy(x, at(i))) return res(i); return res(e); }
        if (op == "all_of") { for (long i = b; i < e; ++i) if (!truthy(x, at(i))) return B(false); return B(true); }
        if (op == "any_of") { for (long i = b; i < e; ++i) if (truthy(x, at(i))) return B(true); return B(false); }
        if (op == "none_of") { for (long i = b; i < e; ++i) if (truthy(x, at(i))) return B(false); return B(true); }
        if (op == "for_each") { for (long i = b; i < e; ++i) call(x, {at(i)}); return x; }
        if (op == "accumulate" || op == "reduce" || op == "accumulate_str")
        {
            QV acc = x.isNil() && op == "reduce" ? N(0) : x;
            for (long i = b; i < e; ++i)
            {
                if (!y.isNil()) acc = call(y, {acc, at(i)});
                else if (acc.isString() || at(i).isString()) acc = S(str(acc) + str(at(i)));
                else acc = N(num(acc) + num(at(i)));
            }
            return acc;
        }
        if (op == "inner_product")
        {
            It f2 = itOf(x);
            double acc = num(y);
            for (long i = 0; i < e - b; ++i) acc += num(at(b + i)) * num(itGet({f2.cont, f2.pos + i}));
            return N(acc);
        }
        if (op == "partial_sum")
        {
            It d = itOf(x);
            QV acc;
            for (long i = 0; i < e - b; ++i) { acc = i == 0 ? at(b) : N(num(acc) + num(at(b + i))); itSet({d.cont, d.pos + i}, acc); }
            return mkIt(d.cont, d.pos + (e - b));
        }
        if (op == "min_element" || op == "max_element")
        {
            if (b >= e) return res(e);
            long best = b;
            for (long i = b + 1; i < e; ++i)
                if (op == "min_element" ? less(x, at(i), at(best)) : less(x, at(best), at(i))) best = i;
            return res(best);
        }
        if (op == "minmax_element")
        {
            long mn = b, mx = b;
            for (long i = b + 1; i < e; ++i) { if (less(x, at(i), at(mn))) mn = i; if (!less(x, at(i), at(mx))) mx = i; }
            return A({res(mn), res(mx)});
        }
        if (op == "binary_search" || op == "lower_bound" || op == "upper_bound")
        {
            long lo = b, hi = e;
            while (lo < hi)
            {
                long mid = (lo + hi) / 2;
                bool goRight = op == "upper_bound" ? !less(y, x, at(mid)) : less(y, at(mid), x);
                if (goRight) lo = mid + 1; else hi = mid;
            }
            if (op == "binary_search") return B(lo < e && !less(y, x, at(lo)));
            return res(lo);
        }
        if (!f.cont.isArray()) return QV();
        Array &v = *f.cont.asArray();
        b = std::clamp<long>(b, 0, (long)v.size());
        e = std::clamp<long>(e, b, (long)v.size());
        if (op == "fill") { for (long i = b; i < e; ++i) v[i] = x; return QV(); }
        if (op == "fill_n") { long n = (long)num(arg(a, 2)); for (long i = 0; i < n && b + i < (long)v.size(); ++i) v[b + i] = arg(a, 3); return QV(); }
        if (op == "iota") { double s = num(x); for (long i = b; i < e; ++i) v[i] = N(s++); return QV(); }
        if (op == "replace") { for (long i = b; i < e; ++i) if (eqValues(v[i], x)) v[i] = y; return QV(); }
        if (op == "replace_if") { for (long i = b; i < e; ++i) if (truthy(x, v[i])) v[i] = y; return QV(); }
        if (op == "remove" || op == "remove_if" || op == "unique")
        {
            long w = b;
            for (long i = b; i < e; ++i)
            {
                bool drop = op == "remove" ? eqValues(v[i], x) : op == "remove_if" ? truthy(x, v[i]) : (w > b && eqValues(v[w - 1], v[i]));
                if (!drop) v[w++] = v[i];
            }
            return res(w);
        }
        if (op == "next_permutation" || op == "prev_permutation")
        {
            auto lt = [&](const QV &p, const QV &q) { return less(x, p, q); };
            bool ok = op == "next_permutation" ? std::next_permutation(v.begin() + b, v.begin() + e, lt)
                                               : std::prev_permutation(v.begin() + b, v.begin() + e, lt);
            return B(ok);
        }
        if (op == "is_sorted") { for (long i = b + 1; i < e; ++i) if (less(x, v[i], v[i - 1])) return B(false); return B(true); }
        if (op == "adjacent_find") { for (long i = b + 1; i < e; ++i) if (eqValues(v[i - 1], v[i])) return res(i - 1); return res(e); }
        if (op == "equal")
        {
            It f2 = itOf(x);
            for (long i = 0; i < e - b; ++i) if (!eqValues(v[b + i], itGet({f2.cont, f2.pos + i}))) return B(false);
            return B(true);
        }
        if (op == "copy" || op == "copy_if")
        {
            It d = itOf(x);
            long w = d.pos;
            for (long i = b; i < e; ++i)
                if (op == "copy" || truthy(y, v[i])) itSet({d.cont, w++}, v[i]);
            return mkIt(d.cont, w);
        }
        if (op == "rotate")
        {
            long mid = itOf(arg(a, 2)).pos, last = itOf(x).pos;
            mid = std::clamp<long>(mid, b, (long)v.size());
            last = std::clamp<long>(last, mid, (long)v.size());
            std::rotate(v.begin() + b, v.begin() + mid, v.begin() + last);
            return res(b + (last - mid));
        }
        if (op == "nth_element")
        {
            long last = itOf(x).pos;
            std::stable_sort(v.begin() + b, v.begin() + std::clamp<long>(last, b, (long)v.size()), [&](const QV &p, const QV &q) { return less(y, p, q); });
            return QV();
        }
        return QV(); });
}
