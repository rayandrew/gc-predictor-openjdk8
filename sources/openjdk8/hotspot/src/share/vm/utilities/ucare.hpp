#ifndef SHARE_VM_UTILITIES_UCARE_HPP
#define SHARE_VM_UTILITIES_UCARE_HPP

#include "gc_implementation/shared/gcId.hpp"
#include "memory/allocation.hpp"
#include "memory/iterator.hpp"
#include "memory/universe.hpp"
#include "runtime/timer.hpp"
#include "utilities/globalDefinitions.hpp"
#include "utilities/growableArray.hpp"

// forward declarations
#if INCLUDE_ALL_GCS
// PSScavenge
class PSPromotionManager;
class MutableSpace;
// PSParallelCompact
class ParCompactionManager;
#endif

// @rayandrew
// create new helper classes to contain all data for UCARE Research
class Ucare : AllStatic {
  friend class Universe;
  friend jint  universe_init();

  // forward declarations
  private:
    // mixins
    class ObjectCounterMixin;
    class TraceTimeMixin;
    class RootTypeMixin;
  
    // private closures
    class OopIterationClosure;
    class ObjectIterationClosure;
    class TraceClosure;
    class BeforeGCRootsOopClosure;
    class AfterGCRootsOopClosure;

  public:
    // public closures
    class TraceKlassClosure;
    class TraceAndCountKlassClosure;
  
    class TraceAndCountBoolObjectClosure;
  
    class BoolOopClosure;
    class CountingOopClosure;
    class TraceOopClosure;
    class TraceAndCountOopClosure;
    class TraceAndCountRootOopClosure;

    // gc specifics closure
    #if INCLUDE_ALL_GCS
    // PSScavenge
    template<bool promote_immediately> class PSRootsClosure;
    typedef PSRootsClosure</*promote_immediately=*/false> PSScavengeRootsClosure;
    typedef PSRootsClosure</*promote_immediately=*/true> PSPromoteRootsClosure;

    class PSKeepAliveClosure;
    class PSIsAliveClosure;
  
    // PSParallelCompact
    class MarkAndPushClosure;
    #endif

  private:
    // --------------------------------------------------
    // GC Helpers
    // --------------------------------------------------
    #if INCLUDE_ALL_GCS
    // PSScavenge
    template<class T, bool promote_immediately>
    static inline void copy_and_push_safe_barrier(TraceAndCountRootOopClosure* closure,
                                                  PSPromotionManager* pm,
                                                  T*                  p);

    // PSParallelCompact
    template<class T>
    static inline void mark_and_push(TraceAndCountRootOopClosure* closure,
                                     ParCompactionManager* cm,
                                     T* p);
    #endif
  
  public:
    enum RootType {
      unknown               = -999,
      universe              = 1,
      jni_handles           = 2,
      threads               = 3,
      object_synchronizer   = 4,
      flat_profiler         = 5,
      system_dictionary     = 6,
      class_loader_data     = 7,
      management            = 8,
      jvmti                 = 9,
      code_cache            = 10,
      reference             = 11,
      string_table          = 12,
    };
  
  private:
    class ObjectCounterMixin: public StackObj {
      private:
        size_t _dead;
        size_t _live;
        size_t _total;
      protected:
        ObjectCounterMixin(): _dead(0), _live(0), _total(0) {}
        ObjectCounterMixin(size_t dead, size_t live, size_t total): _dead(dead), _live(live), _total(total) {}
      public:
        void reset();
        void set_dead_object_counts(size_t dead);
        void inc_dead_object_counts();
        void dec_dead_object_counts();
        void set_live_object_counts(size_t live);
        void inc_live_object_counts();
        void dec_live_object_counts();
        void set_total_object_counts(size_t total);
        void inc_total_object_counts();
        void dec_total_object_counts();
        void add_counter(const ObjectCounterMixin* object_counter);
        void remove_counter(const ObjectCounterMixin* object_counter);
        size_t get_dead_object_counts() const;
        size_t get_live_object_counts() const;
        size_t get_total_object_counts() const;
    };

    class TraceTimeMixin : public StackObj {
      protected:
        bool _active;
        elapsedTimer _t;
        elapsedTimer* _accumulator;
      
      public:
        TraceTimeMixin(elapsedTimer* accumulator = NULL, bool active = true);
        ~TraceTimeMixin();
        double elapsed_seconds() const;
        double elapsed_milliseconds() const;

        // Activation
        inline void suspend()  { if (_active) _t.stop();  }
        inline void resume()   { if (_active) _t.start(); }
    };
  
    class RootTypeMixin : public StackObj {
      protected:
        RootType _root_type;

      public:
        static int NUM_OF_ROOTS;
    
      protected:
        static const char* get_root_type_as_string(RootType type);
        static const char* get_identifier(RootType type, const char* name = "");
      
      public:
        RootTypeMixin(RootType type = unknown): _root_type(type) {}
        void set_root_type(RootType root_type) { _root_type = root_type; }
        RootType get_root_type() const { return _root_type; }
        const char* get_root_type_as_string() const { return get_root_type_as_string(_root_type); }
    };
  
    class ObjectIterationClosure : public ObjectClosure, public ObjectCounterMixin {
      private:
        BoolObjectClosure* _live_filter;
      
      private:
        bool is_object_live(oop obj);
    
      public:
        ObjectIterationClosure(BoolObjectClosure* filter): _live_filter(filter) {}
        virtual void do_object(oop obj);
    };

    class OopIterationClosure : public OopClosure, public ObjectCounterMixin {
      private:
        BoolOopClosure* _live_filter;
      
      private:
        template<class T>
        inline bool is_oop_live(T* p) {
          return _live_filter != NULL && _live_filter->do_oop_b(p);
        }

        template<class T>
        inline void do_oop_work(T* p) {
          inc_total_object_counts();
          if (is_oop_live(p)) {
            inc_live_object_counts();
          } else {
            inc_dead_object_counts();
          }
        }
      
      public:
        OopIterationClosure(BoolOopClosure* filter): _live_filter(filter) {}    
        virtual void do_oop(oop* p) { OopIterationClosure::do_oop_work(p); }
        virtual void do_oop(narrowOop* p) { OopIterationClosure::do_oop_work(p); }
    };

    class TraceClosure: public Closure, public TraceTimeMixin {
      public:
        TraceClosure(elapsedTimer* accumulator = NULL, bool active = true): TraceTimeMixin(accumulator, active) {}
    };
  
  public:
    class TraceKlassClosure : public KlassClosure, public TraceClosure {};
    class TraceAndCountKlassClosure :  public TraceKlassClosure, public ObjectCounterMixin {};

  class TraceAndCountBoolObjectClosure : public BoolObjectClosure, public ObjectCounterMixin, public TraceTimeMixin, public RootTypeMixin {
      public:
        TraceAndCountBoolObjectClosure(elapsedTimer* accumulator = NULL, bool active = true): TraceTimeMixin(accumulator, active) {}
        void print_info(const char* additional_id = "");
    };
  
    class BoolOopClosure : public Closure {
      public:
        virtual bool do_oop_b(oop* o) = 0;
        virtual bool do_oop_b_v(oop* o) { do_oop_b(o); }
        virtual bool do_oop_b(narrowOop* o) = 0;
        virtual bool do_oop_b_v(narrowOop* o) { do_oop_b(o); }
    };
    
    class CountingOopClosure : public OopClosure, public ObjectCounterMixin {};
    class TraceOopClosure : public OopClosure, public TraceClosure {};  
    class TraceAndCountOopClosure : public TraceOopClosure, public ObjectCounterMixin {};
    class TraceAndCountRootOopClosure : public TraceAndCountOopClosure, public RootTypeMixin {
      protected:
        const char* _id;
        GCId _gc_id;
        bool _verbose;
      public:
        TraceAndCountRootOopClosure(RootType type = unknown, const char* id = "TraceAndCountRootOopClosure", bool verbose = true): _gc_id(GCId::current()), RootTypeMixin(type), _id(id), _verbose(verbose) {}
        TraceAndCountRootOopClosure(RootType type = unknown, GCId gc_id = GCId::current(), const char* id = "TraceAndCountRootOopClosure", bool verbose = true): RootTypeMixin(type), _gc_id(gc_id), _id(id), _verbose(verbose) {}  
        ~TraceAndCountRootOopClosure();
        void set_gc_id(const GCId gc_id) {
          _gc_id = gc_id;            
        }
        void print_info(const char* additional_id = "");
    };

    class TraceAndCountRootOopClosureContainer : protected ObjectCounterMixin, public TraceTimeMixin {
      private:
        const GCId _gc_id;
        const char* _context;
        size_t _added_count;
        bool _verbose;
        bool _only_summary;
      public:
        TraceAndCountRootOopClosureContainer(GCId gc_id, const char* context = "", bool verbose = true, bool only_summary = true);
        ~TraceAndCountRootOopClosureContainer();
        void reset_counter();

        template<class T>
        inline void add_counter(const T* object_counter);
        // void add_counter(const ObjectCounterMixin* object_counter);
    };

    #if INCLUDE_ALL_GCS
    // --------------------------------------------------
    // PSScavenge
    // --------------------------------------------------
    template<bool promote_immediately>
    class PSRootsClosure : public TraceAndCountRootOopClosure {
      private:
        PSPromotionManager* _promotion_manager;
      protected:
        template <class T>
        inline void do_oop_work(T *p);
      public:
        PSRootsClosure(PSPromotionManager* pm): TraceAndCountRootOopClosure(Ucare::unknown, "UcarePSRootsClosure", false), _promotion_manager(pm) { }
        void do_oop(oop* p)       { Ucare::PSRootsClosure<promote_immediately>::do_oop_work(p); }
        void do_oop(narrowOop* p) { Ucare::PSRootsClosure<promote_immediately>::do_oop_work(p); }
    };

    // Scavenges a single oop in a Klass.
    class PSScavengeFromKlassClosure: public TraceAndCountOopClosure {
      private:
        PSPromotionManager* _pm;
        // Used to redirty a scanned klass if it has oops
        // pointing to the young generation after being scanned.
        Klass*             _scanned_klass;
      private:
        void do_klass_barrier();
      public:
        PSScavengeFromKlassClosure(PSPromotionManager* pm): _pm(pm), _scanned_klass(NULL) { }
        void do_oop(narrowOop* p) { ShouldNotReachHere(); }
        void do_oop(oop* p);
        void set_scanned_klass(Klass* klass);
    };

    class PSScavengeKlassClosure: public KlassClosure {
      private:
        PSScavengeFromKlassClosure _oop_closure;
      public:
        PSScavengeKlassClosure(PSPromotionManager* pm) : _oop_closure(pm) { }
        ~PSScavengeKlassClosure();
        void do_klass(Klass* klass);
        const TraceAndCountOopClosure* get_oop_closure() const {
          return (const TraceAndCountOopClosure*) &_oop_closure;
        }
    };

    class PSKeepAliveClosure: public TraceAndCountRootOopClosure {
      protected:
        MutableSpace* _to_space;
        PSPromotionManager* _promotion_manager;
      private:
        template <class T>
        inline void do_oop_work(T* p);
      public:
        PSKeepAliveClosure(PSPromotionManager* pm);
        virtual void do_oop(oop* p)       { Ucare::PSKeepAliveClosure::do_oop_work(p); }
        virtual void do_oop(narrowOop* p) { Ucare::PSKeepAliveClosure::do_oop_work(p); }
    };

    class PSIsAliveClosure: public TraceAndCountBoolObjectClosure {
      public:
        bool do_object_b(oop p);
    };

    // --------------------------------------------------
    // PSParallelCompact
    // --------------------------------------------------
    class MarkAndPushClosure: public TraceAndCountRootOopClosure {
      private:
        ParCompactionManager* _compaction_manager;
      public:
        MarkAndPushClosure(ParCompactionManager* cm): TraceAndCountRootOopClosure(Ucare::unknown, "UcareMarkAndPushClosure", false), _compaction_manager(cm) { }
        void do_oop(oop* p);
        void do_oop(narrowOop* p);
    };

    class FollowKlassClosure : public KlassClosure {
      private:
        MarkAndPushClosure* _mark_and_push_closure;
      public:
        FollowKlassClosure(MarkAndPushClosure* mark_and_push_closure) :
          _mark_and_push_closure(mark_and_push_closure) { }
        void do_klass(Klass* klass);
    };
    // --------------------------------------------------
    #endif

  private:
    class BeforeGCRootsOopClosure : public BoolOopClosure {
      private:
        template<class T> bool do_oop_work(T* p);
      public:
        bool do_oop_b(oop* p) { return Ucare::BeforeGCRootsOopClosure::do_oop_work(p); }
        bool do_oop_b(narrowOop* p) { return Ucare::BeforeGCRootsOopClosure::do_oop_work(p); }
    };

    class AfterGCRootsOopClosure : public BoolOopClosure {
      private:
        template<class T> bool do_oop_work(T* p);
      public:
        bool do_oop_b(oop* p) { return Ucare::AfterGCRootsOopClosure::do_oop_work(p); }
        bool do_oop_b(narrowOop* p) { return Ucare::AfterGCRootsOopClosure::do_oop_work(p); }
    };
  
  private:
    static jint initialize();
  
    // add oop container
    static TraceAndCountRootOopClosureContainer* _young_gen_oop_container;
    static TraceAndCountRootOopClosureContainer* _old_gen_oop_container;
    static BeforeGCRootsOopClosure _before_gc_roots_oop_closure;
    static AfterGCRootsOopClosure  _after_gc_roots_oop_closure;

    // phase container
    static GrowableArray<const char*>* _phase;
  
  public:
    Ucare() { ShouldNotReachHere(); }
    ~Ucare() { ShouldNotReachHere(); }


    // These functions are not MT-safe
    // make sure this called in `Safepoint`
    static BeforeGCRootsOopClosure* get_before_gc_roots_oop_closure();
    static AfterGCRootsOopClosure*  get_after_gc_roots_oop_closure(); 
    static void reset_young_gen_oop_container();
    static void reset_old_gen_oop_container();
    static void set_young_gen_oop_container(TraceAndCountRootOopClosureContainer* container);
    static void set_old_gen_oop_container(TraceAndCountRootOopClosureContainer* container);
    static TraceAndCountRootOopClosureContainer* get_young_gen_oop_container();
    static TraceAndCountRootOopClosureContainer* get_old_gen_oop_container();

    static void count_oops(BoolOopClosure* filter, const GCId& gc_id, const char* phase = "");
    static void count_all_oops(const GCId& gc_id, const char* phase = "");
  
    static inline void count_oops_before_gc(const GCId& gc_id) {
      count_oops(get_before_gc_roots_oop_closure(), gc_id, "BeforeGC");
    }
    static inline void count_oops_after_gc(const GCId& gc_id) {
      count_oops(get_after_gc_roots_oop_closure(), gc_id, "AfterGC");
    }

    // static inline GrowableArray<const char*>* phase_passed() { return _phase; }
    static void add_phase(const char* phase);
    static void flush_phase(const GCId& gc_id);
  
    static void count_objects(BoolObjectClosure* filter, const GCId& gc_id, const char* phase = "");
    static void count_all_objects(const GCId& gc_id, const char* phase = "");
};

#endif // SHARE_VM_UTILITIES_UCARE_HPP
