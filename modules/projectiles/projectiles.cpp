/**************************************************************************/
/*  projectiles.cpp                                                       */
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

#include "projectiles.h"

#include "core/config/engine.h"
#include "core/object/class_db.h"
#include "scene/resources/3d/world_3d.h"
#include "servers/rendering/rendering_server.h"

// How far past a surface a surviving projectile is placed before its next substep.
// Without it a pierce restarts its ray exactly on the face it just crossed and the
// solver is free to report the same contact again at t=0.
#define SURFACE_MARGIN 0.001f

// Below this a bounced projectile is dead rather than crawling. Unreal calls the
// same number BounceVelocityStopSimulatingThreshold and defaults it to 5 cm/s.
#define BOUNCE_STOP_SPEED 0.05f

// Remaining tick time below which another iteration is not worth a ray query.
#define MIN_STEP_TIME 1e-6f

/**************************************************************************/
/* ProjectileKind                                                    */
/**************************************************************************/

void ProjectileKind::set_radius(float p_radius) {
	radius = MAX(p_radius, 0.001f);
	emit_changed();
}

void ProjectileKind::set_lifetime(float p_lifetime) {
	lifetime = MAX(p_lifetime, 0.0f);
	emit_changed();
}

void ProjectileKind::set_max_distance(float p_distance) {
	max_distance = MAX(p_distance, 0.0f);
	emit_changed();
}

void ProjectileKind::set_gravity_scale(float p_scale) {
	gravity_scale = p_scale;
	emit_changed();
}

void ProjectileKind::set_drag(float p_drag) {
	drag = MAX(p_drag, 0.0f);
	emit_changed();
}

void ProjectileKind::set_homing_acceleration(float p_accel) {
	homing_acceleration = MAX(p_accel, 0.0f);
	emit_changed();
}

void ProjectileKind::set_behaviours(BitField<Behavior> p_behaviours) {
	behaviors = p_behaviours;
	emit_changed();
}

void ProjectileKind::set_split_count(int p_count) {
	split_count = CLAMP(p_count, 0, 255);
	emit_changed();
}

void ProjectileKind::set_pierce_count(int p_count) {
	pierce_count = CLAMP(p_count, 0, 255);
	emit_changed();
}

void ProjectileKind::set_fork_count(int p_count) {
	fork_count = CLAMP(p_count, 0, 255);
	emit_changed();
}

void ProjectileKind::set_chain_count(int p_count) {
	chain_count = CLAMP(p_count, 0, 255);
	emit_changed();
}

void ProjectileKind::set_return_count(int p_count) {
	return_count = CLAMP(p_count, 0, 255);
	emit_changed();
}

void ProjectileKind::set_bounce_count(int p_count) {
	bounce_count = CLAMP(p_count, 0, 255);
	emit_changed();
}

void ProjectileKind::set_fork_angle(float p_radians) {
	fork_angle = CLAMP(p_radians, 0.0f, (float)Math::PI);
	emit_changed();
}

void ProjectileKind::set_seek_radius(float p_radius) {
	seek_radius = MAX(p_radius, 0.0f);
	emit_changed();
}

void ProjectileKind::set_bounce_restitution(float p_restitution) {
	bounce_restitution = CLAMP(p_restitution, 0.0f, 1.0f);
	emit_changed();
}

void ProjectileKind::set_bounce_friction(float p_friction) {
	bounce_friction = CLAMP(p_friction, 0.0f, 1.0f);
	emit_changed();
}

void ProjectileKind::set_collision_mask(uint32_t p_mask) {
	collision_mask = p_mask;
	emit_changed();
}

void ProjectileKind::set_seek_mask(uint32_t p_mask) {
	seek_mask = p_mask;
	emit_changed();
}

void ProjectileKind::set_collide_with_areas(bool p_enabled) {
	collide_with_areas = p_enabled;
	emit_changed();
}

void ProjectileKind::_bind_methods() {
	ClassDB::bind_method(D_METHOD("set_radius", "radius"), &ProjectileKind::set_radius);
	ClassDB::bind_method(D_METHOD("get_radius"), &ProjectileKind::get_radius);
	ClassDB::bind_method(D_METHOD("set_lifetime", "lifetime"), &ProjectileKind::set_lifetime);
	ClassDB::bind_method(D_METHOD("get_lifetime"), &ProjectileKind::get_lifetime);
	ClassDB::bind_method(D_METHOD("set_max_distance", "distance"), &ProjectileKind::set_max_distance);
	ClassDB::bind_method(D_METHOD("get_max_distance"), &ProjectileKind::get_max_distance);
	ClassDB::bind_method(D_METHOD("set_gravity_scale", "scale"), &ProjectileKind::set_gravity_scale);
	ClassDB::bind_method(D_METHOD("get_gravity_scale"), &ProjectileKind::get_gravity_scale);
	ClassDB::bind_method(D_METHOD("set_drag", "drag"), &ProjectileKind::set_drag);
	ClassDB::bind_method(D_METHOD("get_drag"), &ProjectileKind::get_drag);
	ClassDB::bind_method(D_METHOD("set_homing_acceleration", "acceleration"), &ProjectileKind::set_homing_acceleration);
	ClassDB::bind_method(D_METHOD("get_homing_acceleration"), &ProjectileKind::get_homing_acceleration);
	ClassDB::bind_method(D_METHOD("set_behaviours", "behaviors"), &ProjectileKind::set_behaviours);
	ClassDB::bind_method(D_METHOD("get_behaviours"), &ProjectileKind::get_behaviours);
	ClassDB::bind_method(D_METHOD("set_split_count", "count"), &ProjectileKind::set_split_count);
	ClassDB::bind_method(D_METHOD("get_split_count"), &ProjectileKind::get_split_count);
	ClassDB::bind_method(D_METHOD("set_pierce_count", "count"), &ProjectileKind::set_pierce_count);
	ClassDB::bind_method(D_METHOD("get_pierce_count"), &ProjectileKind::get_pierce_count);
	ClassDB::bind_method(D_METHOD("set_fork_count", "count"), &ProjectileKind::set_fork_count);
	ClassDB::bind_method(D_METHOD("get_fork_count"), &ProjectileKind::get_fork_count);
	ClassDB::bind_method(D_METHOD("set_chain_count", "count"), &ProjectileKind::set_chain_count);
	ClassDB::bind_method(D_METHOD("get_chain_count"), &ProjectileKind::get_chain_count);
	ClassDB::bind_method(D_METHOD("set_return_count", "count"), &ProjectileKind::set_return_count);
	ClassDB::bind_method(D_METHOD("get_return_count"), &ProjectileKind::get_return_count);
	ClassDB::bind_method(D_METHOD("set_bounce_count", "count"), &ProjectileKind::set_bounce_count);
	ClassDB::bind_method(D_METHOD("get_bounce_count"), &ProjectileKind::get_bounce_count);
	ClassDB::bind_method(D_METHOD("set_fork_angle", "radians"), &ProjectileKind::set_fork_angle);
	ClassDB::bind_method(D_METHOD("get_fork_angle"), &ProjectileKind::get_fork_angle);
	ClassDB::bind_method(D_METHOD("set_seek_radius", "radius"), &ProjectileKind::set_seek_radius);
	ClassDB::bind_method(D_METHOD("get_seek_radius"), &ProjectileKind::get_seek_radius);
	ClassDB::bind_method(D_METHOD("set_bounce_restitution", "restitution"), &ProjectileKind::set_bounce_restitution);
	ClassDB::bind_method(D_METHOD("get_bounce_restitution"), &ProjectileKind::get_bounce_restitution);
	ClassDB::bind_method(D_METHOD("set_bounce_friction", "friction"), &ProjectileKind::set_bounce_friction);
	ClassDB::bind_method(D_METHOD("get_bounce_friction"), &ProjectileKind::get_bounce_friction);
	ClassDB::bind_method(D_METHOD("set_collision_mask", "mask"), &ProjectileKind::set_collision_mask);
	ClassDB::bind_method(D_METHOD("get_collision_mask"), &ProjectileKind::get_collision_mask);
	ClassDB::bind_method(D_METHOD("set_seek_mask", "mask"), &ProjectileKind::set_seek_mask);
	ClassDB::bind_method(D_METHOD("get_seek_mask"), &ProjectileKind::get_seek_mask);
	ClassDB::bind_method(D_METHOD("set_collide_with_areas", "enabled"), &ProjectileKind::set_collide_with_areas);
	ClassDB::bind_method(D_METHOD("get_collide_with_areas"), &ProjectileKind::get_collide_with_areas);

	ADD_GROUP("Motion", "");
	ADD_PROPERTY(PropertyInfo(Variant::FLOAT, "radius", PROPERTY_HINT_RANGE, "0.01,2,0.005,suffix:m"), "set_radius", "get_radius");
	ADD_PROPERTY(PropertyInfo(Variant::FLOAT, "lifetime", PROPERTY_HINT_RANGE, "0.1,60,0.1,suffix:s"), "set_lifetime", "get_lifetime");
	ADD_PROPERTY(PropertyInfo(Variant::FLOAT, "max_distance", PROPERTY_HINT_RANGE, "0,500,0.5,suffix:m"), "set_max_distance", "get_max_distance");
	ADD_PROPERTY(PropertyInfo(Variant::FLOAT, "gravity_scale", PROPERTY_HINT_RANGE, "-4,4,0.01"), "set_gravity_scale", "get_gravity_scale");
	ADD_PROPERTY(PropertyInfo(Variant::FLOAT, "drag", PROPERTY_HINT_RANGE, "0,2,0.001"), "set_drag", "get_drag");
	ADD_PROPERTY(PropertyInfo(Variant::FLOAT, "homing_acceleration", PROPERTY_HINT_RANGE, "0,200,0.5,suffix:m/s²"), "set_homing_acceleration", "get_homing_acceleration");

	ADD_GROUP("Behaviors", "");
	ADD_PROPERTY(PropertyInfo(Variant::INT, "behaviors", PROPERTY_HINT_FLAGS, "Split,Pierce,Fork,Chain,Return,Bounce"), "set_behaviours", "get_behaviours");
	ADD_PROPERTY(PropertyInfo(Variant::INT, "split_count", PROPERTY_HINT_RANGE, "0,32,1"), "set_split_count", "get_split_count");
	ADD_PROPERTY(PropertyInfo(Variant::INT, "pierce_count", PROPERTY_HINT_RANGE, "0,32,1"), "set_pierce_count", "get_pierce_count");
	ADD_PROPERTY(PropertyInfo(Variant::INT, "fork_count", PROPERTY_HINT_RANGE, "0,32,1"), "set_fork_count", "get_fork_count");
	ADD_PROPERTY(PropertyInfo(Variant::INT, "chain_count", PROPERTY_HINT_RANGE, "0,32,1"), "set_chain_count", "get_chain_count");
	ADD_PROPERTY(PropertyInfo(Variant::INT, "return_count", PROPERTY_HINT_RANGE, "0,32,1"), "set_return_count", "get_return_count");
	ADD_PROPERTY(PropertyInfo(Variant::INT, "bounce_count", PROPERTY_HINT_RANGE, "0,32,1"), "set_bounce_count", "get_bounce_count");
	ADD_PROPERTY(PropertyInfo(Variant::FLOAT, "fork_angle", PROPERTY_HINT_RANGE, "0,180,0.5,radians_as_degrees"), "set_fork_angle", "get_fork_angle");
	ADD_PROPERTY(PropertyInfo(Variant::FLOAT, "seek_radius", PROPERTY_HINT_RANGE, "0,50,0.1,suffix:m"), "set_seek_radius", "get_seek_radius");
	ADD_PROPERTY(PropertyInfo(Variant::FLOAT, "bounce_restitution", PROPERTY_HINT_RANGE, "0,1,0.01"), "set_bounce_restitution", "get_bounce_restitution");
	ADD_PROPERTY(PropertyInfo(Variant::FLOAT, "bounce_friction", PROPERTY_HINT_RANGE, "0,1,0.01"), "set_bounce_friction", "get_bounce_friction");

	ADD_GROUP("Collision", "");
	ADD_PROPERTY(PropertyInfo(Variant::INT, "collision_mask", PROPERTY_HINT_LAYERS_3D_PHYSICS), "set_collision_mask", "get_collision_mask");
	ADD_PROPERTY(PropertyInfo(Variant::INT, "seek_mask", PROPERTY_HINT_LAYERS_3D_PHYSICS), "set_seek_mask", "get_seek_mask");
	ADD_PROPERTY(PropertyInfo(Variant::BOOL, "collide_with_areas"), "set_collide_with_areas", "get_collide_with_areas");

	BIND_BITFIELD_FLAG(BEHAVIOUR_NONE);
	BIND_BITFIELD_FLAG(BEHAVIOUR_SPLIT);
	BIND_BITFIELD_FLAG(BEHAVIOUR_PIERCE);
	BIND_BITFIELD_FLAG(BEHAVIOUR_FORK);
	BIND_BITFIELD_FLAG(BEHAVIOUR_CHAIN);
	BIND_BITFIELD_FLAG(BEHAVIOUR_RETURN);
	BIND_BITFIELD_FLAG(BEHAVIOUR_BOUNCE);
}

/**************************************************************************/
/* ProjectilePool - pool                                                */
/**************************************************************************/

ProjectilePool::ProjectilePool() {
	PhysicsServer3D *ps = PhysicsServer3D::get_singleton();
	if (ps) {
		_seek_shape = ps->sphere_shape_create();
	}
	_reset_pools();
}

ProjectilePool::~ProjectilePool() {
	PhysicsServer3D *ps = PhysicsServer3D::get_singleton();
	if (ps && _seek_shape.is_valid()) {
		ps->free_rid(_seek_shape);
	}
}

void ProjectilePool::_reset_pools() {
	_hot.clear();
	_cold.clear();
	_pending.clear();
	_reset_slots.clear();

	_dense_of_slot.resize(_capacity);
	_generation.resize(_capacity);
	_free_slots.clear();
	_free_slots.reserve(_capacity);

	// Handed out from the back, so the first spawn takes slot 0 and a freshly reset
	// pool issues handles in the order a reader would expect while debugging.
	for (int i = _capacity - 1; i >= 0; i--) {
		_dense_of_slot[i] = INVALID_INDEX;
		// Generations start at 1, never 0. That is what makes handle 0 impossible
		// to issue and therefore safe as the "no projectile" value.
		_generation[i] = 1;
		_free_slots.push_back((uint32_t)i);
	}

	_hot.reserve(_capacity);
	_cold.reserve(_capacity);
	_last_visible_instances = 0;
}

int64_t ProjectilePool::_make_handle(uint32_t p_slot) const {
	return (int64_t)(((uint64_t)_generation[p_slot] << 32) | (uint64_t)(p_slot + 1));
}

bool ProjectilePool::_resolve_handle(int64_t p_handle, uint32_t &r_dense) const {
	if (p_handle == 0) {
		return false;
	}
	const uint64_t h = (uint64_t)p_handle;
	const uint32_t slot_plus_one = (uint32_t)(h & 0xFFFFFFFFULL);
	if (slot_plus_one == 0) {
		return false;
	}
	const uint32_t slot = slot_plus_one - 1;
	if (slot >= _dense_of_slot.size()) {
		return false;
	}
	// The generation check is the whole point of a handle. Without it a stale
	// handle addresses whichever projectile later took the slot, and `despawn`
	// on a dead handle quietly kills a live stranger.
	if (_generation[slot] != (uint32_t)(h >> 32)) {
		return false;
	}
	const uint32_t dense = _dense_of_slot[slot];
	if (dense == INVALID_INDEX) {
		return false;
	}
	r_dense = dense;
	return true;
}

uint32_t ProjectilePool::_alloc_dense() {
	if ((int)_hot.size() >= _capacity || _free_slots.is_empty()) {
		return INVALID_INDEX;
	}
	const uint32_t slot = _free_slots[_free_slots.size() - 1];
	_free_slots.resize(_free_slots.size() - 1);

	const uint32_t dense = _hot.size();
	_hot.push_back(Hot());
	_cold.push_back(Cold());
	_cold[dense].slot = slot;
	_dense_of_slot[slot] = dense;
	_reset_slots.push_back(dense);
	return dense;
}

void ProjectilePool::_swap_remove(uint32_t p_dense) {
	const uint32_t last = _hot.size() - 1;
	const uint32_t slot = _cold[p_dense].slot;

	_dense_of_slot[slot] = INVALID_INDEX;
	// Bumping on death rather than on spawn means every handle ever issued for
	// this slot is invalidated the instant the projectile ends, not the next time
	// something happens to reuse it.
	_generation[slot]++;
	_free_slots.push_back(slot);

	if (p_dense != last) {
		_hot[p_dense] = _hot[last];
		_cold[p_dense] = _cold[last];
		_dense_of_slot[_cold[p_dense].slot] = p_dense;
		// A different projectile now occupies this dense index, and therefore this
		// MultiMesh instance. Without the reset it interpolates from where the old
		// occupant died to where the new one is, which draws a streak across the
		// level for exactly one frame.
		_reset_slots.push_back(p_dense);
	}

	_hot.resize(last);
	_cold.resize(last);
}

uint32_t ProjectilePool::_kind_index(const Ref<ProjectileKind> &p_kind) {
	// Linear, because a game has a handful of projectile kinds and a hash would
	// cost more than the scan. The index is stored per projectile so the tick loop
	// reads one small shared record instead of copying twenty fields per projectile.
	for (uint32_t i = 0; i < _kinds.size(); i++) {
		if (_kinds[i] == p_kind) {
			return i;
		}
	}
	_kinds.push_back(p_kind);
	return _kinds.size() - 1;
}

/**************************************************************************/
/* ProjectilePool - public API                                          */
/**************************************************************************/

int64_t ProjectilePool::spawn(const Ref<ProjectileKind> &p_kind, const Vector3 &p_position, const Vector3 &p_velocity) {
	ERR_FAIL_COND_V_MSG(p_kind.is_null(), 0, "ProjectilePool.spawn() needs a ProjectileKind.");

	const uint32_t dense = _alloc_dense();
	if (dense == INVALID_INDEX) {
		// Not an error. A pool running dry is a tuning fact, and a caster that
		// silently fails to add its 257th projectile is better than one that spikes
		// the frame growing an array mid-tick.
		return 0;
	}

	const uint32_t kind_index = _kind_index(p_kind);
	const ProjectileKind &kind = *p_kind.ptr();

	Hot &hot = _hot[dense];
	hot.position = p_position;
	hot.velocity = p_velocity;
	hot.radius = kind.get_radius();
	hot.life_left = kind.get_lifetime();
	hot.travelled = 0.0f;
	hot.kind = kind_index;
	hot.behaviors = (uint32_t)kind.get_behaviours();

	Cold &cold = _cold[dense];
	cold.origin = p_position;
	cold.homing_target = Vector3();
	cold.has_homing_target = false;
	cold.recent_head = 0;
	for (uint32_t i = 0; i < RECENT_TARGETS; i++) {
		cold.recent[i] = 0;
	}
	// Split is once-per-projectile however many children it makes, so its counter
	// is a flag. The rest are counts.
	cold.split_left = (hot.behaviors & ProjectileKind::BEHAVIOUR_SPLIT) ? 1 : 0;
	cold.pierce_left = (uint8_t)kind.get_pierce_count();
	cold.fork_left = (uint8_t)kind.get_fork_count();
	cold.chain_left = (uint8_t)kind.get_chain_count();
	cold.return_left = (uint8_t)kind.get_return_count();
	cold.bounce_left = (uint8_t)kind.get_bounce_count();

	return _make_handle(cold.slot);
}

bool ProjectilePool::is_alive(int64_t p_handle) const {
	uint32_t dense = 0;
	return _resolve_handle(p_handle, dense);
}

void ProjectilePool::despawn(int64_t p_handle) {
	uint32_t dense = 0;
	if (!_resolve_handle(p_handle, dense)) {
		return;
	}
	_ended_handles.push_back(p_handle);
	_swap_remove(dense);
}

void ProjectilePool::clear() {
	_reset_pools();
}

Vector3 ProjectilePool::get_projectile_position(int64_t p_handle) const {
	uint32_t dense = 0;
	ERR_FAIL_COND_V(!_resolve_handle(p_handle, dense), Vector3());
	return _hot[dense].position;
}

Vector3 ProjectilePool::get_projectile_velocity(int64_t p_handle) const {
	uint32_t dense = 0;
	ERR_FAIL_COND_V(!_resolve_handle(p_handle, dense), Vector3());
	return _hot[dense].velocity;
}

void ProjectilePool::set_projectile_velocity(int64_t p_handle, const Vector3 &p_velocity) {
	uint32_t dense = 0;
	ERR_FAIL_COND(!_resolve_handle(p_handle, dense));
	_hot[dense].velocity = p_velocity;
}

void ProjectilePool::set_homing_target(int64_t p_handle, const Vector3 &p_target) {
	uint32_t dense = 0;
	ERR_FAIL_COND(!_resolve_handle(p_handle, dense));
	_cold[dense].homing_target = p_target;
	_cold[dense].has_homing_target = true;
}

void ProjectilePool::clear_homing_target(int64_t p_handle) {
	uint32_t dense = 0;
	ERR_FAIL_COND(!_resolve_handle(p_handle, dense));
	_cold[dense].has_homing_target = false;
}

PackedVector3Array ProjectilePool::get_positions() const {
	PackedVector3Array out;
	out.resize(_hot.size());
	Vector3 *w = out.ptrw();
	for (uint32_t i = 0; i < _hot.size(); i++) {
		w[i] = _hot[i].position;
	}
	return out;
}

void ProjectilePool::set_capacity(int p_capacity) {
	const int wanted = CLAMP(p_capacity, 1, 65535);
	if (wanted == _capacity) {
		return;
	}
	_capacity = wanted;
	// Everything in flight is dropped. Resizing a pool while it holds live
	// projectiles would have to remap every outstanding handle, and the only time
	// this is called is at set-up.
	_reset_pools();
}

void ProjectilePool::set_max_substeps(int p_substeps) {
	_max_substeps = CLAMP(p_substeps, 1, 64);
}

void ProjectilePool::set_gravity(const Vector3 &p_gravity) {
	_gravity = p_gravity;
}

void ProjectilePool::set_multimesh(const Ref<MultiMesh> &p_multimesh) {
	_multimesh = p_multimesh;
	_last_visible_instances = 0;
	if (_multimesh.is_valid()) {
		// The renderer has to know these interpolate, or the per-instance reset
		// below has nothing to reset and every recycled slot pops.
		RenderingServer::get_singleton()->multimesh_set_physics_interpolated(_multimesh->get_rid(), true);
	}
}

/**************************************************************************/
/* ProjectilePool - simulation                                          */
/**************************************************************************/

bool ProjectilePool::_remembers(const Cold &p_cold, uint64_t p_id) {
	for (uint32_t i = 0; i < RECENT_TARGETS; i++) {
		if (p_cold.recent[i] == p_id) {
			return true;
		}
	}
	return false;
}

void ProjectilePool::_remember(Cold &p_cold, uint64_t p_id) {
	p_cold.recent[p_cold.recent_head] = p_id;
	p_cold.recent_head = (uint8_t)((p_cold.recent_head + 1) % RECENT_TARGETS);
}

int ProjectilePool::_seek_targets(const Vector3 &p_from, const Cold &p_cold, const ProjectileKind &p_kind,
		PhysicsDirectSpaceState3D *p_space, Vector3 *r_positions, uint64_t *r_ids, int p_max) {
	if (!_seek_shape.is_valid() || p_max <= 0 || p_kind.get_seek_radius() <= 0.0f) {
		return 0;
	}
	PhysicsServer3D::get_singleton()->shape_set_data(_seek_shape, p_kind.get_seek_radius());

	PhysicsDirectSpaceState3D::ShapeParameters params;
	params.shape_rid = _seek_shape;
	params.transform = Transform3D(Basis(), p_from);
	params.collision_mask = p_kind.get_seek_mask();
	params.collide_with_bodies = true;
	params.collide_with_areas = p_kind.get_collide_with_areas();

	PhysicsDirectSpaceState3D::ShapeResult results[MAX_SEEK_RESULTS];
	const int found = p_space->intersect_shape(params, results, MAX_SEEK_RESULTS);

	// Insertion sort into a nearest-first list. Not a heap: p_max is one or two in
	// every real case and MAX_SEEK_RESULTS is 16, so a heap would be more code and
	// more time both.
	float best[MAX_SEEK_RESULTS];
	int count = 0;

	for (int i = 0; i < found; i++) {
		const uint64_t id = (uint64_t)results[i].collider_id;
		if (id == 0 || _remembers(p_cold, id)) {
			continue;
		}
		Node3D *node = Object::cast_to<Node3D>(results[i].collider);
		if (!node) {
			continue;
		}
		const Vector3 pos = node->get_global_position();
		const float d2 = (float)pos.distance_squared_to(p_from);

		int at = MIN(count, p_max - 1);
		while (at > 0 && best[at - 1] > d2) {
			at--;
		}
		if (at >= p_max || (count == p_max && d2 >= best[p_max - 1])) {
			continue;
		}
		for (int k = MIN(count, p_max - 1); k > at; k--) {
			best[k] = best[k - 1];
			r_positions[k] = r_positions[k - 1];
			r_ids[k] = r_ids[k - 1];
		}
		best[at] = d2;
		r_positions[at] = pos;
		r_ids[at] = id;
		count = MIN(count + 1, p_max);
	}
	return count;
}

void ProjectilePool::_simulate(uint32_t p_dense, double p_delta, PhysicsDirectSpaceState3D *p_space) {
	const ProjectileKind &kind = *_kinds[_hot[p_dense].kind].ptr();

	// Built once and mutated in place across iterations. Assigning a fresh
	// RayParameters each time would copy its exclude set, which allocates; this way
	// the loop allocates nothing unless a pierce actually happens.
	PhysicsDirectSpaceState3D::RayParameters params;
	params.collision_mask = kind.get_collision_mask();
	params.collide_with_bodies = true;
	params.collide_with_areas = kind.get_collide_with_areas();
	params.hit_from_inside = false;

	// Unreal's remaining-time loop, and for Unreal's reason: a projectile that hits
	// something a third of the way through the tick has two thirds of a tick left
	// to spend, and a pierce that does not spend it stops dead inside the thing it
	// just pierced until the next tick.
	//
	// Iteration count is therefore driven by *collisions*, not by speed. This is a
	// correction to the obvious design and worth stating plainly: a ray covers its
	// whole segment, so a long step cannot tunnel however fast the projectile is
	// going. Slicing a straight flight into eight ray queries buys nothing at all.
	float remaining = (float)p_delta;
	int iterations = 0;

	while (remaining > MIN_STEP_TIME && iterations < _max_substeps) {
		iterations++;
		Hot &hot = _hot[p_dense];

		Vector3 accel = _gravity * kind.get_gravity_scale();
		if (kind.get_drag() > 0.0f) {
			// Quadratic, as air resistance is. A linear term would slow the light
			// projectiles as hard as the heavy ones; the square is what lets a
			// heavy one read as heavy without making a dart feel like syrup.
			accel -= hot.velocity * (kind.get_drag() * hot.velocity.length());
		}
		if (_cold[p_dense].has_homing_target && kind.get_homing_acceleration() > 0.0f) {
			const Vector3 to_target = _cold[p_dense].homing_target - hot.position;
			if (!to_target.is_zero_approx()) {
				accel += to_target.normalized() * kind.get_homing_acceleration();
			}
		}

		// What a long step *can* get wrong is cutting the corner of a curved path:
		// the ray is a chord, and the true arc bulges away from it by about a*h*h/8.
		// Holding that under the projectile's own radius means the chord is never
		// wrong by more than the thing is wide. At 90 Hz under gravity alone this
		// never binds - which is the point, it costs nothing until a hard-homing
		// projectile makes it matter.
		float h = remaining;
		const float accel_len = accel.length();
		if (accel_len > 0.0f) {
			h = MIN(h, Math::sqrt(8.0f * hot.radius / accel_len));
		}

		const Vector3 v0 = hot.velocity;
		const Vector3 v1 = v0 + accel * h;
		// Velocity Verlet, straight out of Unreal's ComputeMoveDelta:
		//     p = v0*t + 0.5*(v1 - v0)*t
		// Explicit Euler would sag an arcing projectile by half a gravity step every
		// tick - imperceptible per tick at 90 Hz and visibly wrong by 15 m.
		const Vector3 delta = v0 * h + (v1 - v0) * (0.5f * h);
		const float step_len = delta.length();

		if (step_len <= (float)CMP_EPSILON) {
			hot.velocity = v1;
			remaining -= h;
			continue;
		}
		const Vector3 dir = delta / step_len;

		params.from = hot.position;
		params.to = hot.position + delta;

		PhysicsDirectSpaceState3D::RayResult hit;
		if (!p_space->intersect_ray(params, hit)) {
			hot.position += delta;
			hot.velocity = v1;
			hot.travelled += step_len;
			remaining -= h;
			if (hot.travelled >= kind.get_max_distance()) {
				return;
			}
			continue;
		}

		// Only the part of the step actually flown is spent. Charging the whole
		// step would let a projectile that clips something in its first centimeter
		// lose the rest of the tick.
		const float hit_len = (float)hot.position.distance_to(hit.position);
		const float consumed = h * (hit_len / step_len);

		hot.travelled += hit_len;
		hot.position = hit.position;
		// Velocity at the impact, not at the end of a step it never finished.
		hot.velocity = v0 + accel * consumed;

		if (!_resolve_hit(p_dense, hit, dir, params.exclude, p_space)) {
			// Consumed. Marked rather than removed, because removing mid-walk is
			// the tick loop's job and doing it here would invalidate p_dense.
			_hot[p_dense].life_left = -1.0f;
			return;
		}

		remaining -= consumed;
		if (_hot[p_dense].travelled >= kind.get_max_distance()) {
			return;
		}
	}
}

bool ProjectilePool::_resolve_hit(uint32_t p_dense, const PhysicsDirectSpaceState3D::RayResult &p_hit,
		const Vector3 &p_direction, HashSet<RID> &r_exclude, PhysicsDirectSpaceState3D *p_space) {
	Hot &hot = _hot[p_dense];
	Cold &cold = _cold[p_dense];
	const ProjectileKind &kind = *_kinds[hot.kind].ptr();
	const uint64_t target_id = (uint64_t)p_hit.collider_id;

	// Reported whatever happens next. What damage a hit does, and whether it does
	// any, is the game's business - this module only ever says where, what, and
	// which projectile.
	_hit_handles.push_back(_make_handle(cold.slot));
	_hit_positions.push_back(p_hit.position);
	_hit_normals.push_back(p_hit.normal);
	_hit_colliders.push_back((int64_t)target_id);
	_remember(cold, target_id);

	const uint32_t b = hot.behaviors;
	const float speed = hot.velocity.length();

	// Path of Exile's ladder, in Path of Exile's order: Split, Pierce, Fork, Chain,
	// Return, and then Bounce, which is ours. One behavior per collision - the
	// first the projectile still has a charge for wins, and a behavior that
	// cannot be performed falls through to the next rather than ending the
	// projectile.

	if ((b & ProjectileKind::BEHAVIOUR_SPLIT) && cold.split_left > 0) {
		Vector3 positions[MAX_SEEK_RESULTS];
		uint64_t ids[MAX_SEEK_RESULTS];
		const int wanted = MIN(kind.get_split_count(), MAX_SEEK_RESULTS);
		const int found = _seek_targets(p_hit.position, cold, kind, p_space, positions, ids, wanted);

		for (int i = 0; i < wanted; i++) {
			Vector3 aim;
			if (i < found) {
				const Vector3 to = positions[i] - p_hit.position;
				aim = to.is_zero_approx() ? p_direction : to.normalized();
			} else {
				// Nothing left to auto-target: fan the remainder evenly about the
				// incoming direction so a split into an empty room still looks
				// like a split rather than vanishing.
				const float t = (wanted > 1) ? ((float)i / (float)(wanted - 1)) - 0.5f : 0.0f;
				aim = p_direction.rotated(Vector3(0, 1, 0), t * kind.get_fork_angle() * 2.0f);
			}
			PendingSpawn ps;
			ps.position = p_hit.position + aim * (hot.radius + SURFACE_MARGIN);
			ps.velocity = aim * speed;
			ps.origin = p_hit.position;
			ps.kind = hot.kind;
			// Split is once per projectile however far down the tree you go, so the
			// children never carry the bit.
			ps.behaviors = b & ~(uint32_t)ProjectileKind::BEHAVIOUR_SPLIT;
			ps.life_left = hot.life_left;
			// Range resets. Carrying the parent's travelled distance would kill a
			// split fired at the far edge of its range the instant it appeared.
			ps.travelled = 0.0f;
			ps.radius = hot.radius;
			ps.recent_head = cold.recent_head;
			for (uint32_t k = 0; k < RECENT_TARGETS; k++) {
				ps.recent[k] = cold.recent[k];
			}
			ps.split_left = 0;
			ps.pierce_left = cold.pierce_left;
			ps.fork_left = cold.fork_left;
			ps.chain_left = cold.chain_left;
			ps.return_left = cold.return_left;
			ps.bounce_left = cold.bounce_left;
			_pending.push_back(ps);
		}
		return false; // The parent is converted into its children, not kept.
	}

	if ((b & ProjectileKind::BEHAVIOUR_PIERCE) && cold.pierce_left > 0) {
		cold.pierce_left--;
		// Excluded for the rest of this tick. Stepping past the face is not enough
		// on its own: a pierce that resumes inside a wall thicker than one substep
		// hits that wall's exit face and burns a second charge on one object.
		r_exclude.insert(p_hit.rid);
		hot.position = p_hit.position + p_direction * (hot.radius + SURFACE_MARGIN);
		return true;
	}

	if ((b & ProjectileKind::BEHAVIOUR_FORK) && cold.fork_left > 0) {
		// PoE forks in the horizontal plane. World up is the 3D reading of that,
		// with the surface normal as the fallback for a projectile travelling straight up
		// or down, where "horizontal" has no single answer.
		Vector3 axis = Vector3(0, 1, 0);
		if (Math::abs(p_direction.dot(axis)) > 0.99f) {
			axis = p_hit.normal.cross(p_direction).normalized();
			if (axis.is_zero_approx()) {
				axis = Vector3(1, 0, 0);
			}
		}
		for (int i = 0; i < 2; i++) {
			const float angle = (i == 0 ? 1.0f : -1.0f) * kind.get_fork_angle();
			const Vector3 aim = p_direction.rotated(axis, angle).normalized();
			PendingSpawn ps;
			ps.position = p_hit.position + aim * (hot.radius + SURFACE_MARGIN);
			ps.velocity = aim * speed;
			ps.origin = p_hit.position;
			ps.kind = hot.kind;
			ps.behaviors = b;
			ps.life_left = hot.life_left;
			ps.travelled = 0.0f;
			ps.radius = hot.radius;
			ps.recent_head = cold.recent_head;
			for (uint32_t k = 0; k < RECENT_TARGETS; k++) {
				ps.recent[k] = cold.recent[k];
			}
			ps.split_left = 0;
			ps.pierce_left = cold.pierce_left;
			ps.fork_left = (uint8_t)(cold.fork_left - 1);
			ps.chain_left = cold.chain_left;
			ps.return_left = cold.return_left;
			ps.bounce_left = cold.bounce_left;
			_pending.push_back(ps);
		}
		return false; // Replaced by the two halves, as PoE does it.
	}

	if ((b & ProjectileKind::BEHAVIOUR_CHAIN) && cold.chain_left > 0) {
		Vector3 positions[1];
		uint64_t ids[1];
		const int found = _seek_targets(p_hit.position, cold, kind, p_space, positions, ids, 1);
		if (found > 0) {
			const Vector3 to = positions[0] - p_hit.position;
			if (!to.is_zero_approx()) {
				cold.chain_left--;
				const Vector3 aim = to.normalized();
				hot.velocity = aim * speed;
				hot.position = p_hit.position + aim * (hot.radius + SURFACE_MARGIN);
				// Excluded for the rest of the tick, for a reason that is not
				// obvious until it bites: a chain turns *sideways* off a target, so
				// stepping forward by one radius leaves the projectile still inside
				// the body it just left. Without this it re-collides with its own
				// source on the next iteration, finds no chains left, and dies on
				// the spot instead of reaching the target it had just picked.
				r_exclude.insert(p_hit.rid);
				return true;
			}
		}
		// Nothing in range. Falls through to Return rather than ending here - PoE 2
		// does the same, and it is what makes a chain-and-return projectile come home
		// instead of dying in an empty room.
	}

	if ((b & ProjectileKind::BEHAVIOUR_RETURN) && cold.return_left > 0) {
		cold.return_left--;
		Vector3 aim = cold.origin - p_hit.position;
		aim = aim.is_zero_approx() ? -p_direction : aim.normalized();
		hot.velocity = aim * speed;
		hot.position = p_hit.position + aim * (hot.radius + SURFACE_MARGIN);
		r_exclude.insert(p_hit.rid);
		// A returning projectile gets one more go at everything it already hit,
		// which is PoE's rule and the reason a returning projectile is worth firing.
		for (uint32_t k = 0; k < RECENT_TARGETS; k++) {
			cold.recent[k] = 0;
		}
		cold.recent_head = 0;
		hot.travelled = 0.0f;
		return true;
	}

	if ((b & ProjectileKind::BEHAVIOUR_BOUNCE) && cold.bounce_left > 0) {
		cold.bounce_left--;
		// Unreal's ComputeBounceResult: restitution on the normal component,
		// friction on the tangential one. Splitting them is what makes a shallow
		// graze skid along a wall while a square hit comes back.
		const Vector3 vn = p_hit.normal * hot.velocity.dot(p_hit.normal);
		const Vector3 vt = hot.velocity - vn;
		hot.velocity = vt * (1.0f - kind.get_bounce_friction()) - vn * kind.get_bounce_restitution();

		if (hot.velocity.length() < BOUNCE_STOP_SPEED) {
			return false;
		}
		hot.position = p_hit.position + p_hit.normal * (hot.radius + SURFACE_MARGIN);
		return true;
	}

	return false;
}

void ProjectilePool::_flush_pending() {
	for (uint32_t i = 0; i < _pending.size(); i++) {
		const PendingSpawn &ps = _pending[i];
		const uint32_t dense = _alloc_dense();
		if (dense == INVALID_INDEX) {
			// Pool full. The remaining children are dropped rather than queued: a
			// fork that arrives a tick late is worse than one that never was.
			break;
		}
		Hot &hot = _hot[dense];
		hot.position = ps.position;
		hot.velocity = ps.velocity;
		hot.radius = ps.radius;
		hot.life_left = ps.life_left;
		hot.travelled = ps.travelled;
		hot.kind = ps.kind;
		hot.behaviors = ps.behaviors;

		Cold &cold = _cold[dense];
		const uint32_t slot = cold.slot; // Set by _alloc_dense; must survive.
		cold.origin = ps.origin;
		cold.homing_target = Vector3();
		cold.has_homing_target = false;
		cold.slot = slot;
		cold.recent_head = ps.recent_head;
		for (uint32_t k = 0; k < RECENT_TARGETS; k++) {
			cold.recent[k] = ps.recent[k];
		}
		cold.split_left = ps.split_left;
		cold.pierce_left = ps.pierce_left;
		cold.fork_left = ps.fork_left;
		cold.chain_left = ps.chain_left;
		cold.return_left = ps.return_left;
		cold.bounce_left = ps.bounce_left;
	}
	_pending.clear();
}

void ProjectilePool::_tick(double p_delta) {
	Ref<World3D> world = get_world_3d();
	if (world.is_null()) {
		return;
	}
	PhysicsDirectSpaceState3D *space = world->get_direct_space_state();
	if (!space) {
		return;
	}

	_hit_handles.clear();
	_hit_positions.clear();
	_hit_normals.clear();
	_hit_colliders.clear();
	_pending.clear();
	// `_ended_handles` and `_reset_slots` are deliberately *not* cleared here.
	// Both are also written by `spawn` and `despawn`, which GDScript calls between
	// ticks; clearing at the top of the tick would drop a spawn's interpolation
	// reset and swallow an explicit despawn before anyone was told about it. They
	// are cleared once consumed, at the bottom.

	uint32_t i = 0;
	while (i < _hot.size()) {
		_hot[i].life_left -= (float)p_delta;
		if (_hot[i].life_left <= 0.0f) {
			_ended_handles.push_back(_make_handle(_cold[i].slot));
			_swap_remove(i);
			continue; // A swap-remove moved a new occupant into i; do not advance.
		}

		_simulate(i, p_delta, space);

		const ProjectileKind &kind = *_kinds[_hot[i].kind].ptr();
		if (_hot[i].life_left <= 0.0f || _hot[i].travelled >= kind.get_max_distance()) {
			_ended_handles.push_back(_make_handle(_cold[i].slot));
			_swap_remove(i);
			continue;
		}
		i++;
	}

	_flush_pending();

	// One signal per tick carrying packed arrays, not one signal per hit carrying a
	// Dictionary. At a few hundred projectiles the per-call marshaling into
	// GDScript costs more than the entire simulation above it.
	if (!_hit_handles.is_empty()) {
		emit_signal(SNAME("projectiles_hit"), _hit_handles, _hit_positions, _hit_normals, _hit_colliders);
	}
	if (!_ended_handles.is_empty()) {
		emit_signal(SNAME("projectiles_ended"), _ended_handles);
		_ended_handles.clear();
	}

	_write_multimesh();
	_reset_slots.clear();
}

void ProjectilePool::_write_multimesh() {
	if (_multimesh.is_null()) {
		return;
	}
	const uint32_t instances = (uint32_t)MAX(_multimesh->get_instance_count(), 0);
	const uint32_t count = MIN(instances, _hot.size());

	// Written in this node's local space, so the MultiMeshInstance3D drawing them
	// is expected to be a child of this node at identity. World space would work
	// too right up until somebody moves either node.
	const Transform3D to_local = get_global_transform().affine_inverse();

	for (uint32_t i = 0; i < count; i++) {
		Transform3D xf;
		xf.basis.scale(Vector3(_hot[i].radius, _hot[i].radius, _hot[i].radius));
		xf.origin = to_local.xform(_hot[i].position);
		_multimesh->set_instance_transform((int)i, xf);
	}

	// Only the instances whose occupant actually changed. Resetting every instance
	// every tick would defeat interpolation entirely and make all of them stutter;
	// resetting none leaves recycled slots drawing a one-frame streak from wherever
	// the previous occupant died. `_reset_slots` is exactly the difference.
	const RID rid = _multimesh->get_rid();
	RenderingServer *rs = RenderingServer::get_singleton();
	for (uint32_t k = 0; k < _reset_slots.size(); k++) {
		const uint32_t idx = _reset_slots[k];
		if (idx < count) {
			rs->multimesh_instance_reset_physics_interpolation(rid, (int)idx);
		}
	}

	if (count != _last_visible_instances) {
		_multimesh->set_visible_instance_count((int)count);
		_last_visible_instances = count;
	}
}

/**************************************************************************/
/* ProjectilePool - bindings                                            */
/**************************************************************************/

void ProjectilePool::_notification(int p_what) {
	switch (p_what) {
		case NOTIFICATION_ENTER_TREE: {
			set_physics_process_internal(true);
		} break;

		case NOTIFICATION_EXIT_TREE: {
			set_physics_process_internal(false);
			_reset_pools();
		} break;

		case NOTIFICATION_INTERNAL_PHYSICS_PROCESS: {
			// Nothing simulates in the editor. A pool that ticks at design time
			// fires signals into scripts that are not running and leaves the
			// MultiMesh holding positions from an editor session.
			if (Engine::get_singleton()->is_editor_hint()) {
				break;
			}
			_tick(get_physics_process_delta_time());
		} break;
	}
}

void ProjectilePool::_bind_methods() {
	ClassDB::bind_method(D_METHOD("spawn", "kind", "position", "velocity"), &ProjectilePool::spawn);
	ClassDB::bind_method(D_METHOD("is_alive", "handle"), &ProjectilePool::is_alive);
	ClassDB::bind_method(D_METHOD("despawn", "handle"), &ProjectilePool::despawn);
	ClassDB::bind_method(D_METHOD("clear"), &ProjectilePool::clear);

	ClassDB::bind_method(D_METHOD("get_projectile_position", "handle"), &ProjectilePool::get_projectile_position);
	ClassDB::bind_method(D_METHOD("get_projectile_velocity", "handle"), &ProjectilePool::get_projectile_velocity);
	ClassDB::bind_method(D_METHOD("set_projectile_velocity", "handle", "velocity"), &ProjectilePool::set_projectile_velocity);
	ClassDB::bind_method(D_METHOD("set_homing_target", "handle", "target"), &ProjectilePool::set_homing_target);
	ClassDB::bind_method(D_METHOD("clear_homing_target", "handle"), &ProjectilePool::clear_homing_target);

	ClassDB::bind_method(D_METHOD("get_live_count"), &ProjectilePool::get_live_count);
	ClassDB::bind_method(D_METHOD("get_positions"), &ProjectilePool::get_positions);

	ClassDB::bind_method(D_METHOD("set_capacity", "capacity"), &ProjectilePool::set_capacity);
	ClassDB::bind_method(D_METHOD("get_capacity"), &ProjectilePool::get_capacity);
	ClassDB::bind_method(D_METHOD("set_max_substeps", "substeps"), &ProjectilePool::set_max_substeps);
	ClassDB::bind_method(D_METHOD("get_max_substeps"), &ProjectilePool::get_max_substeps);
	ClassDB::bind_method(D_METHOD("set_gravity", "gravity"), &ProjectilePool::set_gravity);
	ClassDB::bind_method(D_METHOD("get_gravity"), &ProjectilePool::get_gravity);
	ClassDB::bind_method(D_METHOD("set_multimesh", "multimesh"), &ProjectilePool::set_multimesh);
	ClassDB::bind_method(D_METHOD("get_multimesh"), &ProjectilePool::get_multimesh);

	ADD_PROPERTY(PropertyInfo(Variant::INT, "capacity", PROPERTY_HINT_RANGE, "1,4096,1"), "set_capacity", "get_capacity");
	ADD_PROPERTY(PropertyInfo(Variant::INT, "max_substeps", PROPERTY_HINT_RANGE, "1,64,1"), "set_max_substeps", "get_max_substeps");
	ADD_PROPERTY(PropertyInfo(Variant::VECTOR3, "gravity", PROPERTY_HINT_NONE, "suffix:m/s²"), "set_gravity", "get_gravity");
	ADD_PROPERTY(PropertyInfo(Variant::OBJECT, "multimesh", PROPERTY_HINT_RESOURCE_TYPE, "MultiMesh"), "set_multimesh", "get_multimesh");

	ADD_SIGNAL(MethodInfo("projectiles_hit",
			PropertyInfo(Variant::PACKED_INT64_ARRAY, "handles"),
			PropertyInfo(Variant::PACKED_VECTOR3_ARRAY, "positions"),
			PropertyInfo(Variant::PACKED_VECTOR3_ARRAY, "normals"),
			PropertyInfo(Variant::PACKED_INT64_ARRAY, "collider_ids")));
	ADD_SIGNAL(MethodInfo("projectiles_ended",
			PropertyInfo(Variant::PACKED_INT64_ARRAY, "handles")));
}
