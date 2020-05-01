#ifndef SHARE_VM_GC_IMPLEMENTATION_SHARED_UCARE_PSGC_INLINE_HPP
#define SHARE_VM_GC_IMPLEMENTATION_SHARED_UCARE_PSGC_INLINE_HPP

#include "memory/allocation.hpp"
#include "utilities/ucare.hpp"
#include "runtime/globals.hpp"

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
#include "gc_implementation/parallelScavenge/gcTaskManager.hpp"
#include "gc_implementation/parallelScavenge/psTasks.hpp"

// forward declaration
class GCWorkerTask;
class GCWorkerTracker;


class GCWorkerTask: public CHeapObj<mtGC> {
public:
    enum Type {
      OTYRT,
      SRT,
      TRT,
      BARRIER,
      STEAL,
      IDLE,
      NOOP,
      UNK
    };
private:
    const char*                    _name;
    const GCTask::Kind::kind       _kind;
    const uint                     _affinity;
    const Type                     _type;

    const char* type_to_string() {
      switch(_type) {
        case OTYRT:
          return "OTYRT";
        case SRT:
          return "SRT";
        case TRT:
          return "TRT";
        case BARRIER:
          return "BARRIER";
        case STEAL:
          return "STEAL";
        case IDLE:
          return "IDLE";
        case NOOP:
          return "NOOP";
        default:
          return "UNK";
      }
    }
// so many setter, getter, make all public
public:
    double                   elapsed;
    uint                     worker;

    // otyrt
    uint                     stripe_num;
    uint                     stripe_total;
    uint                     ssize;
    uint                     slice_width;
    size_t                   slice_counter;
    size_t                   dirty_card_counter;
    size_t                   objects_scanned_counter;
    size_t                   card_increment_counter;
    size_t                   total_max_card_pointer_being_walked_through;

    // sr
    size_t                   live_objects;
    size_t                   dead_objects;
    size_t                   total_objects;

    // barrier
    uint                     busy_workers;

    // steal
    size_t                   stack_depth_counter;

public:
    virtual const char* get_value();

    inline const char* get_name() const { return _name; }
    inline const GCTask::Kind::kind get_kind() const { return _kind; }
    inline const uint get_affinity() const { return _affinity; }
    inline const Type get_type() const { return _type; }

    static GCWorkerTask* create(const char* name, GCTask::Kind::kind kind, uint affinity, Type type = UNK) {
      return new GCWorkerTask(name, kind, affinity, type);
    }

    static void destroy(GCWorkerTask* that) {
      if (that != NULL) {
        delete that;
      }
    }

protected:
    GCWorkerTask(const char* name, GCTask::Kind::kind kind, uint affinity, Type type);
    ~GCWorkerTask();
};

class GCWorkerTracker: public CHeapObj<mtGC> {
private:
    const uint      _id;
    // float           _idle_time;
    const uint      _max_gc_worker_tasks;
    double          _elapsed_time;
    GCWorkerTask**  _tasks;

    bool            _is_containing_sr_tasks;
    uint            _last_idx;

public:
    static GCWorkerTracker* create(uint id, uint max_gc_worker_tasks = 1000) {
      return new GCWorkerTracker(id, max_gc_worker_tasks);
    }

    static void destroy(GCWorkerTracker* that) {
      if (that != NULL) {
        delete that;
      }
    }

    void add_task(GCWorkerTask* task) {
      if (task != NULL) {
        if (_last_idx < _max_gc_worker_tasks) {
          task->worker = _id;
          if (!_is_containing_sr_tasks && task->get_type() == GCWorkerTask::SRT) {
            _is_containing_sr_tasks = true;
          }
          _tasks[_last_idx++] = task;
          _elapsed_time += task->elapsed;
        }
      }
    }

protected:
    GCWorkerTracker(uint id, uint max_gc_worker_tasks);
    ~GCWorkerTracker();
};

Ucare::RootType scavenge_root_to_ucare_root(ScavengeRootsTask::RootType type);
const char* scavenge_root_to_ucare_root_as_string(ScavengeRootsTask::RootType type);


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
