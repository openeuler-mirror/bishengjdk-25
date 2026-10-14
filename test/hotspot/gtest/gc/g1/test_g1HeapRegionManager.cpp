/*
 * Copyright (c) Huawei Technologies Co., Ltd. 2026. All rights reserved.
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
 */

#include "gc/g1/g1HeapRegionManager.inline.hpp"
#include "gc/g1/g1RegionToSpaceMapper.hpp"
#include "memory/memoryReserver.hpp"
#include "memory/universe.hpp"
#include "runtime/interfaceSupport.inline.hpp"
#include "runtime/os.hpp"
#include "runtime/vmOperations.hpp"
#include "runtime/vmThread.hpp"
#include "unittest.hpp"

// Universe::is_dynamic_max_heap_enable() is always false off AArch64.
#ifdef AARCH64

class DynamicMaxHeapTestMark : public StackObj {
  const bool _saved;

public:
  DynamicMaxHeapTestMark() : _saved(Universe::is_dynamic_max_heap_enable()) {
    Universe::set_dynamic_max_heap_enable(true);
  }

  ~DynamicMaxHeapTestMark() {
    Universe::set_dynamic_max_heap_enable(_saved);
  }
};

class G1CapTestReservation : public StackObj {
  ReservedSpace _rs;
  G1RegionToSpaceMapper* _mapper;

public:
  G1CapTestReservation()
    : _rs(MemoryReserver::reserve(2 * G1HeapRegion::GrainBytes,
                                 G1HeapRegion::GrainBytes,
                                 os::vm_page_size(),
                                 mtTest)),
      _mapper(nullptr) {
    if (_rs.is_reserved()) {
      _mapper = G1RegionToSpaceMapper::create_mapper(_rs,
                                                    _rs.size(),
                                                    os::vm_page_size(),
                                                    G1HeapRegion::GrainBytes,
                                                    1,
                                                    mtTest);
    }
  }

  ~G1CapTestReservation() {
    delete _mapper;
    if (_rs.is_reserved()) {
      MemoryReserver::release(_rs);
    }
  }

  bool is_reserved() const { return _rs.is_reserved(); }
  G1RegionToSpaceMapper* mapper() const { return _mapper; }
};

class VM_G1AllocateContainingRegionsCapTest : public VM_GTestExecuteAtSafepoint {
public:
  void doit() override {
    // Change the global feature gate only at a safepoint, and restore it
    // before returning to the live JVM.
    DynamicMaxHeapTestMark dynamic_heap;
    G1CapTestReservation reservation;
    ASSERT_TRUE(reservation.is_reserved());

    G1HeapRegionManager manager;
    // Auxiliary mappers are unused: this request must not commit anything.
    manager.initialize(reservation.mapper(), nullptr, nullptr, nullptr, nullptr);

    // An empty private manager makes cap == committed deterministic. This
    // tests the allocator's exhausted-budget state, not JVM startup reachability.
    manager.set_dynamic_max_heap_length(0);
    ASSERT_EQ(0u, manager.num_committed_regions());
    ASSERT_EQ(0u, manager.available());

    MemRegion target(manager.reserved().start() + G1HeapRegion::GrainWords,
                     G1HeapRegion::GrainWords);
    ASSERT_FALSE(manager.is_available(1));
    ASSERT_EQ(nullptr, manager.addr_to_region(target.start()));

    size_t commits = 0;
    // Before the fix, expansion is skipped and the null G1HeapRegion is
    // dereferenced by curr_region->is_free(), causing SIGSEGV.
    EXPECT_FALSE(manager.allocate_containing_regions(target, &commits, nullptr));
    EXPECT_EQ(0u, manager.num_committed_regions());
    EXPECT_FALSE(manager.is_available(1));
    EXPECT_EQ(nullptr, manager.addr_to_region(target.start()));
  }
};

TEST_OTHER_VM(G1HeapRegionManager, allocate_containing_regions_cap_exhausted) {
  if (!UseG1GC) {
    return;
  }

  VM_G1AllocateContainingRegionsCapTest op;
  ThreadInVMfromNative invm(JavaThread::current());
  VMThread::execute(&op);
}

#endif // AARCH64
