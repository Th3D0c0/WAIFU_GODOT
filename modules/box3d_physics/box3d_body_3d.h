/**************************************************************************/
/*  box3d_body_3d.h                                                       */
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

#include "box3d_shape_3d.h"

#include "core/math/transform_3d.h"
#include "core/object/object_id.h"
#include "core/templates/hash_set.h"
#include "core/templates/local_vector.h"
#include "core/templates/rid.h"
#include "servers/physics_3d/physics_server_3d.h"

#include <box3d/box3d.h>

class Box3DSpace3D;
class Box3DDirectBodyState3D;
class Box3DJoint3D;

// A Godot body and the Box3D body backing it.
//
// The b3BodyId only exists while the body is in a space: Box3D bodies are created
// from a b3WorldId and cannot be moved between worlds, whereas Godot creates a body
// RID first and assigns a space afterwards, possibly never. So everything Godot sets
// before that point is buffered here and replayed by _build() when a space arrives,
// and _destroy() tears the Box3D side back down when the space is cleared. State
// therefore lives in this object, not in Box3D, and Box3D is treated as a cache.
class Box3DBody3D {
	struct ShapeSlot {
		Box3DShape3D *shape = nullptr;
		Transform3D transform;
		bool disabled = false;
		b3ShapeId id = b3_nullShapeId;
		// Owned geometry that b3CreateMeshShape does not clone; freed with the slot.
		b3MeshData *owned_mesh = nullptr;
		b3HullData *owned_hull = nullptr;
	};

	RID self;
	Box3DSpace3D *space = nullptr;
	b3BodyId body_id = b3_nullBodyId;

	PhysicsServer3D::BodyMode mode = PhysicsServer3D::BODY_MODE_RIGID;
	Transform3D transform;
	Vector3 linear_velocity;
	Vector3 angular_velocity;
	real_t mass = 1.0;
	real_t linear_damp = 0.0;
	real_t angular_damp = 0.0;
	real_t gravity_scale = 1.0;
	real_t friction = 1.0;
	real_t bounce = 0.0;
	uint32_t collision_layer = 1;
	uint32_t collision_mask = 1;
	bool ccd_enabled = false;
	bool sleep_allowed = true;
	bool ray_pickable = true;
	bool omit_force_integration = false;

	ObjectID instance_id;
	Callable state_sync_callback;
	LocalVector<ShapeSlot> shapes;
	Box3DDirectBodyState3D *direct_state = nullptr;
	// Joints referencing this body. A Box3D constraint can only exist while both its
	// endpoints are resident in the same world, so every joint here has to be told
	// when this body enters or leaves a space.
	HashSet<Box3DJoint3D *> joints;

	void _build();
	void _destroy();
	void _rebuild_shapes();
	void _destroy_shape_slot(ShapeSlot &p_slot);
	b3BodyType _to_b3_body_type() const;

public:
	void set_self(const RID &p_self) { self = p_self; }
	RID get_self() const { return self; }

	b3BodyId get_body_id() const { return body_id; }
	bool is_in_space() const { return B3_IS_NON_NULL(body_id); }
	Box3DSpace3D *get_space() const { return space; }
	void set_space(Box3DSpace3D *p_space);

	void set_mode(PhysicsServer3D::BodyMode p_mode);
	PhysicsServer3D::BodyMode get_mode() const { return mode; }

	void set_transform(const Transform3D &p_transform);
	Transform3D get_transform() const;

	void set_linear_velocity(const Vector3 &p_velocity);
	Vector3 get_linear_velocity() const;
	void set_angular_velocity(const Vector3 &p_velocity);
	Vector3 get_angular_velocity() const;

	real_t get_mass() const { return mass; }
	real_t get_gravity_scale() const { return gravity_scale; }
	real_t get_linear_damp() const { return linear_damp; }
	real_t get_angular_damp() const { return angular_damp; }

	void set_param(PhysicsServer3D::BodyParameter p_param, const Variant &p_value);
	Variant get_param(PhysicsServer3D::BodyParameter p_param) const;

	void set_collision_layer(uint32_t p_layer);
	uint32_t get_collision_layer() const { return collision_layer; }
	void set_collision_mask(uint32_t p_mask);
	uint32_t get_collision_mask() const { return collision_mask; }

	void set_ccd_enabled(bool p_enabled);
	bool is_ccd_enabled() const { return ccd_enabled; }
	void set_sleep_allowed(bool p_allowed);
	bool is_sleep_allowed() const { return sleep_allowed; }
	void set_sleeping(bool p_sleeping);
	bool is_sleeping() const;

	void set_ray_pickable(bool p_pickable) { ray_pickable = p_pickable; }
	bool is_ray_pickable() const { return ray_pickable; }

	void set_omit_force_integration(bool p_omit) { omit_force_integration = p_omit; }
	bool is_omitting_force_integration() const { return omit_force_integration; }

	void set_instance_id(ObjectID p_id) { instance_id = p_id; }
	ObjectID get_instance_id() const { return instance_id; }
	void set_state_sync_callback(const Callable &p_callable) { state_sync_callback = p_callable; }
	const Callable &get_state_sync_callback() const { return state_sync_callback; }

	void apply_central_impulse(const Vector3 &p_impulse);
	void apply_impulse(const Vector3 &p_impulse, const Vector3 &p_position);
	void apply_torque_impulse(const Vector3 &p_impulse);
	void apply_central_force(const Vector3 &p_force);
	void apply_force(const Vector3 &p_force, const Vector3 &p_position);
	void apply_torque(const Vector3 &p_torque);

	void add_shape(Box3DShape3D *p_shape, const Transform3D &p_transform, bool p_disabled);
	void set_shape(int p_index, Box3DShape3D *p_shape);
	void set_shape_transform(int p_index, const Transform3D &p_transform);
	void set_shape_disabled(int p_index, bool p_disabled);
	void remove_shape(int p_index);
	void clear_shapes();
	int get_shape_count() const { return shapes.size(); }
	Box3DShape3D *get_shape(int p_index) const;
	Transform3D get_shape_transform(int p_index) const;

	// Called by a shape whose data changed under a body already using it.
	void shape_changed(Box3DShape3D *p_shape);

	// Created on first use rather than per body up front: most bodies in a scene
	// never have their direct state asked for, and Godot only reaches for it through
	// body_get_direct_state() or the state sync callback.
	Box3DDirectBodyState3D *get_direct_state();

	void add_joint(Box3DJoint3D *p_joint) { joints.insert(p_joint); }
	void remove_joint(Box3DJoint3D *p_joint) { joints.erase(p_joint); }
	const HashSet<Box3DJoint3D *> &get_joints() const { return joints; }

	~Box3DBody3D();
};
