/*
 * Copyright (c) 2015, 2025, Oracle and/or its affiliates. All rights reserved.
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

#include "gc/g1/g1HeapRegion.hpp"
#include "gc/g1/g1SurvivorRegions.hpp"
#include "utilities/debug.hpp"
#include "utilities/growableArray.hpp"

G1SurvivorRegions::G1SurvivorRegions() :
#ifdef AARCH64
  _regions(8, mtGC),
#else // AARCH64
  _regions(new (mtGC) GrowableArray<G1HeapRegion*>(8, mtGC)),
#endif // AARCH64
  _used_bytes(0),
  _regions_on_node() {}

uint G1SurvivorRegions::add(G1HeapRegion* hr) {
  assert(hr->is_survivor(), "should be flagged as survivor region");
#ifdef AARCH64
  _regions.append(hr);
#else // AARCH64
  _regions->append(hr);
#endif // AARCH64
  return _regions_on_node.add(hr);
}

uint G1SurvivorRegions::length() const {
#ifdef AARCH64
  return (uint)_regions.length();
#else // AARCH64
  return (uint)_regions->length();
#endif // AARCH64
}

uint G1SurvivorRegions::regions_on_node(uint node_index) const {
  return _regions_on_node.count(node_index);
}

void G1SurvivorRegions::convert_to_eden() {
#ifdef AARCH64
  for (G1HeapRegion* r : _regions) {
    r->set_eden_pre_gc();
#else // AARCH64
  for (GrowableArrayIterator<G1HeapRegion*> it = _regions->begin();
       it != _regions->end();
       ++it) {
    G1HeapRegion* hr = *it;
    hr->set_eden_pre_gc();
#endif // AARCH64
  }
  clear();
}

void G1SurvivorRegions::clear() {
#ifdef AARCH64
  _regions.clear();
#else // AARCH64
  _regions->clear();
#endif // AARCH64
  _used_bytes = 0;
  _regions_on_node.clear();
}

void G1SurvivorRegions::add_used_bytes(size_t used_bytes) {
  _used_bytes += used_bytes;
}
