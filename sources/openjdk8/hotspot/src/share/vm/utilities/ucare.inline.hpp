#ifndef SHARE_VM_UTILITIES_UCARE_INLINE_HPP
#define SHARE_VM_UTILITIES_UCARE_INLINE_HPP

#include "utilities/ucare.hpp"

template<class T>
void Ucare::TraceAndCountRootOopClosureContainer::add_counter(const T* object_counter) {
  _added_count++;
  ObjectCounterMixin::add_counter((ObjectCounterMixin*) object_counter);
}


template<class T>
bool Ucare::BeforeGCRootsOopClosure::do_oop_work(T* p) {
  if (oopDesc::is_null(*p)) { return false; }
  // expecting oop here
  oop o = oopDesc::load_decode_heap_oop_not_null(p);

  if (o->is_oop()) {
    return o->is_forwarded();
  } else {
    return false;
  }
}

template<class T>
bool Ucare::AfterGCRootsOopClosure::do_oop_work(T* p) {
  if (oopDesc::is_null(*p)) { return false; }
  // expecting oop here
  oop o = oopDesc::load_decode_heap_oop_not_null(p);

  if (o->is_oop()) {
    return o->is_forwarded();
  } else {
    return false;
  }
}

#endif
