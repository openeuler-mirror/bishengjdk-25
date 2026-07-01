/*
 * Copyright (c) 2001, 2026, Oracle and/or its affiliates. All rights reserved.
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

#ifdef AARCH64
#include "gc/g1/g1Analytics.hpp"
#endif // AARCH64
#include "gc/g1/g1BarrierSet.hpp"
#ifdef AARCH64
#include "gc/g1/g1CardTableClaimTable.inline.hpp"
#include "gc/g1/g1CollectedHeap.inline.hpp"
#endif // AARCH64
#include "gc/g1/g1CollectionSet.hpp"
#include "gc/g1/g1ConcurrentRefine.hpp"
#ifdef AARCH64
#include "gc/g1/g1ConcurrentRefineSweepTask.hpp"
#endif // AARCH64
#include "gc/g1/g1ConcurrentRefineThread.hpp"
#ifndef AARCH64
#include "gc/g1/g1DirtyCardQueue.hpp"
#endif // !AARCH64
#include "gc/g1/g1HeapRegion.inline.hpp"
#include "gc/g1/g1HeapRegionRemSet.inline.hpp"
#include "gc/g1/g1Policy.hpp"
#include "gc/shared/gc_globals.hpp"
#ifdef AARCH64
#include "gc/shared/gcTraceTime.inline.hpp"
#include "gc/shared/workerThread.hpp"
#endif // AARCH64
#include "logging/log.hpp"
#include "memory/allocation.inline.hpp"
#include "memory/iterator.hpp"
#include "runtime/java.hpp"
#include "runtime/mutexLocker.hpp"
#include "utilities/debug.hpp"
#include "utilities/globalDefinitions.hpp"
#ifdef AARCH64
#include "utilities/ticks.hpp"
#endif // AARCH64

#include <math.h>

#ifdef AARCH64
G1ConcurrentRefineThread* G1ConcurrentRefineThreadControl::create_refinement_thread() {
#else // AARCH64
G1ConcurrentRefineThread* G1ConcurrentRefineThreadControl::create_refinement_thread(uint worker_id, bool initializing) {
#endif // AARCH64
  G1ConcurrentRefineThread* result = nullptr;
#ifdef AARCH64
  result = G1ConcurrentRefineThread::create(_cr);
#else // AARCH64
  if (initializing || !InjectGCWorkerCreationFailure) {
    result = G1ConcurrentRefineThread::create(_cr, worker_id);
  }
#endif // AARCH64
  if (result == nullptr || result->osthread() == nullptr) {
#ifdef AARCH64
    log_warning(gc)("Failed to create refinement control thread, no more %s",
#else // AARCH64
    log_warning(gc)("Failed to create refinement thread %u, no more %s",
                    worker_id,
#endif // AARCH64
                    result == nullptr ? "memory" : "OS threads");
    if (result != nullptr) {
      delete result;
      result = nullptr;
    }
  }
  return result;
}

G1ConcurrentRefineThreadControl::G1ConcurrentRefineThreadControl(uint max_num_threads) :
  _cr(nullptr),
#ifdef AARCH64
  _control_thread(nullptr),
  _workers(nullptr),
  _max_num_threads(max_num_threads)
#else // AARCH64
  _threads(max_num_threads)
#endif // AARCH64
{}

G1ConcurrentRefineThreadControl::~G1ConcurrentRefineThreadControl() {
#ifdef AARCH64
  delete _control_thread;
  delete _workers;
#else // AARCH64
  while (_threads.is_nonempty()) {
    delete _threads.pop();
  }
}

bool G1ConcurrentRefineThreadControl::ensure_threads_created(uint worker_id, bool initializing) {
  assert(worker_id < max_num_threads(), "precondition");

  while ((uint)_threads.length() <= worker_id) {
    G1ConcurrentRefineThread* rt = create_refinement_thread(_threads.length(), initializing);
    if (rt == nullptr) {
      return false;
    }
    _threads.push(rt);
  }

  return true;
#endif // AARCH64
}

jint G1ConcurrentRefineThreadControl::initialize(G1ConcurrentRefine* cr) {
  assert(cr != nullptr, "G1ConcurrentRefine must not be null");
  _cr = cr;

#ifdef AARCH64
  if (is_refinement_enabled()) {
    _control_thread = create_refinement_thread();
    if (_control_thread == nullptr) {
      vm_shutdown_during_initialization("Could not allocate refinement control thread");
#else // AARCH64
  if (max_num_threads() > 0) {
    _threads.push(create_refinement_thread(0, true));
    if (_threads.at(0) == nullptr) {
      vm_shutdown_during_initialization("Could not allocate primary refinement thread");
#endif // AARCH64
      return JNI_ENOMEM;
    }
#ifdef AARCH64
    _workers = new WorkerThreads("G1 Refinement Workers", max_num_threads());
    _workers->initialize_workers();
#else // AARCH64
    if (!UseDynamicNumberOfGCThreads) {
      if (!ensure_threads_created(max_num_threads() - 1, true)) {
        vm_shutdown_during_initialization("Could not allocate refinement threads");
        return JNI_ENOMEM;
      }
    }
#endif // AARCH64
  }
  return JNI_OK;
}

#ifdef ASSERT
#ifdef AARCH64
void G1ConcurrentRefineThreadControl::assert_current_thread_is_control_refinement_thread() const {
  assert(Thread::current() == _control_thread, "Not refinement control thread");
#else // AARCH64
void G1ConcurrentRefineThreadControl::assert_current_thread_is_primary_refinement_thread() const {
  assert(Thread::current() == _threads.at(0), "Not primary thread");
#endif // AARCH64
}
#endif // ASSERT

#ifdef AARCH64
void G1ConcurrentRefineThreadControl::activate() {
  _control_thread->activate();
}

void G1ConcurrentRefineThreadControl::run_task(WorkerTask* task, uint num_workers) {
  assert(num_workers >= 1, "must be");

  WithActiveWorkers w(_workers, num_workers);
  _workers->run_task(task);
}

void G1ConcurrentRefineThreadControl::control_thread_do(ThreadClosure* tc) {
  if (is_refinement_enabled()) {
    tc->do_thread(_control_thread);
#else // AARCH64
bool G1ConcurrentRefineThreadControl::activate(uint worker_id) {
  if (ensure_threads_created(worker_id, false)) {
    _threads.at(worker_id)->activate();
    return true;
#endif // AARCH64
  }

#ifndef AARCH64
  return false;
#endif // !AARCH64
}

void G1ConcurrentRefineThreadControl::worker_threads_do(ThreadClosure* tc) {
#ifdef AARCH64
  if (is_refinement_enabled()) {
    _workers->threads_do(tc);
#else // AARCH64
  for (G1ConcurrentRefineThread* t : _threads) {
    tc->do_thread(t);
#endif // AARCH64
  }
}

void G1ConcurrentRefineThreadControl::stop() {
#ifdef AARCH64
  if (is_refinement_enabled()) {
    _control_thread->stop();
#else // AARCH64
  for (G1ConcurrentRefineThread* t : _threads) {
    t->stop();
#endif // AARCH64
  }
}

#ifdef AARCH64
G1ConcurrentRefineSweepState::G1ConcurrentRefineSweepState(uint max_reserved_regions) :
  _state(State::Idle),
  _sweep_table(new G1CardTableClaimTable(G1CollectedHeap::get_chunks_per_region_for_merge())),
  _stats()
{
  _sweep_table->initialize(max_reserved_regions);
}

G1ConcurrentRefineSweepState::~G1ConcurrentRefineSweepState() {
  delete _sweep_table;
}

void G1ConcurrentRefineSweepState::set_state_start_time() {
  _state_start[static_cast<uint>(_state)] = Ticks::now();
}

Tickspan G1ConcurrentRefineSweepState::get_duration(State start, State end) {
  return _state_start[static_cast<uint>(end)] - _state_start[static_cast<uint>(start)];
}

void G1ConcurrentRefineSweepState::reset_stats() {
  stats()->reset();
}

void G1ConcurrentRefineSweepState::add_yield_during_sweep_duration(jlong duration) {
  stats()->inc_yield_during_sweep_duration(duration);
}

bool G1ConcurrentRefineSweepState::advance_state(State next_state) {
  bool result = is_in_progress();
  if (result) {
    _state = next_state;
  } else {
    _state = State::Idle;
  }
  return result;
}

void G1ConcurrentRefineSweepState::assert_state(State expected) {
  assert(_state == expected, "must be %s but is %s", state_name(expected), state_name(_state));
}

void G1ConcurrentRefineSweepState::start_work() {
  assert_state(State::Idle);

  set_state_start_time();

  _stats.reset();

  _state = State::SwapGlobalCT;
}

bool G1ConcurrentRefineSweepState::swap_global_card_table() {
  assert_state(State::SwapGlobalCT);

  GCTraceTime(Info, gc, refine) tm("Concurrent Refine Global Card Table Swap");
  set_state_start_time();

  {
    // We can't have any new threads being in the process of created while we
    // swap the card table because we read the current card table state during
    // initialization.
    // A safepoint may occur during that time, so leave the STS temporarily.
    SuspendibleThreadSetLeaver sts_leave;

    MutexLocker mu(Threads_lock);
    // A GC that advanced the epoch might have happened, which already switched
    // The global card table. Do nothing.
    if (is_in_progress()) {
      G1BarrierSet::g1_barrier_set()->swap_global_card_table();
    }
  }

  return advance_state(State::SwapJavaThreadsCT);
}

bool G1ConcurrentRefineSweepState::swap_java_threads_ct() {
  assert_state(State::SwapJavaThreadsCT);

  GCTraceTime(Info, gc, refine) tm("Concurrent Refine Java Thread CT swap");

  set_state_start_time();

  {
    // Need to leave the STS to avoid potential deadlock in the handshake.
    SuspendibleThreadSetLeaver sts;

    class G1SwapThreadCardTableClosure : public HandshakeClosure {
    public:
      G1SwapThreadCardTableClosure() : HandshakeClosure("G1 Java Thread CT swap") { }

      virtual void do_thread(Thread* thread) {
        G1BarrierSet* bs = G1BarrierSet::g1_barrier_set();
        bs->update_card_table_base(thread);
      }
    } cl;
    Handshake::execute(&cl);
  }

  return advance_state(State::SynchronizeGCThreads);
}

bool G1ConcurrentRefineSweepState::swap_gc_threads_ct() {
  assert_state(State::SynchronizeGCThreads);

  GCTraceTime(Info, gc, refine) tm("Concurrent Refine GC Thread CT swap");

  set_state_start_time();

  {
    class RendezvousGCThreads: public VM_Operation {
    public:
      VMOp_Type type() const { return VMOp_G1RendezvousGCThreads; }

      virtual bool evaluate_at_safepoint() const {
        // We only care about synchronizing the GC threads.
        // Leave the Java threads running.
        return false;
      }

      virtual bool skip_thread_oop_barriers() const {
        fatal("Concurrent VMOps should not call this");
        return true;
      }

      void doit() {
        // Light weight "handshake" of the GC threads for memory synchronization;
        // both changes to the Java heap need to be synchronized as well as the
        // previous global card table reference change, so that no GC thread
        // accesses the wrong card table.
        // For example in the rebuild remset process the marking threads write
        // marks into the card table, and that card table reference must be the
        // correct one.
        SuspendibleThreadSet::synchronize();
        SuspendibleThreadSet::desynchronize();
      };
    } op;

    SuspendibleThreadSetLeaver sts_leave;
    VMThread::execute(&op);
  }

  return advance_state(State::SnapshotHeap);
}

void G1ConcurrentRefineSweepState::snapshot_heap(bool concurrent) {
  if (concurrent) {
    GCTraceTime(Info, gc, refine) tm("Concurrent Refine Snapshot Heap");

    assert_state(State::SnapshotHeap);

    set_state_start_time();

    snapshot_heap_inner();

    advance_state(State::SweepRT);
  } else {
    assert_state(State::Idle);
    assert_at_safepoint();

    snapshot_heap_inner();
  }
}

void G1ConcurrentRefineSweepState::sweep_refinement_table_start() {
  assert_state(State::SweepRT);

  set_state_start_time();
}

bool G1ConcurrentRefineSweepState::sweep_refinement_table_step() {
  assert_state(State::SweepRT);

  GCTraceTime(Info, gc, refine) tm("Concurrent Refine Table Step");

  G1ConcurrentRefine* cr = G1CollectedHeap::heap()->concurrent_refine();

  G1ConcurrentRefineSweepTask task(_sweep_table, &_stats, cr->num_threads_wanted());
  cr->run_with_refinement_workers(&task);

  if (task.sweep_completed()) {
    advance_state(State::CompleteRefineWork);
    return true;
  } else {
    return false;
  }
}

bool G1ConcurrentRefineSweepState::complete_work(bool concurrent, bool print_log) {
  if (concurrent) {
    assert_state(State::CompleteRefineWork);
  } else {
    // May have been forced to complete at any other time.
    assert(is_in_progress() && _state != State::CompleteRefineWork, "must be but is %s", state_name(_state));
  }

  set_state_start_time();

  if (print_log) {
    G1ConcurrentRefineStats* s = &_stats;

    State state_bounded_by_sweeprt = (_state == State::SweepRT || _state == State::CompleteRefineWork)
                                   ? State::SweepRT : _state;

    log_debug(gc, refine)("Refinement took %.2fms (pre-sweep %.2fms card refine %.2fms) "
                          "(scanned %zu clean %zu (%.2f%%) not_clean %zu (%.2f%%) not_parsable %zu "
                          "refers_to_cset %zu (%.2f%%) still_refers_to_cset %zu (%.2f%%) no_cross_region %zu pending %zu)",
                          get_duration(State::Idle, _state).seconds() * 1000.0,
                          get_duration(State::Idle, state_bounded_by_sweeprt).seconds() * 1000.0,
                          TimeHelper::counter_to_millis(s->refine_duration()),
                          s->cards_scanned(),
                          s->cards_clean(),
                          percent_of(s->cards_clean(), s->cards_scanned()),
                          s->cards_not_clean(),
                          percent_of(s->cards_not_clean(), s->cards_scanned()),
                          s->cards_not_parsable(),
                          s->cards_refer_to_cset(),
                          percent_of(s->cards_refer_to_cset(), s->cards_not_clean()),
                          s->cards_already_refer_to_cset(),
                          percent_of(s->cards_already_refer_to_cset(), s->cards_not_clean()),
                          s->cards_no_cross_region(),
                          s->cards_pending()
                         );
  }

  bool has_sweep_rt_work = _state == State::SweepRT;

  advance_state(State::Idle);
  return has_sweep_rt_work;
}

void G1ConcurrentRefineSweepState::snapshot_heap_inner() {
  // G1CollectedHeap::heap_region_iterate() below will only visit currently committed
  // regions. Initialize all entries in the state table here and later in this method
  // selectively enable regions that we are interested. This way regions committed
  // later will be automatically excluded from iteration.
  // Their refinement table must be completely empty anyway.
  _sweep_table->reset_all_to_claimed();

  class SnapshotRegionsClosure : public G1HeapRegionClosure {
    G1CardTableClaimTable* _sweep_table;

  public:
    SnapshotRegionsClosure(G1CardTableClaimTable* sweep_table) : G1HeapRegionClosure(), _sweep_table(sweep_table) { }

    bool do_heap_region(G1HeapRegion* r) override {
      if (!r->is_free()) {
        // Need to scan all parts of non-free regions, so reset the claim.
        // No need for synchronization: we are only interested in regions
        // that were allocated before the handshake; the handshake makes such
        // regions' metadata visible to all threads, and we do not care about
        // humongous regions that were allocated afterwards.
        _sweep_table->reset_to_unclaimed(r->hrm_index());
      }
      return false;
    }
  } cl(_sweep_table);
  G1CollectedHeap::heap()->heap_region_iterate(&cl);
}

bool G1ConcurrentRefineSweepState::is_in_progress() const {
  return _state != State::Idle;
}

bool G1ConcurrentRefineSweepState::are_java_threads_synched() const {
  return _state > State::SwapJavaThreadsCT || !is_in_progress();
}
#endif // AARCH64

uint64_t G1ConcurrentRefine::adjust_threads_period_ms() const {
  // Instead of a fixed value, this could be a command line option.  But then
  // we might also want to allow configuration of adjust_threads_wait_ms().

#ifdef AARCH64
  // Use a prime number close to 50ms, different to other components that derive
  // their wait time from the try_get_available_bytes_estimate() call to minimize
  // interference.
  return 53;
#else // AARCH64
  return 50;
#endif // AARCH64
}

static size_t minimum_pending_cards_target() {
#ifdef AARCH64
  return ParallelGCThreads * G1PerThreadPendingCardThreshold;
#else // AARCH64
  // One buffer per thread.
  return ParallelGCThreads * G1UpdateBufferSize;
#endif // AARCH64
}

#ifdef AARCH64
G1ConcurrentRefine::G1ConcurrentRefine(G1CollectedHeap* g1h) :
  _policy(g1h->policy()),
  _num_threads_wanted(0),
#else // AARCH64
G1ConcurrentRefine::G1ConcurrentRefine(G1Policy* policy) :
  _policy(policy),
  _threads_wanted(0),
#endif // AARCH64
  _pending_cards_target(PendingCardsTargetUninitialized),
  _last_adjust(),
  _needs_adjust(false),
#ifdef AARCH64
  _heap_was_locked(false),
  _threads_needed(g1h->policy(), adjust_threads_period_ms()),
#else // AARCH64
  _threads_needed(policy, adjust_threads_period_ms()),
#endif // AARCH64
  _thread_control(G1ConcRefinementThreads),
#ifdef AARCH64
  _sweep_state(g1h->max_num_regions())
{ }
#else // AARCH64
  _dcqs(G1BarrierSet::dirty_card_queue_set())
{}
#endif // AARCH64

jint G1ConcurrentRefine::initialize() {
  return _thread_control.initialize(this);
}

#ifdef AARCH64
G1ConcurrentRefineSweepState& G1ConcurrentRefine::sweep_state_for_merge() {
  bool has_sweep_claims = sweep_state().complete_work(false /* concurrent */);
  if (has_sweep_claims) {
    log_debug(gc, refine)("Continue existing work");
  } else {
    // Refinement has been interrupted without having a snapshot. There may
    // be a mix of already swapped and not-swapped card tables assigned to threads,
    // so they might have already dirtied the swapped card tables.
    // Conservatively scan all (non-free, non-committed) region's card tables,
    // creating the snapshot right now.
    log_debug(gc, refine)("Create work from scratch");

    sweep_state().snapshot_heap(false /* concurrent */);
  }
  return sweep_state();
}

void G1ConcurrentRefine::run_with_refinement_workers(WorkerTask* task) {
  _thread_control.run_task(task, num_threads_wanted());
}

void G1ConcurrentRefine::notify_region_reclaimed(G1HeapRegion* r) {
  assert_at_safepoint();
  if (_sweep_state.is_in_progress()) {
    _sweep_state.sweep_table()->claim_all_cards(r->hrm_index());
  }
}

G1ConcurrentRefine* G1ConcurrentRefine::create(G1CollectedHeap* g1h, jint* ecode) {
  G1ConcurrentRefine* cr = new G1ConcurrentRefine(g1h);
#else // AARCH64
G1ConcurrentRefine* G1ConcurrentRefine::create(G1Policy* policy, jint* ecode) {
  G1ConcurrentRefine* cr = new G1ConcurrentRefine(policy);
#endif // AARCH64
  *ecode = cr->initialize();
  if (*ecode != 0) {
    delete cr;
    cr = nullptr;
  }
  return cr;
}

void G1ConcurrentRefine::stop() {
  _thread_control.stop();
}

G1ConcurrentRefine::~G1ConcurrentRefine() {
}

void G1ConcurrentRefine::threads_do(ThreadClosure *tc) {
#ifdef AARCH64
  worker_threads_do(tc);
  control_thread_do(tc);
}

void G1ConcurrentRefine::worker_threads_do(ThreadClosure *tc) {
#endif // AARCH64
  _thread_control.worker_threads_do(tc);
}

#ifdef AARCH64
void G1ConcurrentRefine::control_thread_do(ThreadClosure *tc) {
  _thread_control.control_thread_do(tc);
}

void G1ConcurrentRefine::update_pending_cards_target(double pending_cards_time_ms,
                                                     size_t processed_pending_cards,
#else // AARCH64
void G1ConcurrentRefine::update_pending_cards_target(double logged_cards_time_ms,
                                                     size_t processed_logged_cards,
                                                     size_t predicted_thread_buffer_cards,
#endif // AARCH64
                                                     double goal_ms) {
  size_t minimum = minimum_pending_cards_target();
#ifdef AARCH64
  if ((processed_pending_cards < minimum) || (pending_cards_time_ms == 0.0)) {
    log_debug(gc, ergo, refine)("Unchanged pending cards target: %zu (processed %zu minimum %zu time %1.2f)",
                                _pending_cards_target, processed_pending_cards, minimum, pending_cards_time_ms);
#else // AARCH64
  if ((processed_logged_cards < minimum) || (logged_cards_time_ms == 0.0)) {
    log_debug(gc, ergo, refine)("Unchanged pending cards target: %zu",
                                _pending_cards_target);
#endif // AARCH64
    return;
  }

  // Base the pending cards budget on the measured rate.
#ifdef AARCH64
  double rate = processed_pending_cards / pending_cards_time_ms;
  size_t new_target = static_cast<size_t>(goal_ms * rate);
#else // AARCH64
  double rate = processed_logged_cards / logged_cards_time_ms;
  size_t budget = static_cast<size_t>(goal_ms * rate);
  // Deduct predicted cards in thread buffers to get target.
  size_t new_target = budget - MIN2(budget, predicted_thread_buffer_cards);
#endif // AARCH64
  // Add some hysteresis with previous values.
  if (is_pending_cards_target_initialized()) {
    new_target = (new_target + _pending_cards_target) / 2;
  }
  // Apply minimum target.
  new_target = MAX2(new_target, minimum_pending_cards_target());
  _pending_cards_target = new_target;
  log_debug(gc, ergo, refine)("New pending cards target: %zu", new_target);
}

#ifdef AARCH64
void G1ConcurrentRefine::adjust_after_gc(double pending_cards_time_ms,
                                         size_t processed_pending_cards,
#else // AARCH64
void G1ConcurrentRefine::adjust_after_gc(double logged_cards_time_ms,
                                         size_t processed_logged_cards,
                                         size_t predicted_thread_buffer_cards,
#endif // AARCH64
                                         double goal_ms) {
#ifdef AARCH64
  if (!G1UseConcRefinement) {
    return;
  }
#else // AARCH64
  if (!G1UseConcRefinement) return;
#endif // AARCH64

#ifdef AARCH64
  update_pending_cards_target(pending_cards_time_ms,
                              processed_pending_cards,
#else // AARCH64
  update_pending_cards_target(logged_cards_time_ms,
                              processed_logged_cards,
                              predicted_thread_buffer_cards,
#endif // AARCH64
                              goal_ms);
#ifdef AARCH64
  if (_thread_control.is_refinement_enabled()) {
#else // AARCH64
  if (_thread_control.max_num_threads() == 0) {
    // If no refinement threads then the mutator threshold is the target.
    _dcqs.set_mutator_refinement_threshold(_pending_cards_target);
  } else {
    // Provisionally make the mutator threshold unlimited, to be updated by
    // the next periodic adjustment.  Because card state may have changed
    // drastically, record that adjustment is needed and kick the primary
    // thread, in case it is waiting.
    _dcqs.set_mutator_refinement_threshold(SIZE_MAX);
#endif // AARCH64
    _needs_adjust = true;
    if (is_pending_cards_target_initialized()) {
#ifdef AARCH64
      _thread_control.activate();
#else // AARCH64
      _thread_control.activate(0);
#endif // AARCH64
    }
  }
}

#ifdef AARCH64
uint64_t G1ConcurrentRefine::adjust_threads_wait_ms() const {
  assert_current_thread_is_control_refinement_thread();
  if (is_pending_cards_target_initialized()) {
    // Retry asap when the cause for not getting a prediction was that we temporarily
    // did not get the heap lock. Otherwise we might wait for too long until we get
    // back here.
    if (_heap_was_locked) {
      return 1;
    }
    double available_time_ms = _threads_needed.predicted_time_until_next_gc_ms();
#else // AARCH64
// Wake up the primary thread less frequently when the time available until
// the next GC is longer.  But don't increase the wait time too rapidly.
// This reduces the number of primary thread wakeups that just immediately
// go back to waiting, while still being responsive to behavior changes.
static uint64_t compute_adjust_wait_time_ms(double available_ms) {
  return static_cast<uint64_t>(sqrt(available_ms) * 4.0);
}
#endif // AARCH64

#ifdef AARCH64
    return _policy->adjust_wait_time_ms(available_time_ms, adjust_threads_period_ms());
#else // AARCH64
uint64_t G1ConcurrentRefine::adjust_threads_wait_ms() const {
  assert_current_thread_is_primary_refinement_thread();
  if (is_pending_cards_target_initialized()) {
    double available_ms = _threads_needed.predicted_time_until_next_gc_ms();
    uint64_t wait_time_ms = compute_adjust_wait_time_ms(available_ms);
    return MAX2(wait_time_ms, adjust_threads_period_ms());
#endif // AARCH64
  } else {
    // If target not yet initialized then wait forever (until explicitly
    // activated).  This happens during startup, when we don't bother with
    // refinement.
    return 0;
  }
}

#ifdef AARCH64
bool G1ConcurrentRefine::adjust_num_threads_periodically() {
  assert_current_thread_is_control_refinement_thread();
#else // AARCH64
class G1ConcurrentRefine::RemSetSamplingClosure : public G1HeapRegionClosure {
  size_t _sampled_code_root_rs_length;
#endif // AARCH64

#ifdef AARCH64
  _heap_was_locked = false;
  // Check whether it's time to do a periodic adjustment if there is no explicit
  // request pending. We might have spuriously woken up.
#else // AARCH64
public:
  RemSetSamplingClosure() :
    _sampled_code_root_rs_length(0) {}

  bool do_heap_region(G1HeapRegion* r) override {
    G1HeapRegionRemSet* rem_set = r->rem_set();
    _sampled_code_root_rs_length += rem_set->code_roots_list_length();
    return false;
  }

  size_t sampled_code_root_rs_length() const { return _sampled_code_root_rs_length; }
};

// Adjust the target length (in regions) of the young gen, based on the
// current length of the remembered sets.
//
// At the end of the GC G1 determines the length of the young gen based on
// how much time the next GC can take, and when the next GC may occur
// according to the MMU.
//
// The assumption is that a significant part of the GC is spent on scanning
// the remembered sets (and many other components), so this thread constantly
// reevaluates the prediction for the remembered set scanning costs, and potentially
// resizes the young gen. This may do a premature GC or even increase the young
// gen size to keep pause time length goal.
void G1ConcurrentRefine::adjust_young_list_target_length() {
  if (_policy->use_adaptive_young_list_length()) {
    G1CollectedHeap* g1h = G1CollectedHeap::heap();
    G1CollectionSet* cset = g1h->collection_set();
    RemSetSamplingClosure cl;
    cset->iterate(&cl);

    size_t card_rs_length = g1h->young_regions_cardset()->occupied();

    size_t sampled_code_root_rs_length = cl.sampled_code_root_rs_length();
    _policy->revise_young_list_target_length(card_rs_length, sampled_code_root_rs_length);
  }
}

bool G1ConcurrentRefine::adjust_threads_periodically() {
  assert_current_thread_is_primary_refinement_thread();

  // Check whether it's time to do a periodic adjustment.
#endif // AARCH64
  if (!_needs_adjust) {
    Tickspan since_adjust = Ticks::now() - _last_adjust;
#ifdef AARCH64
    if (since_adjust.milliseconds() < adjust_threads_period_ms()) {
      Atomic::store(&_num_threads_wanted, 0u);
      return false;
#else // AARCH64
    if (since_adjust.milliseconds() >= adjust_threads_period_ms()) {
      _needs_adjust = true;
#endif // AARCH64
    }
  }

#ifdef AARCH64
  // Reset pending request.
  _needs_adjust = false;
  size_t available_bytes = 0;
  if (_policy->try_get_available_bytes_estimate(available_bytes)) {
    adjust_threads_wanted(available_bytes);
    _last_adjust = Ticks::now();
  } else {
    _heap_was_locked = true;
    // Defer adjustment to next time.
    _needs_adjust = true;
#else // AARCH64
  // If needed, try to adjust threads wanted.
  if (_needs_adjust) {
    // Getting used young bytes requires holding Heap_lock.  But we can't use
    // normal lock and block until available.  Blocking on the lock could
    // deadlock with a GC VMOp that is holding the lock and requesting a
    // safepoint.  Instead try to lock, and if fail then skip adjustment for
    // this iteration of the thread, do some refinement work, and retry the
    // adjustment later.
    if (Heap_lock->try_lock()) {
      size_t used_bytes = _policy->estimate_used_young_bytes_locked();
      Heap_lock->unlock();
      adjust_young_list_target_length();
      size_t young_bytes = _policy->young_list_target_length() * G1HeapRegion::GrainBytes;
      size_t available_bytes = young_bytes - MIN2(young_bytes, used_bytes);
      adjust_threads_wanted(available_bytes);
      _needs_adjust = false;
      _last_adjust = Ticks::now();
      return true;
    }
#endif // AARCH64
  }

#ifdef AARCH64
  return (num_threads_wanted() > 0) && !heap_was_locked();
#else // AARCH64
  return false;
}

bool G1ConcurrentRefine::is_in_last_adjustment_period() const {
  return _threads_needed.predicted_time_until_next_gc_ms() <= adjust_threads_period_ms();
#endif // AARCH64
}

void G1ConcurrentRefine::adjust_threads_wanted(size_t available_bytes) {
#ifdef AARCH64
  assert_current_thread_is_control_refinement_thread();
#else // AARCH64
  assert_current_thread_is_primary_refinement_thread();
  size_t num_cards = _dcqs.num_cards();
  size_t mutator_threshold = SIZE_MAX;
  uint old_wanted = Atomic::load(&_threads_wanted);
#endif // AARCH64

#ifdef AARCH64
  G1Policy* policy = G1CollectedHeap::heap()->policy();
  const G1Analytics* analytics = policy->analytics();

  size_t num_cards = policy->current_pending_cards();

  _threads_needed.update(num_threads_wanted(),
#else // AARCH64
  _threads_needed.update(old_wanted,
#endif // AARCH64
                         available_bytes,
                         num_cards,
                         _pending_cards_target);
  uint new_wanted = _threads_needed.threads_needed();
  if (new_wanted > _thread_control.max_num_threads()) {
#ifdef AARCH64
    // Bound the wanted threads by maximum available.
#else // AARCH64
    // If running all the threads can't reach goal, turn on refinement by
    // mutator threads.  Using target as the threshold may be stronger
    // than required, but will do the most to get us under goal, and we'll
    // reevaluate with the next adjustment.
    mutator_threshold = _pending_cards_target;
#endif // AARCH64
    new_wanted = _thread_control.max_num_threads();
#ifndef AARCH64
  } else if (is_in_last_adjustment_period()) {
    // If very little time remains until GC, enable mutator refinement.  If
    // the target has been reached, this keeps the number of pending cards on
    // target even if refinement threads deactivate in the meantime.  And if
    // the target hasn't been reached, this prevents things from getting
    // worse.
    mutator_threshold = _pending_cards_target;
#endif // !AARCH64
  }

#ifdef AARCH64
  Atomic::store(&_num_threads_wanted, new_wanted);

  log_debug(gc, refine)("Concurrent refinement: wanted %u, pending cards: %zu (pending-from-gc %zu), "
                        "predicted: %zu, goal %zu, time-until-next-gc: %1.2fms pred-refine-rate %1.2fc/ms log-rate %1.2fc/ms",
#else // AARCH64
  Atomic::store(&_threads_wanted, new_wanted);
  _dcqs.set_mutator_refinement_threshold(mutator_threshold);
  log_debug(gc, refine)("Concurrent refinement: wanted %u, cards: %zu, "
                        "predicted: %zu, time: %1.2fms",
#endif // AARCH64
                        new_wanted,
                        num_cards,
#ifdef AARCH64
                        G1CollectedHeap::heap()->policy()->pending_cards_from_gc(),
#endif // AARCH64
                        _threads_needed.predicted_cards_at_next_gc(),
#ifdef AARCH64
                        _pending_cards_target,
                        _threads_needed.predicted_time_until_next_gc_ms(),
                        analytics->predict_concurrent_refine_rate_ms(),
                        analytics->predict_dirtied_cards_rate_ms()
                        );
#else // AARCH64
                        _threads_needed.predicted_time_until_next_gc_ms());
  // Activate newly wanted threads.  The current thread is the primary
  // refinement thread, so is already active.
  for (uint i = MAX2(old_wanted, 1u); i < new_wanted; ++i) {
    if (!_thread_control.activate(i)) {
      // Failed to allocate and activate thread.  Stop trying to activate, and
      // instead use mutator threads to make up the gap.
      Atomic::store(&_threads_wanted, i);
      _dcqs.set_mutator_refinement_threshold(_pending_cards_target);
      break;
    }
  }
}

void G1ConcurrentRefine::reduce_threads_wanted() {
  assert_current_thread_is_primary_refinement_thread();
  if (!_needs_adjust) {         // Defer if adjustment request is active.
    uint wanted = Atomic::load(&_threads_wanted);
    if (wanted > 0) {
      Atomic::store(&_threads_wanted, --wanted);
    }
    // If very little time remains until GC, enable mutator refinement.  If
    // the target has been reached, this keeps the number of pending cards on
    // target even as refinement threads deactivate in the meantime.
    if (is_in_last_adjustment_period()) {
      _dcqs.set_mutator_refinement_threshold(_pending_cards_target);
    }
  }
}

bool G1ConcurrentRefine::is_thread_wanted(uint worker_id) const {
  return worker_id < Atomic::load(&_threads_wanted);
#endif // AARCH64
}

bool G1ConcurrentRefine::is_thread_adjustment_needed() const {
#ifdef AARCH64
  assert_current_thread_is_control_refinement_thread();
#else // AARCH64
  assert_current_thread_is_primary_refinement_thread();
#endif // AARCH64
  return _needs_adjust;
}

void G1ConcurrentRefine::record_thread_adjustment_needed() {
#ifdef AARCH64
  assert_current_thread_is_control_refinement_thread();
#else // AARCH64
  assert_current_thread_is_primary_refinement_thread();
#endif // AARCH64
  _needs_adjust = true;
}

#ifndef AARCH64
G1ConcurrentRefineStats G1ConcurrentRefine::get_and_reset_refinement_stats() {
  struct CollectStats : public ThreadClosure {
    G1ConcurrentRefineStats _total_stats;
    virtual void do_thread(Thread* t) {
      G1ConcurrentRefineThread* crt = static_cast<G1ConcurrentRefineThread*>(t);
      G1ConcurrentRefineStats& stats = *crt->refinement_stats();
      _total_stats += stats;
      stats.reset();
    }
  } collector;
  threads_do(&collector);
  return collector._total_stats;
}

uint G1ConcurrentRefine::worker_id_offset() {
  return G1DirtyCardQueueSet::num_par_ids();
}

bool G1ConcurrentRefine::try_refinement_step(uint worker_id,
                                             size_t stop_at,
                                             G1ConcurrentRefineStats* stats) {
  uint adjusted_id = worker_id + worker_id_offset();
  return _dcqs.refine_completed_buffer_concurrently(adjusted_id, stop_at, stats);
}
#endif // !AARCH64
