#include "json.h"

#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstdlib>

namespace rt {

static void escape(const std::string& s, std::string& out) {
  out += '"';
  for (unsigned char c : s) {
    switch (c) {
      case '"': out += "\\\""; break;
      case '\\': out += "\\\\"; break;
      case '\n': out += "\\n"; break;
      case '\r': out += "\\r"; break;
      case '\t': out += "\\t"; break;
      default:
        if (c < 0x20) {
          char b[8];
          snprintf(b, sizeof b, "\\u%04x", c);
          out += b;
        } else {
          out += (char)c;
        }
    }
  }
  out += '"';
}

static void write(const Json& j, std::string& out, int ind) {
  switch (j.t) {
    case Json::T::Null: out += "null"; break;
    case Json::T::Bool: out += j.b ? "true" : "false"; break;
    case Json::T::Num: {
      char b[40];
      if (!std::isfinite(j.n)) snprintf(b, sizeof b, "0");
      else if (fabs(j.n) < 1e15 && j.n == std::floor(j.n)) snprintf(b, sizeof b, "%.0f", j.n);
      else snprintf(b, sizeof b, "%.5g", j.n);
      out += b;
      break;
    }
    case Json::T::Str: escape(j.s, out); break;
    case Json::T::Arr:
      if (j.a.empty()) { out += "[]"; break; }
      out += "[\n";
      for (size_t i = 0; i < j.a.size(); i++) {
        out.append((size_t)ind + 2, ' ');
        write(j.a[i], out, ind + 2);
        out += i + 1 < j.a.size() ? ",\n" : "\n";
      }
      out.append((size_t)ind, ' ');
      out += ']';
      break;
    case Json::T::Obj:
      if (j.o.empty()) { out += "{}"; break; }
      out += "{\n";
      for (size_t i = 0; i < j.o.size(); i++) {
        out.append((size_t)ind + 2, ' ');
        escape(j.o[i].first, out);
        out += ": ";
        write(j.o[i].second, out, ind + 2);
        out += i + 1 < j.o.size() ? ",\n" : "\n";
      }
      out.append((size_t)ind, ' ');
      out += '}';
      break;
  }
}

std::string Json::dump() const {
  std::string s;
  write(*this, s, 0);
  s += '\n';
  return s;
}

namespace {
struct Parser {
  const char* p;
  const char* e;
  int depth = 0;

  void ws() {
    while (p < e && (*p == ' ' || *p == '\t' || *p == '\r' || *p == '\n')) p++;
  }
  bool lit(const char* w) {
    const char* q = p;
    while (*w) {
      if (q >= e || *q != *w) return false;
      q++;
      w++;
    }
    p = q;
    return true;
  }
  static void utf8(uint32_t c, std::string& o) {
    if (c < 0x80) {
      o += (char)c;
    } else if (c < 0x800) {
      o += (char)(0xC0 | (c >> 6));
      o += (char)(0x80 | (c & 63));
    } else if (c < 0x10000) {
      o += (char)(0xE0 | (c >> 12));
      o += (char)(0x80 | ((c >> 6) & 63));
      o += (char)(0x80 | (c & 63));
    } else {
      o += (char)(0xF0 | (c >> 18));
      o += (char)(0x80 | ((c >> 12) & 63));
      o += (char)(0x80 | ((c >> 6) & 63));
      o += (char)(0x80 | (c & 63));
    }
  }
  bool hex4(uint32_t& v) {
    v = 0;
    for (int i = 0; i < 4; i++) {
      if (p >= e) return false;
      char c = *p++;
      v <<= 4;
      if (c >= '0' && c <= '9') v |= (uint32_t)(c - '0');
      else if (c >= 'a' && c <= 'f') v |= (uint32_t)(c - 'a' + 10);
      else if (c >= 'A' && c <= 'F') v |= (uint32_t)(c - 'A' + 10);
      else return false;
    }
    return true;
  }
  bool string(std::string& o) {
    if (p >= e || *p != '"') return false;
    p++;
    while (p < e && *p != '"') {
      if (*p == '\\') {
        if (++p >= e) return false;
        char c = *p++;
        switch (c) {
          case 'n': o += '\n'; break;
          case 'r': o += '\r'; break;
          case 't': o += '\t'; break;
          case 'b': o += '\b'; break;
          case 'f': o += '\f'; break;
          case 'u': {
            uint32_t v;
            if (!hex4(v)) return false;
            if (v >= 0xD800 && v < 0xDC00 && e - p >= 6 && p[0] == '\\' && p[1] == 'u') {
              p += 2;
              uint32_t lo;
              if (!hex4(lo)) return false;
              if (lo >= 0xDC00 && lo < 0xE000) v = 0x10000 + ((v - 0xD800) << 10) + (lo - 0xDC00);
            }
            utf8(v, o);
            break;
          }
          default: o += c;
        }
      } else {
        o += *p++;
      }
    }
    if (p >= e) return false;
    p++;
    return true;
  }
  bool value(Json& j) {
    if (++depth > 64) return false;
    ws();
    if (p >= e) return false;
    bool ok = false;
    if (*p == '{') {
      p++;
      j.t = Json::T::Obj;
      ws();
      if (p < e && *p == '}') {
        p++;
        ok = true;
      } else {
        for (;;) {
          ws();
          std::string k;
          if (!string(k)) return false;
          ws();
          if (p >= e || *p++ != ':') return false;
          Json v;
          if (!value(v)) return false;
          j.o.emplace_back(std::move(k), std::move(v));
          ws();
          if (p >= e) return false;
          if (*p == ',') { p++; continue; }
          if (*p == '}') { p++; ok = true; break; }
          return false;
        }
      }
    } else if (*p == '[') {
      p++;
      j.t = Json::T::Arr;
      ws();
      if (p < e && *p == ']') {
        p++;
        ok = true;
      } else {
        for (;;) {
          Json v;
          if (!value(v)) return false;
          j.a.push_back(std::move(v));
          ws();
          if (p >= e) return false;
          if (*p == ',') { p++; continue; }
          if (*p == ']') { p++; ok = true; break; }
          return false;
        }
      }
    } else if (*p == '"') {
      j.t = Json::T::Str;
      ok = string(j.s);
    } else if (*p == 't') {
      ok = lit("true");
      j = Json(true);
    } else if (*p == 'f') {
      ok = lit("false");
      j = Json(false);
    } else if (*p == 'n') {
      ok = lit("null");
      j = Json();
    } else {
      std::string tmp;
      while (p < e && tmp.size() < 40 &&
             (*p == '-' || *p == '+' || *p == '.' || *p == 'e' || *p == 'E' || (*p >= '0' && *p <= '9')))
        tmp += *p++;
      if (tmp.empty()) return false;
      char* end = nullptr;
      double d = strtod(tmp.c_str(), &end);
      if (end != tmp.c_str() + tmp.size()) return false;
      j = Json(d);
      ok = true;
    }
    depth--;
    return ok;
  }
};
}  // namespace

bool Json::parse(std::string_view text, Json& out) {
  if (text.size() >= 3 && (unsigned char)text[0] == 0xEF && (unsigned char)text[1] == 0xBB && (unsigned char)text[2] == 0xBF)
    text.remove_prefix(3);
  Parser ps{text.data(), text.data() + text.size()};
  Json j;
  if (!ps.value(j)) return false;
  ps.ws();
  if (ps.p != ps.e) return false;
  out = std::move(j);
  return true;
}

}  // namespace rt
