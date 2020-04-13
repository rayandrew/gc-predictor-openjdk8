/*
 * Copyright (c) 2002, 2013, Oracle and/or its affiliates. All rights reserved.
 * DO NOT ALTER OR REMOVE COPYRIGHT NOTICES OR THIS FILE HEADER.
 *
 * This code is free software; you can redistribute it and/or modify it
 * under the terms of the GNU General Public License version 2 only, as
 * published by the Free Software Foundation.
 *
 * This code is distributed in the hope that it will be useful, but WITHOUT
 * ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or
 * FITNESS FOR A PARTICULAR PURPOSE.  See the GNU General Public License
 * version 2 for more details (a copy is included in the LICENSE file that
 * accompanied this code).
 *
 * You should have received a copy of the GNU General Public License version
 * 2 along with this work; if not, write to the Free Software Foundation,
 * Inc., 51 Franklin St, Fifth Floor, Boston, MA 02110-1301 USA.
 *
 * Please contact Oracle, 500 Oracle Parkway, Redwood Shores, CA 94065 USA
 * or visit www.oracle.com if you need additional information or have any
 * questions.
 *
 */

#include "precompiled.hpp"
#include "classfile/systemDictionary.hpp"
#include "code/codeCache.hpp"
#include "gc_implementation/parallelScavenge/cardTableExtension.hpp"
#include "gc_implementation/parallelScavenge/gcTaskManager.hpp"
#include "gc_implementation/parallelScavenge/psMarkSweep.hpp"
#include "gc_implementation/parallelScavenge/psPromotionManager.hpp"
#include "gc_implementation/parallelScavenge/psPromotionManager.inline.hpp"
#include "gc_implementation/parallelScavenge/psScavenge.inline.hpp"
#include "gc_implementation/parallelScavenge/psTasks.hpp"
#include "memory/iterator.hpp"
#include "memory/universe.hpp"
#include "oops/oop.inline.hpp"
#include "oops/oop.psgc.inline.hpp"
#include "runtime/fprofiler.hpp"
#include "runtime/thread.hpp"
#include "runtime/vmThread.hpp"
#include "services/management.hpp"
#include "utilities/taskqueue.hpp"

// @rayandrew
// add ucare
#include "gc_implementation/shared/gcId.hpp"
#include "utilities/ucare.hpp"
#include "utilities/ucare.inline.hpp"
#include "gc_implementation/parallelScavenge/ucare.psgc.inline.hpp"

//
// ScavengeRootsTask
//

void ScavengeRootsTask::do_it(GCTaskManager* manager, uint which) {
  assert(Universe::heap()->is_gc_active(), "called outside gc");

  PSPromotionManager* pm = PSPromotionManager::gc_thread_promotion_manager(which);
  
  Ucare::PSScavengeRootsClosure roots_closure(pm);
  Ucare::PSPromoteRootsClosure  roots_to_old_closure(pm);
  
  switch (_root_type) {
    case universe:
      roots_closure.set_root_type(Ucare::universe);
      Universe::oops_do(&roots_closure);
      roots_closure.print_info();
      break;

    case jni_handles:
      roots_closure.set_root_type(Ucare::jni_handles);
      JNIHandles::oops_do(&roots_closure);
      roots_closure.print_info();
      break;

    case threads:
      roots_closure.set_root_type(Ucare::threads);
      {
        ResourceMark rm;
        CLDClosure* cld_closure = NULL; // Not needed. All CLDs are already visited.
        Threads::oops_do(&roots_closure, cld_closure, NULL);
      }
      roots_closure.print_info();
      break;

    case object_synchronizer:
      roots_closure.set_root_type(Ucare::object_synchronizer);
      ObjectSynchronizer::oops_do(&roots_closure);
      roots_closure.print_info();
      break;

    case flat_profiler:
      roots_closure.set_root_type(Ucare::flat_profiler);
      FlatProfiler::oops_do(&roots_closure);
      roots_closure.print_info();
      break;

    case system_dictionary:
      roots_closure.set_root_type(Ucare::system_dictionary);
      SystemDictionary::oops_do(&roots_closure);
      roots_closure.print_info();
      break;

    case class_loader_data:
      roots_closure.set_root_type(Ucare::class_loader_data);
      {
        Ucare::PSScavengeKlassClosure klass_closure(pm);
        ClassLoaderDataGraph::oops_do(&roots_closure, &klass_closure, false);
        roots_closure.print_info();
        Ucare::get_young_gen_oop_container()->add_counter(klass_closure.get_oop_closure());
        // roots_closure.add_counter(klass_closure.get_oop_closure());
        // Ucare::get_young_gen_oop_container()->add_counter(&klass_closure);
      }
      break;

    case management:
      roots_closure.set_root_type(Ucare::management);
      Management::oops_do(&roots_closure);
      roots_closure.print_info();
      break;

    case jvmti:
      roots_closure.set_root_type(Ucare::jvmti);
      JvmtiExport::oops_do(&roots_closure);
      roots_closure.print_info();
      break;

    case code_cache: 
      roots_to_old_closure.set_root_type(Ucare::code_cache);
      {
        MarkingCodeBlobClosure each_scavengable_code_blob(&roots_to_old_closure, CodeBlobToOopClosure::FixRelocations);
        CodeCache::scavenge_root_nmethods_do(&each_scavengable_code_blob);
      }
      roots_to_old_closure.print_info();
      break;

    default:
      fatal("Unknown root type");
  }

  if (_root_type == code_cache) {
    Ucare::get_young_gen_oop_container()->add_counter(&roots_to_old_closure);
  } else {
    Ucare::get_young_gen_oop_container()->add_counter(&roots_closure);
  }

  // Do the real work
  pm->drain_stacks(false);
}

//
// ThreadRootsTask
//

void ThreadRootsTask::do_it(GCTaskManager* manager, uint which) {
  assert(Universe::heap()->is_gc_active(), "called outside gc");

  PSPromotionManager* pm = PSPromotionManager::gc_thread_promotion_manager(which);
  // @rayandrew
  // change to `Ucare` closure
  Ucare::PSScavengeRootsClosure roots_closure(pm);
  roots_closure.set_root_type(Ucare::threads);
  
  CLDClosure* roots_from_clds = NULL;  // Not needed. All CLDs are already visited.
  MarkingCodeBlobClosure roots_in_blobs(&roots_closure, CodeBlobToOopClosure::FixRelocations);

  if (_java_thread != NULL)
    _java_thread->oops_do(&roots_closure, roots_from_clds, &roots_in_blobs);

  if (_vm_thread != NULL)
    _vm_thread->oops_do(&roots_closure, roots_from_clds, &roots_in_blobs);

  Ucare::get_young_gen_oop_container()->add_counter(&roots_closure);

  roots_closure.print_info();
  
  // Do the real work
  pm->drain_stacks(false);
}

//
// StealTask
//

StealTask::StealTask(ParallelTaskTerminator* t) :
  _terminator(t) {}

void StealTask::do_it(GCTaskManager* manager, uint which) {
  assert(Universe::heap()->is_gc_active(), "called outside gc");

  PSPromotionManager* pm =
    PSPromotionManager::gc_thread_promotion_manager(which);
  pm->drain_stacks(true);
  guarantee(pm->stacks_empty(),
            "stacks should be empty at this point");

  int random_seed = 17;
  while(true) {
    StarTask p;
    if (PSPromotionManager::steal_depth(which, &random_seed, p)) {
      TASKQUEUE_STATS_ONLY(pm->record_steal(p));
      pm->process_popped_location_depth(p);
      pm->drain_stacks_depth(true);
    } else {
      if (terminator()->offer_termination()) {
        break;
      }
    }
  }
  guarantee(pm->stacks_empty(), "stacks should be empty at this point");
}

//
// OldToYoungRootsTask
//

void OldToYoungRootsTask::do_it(GCTaskManager* manager, uint which) {
  // There are not old-to-young pointers if the old gen is empty.
  assert(!_gen->object_space()->is_empty(),
    "Should not be called is there is no work");
  assert(_gen != NULL, "Sanity");
  assert(_gen->object_space()->contains(_gen_top) || _gen_top == _gen->object_space()->top(), "Sanity");
  assert(_stripe_number < ParallelGCThreads, "Sanity");

  {
    PSPromotionManager* pm = PSPromotionManager::gc_thread_promotion_manager(which);

    assert(Universe::heap()->kind() == CollectedHeap::ParallelScavengeHeap, "Sanity");
    CardTableExtension* card_table = (CardTableExtension *)Universe::heap()->barrier_set();
    // FIX ME! Assert that card_table is the type we believe it to be.

    card_table->scavenge_contents_parallel(_gen->start_array(),
                                           _gen->object_space(),
                                           _gen_top,
                                           pm,
                                           _stripe_number,
                                           _stripe_total);

    // Do the real work
    pm->drain_stacks(false);
  }
}
