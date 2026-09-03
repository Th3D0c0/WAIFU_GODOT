/**************************************************************************/
/*  projectiles.h                                                         */
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

#include "core/io/resource.h"
#include "core/templates/hash_set.h"
#include "core/templates/local_vector.h"
#include "scene/3d/node_3d.h"
#include "scene/resources/multimesh.h"
#include "servers/physics_3d/physics_server_3d.h"

// A pooled projectile simulator: one node owns every projectile in the scene, and
// none of them is a node.
//
// The shape of this module is settled by three constraints the game already lives
// with, and each one rules out the obvious alternative:
//
//   - 90 Hz stereo, ~11.1 ms for both eyes. A node per projectile costs a Node3D
//     transform propagation, a physics body, and - the part that actually hurts -
//     a script `_physics_process` call each. A few hundred of those is the budget.
//   - Tile-based mobile GPUs on both target headsets. Whatever draws these has to
//     be one draw call, so the simulation must be able to hand out a packed buffer
//     rather than a tree of nodes.
//   - `physics/common/physics_interpolation=true`. A pooled renderer recycles
//     instance slots, and a recycled slot interpolates from wherever its previous
//     occupant died unless it is explicitly reset - see `_write_multimesh`.
//
// The behavior model is Path of Exile's, which is the only published projectile
// spec that resolves on-hit behaviors in a defined *order* rather than as a bag of
// independent flags: on each collision a projectile takes the first behavior it
// still has charges for, and only that one. See `_resolve_hit`.
//
// The integration is Unreal's `UProjectileMovementComponent`, and only the
// integration. Its structure is the thing to avoid - a ticking component per
// projectile and a swept move per component - but its Velocity Verlet step and its
// `ComputeBounceResult` are correct and worth carrying over verbatim.

class ProjectileKind : public Resource {
	GDCLASS(ProjectileKind, Resource);

public:
	// Checked in this order on every collision. The order is the design: a
	// projectile that can both pierce and chain should spend its pierces walking
	// through the front rank before it starts hopping between targets, because the
	// reverse reads as the projectile losing interest in what it just hit.
	enum Behavior {
		BEHAVIOUR_NONE = 0,
		BEHAVIOUR_SPLIT = 1 << 0,
		BEHAVIOUR_PIERCE = 1 << 1,
		BEHAVIOUR_FORK = 1 << 2,
		BEHAVIOUR_CHAIN = 1 << 3,
		BEHAVIOUR_RETURN = 1 << 4,
		// Not one of PoE's. Plenty of projectiles want to skip off a wall, and
		// bounce is the only behavior here that reflects off *geometry* rather
		// than off a target, so it sits below the whole target-seeking ladder: a
		// projectile with chains left should chain off an enemy, and bounce only
		// once there is nothing left to chain to.
		BEHAVIOUR_BOUNCE = 1 << 5,
	};

private:
	float radius = 0.15f;
	float lifetime = 5.0f;
	float max_distance = 15.0f;
	float gravity_scale = 0.0f;
	float drag = 0.0f;
	float homing_acceleration = 0.0f;

	BitField<Behavior> behaviors = BEHAVIOUR_NONE;
	int split_count = 3;
	int pierce_count = 0;
	int fork_count = 1;
	int chain_count = 0;
	int return_count = 1;
	int bounce_count = 0;

	float fork_angle = 1.0471976f; // 60 degrees, as PoE forks.
	float seek_radius = 6.0f;
	float bounce_restitution = 0.5f;
	float bounce_friction = 0.2f;

	uint32_t collision_mask = 0xFFFFFFFF;
	uint32_t seek_mask = 0xFFFFFFFF;
	bool collide_with_areas = false;

protected:
	static void _bind_methods();

public:
	void set_radius(float p_radius);
	float get_radius() const { return radius; }

	void set_lifetime(float p_lifetime);
	float get_lifetime() const { return lifetime; }

	void set_max_distance(float p_distance);
	float get_max_distance() const { return max_distance; }

	void set_gravity_scale(float p_scale);
	float get_gravity_scale() const { return gravity_scale; }

	void set_drag(float p_drag);
	float get_drag() const { return drag; }

	void set_homing_acceleration(float p_accel);
	float get_homing_acceleration() const { return homing_acceleration; }

	void set_behaviours(BitField<Behavior> p_behaviours);
	BitField<Behavior> get_behaviours() const { return behaviors; }

	void set_split_count(int p_count);
	int get_split_count() const { return split_count; }

	void set_pierce_count(int p_count);
	int get_pierce_count() const { return pierce_count; }

	void set_fork_count(int p_count);
	int get_fork_count() const { return fork_count; }

	void set_chain_count(int p_count);
	int get_chain_count() const { return chain_count; }

	void set_return_count(int p_count);
	int get_return_count() const { return return_count; }

	void set_bounce_count(int p_count);
	int get_bounce_count() const { return bounce_count; }

	void set_fork_angle(float p_radians);
	float get_fork_angle() const { return fork_angle; }

	void set_seek_radius(float p_radius);
	float get_seek_radius() const { return seek_radius; }

	void set_bounce_restitution(float p_restitution);
	float get_bounce_restitution() const { return bounce_restitution; }

	void set_bounce_friction(float p_friction);
	float get_bounce_friction() const { return bounce_friction; }

	void set_collision_mask(uint32_t p_mask);
	uint32_t get_collision_mask() const { return collision_mask; }

	void set_seek_mask(uint32_t p_mask);
	uint32_t get_seek_mask() const { return seek_mask; }

	void set_collide_with_areas(bool p_enabled);
	bool get_collide_with_areas() const { return collide_with_areas; }
};

VARIANT_BITFIELD_CAST(ProjectileKind::Behavior);

class ProjectilePool : public Node3D {
	GDCLASS(ProjectilePool, Node3D);

public:
	// How many recently-hit targets a projectile remembers, so a chain cannot hop
	// straight back to where it came from. PoE tracks the whole sequence; a fixed
	// ring is the divergence, and four is chosen because a chain long enough to
	// wrap round to its fifth-oldest target has travelled far enough that hitting
	// it again reads as a new engagement rather than as a bug.
	static constexpr uint32_t RECENT_TARGETS = 4;
	static constexpr uint32_t INVALID_INDEX = 0xFFFFFFFF;
	// Ceiling on one chain/split query. A projectile that finds more candidates than
	// this in one sphere is in a crowd, and the nearest few are the only ones any
	// behavior can use anyway.
	static constexpr int MAX_SEEK_RESULTS = 16;

private:
	// Two dense arrays rather than one per field. Pure struct-of-arrays buys
	// nothing below a few thousand elements and costs a great deal of legibility;
	// what does buy something is keeping the fields the substep loop touches every
	// iteration away from the fields only a collision touches, so a tick in which
	// nothing hits anything never pulls the cold half into cache at all.
	struct Hot {
		Vector3 position;
		Vector3 velocity;
		float radius = 0.15f;
		float life_left = 0.0f;
		float travelled = 0.0f;
		uint32_t kind = 0;
		uint32_t behaviors = 0;
	};

	struct Cold {
		Vector3 origin;
		Vector3 homing_target;
		uint64_t recent[RECENT_TARGETS] = {};
		uint32_t slot = INVALID_INDEX;
		uint8_t recent_head = 0;
		uint8_t split_left = 0;
		uint8_t pierce_left = 0;
		uint8_t fork_left = 0;
		uint8_t chain_left = 0;
		uint8_t return_left = 0;
		uint8_t bounce_left = 0;
		bool has_homing_target = false;
	};

	// A spawn requested from inside the tick loop - a fork, a split. Deferred,
	// because appending to `_hot` while walking it invalidates the walk, and
	// because a projectile that forks on its first substep would otherwise get a
	// partial tick of its own inside the tick that created it.
	struct PendingSpawn {
		Vector3 position;
		Vector3 velocity;
		Vector3 origin;
		uint64_t recent[RECENT_TARGETS] = {};
		uint32_t kind = 0;
		uint32_t behaviors = 0;
		float life_left = 0.0f;
		float travelled = 0.0f;
		float radius = 0.15f;
		uint8_t recent_head = 0;
		uint8_t split_left = 0;
		uint8_t pierce_left = 0;
		uint8_t fork_left = 0;
		uint8_t chain_left = 0;
		uint8_t return_left = 0;
		uint8_t bounce_left = 0;
	};

	LocalVector<Hot> _hot;
	LocalVector<Cold> _cold;
	LocalVector<PendingSpawn> _pending;

	// Handles are (generation << 32) | (slot + 1). The generation is what stops a
	// stale handle held in GDScript from addressing whatever projectile later took the
	// slot - without it, `despawn(h)` on a dead handle silently kills a live
	// stranger. The +1 keeps 0 free as the "no projectile" handle.
	LocalVector<uint32_t> _dense_of_slot;
	LocalVector<uint32_t> _generation;
	LocalVector<uint32_t> _free_slots;

	LocalVector<Ref<ProjectileKind>> _kinds;

	int _capacity = 256;
	int _max_substeps = 8;
	Vector3 _gravity = Vector3(0.0f, -9.8f, 0.0f);
	Ref<MultiMesh> _multimesh;

	// Reused across ticks so the hot loop allocates nothing. Cleared, never freed.
	PackedInt64Array _hit_handles;
	PackedVector3Array _hit_positions;
	PackedVector3Array _hit_normals;
	PackedInt64Array _hit_colliders;
	PackedInt64Array _ended_handles;

	// Dense indices whose occupant changed this tick - a slot that was filled, or
	// one that a swap-remove moved a different projectile into. These are exactly
	// the MultiMesh instances that would otherwise interpolate from the position of
	// whatever used to live there, drawing a streak across the level. See
	// `_write_multimesh`.
	LocalVector<uint32_t> _reset_slots;

	// One sphere, resized per query. Owned here because a query shape has to
	// outlive the call, and creating one per chain would allocate on the physics
	// server every time a projectile looked for a target.
	RID _seek_shape;

	uint32_t _last_visible_instances = 0;

	void _reset_pools();
	uint32_t _kind_index(const Ref<ProjectileKind> &p_kind);
	int64_t _make_handle(uint32_t p_slot) const;
	bool _resolve_handle(int64_t p_handle, uint32_t &r_dense) const;

	uint32_t _alloc_dense();
	void _swap_remove(uint32_t p_dense);

	void _tick(double p_delta);
	void _simulate(uint32_t p_dense, double p_delta, PhysicsDirectSpaceState3D *p_space);
	bool _resolve_hit(uint32_t p_dense, const PhysicsDirectSpaceState3D::RayResult &p_hit,
			const Vector3 &p_direction, HashSet<RID> &r_exclude, PhysicsDirectSpaceState3D *p_space);
	void _flush_pending();
	void _write_multimesh();

	static bool _remembers(const Cold &p_cold, uint64_t p_id);
	static void _remember(Cold &p_cold, uint64_t p_id);
	int _seek_targets(const Vector3 &p_from, const Cold &p_cold, const ProjectileKind &p_kind,
			PhysicsDirectSpaceState3D *p_space, Vector3 *r_positions, uint64_t *r_ids, int p_max);

protected:
	void _notification(int p_what);
	static void _bind_methods();

public:
	// Spawns one projectile and returns its handle, or 0 if the pool is full.
	// Zero rather than -1 so `if handle:` is the natural GDScript check.
	int64_t spawn(const Ref<ProjectileKind> &p_kind, const Vector3 &p_position, const Vector3 &p_velocity);

	bool is_alive(int64_t p_handle) const;
	void despawn(int64_t p_handle);
	void clear();

	Vector3 get_projectile_position(int64_t p_handle) const;
	Vector3 get_projectile_velocity(int64_t p_handle) const;
	void set_projectile_velocity(int64_t p_handle, const Vector3 &p_velocity);
	void set_homing_target(int64_t p_handle, const Vector3 &p_target);
	void clear_homing_target(int64_t p_handle);

	int get_live_count() const { return (int)_hot.size(); }

	// Every live projectile's position, packed, in world space. How they are drawn
	// is deliberately left open: this hands out the positions and lets the game
	// choose what renders them.
	PackedVector3Array get_positions() const;

	void set_capacity(int p_capacity);
	int get_capacity() const { return _capacity; }

	void set_max_substeps(int p_substeps);
	int get_max_substeps() const { return _max_substeps; }

	void set_gravity(const Vector3 &p_gravity);
	Vector3 get_gravity() const { return _gravity; }

	void set_multimesh(const Ref<MultiMesh> &p_multimesh);
	Ref<MultiMesh> get_multimesh() const { return _multimesh; }

	ProjectilePool();
	~ProjectilePool();
};
