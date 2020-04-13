#include "precompiled.hpp"

#include "memory/universe.hpp"
#include "oops/oop.inline.hpp"
#include "oops/oop.psgc.inline.hpp"
#include "gc_implementation/parallelScavenge/psScavenge.hpp"
#include "gc_implementation/parallelScavenge/psScavenge.inline.hpp"
#include "gc_implementation/parallelScavenge/psPromotionManager.hpp"
#include "gc_implementation/parallelScavenge/psPromotionManager.inline.hpp"
#include "gc_implementation/parallelScavenge/parallelScavengeHeap.hpp"

#include "utilities/ucare.hpp"
#include "gc_implementation/parallelScavenge/ucare.psgc.inline.hpp"

#if INCLUDE_ALL_GCS

// --------------------------------------------------
// PSScavenge
// --------------------------------------------------

void Ucare::PSScavengeFromKlassClosure::do_oop(oop* p)       {
  ParallelScavengeHeap* psh = ParallelScavengeHeap::heap();
  assert(!psh->is_in_reserved(p), "GC barrier needed");
  if (PSScavenge::should_scavenge(p)) {
    assert(!Universe::heap()->is_in_reserved(p), "Not from meta-data?");
    assert(PSScavenge::should_scavenge(p, true), "revisiting object?");
    
    oop o = *p;
    oop new_obj = o->is_forwarded()
        ? o->forwardee()
        : _pm->copy_to_survivor_space</*promote_immediately=*/false>(o);

    oopDesc::encode_store_heap_oop_not_null(p, new_obj);

    inc_total_object_counts();

    // this pointer content is being copied
    inc_live_object_counts();

    if (PSScavenge::is_obj_in_young(new_obj)) {
      do_klass_barrier();
    }
  }
}

void Ucare::PSScavengeFromKlassClosure::set_scanned_klass(Klass* klass) {
  assert(_scanned_klass == NULL || klass == NULL, "Should always only handling one klass at a time");
  _scanned_klass = klass;
}

void Ucare::PSScavengeFromKlassClosure::do_klass_barrier() {
  assert(_scanned_klass != NULL, "Should not be called without having a scanned klass");
  _scanned_klass->record_modified_oops();
}

Ucare::PSScavengeKlassClosure::~PSScavengeKlassClosure() {
  ucarelog_or_tty->print_cr("    PSScavengeKlassClosure: elapsed=%3.7fms, dead=%zu, live=%zu, total=%zu", _oop_closure.elapsed_milliseconds(), _oop_closure.get_dead_object_counts(), _oop_closure.get_live_object_counts(), _oop_closure.get_total_object_counts());
}

void Ucare::PSScavengeKlassClosure::do_klass(Klass* klass) {
  // If the klass has not been dirtied we know that there's
  // no references into  the young gen and we can skip it.

#ifndef PRODUCT
  if (TraceScavenge) {
    ResourceMark rm;
    ucarelog_or_tty->print_cr("UcarePSScavengeKlassClosure::do_klass %p, %s, dirty: %s",
                           klass,
                           klass->external_name(),
                           klass->has_modified_oops() ? "true" : "false");
  }
#endif

  if (klass->has_modified_oops()) {
    // Clean the klass since we're going to scavenge all the metadata.
    klass->clear_modified_oops();

    // Setup the promotion manager to redirty this klass
    // if references are left in the young gen.
    _oop_closure.set_scanned_klass(klass);

    klass->oops_do(&_oop_closure);

    _oop_closure.set_scanned_klass(NULL);
  }
}

Ucare::PSKeepAliveClosure::PSKeepAliveClosure(PSPromotionManager* pm): TraceAndCountRootOopClosure(Ucare::reference, "UcarePSKeepAliveClosure", false), _promotion_manager(pm) {
  ParallelScavengeHeap* heap = (ParallelScavengeHeap*)Universe::heap();
  assert(heap->kind() == CollectedHeap::ParallelScavengeHeap, "Sanity");
  _to_space = heap->young_gen()->to_space();

  assert(_promotion_manager != NULL, "Sanity");
}

bool Ucare::PSIsAliveClosure::do_object_b(oop p) {
  const bool result = (!PSScavenge::is_obj_in_young(p)) || p->is_forwarded();
  
  inc_total_object_counts();
  
  if (result) {
    inc_live_object_counts();
  } else {
    inc_dead_object_counts();
  }

  return result;
}

// --------------------------------------------------
// PSParallelCompact
// --------------------------------------------------
void Ucare::MarkAndPushClosure::do_oop(oop* p) {
  mark_and_push(this, _compaction_manager, p);
}

void Ucare::MarkAndPushClosure::do_oop(narrowOop* p) {
  mark_and_push(this, _compaction_manager, p);
}

void Ucare::FollowKlassClosure::do_klass(Klass* klass) {
  klass->oops_do(_mark_and_push_closure);
}

#endif
