/**************************************************************************/
/*  test_projectiles.h                                                    */
/**************************************************************************/
/*                         This file is part of:                          */
/*                             GODOT ENGINE                               */
/*                        https://godotengine.org                         */
/**************************************************************************/
/* Copyright (c) 2014-present Godot Engine contributors (see AUTHORS.md). */
/* Copyright (c) 2007-2014 Juan Linietsky, Ariel Manzur.                  */
/*                                                                        */
/* Permission is hereby granted, free of charge, to any person obtaining  */
/* a copy of this software and associated documentation files (the        */
/* "Software"), to deal in the Software without restriction, including    */
/* without limitation the rights to use, copy, modify, merge, publish,    */
/* distribute, sublicense, and/or sell copies of the Software, and to     */
/* permit persons to whom the Software is furnished to do so, subject to  */
/* the following conditions:                                              */
/*                                                                        */
/* The above copyright notice and this permission notice shall be         */
/* included in all copies or substantial portions of the Software.        */
/*                                                                        */
/* THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND,        */
/* EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF     */
/* MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. */
/* IN NO EVENT SHALL THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY   */
/* CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER IN AN ACTION OF CONTRACT,   */
/* TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN CONNECTION WITH THE      */
/* SOFTWARE OR THE USE OR OTHER DEALINGS IN THE SOFTWARE.                 */
/**************************************************************************/

#pragma once

#include "../projectiles.h"

#include "tests/test_macros.h"

// Everything here runs outside the scene tree on purpose. `_tick` bails when
// `get_world_3d()` is null, so the pool can be exercised without standing up a
// physics space - which is what makes these tests deterministic. The simulation
// itself is not covered here; it needs a space and belongs in a scene test.

namespace TestProjectilePool {

static Ref<ProjectileKind> make_kind() {
	Ref<ProjectileKind> kind;
	kind.instantiate();
	return kind;
}

TEST_CASE("[Modules][ProjectilePool] Default state") {
	ProjectilePool *pool = memnew(ProjectilePool);

	CHECK(pool->get_live_count() == 0);
	CHECK(pool->get_capacity() == 256);
	CHECK(pool->get_positions().is_empty());
	CHECK_FALSE(pool->is_alive(0));

	memdelete(pool);
}

TEST_CASE("[Modules][ProjectilePool] Spawn issues a live handle") {
	ProjectilePool *pool = memnew(ProjectilePool);
	Ref<ProjectileKind> kind = make_kind();

	const int64_t handle = pool->spawn(kind, Vector3(1, 2, 3), Vector3(0, 0, -10));

	// Never zero: zero is reserved for "no projectile", which is what makes
	// `if handle:` the correct check on the GDScript side.
	CHECK(handle != 0);
	CHECK(pool->is_alive(handle));
	CHECK(pool->get_live_count() == 1);
	CHECK(pool->get_projectile_position(handle) == Vector3(1, 2, 3));
	CHECK(pool->get_projectile_velocity(handle) == Vector3(0, 0, -10));

	memdelete(pool);
}

TEST_CASE("[Modules][ProjectilePool] Despawn ends the projectile exactly once") {
	ProjectilePool *pool = memnew(ProjectilePool);
	Ref<ProjectileKind> kind = make_kind();

	const int64_t handle = pool->spawn(kind, Vector3(), Vector3(0, 0, -1));
	pool->despawn(handle);

	CHECK_FALSE(pool->is_alive(handle));
	CHECK(pool->get_live_count() == 0);

	// A second despawn is a no-op rather than an error. Callers hold handles
	// across frames and cannot always know whether the projectile ended itself.
	pool->despawn(handle);
	CHECK(pool->get_live_count() == 0);

	memdelete(pool);
}

TEST_CASE("[Modules][ProjectilePool] A stale handle cannot address its successor") {
	ProjectilePool *pool = memnew(ProjectilePool);
	Ref<ProjectileKind> kind = make_kind();

	const int64_t first = pool->spawn(kind, Vector3(1, 0, 0), Vector3());
	pool->despawn(first);

	// The freed slot is handed straight back out, so this reuses it. Without the
	// generation in the handle, `first` would now address `second` and despawning
	// a dead handle would kill a live stranger - which is the entire reason the
	// generation is there.
	const int64_t second = pool->spawn(kind, Vector3(2, 0, 0), Vector3());

	CHECK(first != second);
	CHECK_FALSE(pool->is_alive(first));
	CHECK(pool->is_alive(second));

	pool->despawn(first);
	CHECK(pool->is_alive(second));
	CHECK(pool->get_live_count() == 1);

	memdelete(pool);
}

TEST_CASE("[Modules][ProjectilePool] A full pool refuses rather than grows") {
	ProjectilePool *pool = memnew(ProjectilePool);
	Ref<ProjectileKind> kind = make_kind();

	pool->set_capacity(4);
	REQUIRE(pool->get_capacity() == 4);

	for (int i = 0; i < 4; i++) {
		CHECK(pool->spawn(kind, Vector3(), Vector3(0, 0, -1)) != 0);
	}
	CHECK(pool->get_live_count() == 4);

	// Zero, not an error and not a reallocation: growing an array mid-tick is a
	// frame spike, and in VR a frame spike is the thing being avoided.
	CHECK(pool->spawn(kind, Vector3(), Vector3(0, 0, -1)) == 0);
	CHECK(pool->get_live_count() == 4);

	memdelete(pool);
}

TEST_CASE("[Modules][ProjectilePool] Removal keeps the live set packed") {
	ProjectilePool *pool = memnew(ProjectilePool);
	Ref<ProjectileKind> kind = make_kind();

	const int64_t a = pool->spawn(kind, Vector3(1, 0, 0), Vector3());
	const int64_t b = pool->spawn(kind, Vector3(2, 0, 0), Vector3());
	const int64_t c = pool->spawn(kind, Vector3(3, 0, 0), Vector3());

	// Removing from the middle swap-removes the tail into the hole, so `c` moves
	// and its handle has to keep working through the move.
	pool->despawn(b);

	CHECK(pool->get_live_count() == 2);
	CHECK(pool->is_alive(a));
	CHECK_FALSE(pool->is_alive(b));
	CHECK(pool->is_alive(c));
	CHECK(pool->get_projectile_position(c) == Vector3(3, 0, 0));

	const PackedVector3Array positions = pool->get_positions();
	REQUIRE(positions.size() == 2);
	CHECK(positions[0] == Vector3(1, 0, 0));
	CHECK(positions[1] == Vector3(3, 0, 0));

	memdelete(pool);
}

TEST_CASE("[Modules][ProjectilePool] Clear drops everything in flight") {
	ProjectilePool *pool = memnew(ProjectilePool);
	Ref<ProjectileKind> kind = make_kind();

	const int64_t handle = pool->spawn(kind, Vector3(), Vector3(0, 0, -1));
	pool->clear();

	CHECK(pool->get_live_count() == 0);
	CHECK_FALSE(pool->is_alive(handle));

	memdelete(pool);
}

TEST_CASE("[Modules][ProjectileKind] Tunables clamp to usable ranges") {
	Ref<ProjectileKind> kind = make_kind();

	// A zero radius would make the substep count divide by zero, so it has a
	// floor rather than an assert.
	kind->set_radius(-1.0f);
	CHECK(kind->get_radius() > 0.0f);

	kind->set_bounce_restitution(4.0f);
	CHECK(kind->get_bounce_restitution() == doctest::Approx(1.0));

	kind->set_bounce_friction(-2.0f);
	CHECK(kind->get_bounce_friction() == doctest::Approx(0.0));

	// Counters are stored as uint8_t on the projectile, so anything above 255
	// would wrap silently.
	kind->set_pierce_count(9999);
	CHECK(kind->get_pierce_count() == 255);

	kind->set_lifetime(-5.0f);
	CHECK(kind->get_lifetime() == doctest::Approx(0.0));

	// PoE's defaults, which the module carries over deliberately.
	Ref<ProjectileKind> fresh = make_kind();
	CHECK(fresh->get_max_distance() == doctest::Approx(15.0));
	CHECK(fresh->get_seek_radius() == doctest::Approx(6.0));
	CHECK(Math::rad_to_deg(fresh->get_fork_angle()) == doctest::Approx(60.0).epsilon(0.01));
}

} // namespace TestProjectilePool
