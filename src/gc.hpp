#pragma once
#include "value.hpp"
#include <functional>
#include <string>
#include <string_view>
#include <unordered_map>
#include <utility>

namespace lox {

// https://www.cppstories.com/2021/heterogeneous-access-cpp20/
struct string_hash {
  using is_transparent = void;
  [[nodiscard]] size_t operator()(std::string_view txt) const {
    return std::hash<std::string_view>{}(txt);
  }
  [[nodiscard]] size_t operator()(const std::string& txt) const {
    return std::hash<std::string>{}(txt);
  }
};

class StringMap {
public:
  StringMap() = default;
  std::unordered_map<std::string, ObjString*, string_hash, std::equal_to<>> map;
  ObjString* get_ptr(std::string_view key);
};

class GC {
public:
  // Central allocation function
  template <typename T, typename... Args> T* alloc(Args&&... args) {
    static_assert(std::is_base_of_v<Obj, T>,
                  "GC::alloc can only be used to allocate subclasses of Obj");
    // Create new object
    T* obj = new T(std::forward<Args>(args)...);
    // and make it point to the old head
    obj->next = head;
    // and make it be the new head
    head = obj;
    return obj;
  }

  // Get a pointer to an interned ObjString object, creating it if necessary.
  ObjString* get_string_ptr(std::string_view);

private:
  // First object in the linked list of all objects tracked by the GC.
  Obj* head = nullptr;
  StringMap interned_strings;
};

} // namespace lox
