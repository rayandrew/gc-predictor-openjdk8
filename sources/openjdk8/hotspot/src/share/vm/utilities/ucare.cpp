#include "precompiled.hpp"
#include "classfile/systemDictionary.hpp"
#include "code/codeCache.hpp"
#include "gc_implementation/shared/gcId.hpp"
#include "memory/allocation.hpp"
#include "memory/iterator.hpp"
#include "memory/resourceArea.hpp"
#include "memory/universe.hpp"
#include "oops/oop.inline.hpp"
#include "oops/oop.psgc.inline.hpp"
#include "runtime/fprofiler.hpp"
#include "runtime/thread.hpp"
#include "runtime/vmThread.hpp"
#include "services/management.hpp"
#include "utilities/globalDefinitions.hpp"
#include "utilities/ostream.hpp"

#include "utilities/ucare.hpp"
#include "utilities/ucare.inline.hpp"

GrowableArray<const char*>* Ucare::_phase;

void Ucare::add_phase(const char* phase) {
  _phase->append_if_missing(phase);
}

void Ucare::flush_phase(const GCId &gc_id) {
  stringStream ss;
  ss.print("[Phase gc_id %u: { ", gc_id.id());
  for (GrowableArrayIterator<const char*> it = _phase->begin(); it != _phase->end(); ++it) {
    const char* phase = *it;
    ss.print("%s ", phase);
  }
  ss.print("} ]");
    // ss.print("%s::%s", name, get_root_type_as_string(type));
  // return ss.as_string();
  ucarelog_or_tty->stamp(PrintGCTimeStamps);
  ucarelog_or_tty->print_cr("%s", ss.as_string());
  _phase->clear();
}

// UcareAlwaysTrueClosure
class UcareAlwaysTrueClosure: public BoolObjectClosure, public Ucare::BoolOopClosure {
  public:
    bool do_oop_b(oop* p) { return true; }
    bool do_oop_b(narrowOop* p) { return true; }
    bool do_object_b(oop p) { return true; }
};

static UcareAlwaysTrueClosure always_true;

int Ucare::RootTypeMixin::NUM_OF_ROOTS = 10;

jint Ucare::initialize() {
  _phase = new GrowableArray<const char*>(25);
  return JNI_OK;
}

Ucare::BeforeGCRootsOopClosure Ucare::_before_gc_roots_oop_closure;
Ucare::AfterGCRootsOopClosure  Ucare::_after_gc_roots_oop_closure;

Ucare::BeforeGCRootsOopClosure* Ucare::get_before_gc_roots_oop_closure() {
  return &_before_gc_roots_oop_closure;
}

Ucare::AfterGCRootsOopClosure* Ucare::get_after_gc_roots_oop_closure() {
  return &_after_gc_roots_oop_closure;
}

// Container
Ucare::TraceAndCountRootOopClosureContainer* Ucare::_young_gen_oop_container = NULL;
Ucare::TraceAndCountRootOopClosureContainer* Ucare::_old_gen_oop_container = NULL;

void Ucare::reset_young_gen_oop_container() {
  _young_gen_oop_container = NULL;
}

void Ucare::reset_old_gen_oop_container() {
  _old_gen_oop_container = NULL;
}

void Ucare::set_young_gen_oop_container(Ucare::TraceAndCountRootOopClosureContainer* container) {
  assert(container != NULL, "ptr should not be NULL");
  _young_gen_oop_container = container;
}

void Ucare::set_old_gen_oop_container(Ucare::TraceAndCountRootOopClosureContainer* container) {
  assert(container != NULL, "ptr should not be NULL");
  _old_gen_oop_container = container;
}

Ucare::TraceAndCountRootOopClosureContainer* Ucare::get_young_gen_oop_container() {
  assert(_young_gen_oop_container != NULL, "ptr should not be NULL");
  return _young_gen_oop_container;
}

Ucare::TraceAndCountRootOopClosureContainer* Ucare::get_old_gen_oop_container() {
  assert(_old_gen_oop_container != NULL, "ptr should not be NULL");
  return _old_gen_oop_container;
}

// mixins
//// ObjectCounterMixin
void Ucare::ObjectCounterMixin::reset() {
  _dead = 0;
  _live = 0;
  _total = 0;
}

void Ucare::ObjectCounterMixin::set_dead_object_counts(size_t dead) {
  _dead = dead;
}

void Ucare::ObjectCounterMixin::inc_dead_object_counts() {
  _dead++;
}

void Ucare::ObjectCounterMixin::dec_dead_object_counts() {
  _dead--;
}

void Ucare::ObjectCounterMixin::set_live_object_counts(size_t live) {
  _live = live;
}

void Ucare::ObjectCounterMixin::inc_live_object_counts() {
  _live++;
}

void Ucare::ObjectCounterMixin::dec_live_object_counts() {
  _live--;
}

void Ucare::ObjectCounterMixin::set_total_object_counts(size_t total) {
  _total = total;
}

void Ucare::ObjectCounterMixin::inc_total_object_counts() {
  _total++;
}

void Ucare::ObjectCounterMixin::dec_total_object_counts() {
  _total--;
}

void Ucare::ObjectCounterMixin::add_counter(const ObjectCounterMixin* object_counter) {
  _dead += object_counter->get_dead_object_counts();
  _live += object_counter->get_live_object_counts();
  _total += object_counter->get_total_object_counts();
}

void Ucare::ObjectCounterMixin::remove_counter(const ObjectCounterMixin* object_counter) {
  _dead -= object_counter->get_dead_object_counts();
  _live -= object_counter->get_live_object_counts();
  _total -= object_counter->get_total_object_counts();
}

size_t Ucare::ObjectCounterMixin::get_dead_object_counts() const {
  return _dead;
}

size_t Ucare::ObjectCounterMixin::get_live_object_counts() const {
  return _live;
}

size_t Ucare::ObjectCounterMixin::get_total_object_counts() const {
  return _total;
}

//// TraceTimeMixin
Ucare::TraceTimeMixin::TraceTimeMixin(elapsedTimer* accumulator, bool active) {
  _active = active;
  if (_active) {
    _accumulator = accumulator;
    _t.start();
  }
}

Ucare::TraceTimeMixin::~TraceTimeMixin() {
  if (_active) {
    _t.stop();
    if (_accumulator != NULL) _accumulator->add(_t);
  }
}

double Ucare::TraceTimeMixin::elapsed_seconds() const {
  return _t.elapsed_seconds();
}

double Ucare::TraceTimeMixin::elapsed_milliseconds() const {
  return _t.elapsed_seconds() * 1000;
}

//// RootTypeMixin

const char* Ucare::RootTypeMixin::get_identifier(RootType type, const char* name) {
  stringStream ss;
  ss.print("%s::%s", name, Ucare::get_root_type_as_string(type));
  return ss.as_string();
}

const char* Ucare::get_root_type_as_string(RootType type) {
  switch (type) {
    case universe:
      return "universe";
    case jni_handles:
      return "jni_handles";
    case threads:
      return "threads";
    case object_synchronizer:
      return "object_synchronizer";
    case flat_profiler:
      return "flat_profiler";
    case system_dictionary:
      return "system_dictionary";
    case class_loader_data:
      return "class_loader_data";      
    case management:
      return "management";
    case jvmti:
      return "jvmti";
    case code_cache:
      return "code_cache";
    case reference:
      return "reference";
    case string_table:
      return "string_table";
    default:
      return "unknown";
  }
}

// closures
// HeapObjectIterationClosure
bool Ucare::ObjectIterationClosure::is_object_live(oop obj) {
  return _live_filter != NULL && _live_filter->do_object_b(obj);
}

void Ucare::ObjectIterationClosure::do_object(oop obj) {
  inc_total_object_counts();
  if (is_object_live(obj)) {
    inc_live_object_counts();
  } else {
    inc_dead_object_counts();
  }
}

//// TraceAndCountBoolObjectClosure
void Ucare::TraceAndCountBoolObjectClosure::print_info(const char* additional_id) {
  ucarelog_or_tty->print_cr("  %s%s: elapsed=%lfms, dead=%zu, live=%zu, total=%zu",
                            Ucare::get_root_type_as_string(_root_type),
                            additional_id,
                            elapsed_milliseconds(),
                            get_dead_object_counts(),
                            get_live_object_counts(),
                            get_total_object_counts());
}

//// TraceAndCountRootOopClosure
Ucare::TraceAndCountRootOopClosure::~TraceAndCountRootOopClosure() {
  if (_verbose) {
    print_info();
  }
}

void Ucare::TraceAndCountRootOopClosure::print_info(const char* additional_id) {
  // no need to check verbose here
  // this call is intended by the caller
  suspend();

  // ucarelog_or_tty->stamp(PrintGCTimeStamps);
  ucarelog_or_tty->print_cr("[%s%s: "
                            "elapsed=%lfms, "
                            "dead=%zu, "
                            "live=%zu, "
                            "total=%zu",
                            Ucare::get_root_type_as_string(_root_type),
                            additional_id,
                            elapsed_milliseconds(),
                            get_dead_object_counts(),
                            get_live_object_counts(),
                            get_total_object_counts());

  resume();
}

//// TraceAndCountRootOopClosureContainer
Ucare::TraceAndCountRootOopClosureContainer::TraceAndCountRootOopClosureContainer(GCId gc_id, const char* context, bool verbose, bool only_summary): _gc_id(gc_id), _context(context), _added_count(0), _verbose(verbose), _only_summary(only_summary) {
  if (_verbose && !_only_summary) {
    ucarelog_or_tty->stamp(PrintGCTimeStamps);
    ucarelog_or_tty->print_cr("[TraceCountRootOopClosureContainer: context=%s, gc_id=%u", context, gc_id.id());
  }
}

Ucare::TraceAndCountRootOopClosureContainer::~TraceAndCountRootOopClosureContainer() {
  if (_verbose) {
    if (_only_summary) {
      ucarelog_or_tty->stamp(PrintGCTimeStamps);
      ucarelog_or_tty->print_cr("[TraceCountRootOopClosureContainer: context=%s, gc_id=%u, elapsed=%3.7fms, roots_walk_count=%zu, dead_objects=%zu, live_objects=%zu, total_objects=%zu]", _context, _gc_id.id(), _t.elapsed_seconds() * 1000.0, _added_count, get_dead_object_counts(), get_live_object_counts(), get_total_object_counts());
    } else {
      ucarelog_or_tty->print_cr("  summaries: context=%s, gc_id=%u, elapsed=%lfms, roots_walk_count=%zu, dead_objects=%zu, live_objects=%zu, total_objects=%zu]", _context, _gc_id.id(), _t.elapsed_seconds() * 1000.0, _added_count, get_dead_object_counts(), get_live_object_counts(), get_total_object_counts());
    }
  }
}

void Ucare::TraceAndCountRootOopClosureContainer::reset_counter() {
  ObjectCounterMixin::reset();
}

// we only need to iterate strong roots
void Ucare::count_oops(BoolOopClosure* filter, const GCId& gc_id, const char* phase) {
  assert(Universe::heap()->is_gc_active(), "called outside gc");
  ResourceMark rm;

  // OopClosure oops;
  // CLDClosure clds;
  // CodeBlobClosure blobs;

  // stringStream ss;
  // ss.print("Count oops for %s", phase);
  
  TraceAndCountRootOopClosureContainer oop_container(gc_id, phase);
  
  {
    OopIterationClosure hic(filter);
    TraceTimeMixin t;
    Universe::oops_do(&hic); // -- strong
    ucarelog_or_tty->print_cr("  Universe: elapsed=%3.7fms, dead=%zu, live=%zu, total=%zu", t.elapsed_milliseconds(), hic.get_dead_object_counts(), hic.get_live_object_counts(), hic.get_total_object_counts());
    oop_container.add_counter(&hic);
  }
  {
    OopIterationClosure hic(filter);
    TraceTimeMixin t;
    JNIHandles::oops_do(&hic); // -- strong
    ucarelog_or_tty->print_cr("  JNIHandles: elapsed=%3.7fms, dead=%zu, live=%zu, total=%zu", t.elapsed_milliseconds(), hic.get_dead_object_counts(), hic.get_live_object_counts(), hic.get_total_object_counts());
    oop_container.add_counter(&hic);
  }
  {
    OopIterationClosure hic(filter);
    TraceTimeMixin t;
    CodeBlobToOopClosure code_blob_closure(&hic, !CodeBlobToOopClosure::FixRelocations);
    CLDToOopClosure cld_to_oop_closure(&hic);
    Threads::oops_do(&hic, &cld_to_oop_closure, &code_blob_closure); // -- strong
    ucarelog_or_tty->print_cr("  Threads: elapsed=%lfms, dead=%zu, live=%zu, total=%zu", t.elapsed_milliseconds(), hic.get_dead_object_counts(), hic.get_live_object_counts(), hic.get_total_object_counts());
    oop_container.add_counter(&hic);
  }
  {
    OopIterationClosure hic(filter);
    TraceTimeMixin t;
    ObjectSynchronizer::oops_do(&hic); // -- strong
    ucarelog_or_tty->print_cr("  ObjectSynchronizer: elapsed=%lfms, dead=%zu, live=%zu, total=%zu", t.elapsed_milliseconds(), hic.get_dead_object_counts(), hic.get_live_object_counts(), hic.get_total_object_counts());
    oop_container.add_counter(&hic);
  }
  {
    OopIterationClosure hic(filter);
    TraceTimeMixin t;
    FlatProfiler::oops_do(&hic); // -- strong
    ucarelog_or_tty->print_cr("  FlatProfiler: elapsed=%lfms, dead=%zu, live=%zu, total=%zu", t.elapsed_milliseconds(), hic.get_dead_object_counts(), hic.get_live_object_counts(), hic.get_total_object_counts());
    oop_container.add_counter(&hic);
  }
  {
    OopIterationClosure hic(filter);
    TraceTimeMixin t;
    SystemDictionary::always_strong_oops_do(&hic); // -- strong
    ucarelog_or_tty->print_cr("  SystemDictionary: elapsed=%lfms, dead=%zu, live=%zu, total=%zu", t.elapsed_milliseconds(), hic.get_dead_object_counts(), hic.get_live_object_counts(), hic.get_total_object_counts());
    oop_container.add_counter(&hic);
  }
  {
    OopIterationClosure hic(filter);
    TraceTimeMixin t;
    KlassToOopClosure klass_closure(&hic);
    ClassLoaderDataGraph::always_strong_oops_do(&hic, &klass_closure, false); // -- strong
    ucarelog_or_tty->print_cr("  ClassLoaderDataGraph: elapsed=%lfms, dead=%zu, live=%zu, total=%zu", t.elapsed_milliseconds(), hic.get_dead_object_counts(), hic.get_live_object_counts(), hic.get_total_object_counts());
    oop_container.add_counter(&hic);
  }
  {
    OopIterationClosure hic(filter);
    TraceTimeMixin t;
    Management::oops_do(&hic); // -- strong
    ucarelog_or_tty->print_cr("  Management: elapsed=%lfms, dead=%zu, live=%zu, total=%zu", t.elapsed_milliseconds(), hic.get_dead_object_counts(), hic.get_live_object_counts(), hic.get_total_object_counts());
    oop_container.add_counter(&hic);
  }
  {
    OopIterationClosure hic(filter);
    TraceTimeMixin t;
    JvmtiExport::oops_do(&hic); // -- strong
    ucarelog_or_tty->print_cr("  JVMTIExport: elapsed=%lfms, dead=%zu, live=%zu, total=%zu", t.elapsed_milliseconds(), hic.get_dead_object_counts(), hic.get_live_object_counts(), hic.get_total_object_counts());
    oop_container.add_counter(&hic);
  }
  {
    // this will be needed somehow in G1GC, take a stub first
    // CodeCache::blobs_do(&blobs);
  }
}

void Ucare::count_all_oops(const GCId& gc_id, const char* phase) {
  assert(Universe::heap()->is_gc_active(), "called outside gc");
  count_oops(&always_true, gc_id, phase);
}

void Ucare::count_objects(BoolObjectClosure* filter, const GCId& gc_id, const char* phase) {
  assert(Universe::heap()->is_gc_active(), "called outside gc");
  
  ResourceMark rm;

  ObjectIterationClosure hic(filter);
  elapsedTimer time_execution;
  
  {
    TraceTimeMixin t(&time_execution);
    Universe::heap()->object_iterate(&hic);
  }
  
  ucarelog_or_tty->stamp(PrintGCTimeStamps);
  ucarelog_or_tty->print_cr("count_objects: { gc_id: %u, phase: \"%s\", elapsed_time: { value: %3.7f, unit: \"ms\" }, objects: { dead: %zu, live: %zu, total: %zu } }", gc_id.id(), phase, time_execution.seconds() * 1000.0, hic.get_dead_object_counts(), hic.get_live_object_counts(), hic.get_total_object_counts());
}

void Ucare::count_all_objects(const GCId& gc_id, const char* phase) {
  assert(Universe::heap()->is_gc_active(), "called outside gc");
  count_objects(&always_true, gc_id, phase);
}
