#include "gc.hpp"
#include "value.hpp"
#include <algorithm>
#include <iostream>
#include <string_view>
#include <unordered_map>

namespace {} // namespace

namespace lox {

ObjString* GC::get_string_ptr(std::string_view key) {
  auto it = interned_strings.map.find(key);
  if (it == interned_strings.map.end()) {
    // Not found; create a new one.
    ObjString* s = alloc<ObjString>(key);
    auto new_pair = interned_strings.map.emplace(std::string(key), s);
    it = new_pair.first;
  }
  return it->second;
}

bool GC::should_gc() {
#ifdef LOX_GC_DEBUG
  return true;
#else
  return bytes_allocated > next_gc_threshold;
#endif
}

void GC::mark_as_grey(const lox::Value& value) {
  if (std::holds_alternative<lox::Obj*>(value)) {
    lox::Obj* obj = std::get<lox::Obj*>(value);
    mark_as_grey(obj);
  }
}
void GC::mark_as_grey(Obj* objptr) {
  if (objptr != nullptr && !objptr->is_marked) {
    objptr->is_marked = true;
    grey_stack.push_back(objptr);
  }
}

void GC::list_objects() const {
  std::cerr << "        === GC Objects ===\n";
  for (Obj* obj = head; obj != nullptr; obj = obj->next) {
    // some nice unicode symbols for white/grey/black
    if (obj->is_marked) {
      if (std::find(grey_stack.begin(), grey_stack.end(), obj) !=
          grey_stack.end()) {
        std::cerr << "        🟡 "; // grey (well... close enough)
      } else {
        std::cerr << "        ⚫ "; // black
      }
    } else {
      std::cerr << "        ⚪ "; // white
    }
    std::cerr << obj->to_repr() << "\n";
  }
  std::cerr << "        === End GC Objects ===\n";
}

void GC::gc() {
  // When we enter this function, all objects should have been unmarked, and
  // the roots should have been marked as grey already. This is handled inside
  // VM::gc() (we don't want to deal with the root finding here since that
  // requires knowledge of the VM's state).

#ifdef LOX_GC_DEBUG
  std::cerr << "\n\n\n        GC: starting mark-and-sweep\n";
  // list_objects();
  size_t nobjs_deleted = 0;
  size_t nbytes_deleted = 0;
#endif

  // Propagate grey markings forward.
  while (!grey_stack.empty()) {
    Obj* objptr = grey_stack.back();
    grey_stack.pop_back();

    if (objptr == nullptr) {
      continue;
    }
    switch (objptr->type) {
    // ObjString has no references to other objects, so we don't need to
    // mark anything else as grey. Likewise for native functions.
    case ObjType::STRING:
      break;
    case ObjType::NATIVE_FUNCTION:
      break;
    case ObjType::FUNCTION: {
      ObjFunction* p = static_cast<ObjFunction*>(objptr);
      for (const auto& constant : p->chunk.get_constants()) {
        mark_as_grey(constant);
      }
      break;
    }
    case ObjType::UPVALUE: {
      // For ObjUpvalue, we only need to care about the value if it's closed. If
      // it's not closed, then the value exists somewhere on the VM's stack, so
      // the GC will already have marked it as grey.
      ObjUpvalue* p = static_cast<ObjUpvalue*>(objptr);
      mark_as_grey(p->closed);
      break;
    }
    case ObjType::CLOSURE: {
      ObjClosure* p = static_cast<ObjClosure*>(objptr);
      mark_as_grey(p->function);
      for (ObjUpvalue* upvalue : p->upvalues) {
        mark_as_grey(upvalue);
      }
      break;
    }
    }

#ifdef LOX_GC_DEBUG
    // list_objects();
#endif
  }

  // Clear interned strings
  for (auto it = interned_strings.map.begin();
       it != interned_strings.map.end();) {
    if (!it->second->is_marked) {
      it = interned_strings.map.erase(it);
    } else {
      ++it;
    }
  }

  // Sweep
  Obj* prev = nullptr;
  Obj* objptr = head;
  while (objptr != nullptr) {
    Obj* next = objptr->next;
    if (!objptr->is_marked) {
      // Unreachable object.

      // Remove from linked list
      if (prev != nullptr) {
        prev->next = next;
      } else {
        head = next;
      }

      // Delete
      size_t bytes = objptr->size;
      bytes_allocated -= bytes;
      delete objptr;
      objptr = next;
#ifdef LOX_GC_DEBUG
      nbytes_deleted += bytes;
      nobjs_deleted++;
#endif

    } else {
      // Reachable object. Unmark it.
      objptr->is_marked = false;
      prev = objptr;
      objptr = next;
    }
  }

#ifdef LOX_GC_DEBUG
  std::cerr << "        GC: finished mark-and-sweep, deleted " << nobjs_deleted
            << " objects, " << nbytes_deleted << " bytes\n\n\n";
#endif

  // Update the GC threshold.
  next_gc_threshold = bytes_allocated * 2;
}

} // namespace lox
