// Minimal JSON value: just enough for a hand-editable settings file.
#pragma once
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace rt {

struct Json {
  enum class T { Null, Bool, Num, Str, Arr, Obj };
  T t = T::Null;
  bool b = false;
  double n = 0;
  std::string s;
  std::vector<Json> a;
  std::vector<std::pair<std::string, Json>> o;

  Json() = default;
  Json(bool v) : t(T::Bool), b(v) {}
  Json(int v) : t(T::Num), n(v) {}
  Json(double v) : t(T::Num), n(v) {}
  Json(float v) : t(T::Num), n(v) {}
  Json(const char* v) : t(T::Str), s(v) {}
  Json(std::string v) : t(T::Str), s(std::move(v)) {}
  static Json array() { Json j; j.t = T::Arr; return j; }
  static Json object() { Json j; j.t = T::Obj; return j; }

  bool isObj() const { return t == T::Obj; }
  bool isArr() const { return t == T::Arr; }
  const Json* find(std::string_view k) const {
    if (t != T::Obj) return nullptr;
    for (auto& kv : o)
      if (kv.first == k) return &kv.second;
    return nullptr;
  }
  void set(std::string k, Json v) {
    t = T::Obj;
    for (auto& kv : o)
      if (kv.first == k) { kv.second = std::move(v); return; }
    o.emplace_back(std::move(k), std::move(v));
  }
  void push(Json v) { t = T::Arr; a.push_back(std::move(v)); }

  double num(std::string_view k, double d) const { auto j = find(k); return j && j->t == T::Num ? j->n : d; }
  bool flag(std::string_view k, bool d) const { auto j = find(k); return j && j->t == T::Bool ? j->b : d; }
  std::string str(std::string_view k, std::string d = {}) const { auto j = find(k); return j && j->t == T::Str ? j->s : d; }

  std::string dump() const;
  static bool parse(std::string_view text, Json& out);
};

}  // namespace rt
