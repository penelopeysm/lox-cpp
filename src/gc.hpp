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
};

class GC {
public:
  // Central allocation function
  template <typename T, typename... Args> T* alloc(Args&&... args) {
    if (alloc_callback != nullptr) {
      alloc_callback();
    }

    static_assert(std::is_base_of_v<Obj, T>,
                  "GC::alloc can only be used to allocate subclasses of Obj");
    // Create new object
    T* obj = new T(std::forward<Args>(args)...);
    obj->is_marked = false;
    // and make it point to the old head
    obj->next = head;
    // and make it be the new head
    head = obj;
    // Update memory usage
    size_t obj_size = sizeof(T);
    bytes_allocated += obj_size;
    obj->size = obj_size;
    return obj;
  }

  // We want GCs to be movable but non-copyable. This is because GC contains a pointer Obj* head
  // which is not safe to copy (if we copy a GC to another one, then delete the first, the
  // second GC will have a dangling pointer). But moving a GC is fine.
  // The methods with `const GC&` are copy constructors and copy assignment operators
  GC(const GC&) = delete;
  GC& operator=(const GC&) = delete;
  GC(GC&&) = default;
  GC& operator=(GC&&) = default;
  // Once we define any constructor (including the copy/move ones), the default
  // constructor is not generated, so we need to explicitly define it.
  GC() = default;

  bool should_gc();

  // Mark a value as grey (i.e., reachable but not yet fully processed)
  void mark_as_grey(const Value& value);
  void mark_as_grey(Obj* objptr);

  // For debugging purposes, list all objects
  void list_objects() const;

  // Run the garbage collector.
  void gc();

  // Get a pointer to an interned ObjString object, creating it if necessary.
  ObjString* get_string_ptr(std::string_view);

  void set_alloc_callback(std::function<void()> callback) {
    alloc_callback = callback;
  }

private:
  // First object in the linked list of all objects tracked by the GC.
  Obj* head = nullptr;
  StringMap interned_strings;
  std::vector<Obj*> grey_stack;
  std::function<void()> alloc_callback = nullptr;
  size_t bytes_allocated = 0;
  size_t next_gc_threshold = 1024 * 1024; // 1MB
};

} // namespace lox
