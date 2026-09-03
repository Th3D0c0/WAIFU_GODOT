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
#include "box3d_joint_3d.h"
#include "box3d_query_3d.h"
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
	body_def.userData = static_cast<Box3DCollisionObject3D *>(this);

	// Godot configures axis locks on the node before it enters the tree, so by the time
	// the b3 body exists the locks are already recorded and have to be replayed here -
	// applying them only in set_axis_lock() silently drops every lock authored in the
	// editor.
	body_def.motionLocks.linearX = (locked_axes & PhysicsServer3D::BODY_AXIS_LINEAR_X) != 0;
	body_def.motionLocks.linearY = (locked_axes & PhysicsServer3D::BODY_AXIS_LINEAR_Y) != 0;
	body_def.motionLocks.linearZ = (locked_axes & PhysicsServer3D::BODY_AXIS_LINEAR_Z) != 0;
	body_def.motionLocks.angularX = (locked_axes & PhysicsServer3D::BODY_AXIS_ANGULAR_X) != 0;
	body_def.motionLocks.angularY = (locked_axes & PhysicsServer3D::BODY_AXIS_ANGULAR_Y) != 0;
	body_def.motionLocks.angularZ = (locked_axes & PhysicsServer3D::BODY_AXIS_ANGULAR_Z) != 0;

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
		shape_def.filter = box3d_make_shape_filter(collision_layer, collision_mask);
		shape_def.userData = this;
		// A sensor only reports a visitor whose own shape has sensor events enabled -
		// the flag applies to both sides and is false by default (types.h:489). Without
		// this every Area3D in the scene silently detects nothing.
		shape_def.enableSensorEvents = true;
		// Box3D only consults the world's custom filter when one of the two shapes asks
		// for it (types.h:68), and that callback is where Godot's layer rule and its
		// collision exceptions both live - neither is expressible in the filter bits.
		// So this is not optional and not conditional: a shape without it collides with
		// everything its AABB touches, because box3d_make_shape_filter deliberately
		// leaves the cheap bit test unable to reject anything.
		shape_def.enableCustomFiltering = true;
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

	// The body's b3BodyId has just been created or destroyed, and a constraint holds
	// those ids directly, so every joint on this body has to be rebuilt against the
	// new world - or torn down, if the body just left one.
	for (Box3DJoint3D *joint : joints) {
		joint->bodies_changed();
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

int Box3DBody3D::find_shape_index(b3ShapeId p_id) const {
	for (uint32_t i = 0; i < shapes.size(); i++) {
		if (B3_ID_EQUALS(shapes[i].id, p_id)) {
			return (int)i;
		}
	}
	return 0;
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

void Box3DBody3D::reset_mass_properties() {
	// Godot's reset means "forget the authored mass and recompute from the shapes",
	// which is exactly what _rebuild_shapes does before it applies the override - so
	// clearing the override first and rebuilding is the whole operation.
	mass = 0;
	_rebuild_shapes();
	if (B3_IS_NON_NULL(body_id)) {
		mass = (real_t)b3Body_GetMass(body_id);
	}
}

void Box3DBody3D::apply_constant_forces() {
	if (!B3_IS_NON_NULL(body_id)) {
		return;
	}
	// Godot's constant force is a standing request, but Box3D clears accumulated force
	// at the end of every step like any impulse-based solver, so it has to be re-applied
	// each time rather than set once.
	if (constant_force != Vector3()) {
		b3Body_ApplyForceToCenter(body_id, to_b3(constant_force), true);
	}
	if (constant_torque != Vector3()) {
		b3Body_ApplyTorque(body_id, to_b3(constant_torque), true);
	}
}

void Box3DBody3D::set_axis_lock(PhysicsServer3D::BodyAxis p_axis, bool p_locked) {
	if (p_locked) {
		locked_axes |= (uint32_t)p_axis;
	} else {
		locked_axes &= ~(uint32_t)p_axis;
	}

	if (!B3_IS_NON_NULL(body_id)) {
		return;
	}
	b3MotionLocks locks = {};
	locks.linearX = (locked_axes & PhysicsServer3D::BODY_AXIS_LINEAR_X) != 0;
	locks.linearY = (locked_axes & PhysicsServer3D::BODY_AXIS_LINEAR_Y) != 0;
	locks.linearZ = (locked_axes & PhysicsServer3D::BODY_AXIS_LINEAR_Z) != 0;
	locks.angularX = (locked_axes & PhysicsServer3D::BODY_AXIS_ANGULAR_X) != 0;
	locks.angularY = (locked_axes & PhysicsServer3D::BODY_AXIS_ANGULAR_Y) != 0;
	locks.angularZ = (locked_axes & PhysicsServer3D::BODY_AXIS_ANGULAR_Z) != 0;
	// BODY_MODE_RIGID_LINEAR is Godot's other route to the same three angular locks, so
	// it is folded in here rather than fighting with them.
	if (mode == PhysicsServer3D::BODY_MODE_RIGID_LINEAR) {
		locks.angularX = true;
		locks.angularY = true;
		locks.angularZ = true;
	}
	b3Body_SetMotionLocks(body_id, locks);
}

Vector3 Box3DBody3D::get_center_of_mass() const {
	if (!B3_IS_NON_NULL(body_id)) {
		return transform.origin;
	}
	return to_godot(b3Body_GetWorldCenterOfMass(body_id));
}

Vector3 Box3DBody3D::get_center_of_mass_local() const {
	if (!B3_IS_NON_NULL(body_id)) {
		return Vector3();
	}
	return to_godot(b3Body_GetLocalCenterOfMass(body_id));
}

Basis Box3DBody3D::get_inverse_inertia_tensor() const {
	if (!B3_IS_NON_NULL(body_id)) {
		return Basis();
	}
	const b3MassData mass_data = b3Body_GetMassData(body_id);
	// b3Matrix3 is column-major (cx, cy, cz), which is also how Godot's Basis is
	// addressed through set_column, so the inversion is the only real work.
	Basis inertia;
	inertia.set_column(Vector3::AXIS_X, to_godot(mass_data.inertia.cx));
	inertia.set_column(Vector3::AXIS_Y, to_godot(mass_data.inertia.cy));
	inertia.set_column(Vector3::AXIS_Z, to_godot(mass_data.inertia.cz));

	// A static or fully angular-locked body has a singular inertia tensor, and Godot
	// spells "infinitely resistant to rotation" as a zero inverse rather than as an
	// inversion that would blow up.
	if (Math::is_zero_approx(inertia.determinant())) {
		return Basis(Vector3(), Vector3(), Vector3());
	}
	return inertia.inverse();
}

Vector3 Box3DBody3D::get_velocity_at_local_position(const Vector3 &p_local_position) const {
	if (!B3_IS_NON_NULL(body_id)) {
		return Vector3();
	}
	return to_godot(b3Body_GetLocalPointVelocity(body_id, to_b3(p_local_position)));
}

const LocalVector<Box3DBody3D::Contact> &Box3DBody3D::get_contacts() const {
	contacts.clear();
	if (!B3_IS_NON_NULL(body_id) || max_contacts_reported <= 0) {
		return contacts;
	}

	const int capacity = MIN(b3Body_GetContactCapacity(body_id), max_contacts_reported);
	if (capacity <= 0) {
		return contacts;
	}

	LocalVector<b3ContactData> data;
	data.resize(capacity);
	const int count = b3Body_GetContactData(body_id, data.ptr(), capacity);

	const Transform3D body_transform = get_transform();
	const Vector3 center_of_mass = to_godot(b3Body_GetWorldCenterOfMass(body_id));

	for (int i = 0; i < count && (int)contacts.size() < max_contacts_reported; i++) {
		const b3ContactData &contact = data[i];

		// Box3D does not order the pair, so which side is "us" has to be established
		// before the anchors mean anything - they are relative to body A.
		const b3BodyId body_a = b3Shape_GetBody(contact.shapeIdA);
		const bool self_is_a = B3_IS_NON_NULL(body_a) && B3_ID_EQUALS(body_a, body_id);
		const b3ShapeId self_shape = self_is_a ? contact.shapeIdA : contact.shapeIdB;
		const b3ShapeId other_shape = self_is_a ? contact.shapeIdB : contact.shapeIdA;

		const b3BodyId other_body = b3Shape_GetBody(other_shape);
		if (!B3_IS_NON_NULL(other_body)) {
			continue;
		}
		Box3DCollisionObject3D *other = static_cast<Box3DCollisionObject3D *>(b3Body_GetUserData(other_body));
		if (other == nullptr) {
			continue;
		}

		for (int m = 0; m < contact.manifoldCount && (int)contacts.size() < max_contacts_reported; m++) {
			const b3Manifold &manifold = contact.manifolds[m];
			for (int p = 0; p < manifold.pointCount && (int)contacts.size() < max_contacts_reported; p++) {
				const b3ManifoldPoint &point = manifold.points[p];
				if (-point.separation < contact_depth_threshold) {
					continue;
				}

				Contact record;
				// Anchors are world-space offsets from body A's center of mass, so they
				// become world points first and are only then taken back into local space.
				const Vector3 anchor_self = to_godot(self_is_a ? point.anchorA : point.anchorB);
				const Vector3 anchor_other = to_godot(self_is_a ? point.anchorB : point.anchorA);
				record.local_position = body_transform.xform_inv(center_of_mass + anchor_self);
				record.collider_position = center_of_mass + anchor_other;
				// Box3D's manifold normal points from shape A toward shape B. Godot wants it
				// pointing from the collider back toward this body - a box resting on the
				// floor reports (0, 1, 0) - so it is negated when this body is A.
				record.local_normal = to_godot(manifold.normal) * (self_is_a ? -1.0f : 1.0f);
				record.impulse = record.local_normal * (real_t)point.totalNormalImpulse;
				record.local_shape = find_shape_index(self_shape);
				record.collider = other->get_self();
				record.collider_id = other->get_instance_id();
				record.collider_shape = other->is_area()
						? 0
						: static_cast<Box3DBody3D *>(other)->find_shape_index(other_shape);
				record.collider_velocity_at_position =
						to_godot(b3Body_GetWorldPointVelocity(other_body, to_b3_pos(record.collider_position)));
				contacts.push_back(record);
			}
		}
	}
	return contacts;
}
