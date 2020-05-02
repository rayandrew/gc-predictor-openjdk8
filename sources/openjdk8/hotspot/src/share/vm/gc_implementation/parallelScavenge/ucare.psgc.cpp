#include "precompiled.hpp"

#include "memory/universe.hpp"
#include "memory/allocation.hpp"
#include "oops/oop.inline.hpp"
#include "oops/oop.psgc.inline.hpp"

#include "utilities/ostream.hpp"

#include "utilities/ucare.hpp"

#if INCLUDE_ALL_GCS
#include "gc_implementation/parallelScavenge/psScavenge.hpp"
#include "gc_implementation/parallelScavenge/psScavenge.inline.hpp"
#include "gc_implementation/parallelScavenge/psPromotionManager.hpp"
#include "gc_implementation/parallelScavenge/psPromotionManager.inline.hpp"
#include "gc_implementation/parallelScavenge/parallelScavengeHeap.hpp"
#include "gc_implementation/parallelScavenge/psTasks.hpp"
#include "gc_implementation/parallelScavenge/ucare.psgc.inline.hpp"
#include "gc_implementation/parallelScavenge/gcTaskManager.hpp"

GCWorkerTask::GCWorkerTask(
  const char* name, GCTask::Kind::kind kind, uint affinity, GCWorkerTask::Type type):
  _name(name), _kind(kind), _affinity(affinity), _type(type) {
  elapsed = 0.0;
  worker = -1;

  // otyrt
  stripe_num = 0;
  stripe_total = 0;
  ssize = 0;
  slice_width = 0;
  slice_counter = 0;
  dirty_card_counter = 0;
  objects_scanned_counter = 0;
  card_increment_counter = 0;
  total_max_card_pointer_being_walked_through = 0;

  // sr
  live_objects = 0;
  dead_objects = 0;
  total_objects = 0;

  // barrier
  busy_workers = 0;

  // steal
  stack_depth_counter = 0;
}

GCWorkerTask::~GCWorkerTask() {
  // noop
  ucarelog_or_tty->print_cr("[%s: %s]", type_to_string(), get_value());
}

const char* GCWorkerTask::get_value() {
  stringStream ss;
  ss.print("name=%s, "
           "worker=%u, "
           "affinity=%u, "
           "kind=%s, "
           "elapsed=%lfs",
           _name,
           worker,
           _affinity,
           GCTask::Kind::to_string(_kind),
           elapsed);

  switch(_type) {
    case GCWorkerTask::OTYRT:
      ss.print(", stripe_num=%u, "
               "stripe_total=%u, "
               // "ssize=%d, "
               "slice_width=%zu, "
               "slice_counter=%zu, "
               "dirty_card_counter=%zu, "
               "objects_scanned_counter=%zu, "
               "card_increment_counter=%zu, "
               "total_max_card_pointer_being_walked_through=%zu",
               stripe_num,
               stripe_total,
               // ssize,
               slice_width,
               slice_counter,
               dirty_card_counter,
               objects_scanned_counter,
               card_increment_counter,
               total_max_card_pointer_being_walked_through);
      break;

    case GCWorkerTask::TRT:
    case GCWorkerTask::SRT:
      ss.print(", live=%zu, "
               "dead=%zu, "
               "total=%zu",
               live_objects,
               dead_objects,
               total_objects);
      break;

    case GCWorkerTask::BARRIER:
      ss.print(", busy_workers=%u",
               busy_workers);
      break;

    case GCWorkerTask::STEAL:
      ss.print(", stack_depth_counter=%zu",
               stack_depth_counter);
      break;

    case GCWorkerTask::NOOP:
    case GCWorkerTask::IDLE:
      break;
  }

  return ss.as_string();
}

GCWorkerTracker::GCWorkerTracker(uint id, uint max_gc_worker_tasks):
  _id(id), _max_gc_worker_tasks(max_gc_worker_tasks) {
  _last_idx = 0;
  _is_containing_sr_tasks = false;
  _elapsed_time = 0.0;
  // initialize _gc_worker_tasks
  _tasks = NEW_C_HEAP_ARRAY(GCWorkerTask*, _max_gc_worker_tasks, mtGC);
  guarantee(_tasks != NULL, "sanity");
  // for (uint i = 0; i < _max_gc_worker_tasks; i += 1) {
  //   _tasks[i] = NULL;
  // }
}

GCWorkerTracker::~GCWorkerTracker() {
  ucarelog_or_tty->print_cr("[WorkerTracker: worker=%u start]", _id);
  ucarelog_or_tty->print_cr("[WorkerTracker: "
                            "worker=%u, "
                            "task_count=%u, "
                            "is_containing_sr_tasks=%u, "
                            "elapsed_time=%lfs]",
                            _id,
                            _last_idx,
                            _is_containing_sr_tasks,
                            _elapsed_time);
  if (_tasks != NULL) {
    for (uint i = 0; i < _last_idx; i += 1) {
      GCWorkerTask::destroy(_tasks[i]);
    }
    FREE_C_HEAP_ARRAY(GCWorkerTask*, _tasks, mtGC);
    _tasks = NULL;
  }
  ucarelog_or_tty->print_cr("[WorkerTracker: worker=%u end]", _id);
  ucarelog_or_tty->flush();
}

Ucare::RootType scavenge_root_to_ucare_root(ScavengeRootsTask::RootType type) {
  switch (type) {
    case ScavengeRootsTask::universe:
      return Ucare::universe;
    case ScavengeRootsTask::jni_handles:
      return Ucare::jni_handles;
    case ScavengeRootsTask::threads:
      return Ucare::threads;
    case ScavengeRootsTask::object_synchronizer:
      return Ucare::object_synchronizer;
    case ScavengeRootsTask::flat_profiler:
      return Ucare::flat_profiler;
    case ScavengeRootsTask::system_dictionary:
      return Ucare::system_dictionary;
    case ScavengeRootsTask::class_loader_data:
      return Ucare::class_loader_data;
    case ScavengeRootsTask::management:
      return Ucare::management;
    case ScavengeRootsTask::jvmti:
      return Ucare::jvmti;
    case ScavengeRootsTask::code_cache:
      return Ucare::code_cache;
    default:
      return Ucare::unknown;
  }
}

const char* scavenge_root_to_ucare_root_as_string(ScavengeRootsTask::RootType type) {
  return Ucare::get_root_type_as_string(scavenge_root_to_ucare_root(type));
}

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
  // ucarelog_or_tty->print_cr("    PSScavengeKlassClosure: elapsed=%3.7fms, dead=%zu, live=%zu, total=%zu", _oop_closure.elapsed_milliseconds(), _oop_closure.get_dead_object_counts(), _oop_closure.get_live_object_counts(), _oop_closure.get_total_object_counts());
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
