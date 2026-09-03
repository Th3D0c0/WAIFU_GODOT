/**************************************************************************/
/*  box3d_body_3d.cpp                                                     */
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

#include "box3d_body_3d.h"

#include "box3d_conversions.h"
#include "box3d_direct_body_state_3d.h"
#include "box3d_space_3d.h"

#include "core/error/error_macros.h"

b3BodyType Box3DBody3D::_to_b3_body_type() const {
	switch (mode) {
		case PhysicsServer3D::BODY_MODE_STATIC:
			return b3_staticBody;
		case PhysicsServer3D::BODY_MODE_KINEMATIC:
			return b3_kinematicBody;
		default:
			// Both RIGID and RIGID_LINEAR are dynamic to Box3D; the linear-only variant
			// is expressed by locking all three angular axes instead.
			return b3_dynamicBody;
	}
}

void Box3DBody3D::_build() {
	ERR_FAIL_NULL(space);
	ERR_FAIL_COND(B3_IS_NON_NULL(body_id));

	b3BodyDef body_def = b3DefaultBodyDef();
	body_def.type = _to_b3_body_type();
	body_def.position = to_b3_pos(transform.origin);
	body_def.rotation = to_b3(transform.basis.get_rotation_quaternion());
	body_def.linearVelocity = to_b3(linear_velocity);
	body_def.angularVelocity = to_b3(angular_velocity);
	body_def.linearDamping = (float)linear_damp;
	body_def.angularDamping = (float)angular_damp;
	body_def.gravityScale = (float)gravity_scale;
	body_def.enableSleep = sleep_allowed;
	body_def.isBullet = ccd_enabled;
	// The Box3D body points back at its owner so move events, which carry only
	// userData and a transform, can be resolved without a side table.
	body_def.userData = this;

	if (mode == PhysicsServer3D::BODY_MODE_RIGID_LINEAR) {
		body_def.motionLocks.angularX = true;
		body_def.motionLocks.angularY = true;
		body_def.motionLocks.angularZ = true;
	}

	body_id = b3CreateBody(space->get_world_id(), &body_def);
	ERR_FAIL_COND(!B3_IS_NON_NULL(body_id));

	_rebuild_shapes();
}

void Box3DBody3D::_destroy_shape_slot(ShapeSlot &p_slot) {
	// The mesh and hull data are ours: b3CreateMeshShape holds a reference rather than
	// cloning, so it must outlive the shape and be freed strictly after it.
	p_slot.id = b3_nullShapeId;
	if (p_slot.owned_mesh != nullptr) {
		b3DestroyMesh(p_slot.owned_mesh);
		p_slot.owned_mesh = nullptr;
	}
	if (p_slot.owned_hull != nullptr) {
		b3DestroyHull(p_slot.owned_hull);
		p_slot.owned_hull = nullptr;
	}
}

void Box3DBody3D::_destroy() {
	if (!B3_IS_NON_NULL(body_id)) {
		return;
	}

	// Destroying the body destroys its shapes with it, so the slots are cleaned up
	// after rather than before - freeing a mesh a live shape still references would
	// leave Box3D holding a dangling pointer until the next step.
	b3DestroyBody(body_id);
	body_id = b3_nullBodyId;

	for (ShapeSlot &slot : shapes) {
		_destroy_shape_slot(slot);
	}
}

void Box3DBody3D::_rebuild_shapes() {
	if (!B3_IS_NON_NULL(body_id)) {
		return;
	}

	for (ShapeSlot &slot : shapes) {
		if (B3_IS_NON_NULL(slot.id)) {
			b3DestroyShape(slot.id, false);
		}
		_destroy_shape_slot(slot);

		if (slot.shape == nullptr || slot.disabled || !slot.shape->is_valid()) {
			continue;
		}
		if (slot.shape->is_static_only() && mode != PhysicsServer3D::BODY_MODE_STATIC) {
			WARN_PRINT_ONCE("Box3D: concave shapes only collide on static bodies; ignoring it on a non-static body.");
			continue;
		}

		b3ShapeDef shape_def = b3DefaultShapeDef();
		shape_def.density = 1.0f;
		shape_def.baseMaterial.friction = (float)friction;
		shape_def.baseMaterial.restitution = (float)bounce;
		shape_def.filter.categoryBits = collision_layer;
		shape_def.filter.maskBits = collision_mask;
		shape_def.userData = this;
		// Deferred so a body with several shapes recomputes its mass once, below.
		shape_def.updateBodyMass = false;

		slot.id = slot.shape->instantiate(body_id, shape_def, slot.transform, &slot.owned_hull, &slot.owned_mesh);
	}

	b3Body_ApplyMassFromShapes(body_id);

	// Godot's mass is authored directly, so the density-derived mass Box3D just
	// computed is rescaled to match. Scaling the inertia tensor by the same ratio
	// keeps its shape, which is what makes this a mass change rather than a
	// differently-proportioned body.
	if (mode == PhysicsServer3D::BODY_MODE_RIGID || mode == PhysicsServer3D::BODY_MODE_RIGID_LINEAR) {
		const float computed = b3Body_GetMass(body_id);
		if (computed > 0.0f && mass > 0) {
			b3MassData mass_data = b3Body_GetMassData(body_id);
			const float ratio = (float)mass / computed;
			mass_data.mass *= ratio;
			mass_data.inertia = b3MulSM(ratio, mass_data.inertia);
			b3Body_SetMassData(body_id, mass_data);
		}
	}
}

void Box3DBody3D::set_space(Box3DSpace3D *p_space) {
	if (space == p_space) {
		return;
	}

	if (space != nullptr) {
		_destroy();
		space->remove_body(this);
	}

	space = p_space;

	if (space != nullptr) {
		space->add_body(this);
		_build();
	}
}

void Box3DBody3D::set_mode(PhysicsServer3D::BodyMode p_mode) {
	if (mode == p_mode) {
		return;
	}
	const bool was_static = (mode == PhysicsServer3D::BODY_MODE_STATIC);
	mode = p_mode;

	if (!B3_IS_NON_NULL(body_id)) {
		return;
	}

	b3Body_SetType(body_id, _to_b3_body_type());

	b3MotionLocks locks = {};
	if (mode == PhysicsServer3D::BODY_MODE_RIGID_LINEAR) {
		locks.angularX = true;
		locks.angularY = true;
		locks.angularZ = true;
	}
	b3Body_SetMotionLocks(body_id, locks);

	// Concave shapes are only instantiated on static bodies, so crossing that boundary
	// in either direction changes which shapes should exist.
	if (was_static != (mode == PhysicsServer3D::BODY_MODE_STATIC)) {
		_rebuild_shapes();
	}
}

void Box3DBody3D::set_transform(const Transform3D &p_transform) {
	transform = p_transform;
	if (B3_IS_NON_NULL(body_id)) {
		b3Body_SetTransform(body_id, to_b3_pos(p_transform.origin), to_b3(p_transform.basis.get_rotation_quaternion()));
	}
}

Transform3D Box3DBody3D::get_transform() const {
	if (!B3_IS_NON_NULL(body_id)) {
		return transform;
	}
	return to_godot(b3Body_GetTransform(body_id));
}

void Box3DBody3D::set_linear_velocity(const Vector3 &p_velocity) {
	linear_velocity = p_velocity;
	if (B3_IS_NON_NULL(body_id)) {
		b3Body_SetLinearVelocity(body_id, to_b3(p_velocity));
	}
}

Vector3 Box3DBody3D::get_linear_velocity() const {
	if (!B3_IS_NON_NULL(body_id)) {
		return linear_velocity;
	}
	return to_godot(b3Body_GetLinearVelocity(body_id));
}

void Box3DBody3D::set_angular_velocity(const Vector3 &p_velocity) {
	angular_velocity = p_velocity;
	if (B3_IS_NON_NULL(body_id)) {
		b3Body_SetAngularVelocity(body_id, to_b3(p_velocity));
	}
}

Vector3 Box3DBody3D::get_angular_velocity() const {
	if (!B3_IS_NON_NULL(body_id)) {
		return angular_velocity;
	}
	return to_godot(b3Body_GetAngularVelocity(body_id));
}

void Box3DBody3D::set_param(PhysicsServer3D::BodyParameter p_param, const Variant &p_value) {
	switch (p_param) {
		case PhysicsServer3D::BODY_PARAM_MASS: {
			mass = p_value;
			_rebuild_shapes();
		} break;

		case PhysicsServer3D::BODY_PARAM_FRICTION: {
			friction = p_value;
			_rebuild_shapes();
		} break;

		case PhysicsServer3D::BODY_PARAM_BOUNCE: {
			bounce = p_value;
			_rebuild_shapes();
		} break;

		case PhysicsServer3D::BODY_PARAM_GRAVITY_SCALE: {
			gravity_scale = p_value;
			if (B3_IS_NON_NULL(body_id)) {
				b3Body_SetGravityScale(body_id, (float)gravity_scale);
			}
		} break;

		case PhysicsServer3D::BODY_PARAM_LINEAR_DAMP: {
			linear_damp = p_value;
			if (B3_IS_NON_NULL(body_id)) {
				b3Body_SetLinearDamping(body_id, (float)linear_damp);
			}
		} break;

		case PhysicsServer3D::BODY_PARAM_ANGULAR_DAMP: {
			angular_damp = p_value;
			if (B3_IS_NON_NULL(body_id)) {
				b3Body_SetAngularDamping(body_id, (float)angular_damp);
			}
		} break;

		default: {
			// The damping *modes* and the inertia / centre-of-mass overrides are not
			// wired up yet. They are deliberately dropped rather than half-applied.
		} break;
	}
}

Variant Box3DBody3D::get_param(PhysicsServer3D::BodyParameter p_param) const {
	switch (p_param) {
		case PhysicsServer3D::BODY_PARAM_MASS:
			return mass;
		case PhysicsServer3D::BODY_PARAM_FRICTION:
			return friction;
		case PhysicsServer3D::BODY_PARAM_BOUNCE:
			return bounce;
		case PhysicsServer3D::BODY_PARAM_GRAVITY_SCALE:
			return gravity_scale;
		case PhysicsServer3D::BODY_PARAM_LINEAR_DAMP:
			return linear_damp;
		case PhysicsServer3D::BODY_PARAM_ANGULAR_DAMP:
			return angular_damp;
		default:
			return Variant();
	}
}

void Box3DBody3D::set_collision_layer(uint32_t p_layer) {
	collision_layer = p_layer;
	_rebuild_shapes();
}

void Box3DBody3D::set_collision_mask(uint32_t p_mask) {
	collision_mask = p_mask;
	_rebuild_shapes();
}

void Box3DBody3D::set_ccd_enabled(bool p_enabled) {
	ccd_enabled = p_enabled;
	if (B3_IS_NON_NULL(body_id)) {
		b3Body_SetBullet(body_id, p_enabled);
	}
}

void Box3DBody3D::set_sleep_allowed(bool p_allowed) {
	sleep_allowed = p_allowed;
	if (B3_IS_NON_NULL(body_id)) {
		b3Body_EnableSleep(body_id, p_allowed);
	}
}

void Box3DBody3D::set_sleeping(bool p_sleeping) {
	if (B3_IS_NON_NULL(body_id)) {
		b3Body_SetAwake(body_id, !p_sleeping);
	}
}

bool Box3DBody3D::is_sleeping() const {
	if (!B3_IS_NON_NULL(body_id)) {
		return false;
	}
	return !b3Body_IsAwake(body_id);
}

void Box3DBody3D::apply_central_impulse(const Vector3 &p_impulse) {
	if (B3_IS_NON_NULL(body_id)) {
		b3Body_ApplyLinearImpulseToCenter(body_id, to_b3(p_impulse), true);
	}
}

void Box3DBody3D::apply_impulse(const Vector3 &p_impulse, const Vector3 &p_position) {
	if (B3_IS_NON_NULL(body_id)) {
		// Godot's position is relative to the body origin; Box3D wants a world point.
		b3Body_ApplyLinearImpulse(body_id, to_b3(p_impulse), to_b3_pos(get_transform().origin + p_position), true);
	}
}

void Box3DBody3D::apply_torque_impulse(const Vector3 &p_impulse) {
	if (B3_IS_NON_NULL(body_id)) {
		b3Body_ApplyAngularImpulse(body_id, to_b3(p_impulse), true);
	}
}

void Box3DBody3D::apply_central_force(const Vector3 &p_force) {
	if (B3_IS_NON_NULL(body_id)) {
		b3Body_ApplyForceToCenter(body_id, to_b3(p_force), true);
	}
}

void Box3DBody3D::apply_force(const Vector3 &p_force, const Vector3 &p_position) {
	if (B3_IS_NON_NULL(body_id)) {
		b3Body_ApplyForce(body_id, to_b3(p_force), to_b3_pos(get_transform().origin + p_position), true);
	}
}

void Box3DBody3D::apply_torque(const Vector3 &p_torque) {
	if (B3_IS_NON_NULL(body_id)) {
		b3Body_ApplyTorque(body_id, to_b3(p_torque), true);
	}
}

void Box3DBody3D::add_shape(Box3DShape3D *p_shape, const Transform3D &p_transform, bool p_disabled) {
	ShapeSlot slot;
	slot.shape = p_shape;
	slot.transform = p_transform;
	slot.disabled = p_disabled;
	shapes.push_back(slot);

	if (p_shape != nullptr) {
		p_shape->add_dependent(this);
	}
	_rebuild_shapes();
}

void Box3DBody3D::set_shape(int p_index, Box3DShape3D *p_shape) {
	ERR_FAIL_INDEX(p_index, (int)shapes.size());
	if (shapes[p_index].shape != nullptr) {
		shapes[p_index].shape->remove_dependent(this);
	}
	shapes[p_index].shape = p_shape;
	if (p_shape != nullptr) {
		p_shape->add_dependent(this);
	}
	_rebuild_shapes();
}

void Box3DBody3D::set_shape_transform(int p_index, const Transform3D &p_transform) {
	ERR_FAIL_INDEX(p_index, (int)shapes.size());
	shapes[p_index].transform = p_transform;
	_rebuild_shapes();
}

void Box3DBody3D::set_shape_disabled(int p_index, bool p_disabled) {
	ERR_FAIL_INDEX(p_index, (int)shapes.size());
	shapes[p_index].disabled = p_disabled;
	_rebuild_shapes();
}

void Box3DBody3D::remove_shape(int p_index) {
	ERR_FAIL_INDEX(p_index, (int)shapes.size());

	if (B3_IS_NON_NULL(shapes[p_index].id)) {
		b3DestroyShape(shapes[p_index].id, false);
	}
	_destroy_shape_slot(shapes[p_index]);
	if (shapes[p_index].shape != nullptr) {
		shapes[p_index].shape->remove_dependent(this);
	}
	shapes.remove_at(p_index);
	_rebuild_shapes();
}

void Box3DBody3D::clear_shapes() {
	while (!shapes.is_empty()) {
		remove_shape(shapes.size() - 1);
	}
}

Box3DShape3D *Box3DBody3D::get_shape(int p_index) const {
	ERR_FAIL_INDEX_V(p_index, (int)shapes.size(), nullptr);
	return shapes[p_index].shape;
}

Transform3D Box3DBody3D::get_shape_transform(int p_index) const {
	ERR_FAIL_INDEX_V(p_index, (int)shapes.size(), Transform3D());
	return shapes[p_index].transform;
}

void Box3DBody3D::shape_changed(Box3DShape3D *p_shape) {
	_rebuild_shapes();
}

Box3DDirectBodyState3D *Box3DBody3D::get_direct_state() {
	if (direct_state == nullptr) {
		direct_state = memnew(Box3DDirectBodyState3D(this));
	}
	return direct_state;
}

Box3DBody3D::~Box3DBody3D() {
	if (direct_state != nullptr) {
		memdelete(direct_state);
		direct_state = nullptr;
	}
	_destroy();
	for (ShapeSlot &slot : shapes) {
		if (slot.shape != nullptr) {
			slot.shape->remove_dependent(this);
		}
	}
}
