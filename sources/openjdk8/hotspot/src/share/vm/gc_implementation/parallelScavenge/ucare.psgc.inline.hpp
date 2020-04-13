#ifndef SHARE_VM_GC_IMPLEMENTATION_SHARED_UCARE_PSGC_INLINE_HPP
#define SHARE_VM_GC_IMPLEMENTATION_SHARED_UCARE_PSGC_INLINE_HPP

#include "utilities/ucare.hpp"

#if INCLUDE_ALL_GCS

// --------------------------------------------------
// PSScavenge
// --------------------------------------------------

#include "gc_implementation/parallelScavenge/parallelScavengeHeap.hpp"
#include "gc_implementation/parallelScavenge/psScavenge.inline.hpp"
#include "oops/oop.inline.hpp"
#include "oops/oop.psgc.inline.hpp"
#include "gc_implementation/parallelScavenge/psScavenge.hpp"
#include "gc_implementation/parallelScavenge/psScavenge.inline.hpp"
#include "gc_implementation/parallelScavenge/psPromotionManager.hpp"
#include "gc_implementation/parallelScavenge/psPromotionManager.inline.hpp"

// Attempt to "claim" oop at p via CAS, push the new obj if successful
// This version tests the oop* to make sure it is within the heap before
// attempting marking.
template <class T, bool promote_immediately>
inline void Ucare::copy_and_push_safe_barrier(TraceAndCountRootOopClosure* closure,
                                              PSPromotionManager* pm,
                                              T*                  p) {
  assert(PSScavenge::should_scavenge(p, true), "revisiting object?");


  // @rayandrew
  // -- this is CAS (compare and swap) method --
  // it tries to compare the object (oop) and swap
  // to the pointer of oop that is reachable from
  // GC roots
  oop o = oopDesc::load_decode_heap_oop_not_null(p);
  oop new_obj = o->is_forwarded()
      ? o->forwardee()
      : pm->copy_to_survivor_space<promote_immediately>(o);

#ifndef PRODUCT
  // This code must come after the CAS test, or it will print incorrect
  // information.
  if (TraceScavenge &&  o->is_forwarded()) {
    ucarelog_or_tty->print_cr("{%s %s " PTR_FORMAT " -> " PTR_FORMAT " (%d)}",
       "forwarding",
       new_obj->klass()->internal_name(), p2i((void *)o), p2i((void *)new_obj), new_obj->size());
  }
#endif

  oopDesc::encode_store_heap_oop_not_null(p, new_obj);

  closure->inc_live_object_counts();
  
  // @rayandrew
  // below is the condition where the old gen object
  // references the young gen object
  // the card table will be marked as `dirty`
  
  // We cannot mark without test, as some code passes us pointers
  // that are outside the heap. These pointers are either from roots
  // or from metadata.
  if ((!PSScavenge::is_obj_in_young((HeapWord*)p)) &&
      Universe::heap()->is_in_reserved(p)) {
    if (PSScavenge::is_obj_in_young(new_obj)) {
      PSScavenge::card_table()->inline_write_ref_field_gc(p, new_obj);
    }
  }
}

// @rayandrew
// add this to count while scavenging
template<bool promote_immediately>
template<class T>
inline void Ucare::PSRootsClosure<promote_immediately>::do_oop_work(T *p) {
  inc_total_object_counts();
  if (PSScavenge::should_scavenge(p)) {
    // We never card mark roots, maybe call a func without test?
    Ucare::copy_and_push_safe_barrier<T, promote_immediately>(this, _promotion_manager, p);
  } else {
    inc_dead_object_counts();
  }
}

template <class T>
inline void Ucare::PSKeepAliveClosure::do_oop_work(T* p) {
  assert (!oopDesc::is_null(*p), "expected non-null ref");
  assert ((oopDesc::load_decode_heap_oop_not_null(p))->is_oop(),
          "expected an oop while scanning weak refs");
  
  inc_total_object_counts();
  // Weak refs may be visited more than once.
  if (PSScavenge::should_scavenge(p, _to_space)) {
    Ucare::copy_and_push_safe_barrier<T, /*promote_immediately=*/false>(this, _promotion_manager, p);
  }
}

// --------------------------------------------------
// PSParallelCompact
// --------------------------------------------------
#include "gc_implementation/parallelScavenge/psParallelCompact.hpp"

template <class T>
inline void Ucare::mark_and_push(TraceAndCountRootOopClosure* closure,
                                 ParCompactionManager* cm,
                                 T* p) {
  T heap_oop = oopDesc::load_heap_oop(p);
  if (!oopDesc::is_null(heap_oop)) {
    oop obj = oopDesc::decode_heap_oop_not_null(heap_oop);
    closure->inc_total_object_counts();
    if (PSParallelCompact::mark_bitmap()->is_unmarked(obj)) {
      if (PSParallelCompact::mark_obj(obj)) {
        cm->push(obj);
        closure->inc_live_object_counts();
      } else {
        closure->inc_dead_object_counts();
      }
    } else {
      // has been marked before?
      assert(PSParallelCompact::mark_bitmap()->is_marked(obj), "obj has been marked");
      closure->inc_live_object_counts();
    }
  }
};

#endif

#endif
