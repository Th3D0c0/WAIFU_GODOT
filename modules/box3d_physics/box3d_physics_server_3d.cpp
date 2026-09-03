/**************************************************************************/
/*  box3d_physics_server_3d.cpp                                           */
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

#include "box3d_physics_server_3d.h"

#include "box3d_area_3d.h"
#include "box3d_body_3d.h"
#include "box3d_conversions.h"
#include "box3d_direct_body_state_3d.h"
#include "box3d_direct_space_state_3d.h"
#include "box3d_joint_3d.h"
#include "box3d_query_3d.h"
#include "box3d_shape_3d.h"
#include "box3d_space_3d.h"

#include "core/error/error_macros.h"

// Everything not defined here lives in box3d_physics_server_3d_todo.cpp, which is
// generated and shrinks as work moves into this file. See box3d_todo.h.

// Box3D's revolute joint rotates about its frame's Z axis, and Godot's HingeJoint3D
// does too, so joint_make_hinge needs no conversion. joint_make_hinge_simple gives a
// pivot and an axis instead of a frame, which this turns into a frame whose Z column
// is that axis; the other two columns only have to complete an orthonormal basis,
// since a hinge is rotationally symmetric about its own axis.
static Transform3D _frame_from_axis(const Vector3 &p_pivot, const Vector3 &p_axis) {
	const Vector3 z = p_axis.normalized();
	// Any vector not parallel to z seeds the cross products; picking the world axis z
	// is least aligned with keeps the result well conditioned.
	const Vector3 seed = Math::abs(z.x) < (real_t)0.9 ? Vector3(1, 0, 0) : Vector3(0, 1, 0);
	const Vector3 x = seed.cross(z).normalized();
	const Vector3 y = z.cross(x);

	Basis basis;
	basis.set_column(Vector3::AXIS_X, x);
	basis.set_column(Vector3::AXIS_Y, y);
	basis.set_column(Vector3::AXIS_Z, z);
	return Transform3D(basis, p_pivot);
}

/* SHAPES */

RID Box3DPhysicsServer3D::_create_shape(int p_type) {
	Box3DShape3D *shape = memnew(Box3DShape3D);
	shape->set_type((Box3DShape3D::Type)p_type);
	RID rid = shape_owner.make_rid(shape);
	shape->set_self(rid);
	return rid;
}

RID Box3DPhysicsServer3D::sphere_shape_create() {
	return _create_shape(Box3DShape3D::TYPE_SPHERE);
}

RID Box3DPhysicsServer3D::box_shape_create() {
	return _create_shape(Box3DShape3D::TYPE_BOX);
}

RID Box3DPhysicsServer3D::capsule_shape_create() {
	return _create_shape(Box3DShape3D::TYPE_CAPSULE);
}

RID Box3DPhysicsServer3D::cylinder_shape_create() {
	return _create_shape(Box3DShape3D::TYPE_CYLINDER);
}

RID Box3DPhysicsServer3D::convex_polygon_shape_create() {
	return _create_shape(Box3DShape3D::TYPE_CONVEX);
}

RID Box3DPhysicsServer3D::concave_polygon_shape_create() {
	return _create_shape(Box3DShape3D::TYPE_CONCAVE);
}

void Box3DPhysicsServer3D::shape_set_data(RID p_shape, const Variant &p_data) {
	Box3DShape3D *shape = shape_owner.get_or_null(p_shape);
	ERR_FAIL_NULL(shape);
	shape->set_data(p_data);

	// Godot allows a shape's data to change while bodies already reference it, and
	// Box3D shapes are baked geometry, so every dependent body has to rebuild.
	for (Box3DBody3D *body : shape->get_dependents()) {
		body->shape_changed(shape);
	}
	for (Box3DArea3D *area : shape->get_area_dependents()) {
		area->shape_changed(shape);
	}
}

Variant Box3DPhysicsServer3D::shape_get_data(RID p_shape) const {
	const Box3DShape3D *shape = shape_owner.get_or_null(p_shape);
	ERR_FAIL_NULL_V(shape, Variant());
	return shape->get_data();
}

PhysicsServer3D::ShapeType Box3DPhysicsServer3D::shape_get_type(RID p_shape) const {
	const Box3DShape3D *shape = shape_owner.get_or_null(p_shape);
	ERR_FAIL_NULL_V(shape, SHAPE_CUSTOM);

	switch (shape->get_type()) {
		case Box3DShape3D::TYPE_SPHERE:
			return SHAPE_SPHERE;
		case Box3DShape3D::TYPE_BOX:
			return SHAPE_BOX;
		case Box3DShape3D::TYPE_CAPSULE:
			return SHAPE_CAPSULE;
		case Box3DShape3D::TYPE_CYLINDER:
			return SHAPE_CYLINDER;
		case Box3DShape3D::TYPE_CONVEX:
			return SHAPE_CONVEX_POLYGON;
		case Box3DShape3D::TYPE_CONCAVE:
			return SHAPE_CONCAVE_POLYGON;
		default:
			return SHAPE_CUSTOM;
	}
}

void Box3DPhysicsServer3D::shape_set_margin(RID p_shape, real_t p_margin) {
	Box3DShape3D *shape = shape_owner.get_or_null(p_shape);
	ERR_FAIL_NULL(shape);
	// Stored so it reads back, but not forwarded: Box3D has no per-shape margin. Its
	// contact softness comes from the world's contactHertz/contactDampingRatio, which
	// is a different mechanism rather than the same one under another name.
	shape->set_margin(p_margin);
}

real_t Box3DPhysicsServer3D::shape_get_margin(RID p_shape) const {
	const Box3DShape3D *shape = shape_owner.get_or_null(p_shape);
	ERR_FAIL_NULL_V(shape, 0);
	return shape->get_margin();
}

/* SPACES */

RID Box3DPhysicsServer3D::space_create() {
	Box3DSpace3D *space = memnew(Box3DSpace3D);
	RID rid = space_owner.make_rid(space);
	space->set_self(rid);

	// Every Godot space owns a default area, and the world's gravity is delivered
	// through that area's parameters rather than to the space directly - so it has to
	// exist from the moment the space does, or a project's gravity setting lands
	// nowhere. Godot addresses it by the space's own RID; see area_set_param.
	Box3DArea3D *default_area = area_owner.get_or_null(area_create());
	ERR_FAIL_NULL_V(default_area, rid);
	space->set_default_area(default_area);
	default_area->set_space(space);

	return rid;
}

void Box3DPhysicsServer3D::space_set_active(RID p_space, bool p_active) {
	Box3DSpace3D *space = space_owner.get_or_null(p_space);
	ERR_FAIL_NULL(space);

	space->set_active(p_active);
	if (p_active) {
		active_spaces.insert(space);
	} else {
		active_spaces.erase(space);
	}
}

bool Box3DPhysicsServer3D::space_is_active(RID p_space) const {
	const Box3DSpace3D *space = space_owner.get_or_null(p_space);
	ERR_FAIL_NULL_V(space, false);
	return space->is_active();
}

void Box3DPhysicsServer3D::space_set_param(RID p_space, SpaceParameter p_param, real_t p_value) {
	Box3DSpace3D *space = space_owner.get_or_null(p_space);
	ERR_FAIL_NULL(space);
	space->set_param(p_param, p_value);
}

real_t Box3DPhysicsServer3D::space_get_param(RID p_space, SpaceParameter p_param) const {
	const Box3DSpace3D *space = space_owner.get_or_null(p_space);
	ERR_FAIL_NULL_V(space, 0);
	return space->get_param(p_param);
}

PhysicsDirectSpaceState3D *Box3DPhysicsServer3D::space_get_direct_state(RID p_space) {
	Box3DSpace3D *space = space_owner.get_or_null(p_space);
	ERR_FAIL_NULL_V(space, nullptr);
	// GodotPhysicsServer3D guards this with (using_threads && !doing_sync), because
	// its space can be stepped on another thread. This backend is always wrapped
	// single-threaded - see create_box3d_physics_server() - so the guard has no
	// condition under which it could fire and is omitted rather than faked.
	return space->get_direct_state();
}

/* BODIES */

RID Box3DPhysicsServer3D::body_create() {
	Box3DBody3D *body = memnew(Box3DBody3D);
	RID rid = body_owner.make_rid(body);
	body->set_self(rid);
	return rid;
}

void Box3DPhysicsServer3D::body_set_space(RID p_body, RID p_space) {
	Box3DBody3D *body = body_owner.get_or_null(p_body);
	ERR_FAIL_NULL(body);

	Box3DSpace3D *space = nullptr;
	if (p_space.is_valid()) {
		space = space_owner.get_or_null(p_space);
		ERR_FAIL_NULL(space);
	}
	body->set_space(space);
}

RID Box3DPhysicsServer3D::body_get_space(RID p_body) const {
	const Box3DBody3D *body = body_owner.get_or_null(p_body);
	ERR_FAIL_NULL_V(body, RID());

	const Box3DSpace3D *space = body->get_space();
	return space != nullptr ? space->get_self() : RID();
}

void Box3DPhysicsServer3D::body_set_mode(RID p_body, BodyMode p_mode) {
	Box3DBody3D *body = body_owner.get_or_null(p_body);
	ERR_FAIL_NULL(body);
	body->set_mode(p_mode);
}

PhysicsServer3D::BodyMode Box3DPhysicsServer3D::body_get_mode(RID p_body) const {
	const Box3DBody3D *body = body_owner.get_or_null(p_body);
	ERR_FAIL_NULL_V(body, BODY_MODE_STATIC);
	return body->get_mode();
}

void Box3DPhysicsServer3D::body_add_shape(RID p_body, RID p_shape, const Transform3D &p_transform, bool p_disabled) {
	Box3DBody3D *body = body_owner.get_or_null(p_body);
	ERR_FAIL_NULL(body);
	Box3DShape3D *shape = shape_owner.get_or_null(p_shape);
	ERR_FAIL_NULL(shape);
	body->add_shape(shape, p_transform, p_disabled);
}

void Box3DPhysicsServer3D::body_set_shape(RID p_body, int p_shape_idx, RID p_shape) {
	Box3DBody3D *body = body_owner.get_or_null(p_body);
	ERR_FAIL_NULL(body);
	Box3DShape3D *shape = shape_owner.get_or_null(p_shape);
	ERR_FAIL_NULL(shape);
	body->set_shape(p_shape_idx, shape);
}

void Box3DPhysicsServer3D::body_set_shape_transform(RID p_body, int p_shape_idx, const Transform3D &p_transform) {
	Box3DBody3D *body = body_owner.get_or_null(p_body);
	ERR_FAIL_NULL(body);
	body->set_shape_transform(p_shape_idx, p_transform);
}

void Box3DPhysicsServer3D::body_set_shape_disabled(RID p_body, int p_shape_idx, bool p_disabled) {
	Box3DBody3D *body = body_owner.get_or_null(p_body);
	ERR_FAIL_NULL(body);
	body->set_shape_disabled(p_shape_idx, p_disabled);
}

int Box3DPhysicsServer3D::body_get_shape_count(RID p_body) const {
	const Box3DBody3D *body = body_owner.get_or_null(p_body);
	ERR_FAIL_NULL_V(body, 0);
	return body->get_shape_count();
}

RID Box3DPhysicsServer3D::body_get_shape(RID p_body, int p_shape_idx) const {
	const Box3DBody3D *body = body_owner.get_or_null(p_body);
	ERR_FAIL_NULL_V(body, RID());

	const Box3DShape3D *shape = body->get_shape(p_shape_idx);
	return shape != nullptr ? shape->get_self() : RID();
}

Transform3D Box3DPhysicsServer3D::body_get_shape_transform(RID p_body, int p_shape_idx) const {
	const Box3DBody3D *body = body_owner.get_or_null(p_body);
	ERR_FAIL_NULL_V(body, Transform3D());
	return body->get_shape_transform(p_shape_idx);
}

void Box3DPhysicsServer3D::body_remove_shape(RID p_body, int p_shape_idx) {
	Box3DBody3D *body = body_owner.get_or_null(p_body);
	ERR_FAIL_NULL(body);
	body->remove_shape(p_shape_idx);
}

void Box3DPhysicsServer3D::body_clear_shapes(RID p_body) {
	Box3DBody3D *body = body_owner.get_or_null(p_body);
	ERR_FAIL_NULL(body);
	body->clear_shapes();
}

void Box3DPhysicsServer3D::body_attach_object_instance_id(RID p_body, ObjectID p_id) {
	Box3DBody3D *body = body_owner.get_or_null(p_body);
	ERR_FAIL_NULL(body);
	body->set_instance_id(p_id);
}

ObjectID Box3DPhysicsServer3D::body_get_object_instance_id(RID p_body) const {
	const Box3DBody3D *body = body_owner.get_or_null(p_body);
	ERR_FAIL_NULL_V(body, ObjectID());
	return body->get_instance_id();
}

void Box3DPhysicsServer3D::body_set_collision_layer(RID p_body, uint32_t p_layer) {
	Box3DBody3D *body = body_owner.get_or_null(p_body);
	ERR_FAIL_NULL(body);
	body->set_collision_layer(p_layer);
}

uint32_t Box3DPhysicsServer3D::body_get_collision_layer(RID p_body) const {
	const Box3DBody3D *body = body_owner.get_or_null(p_body);
	ERR_FAIL_NULL_V(body, 0);
	return body->get_collision_layer();
}

void Box3DPhysicsServer3D::body_set_collision_mask(RID p_body, uint32_t p_mask) {
	Box3DBody3D *body = body_owner.get_or_null(p_body);
	ERR_FAIL_NULL(body);
	body->set_collision_mask(p_mask);
}

uint32_t Box3DPhysicsServer3D::body_get_collision_mask(RID p_body) const {
	const Box3DBody3D *body = body_owner.get_or_null(p_body);
	ERR_FAIL_NULL_V(body, 0);
	return body->get_collision_mask();
}

void Box3DPhysicsServer3D::body_set_state(RID p_body, BodyState p_state, const Variant &p_variant) {
	Box3DBody3D *body = body_owner.get_or_null(p_body);
	ERR_FAIL_NULL(body);

	switch (p_state) {
		case BODY_STATE_TRANSFORM:
			body->set_transform(p_variant);
			break;
		case BODY_STATE_LINEAR_VELOCITY:
			body->set_linear_velocity(p_variant);
			break;
		case BODY_STATE_ANGULAR_VELOCITY:
			body->set_angular_velocity(p_variant);
			break;
		case BODY_STATE_SLEEPING:
			body->set_sleeping(p_variant);
			break;
		case BODY_STATE_CAN_SLEEP:
			body->set_sleep_allowed(p_variant);
			break;
	}
}

Variant Box3DPhysicsServer3D::body_get_state(RID p_body, BodyState p_state) const {
	const Box3DBody3D *body = body_owner.get_or_null(p_body);
	ERR_FAIL_NULL_V(body, Variant());

	switch (p_state) {
		case BODY_STATE_TRANSFORM:
			return body->get_transform();
		case BODY_STATE_LINEAR_VELOCITY:
			return body->get_linear_velocity();
		case BODY_STATE_ANGULAR_VELOCITY:
			return body->get_angular_velocity();
		case BODY_STATE_SLEEPING:
			return body->is_sleeping();
		case BODY_STATE_CAN_SLEEP:
			return body->is_sleep_allowed();
	}
	return Variant();
}

void Box3DPhysicsServer3D::body_set_param(RID p_body, BodyParameter p_param, const Variant &p_value) {
	Box3DBody3D *body = body_owner.get_or_null(p_body);
	ERR_FAIL_NULL(body);
	body->set_param(p_param, p_value);
}

Variant Box3DPhysicsServer3D::body_get_param(RID p_body, BodyParameter p_param) const {
	const Box3DBody3D *body = body_owner.get_or_null(p_body);
	ERR_FAIL_NULL_V(body, Variant());
	return body->get_param(p_param);
}

void Box3DPhysicsServer3D::body_set_enable_continuous_collision_detection(RID p_body, bool p_enable) {
	Box3DBody3D *body = body_owner.get_or_null(p_body);
	ERR_FAIL_NULL(body);
	body->set_ccd_enabled(p_enable);
}

bool Box3DPhysicsServer3D::body_is_continuous_collision_detection_enabled(RID p_body) const {
	const Box3DBody3D *body = body_owner.get_or_null(p_body);
	ERR_FAIL_NULL_V(body, false);
	return body->is_ccd_enabled();
}

void Box3DPhysicsServer3D::body_apply_central_impulse(RID p_body, const Vector3 &p_impulse) {
	Box3DBody3D *body = body_owner.get_or_null(p_body);
	ERR_FAIL_NULL(body);
	body->apply_central_impulse(p_impulse);
}

void Box3DPhysicsServer3D::body_apply_impulse(RID p_body, const Vector3 &p_impulse, const Vector3 &p_position) {
	Box3DBody3D *body = body_owner.get_or_null(p_body);
	ERR_FAIL_NULL(body);
	body->apply_impulse(p_impulse, p_position);
}

void Box3DPhysicsServer3D::body_apply_torque_impulse(RID p_body, const Vector3 &p_impulse) {
	Box3DBody3D *body = body_owner.get_or_null(p_body);
	ERR_FAIL_NULL(body);
	body->apply_torque_impulse(p_impulse);
}

void Box3DPhysicsServer3D::body_apply_central_force(RID p_body, const Vector3 &p_force) {
	Box3DBody3D *body = body_owner.get_or_null(p_body);
	ERR_FAIL_NULL(body);
	body->apply_central_force(p_force);
}

void Box3DPhysicsServer3D::body_apply_force(RID p_body, const Vector3 &p_force, const Vector3 &p_position) {
	Box3DBody3D *body = body_owner.get_or_null(p_body);
	ERR_FAIL_NULL(body);
	body->apply_force(p_force, p_position);
}

void Box3DPhysicsServer3D::body_apply_torque(RID p_body, const Vector3 &p_torque) {
	Box3DBody3D *body = body_owner.get_or_null(p_body);
	ERR_FAIL_NULL(body);
	body->apply_torque(p_torque);
}

void Box3DPhysicsServer3D::body_set_state_sync_callback(RID p_body, const Callable &p_callable) {
	Box3DBody3D *body = body_owner.get_or_null(p_body);
	ERR_FAIL_NULL(body);
	body->set_state_sync_callback(p_callable);
}

void Box3DPhysicsServer3D::body_set_ray_pickable(RID p_body, bool p_enable) {
	Box3DBody3D *body = body_owner.get_or_null(p_body);
	ERR_FAIL_NULL(body);
	body->set_ray_pickable(p_enable);
}

void Box3DPhysicsServer3D::body_set_omit_force_integration(RID p_body, bool p_omit) {
	Box3DBody3D *body = body_owner.get_or_null(p_body);
	ERR_FAIL_NULL(body);
	// Recorded only. Custom force integration needs the force-integration callback,
	// which is not wired up, so there is nothing yet for this flag to suppress.
	body->set_omit_force_integration(p_omit);
}

bool Box3DPhysicsServer3D::body_is_omitting_force_integration(RID p_body) const {
	const Box3DBody3D *body = body_owner.get_or_null(p_body);
	ERR_FAIL_NULL_V(body, false);
	return body->is_omitting_force_integration();
}

PhysicsDirectBodyState3D *Box3DPhysicsServer3D::body_get_direct_state(RID p_body) {
	Box3DBody3D *body = body_owner.get_or_null(p_body);
	ERR_FAIL_NULL_V(body, nullptr);
	return body->get_direct_state();
}

/* MISC */

void Box3DPhysicsServer3D::free_rid(RID p_rid) {
	if (Box3DShape3D *shape = shape_owner.get_or_null(p_rid)) {
		// Detach from every body first; a shape freed while still referenced would
		// otherwise leave those bodies holding a dangling pointer.
		while (!shape->get_dependents().is_empty()) {
			Box3DBody3D *body = *shape->get_dependents().begin();
			for (int i = body->get_shape_count() - 1; i >= 0; i--) {
				if (body->get_shape(i) == shape) {
					body->remove_shape(i);
				}
			}
		}
		while (!shape->get_area_dependents().is_empty()) {
			Box3DArea3D *area = *shape->get_area_dependents().begin();
			for (int i = area->get_shape_count() - 1; i >= 0; i--) {
				if (area->get_shape(i) == shape) {
					area->remove_shape(i);
				}
			}
		}
		shape_owner.free(p_rid);
		memdelete(shape);
		return;
	}

	if (Box3DArea3D *area = area_owner.get_or_null(p_rid)) {
		area->set_space(nullptr);
		area->clear_shapes();
		area_owner.free(p_rid);
		memdelete(area);
		return;
	}

	if (Box3DJoint3D *joint = joint_owner.get_or_null(p_rid)) {
		joint_owner.free(p_rid);
		memdelete(joint);
		return;
	}

	if (Box3DBody3D *body = body_owner.get_or_null(p_rid)) {
		// Joints hold raw pointers to their endpoints, so any joint still attached is
		// cleared first - which also unregisters it from this body.
		while (!body->get_joints().is_empty()) {
			(*body->get_joints().begin())->clear();
		}
		body->set_space(nullptr);
		body->clear_shapes();
		body_owner.free(p_rid);
		memdelete(body);
		return;
	}

	if (Box3DSpace3D *space = space_owner.get_or_null(p_rid)) {
		// Bodies keep a raw pointer to their space, so they must be evicted before it
		// goes away rather than left to discover it later.
		while (!space->get_bodies().is_empty()) {
			(*space->get_bodies().begin())->set_space(nullptr);
		}
		// The default area was created by space_create() and is owned by the space, so
		// it is destroyed here rather than waiting for a free_rid that never comes.
		Box3DArea3D *default_area = space->get_default_area();
		while (!space->get_areas().is_empty()) {
			(*space->get_areas().begin())->set_space(nullptr);
		}
		if (default_area != nullptr) {
			area_owner.free(default_area->get_self());
			memdelete(default_area);
		}
		active_spaces.erase(space);
		space_owner.free(p_rid);
		memdelete(space);
		return;
	}

	ERR_FAIL_MSG("Box3D: attempted to free an invalid RID.");
}

void Box3DPhysicsServer3D::set_active(bool p_active) {
	active = p_active;
}

void Box3DPhysicsServer3D::init() {
}

void Box3DPhysicsServer3D::finish() {
}

void Box3DPhysicsServer3D::step(real_t p_step) {
	if (!active) {
		return;
	}
	for (Box3DSpace3D *space : active_spaces) {
		space->step(p_step);
	}
}

void Box3DPhysicsServer3D::sync() {
}

void Box3DPhysicsServer3D::flush_queries() {
	if (!active) {
		return;
	}

	flushing_queries = true;
	for (Box3DSpace3D *space : active_spaces) {
		space->call_queries();
	}
	flushing_queries = false;
}

void Box3DPhysicsServer3D::end_sync() {
}

bool Box3DPhysicsServer3D::is_flushing_queries() const {
	return flushing_queries;
}

int Box3DPhysicsServer3D::get_process_info(ProcessInfo p_info) {
	// Box3D exposes these through b3World_GetCounters, which is not plumbed through
	// yet; reporting zero is honest, whereas a guess would look like real telemetry.
	return 0;
}

/* JOINTS */

RID Box3DPhysicsServer3D::joint_create() {
	Box3DJoint3D *joint = memnew(Box3DJoint3D);
	RID rid = joint_owner.make_rid(joint);
	joint->set_self(rid);
	return rid;
}

void Box3DPhysicsServer3D::joint_clear(RID p_joint) {
	Box3DJoint3D *joint = joint_owner.get_or_null(p_joint);
	ERR_FAIL_NULL(joint);
	joint->clear();
}

PhysicsServer3D::JointType Box3DPhysicsServer3D::joint_get_type(RID p_joint) const {
	const Box3DJoint3D *joint = joint_owner.get_or_null(p_joint);
	ERR_FAIL_NULL_V(joint, JOINT_TYPE_PIN);
	const JointType type = joint->get_type();
	// A joint that has been created but not yet given a shape by one of the
	// joint_make_* calls reports as a pin, which is what Godot's own backends do.
	return type == Box3DJoint3D::TYPE_NONE ? JOINT_TYPE_PIN : type;
}

void Box3DPhysicsServer3D::joint_set_solver_priority(RID p_joint, int p_priority) {
	Box3DJoint3D *joint = joint_owner.get_or_null(p_joint);
	ERR_FAIL_NULL(joint);
	// Recorded only. Box3D's constraint graph orders islands itself and exposes no
	// per-joint solve priority.
	joint->set_solver_priority(p_priority);
}

int Box3DPhysicsServer3D::joint_get_solver_priority(RID p_joint) const {
	const Box3DJoint3D *joint = joint_owner.get_or_null(p_joint);
	ERR_FAIL_NULL_V(joint, 0);
	return joint->get_solver_priority();
}

void Box3DPhysicsServer3D::joint_disable_collisions_between_bodies(RID p_joint, bool p_disable) {
	Box3DJoint3D *joint = joint_owner.get_or_null(p_joint);
	ERR_FAIL_NULL(joint);
	// Godot asks to *disable*; Box3D is told whether they *collide*.
	joint->set_collide_connected(!p_disable);
}

bool Box3DPhysicsServer3D::joint_is_disabled_collisions_between_bodies(RID p_joint) const {
	const Box3DJoint3D *joint = joint_owner.get_or_null(p_joint);
	ERR_FAIL_NULL_V(joint, false);
	return !joint->is_collide_connected();
}

void Box3DPhysicsServer3D::joint_make_pin(RID p_joint, RID p_body_A, const Vector3 &p_local_A, RID p_body_B, const Vector3 &p_local_B) {
	Box3DJoint3D *joint = joint_owner.get_or_null(p_joint);
	ERR_FAIL_NULL(joint);
	joint->make_pin(body_owner.get_or_null(p_body_A), p_local_A, body_owner.get_or_null(p_body_B), p_local_B);
}

void Box3DPhysicsServer3D::joint_make_hinge(RID p_joint, RID p_body_A, const Transform3D &p_hinge_A, RID p_body_B, const Transform3D &p_hinge_B) {
	Box3DJoint3D *joint = joint_owner.get_or_null(p_joint);
	ERR_FAIL_NULL(joint);
	joint->make_hinge(body_owner.get_or_null(p_body_A), p_hinge_A, body_owner.get_or_null(p_body_B), p_hinge_B);
}

void Box3DPhysicsServer3D::joint_make_hinge_simple(RID p_joint, RID p_body_A, const Vector3 &p_pivot_A, const Vector3 &p_axis_A, RID p_body_B, const Vector3 &p_pivot_B, const Vector3 &p_axis_B) {
	Box3DJoint3D *joint = joint_owner.get_or_null(p_joint);
	ERR_FAIL_NULL(joint);
	// Box3D's revolute joint spins about its frame's z axis, so a pivot-and-axis
	// hinge becomes a frame whose z column is that axis.
	joint->make_hinge(body_owner.get_or_null(p_body_A), _frame_from_axis(p_pivot_A, p_axis_A),
			body_owner.get_or_null(p_body_B), _frame_from_axis(p_pivot_B, p_axis_B));
}

void Box3DPhysicsServer3D::joint_make_slider(RID p_joint, RID p_body_A, const Transform3D &p_local_frame_A, RID p_body_B, const Transform3D &p_local_frame_B) {
	Box3DJoint3D *joint = joint_owner.get_or_null(p_joint);
	ERR_FAIL_NULL(joint);
	joint->make_slider(body_owner.get_or_null(p_body_A), p_local_frame_A, body_owner.get_or_null(p_body_B), p_local_frame_B);
}

void Box3DPhysicsServer3D::joint_make_cone_twist(RID p_joint, RID p_body_A, const Transform3D &p_local_frame_A, RID p_body_B, const Transform3D &p_local_frame_B) {
	Box3DJoint3D *joint = joint_owner.get_or_null(p_joint);
	ERR_FAIL_NULL(joint);
	joint->make_cone_twist(body_owner.get_or_null(p_body_A), p_local_frame_A, body_owner.get_or_null(p_body_B), p_local_frame_B);
}

void Box3DPhysicsServer3D::joint_make_generic_6dof(RID p_joint, RID p_body_A, const Transform3D &p_local_frame_A, RID p_body_B, const Transform3D &p_local_frame_B) {
	Box3DJoint3D *joint = joint_owner.get_or_null(p_joint);
	ERR_FAIL_NULL(joint);
	joint->make_generic_6dof(body_owner.get_or_null(p_body_A), p_local_frame_A, body_owner.get_or_null(p_body_B), p_local_frame_B);
}

void Box3DPhysicsServer3D::generic_6dof_joint_set_param(RID p_joint, Vector3::Axis p_axis, G6DOFJointAxisParam p_param, real_t p_value) {
	Box3DJoint3D *joint = joint_owner.get_or_null(p_joint);
	ERR_FAIL_NULL(joint);
	joint->set_g6dof_param(p_axis, p_param, p_value);
}

real_t Box3DPhysicsServer3D::generic_6dof_joint_get_param(RID p_joint, Vector3::Axis p_axis, G6DOFJointAxisParam p_param) const {
	const Box3DJoint3D *joint = joint_owner.get_or_null(p_joint);
	ERR_FAIL_NULL_V(joint, 0);
	return joint->get_g6dof_param(p_axis, p_param);
}

void Box3DPhysicsServer3D::generic_6dof_joint_set_flag(RID p_joint, Vector3::Axis p_axis, G6DOFJointAxisFlag p_flag, bool p_enable) {
	Box3DJoint3D *joint = joint_owner.get_or_null(p_joint);
	ERR_FAIL_NULL(joint);
	joint->set_g6dof_flag(p_axis, p_flag, p_enable);
}

bool Box3DPhysicsServer3D::generic_6dof_joint_get_flag(RID p_joint, Vector3::Axis p_axis, G6DOFJointAxisFlag p_flag) const {
	const Box3DJoint3D *joint = joint_owner.get_or_null(p_joint);
	ERR_FAIL_NULL_V(joint, false);
	return joint->get_g6dof_flag(p_axis, p_flag);
}

void Box3DPhysicsServer3D::body_set_collision_priority(RID p_body, real_t p_priority) {
	Box3DBody3D *body = body_owner.get_or_null(p_body);
	ERR_FAIL_NULL(body);
	body->set_collision_priority(p_priority);
}

real_t Box3DPhysicsServer3D::body_get_collision_priority(RID p_body) const {
	const Box3DBody3D *body = body_owner.get_or_null(p_body);
	ERR_FAIL_NULL_V(body, 0);
	return body->get_collision_priority();
}

void Box3DPhysicsServer3D::pin_joint_set_param(RID p_joint, PinJointParam p_param, real_t p_value) {
	Box3DJoint3D *joint = joint_owner.get_or_null(p_joint);
	ERR_FAIL_NULL(joint);
	joint->set_pin_param(p_param, p_value);
}

real_t Box3DPhysicsServer3D::pin_joint_get_param(RID p_joint, PinJointParam p_param) const {
	const Box3DJoint3D *joint = joint_owner.get_or_null(p_joint);
	ERR_FAIL_NULL_V(joint, 0);
	return joint->get_pin_param(p_param);
}

void Box3DPhysicsServer3D::pin_joint_set_local_a(RID p_joint, const Vector3 &p_local_a) {
	Box3DJoint3D *joint = joint_owner.get_or_null(p_joint);
	ERR_FAIL_NULL(joint);
	joint->set_pin_local_a(p_local_a);
}

Vector3 Box3DPhysicsServer3D::pin_joint_get_local_a(RID p_joint) const {
	const Box3DJoint3D *joint = joint_owner.get_or_null(p_joint);
	ERR_FAIL_NULL_V(joint, Vector3());
	return joint->get_pin_local_a();
}

void Box3DPhysicsServer3D::pin_joint_set_local_b(RID p_joint, const Vector3 &p_local_b) {
	Box3DJoint3D *joint = joint_owner.get_or_null(p_joint);
	ERR_FAIL_NULL(joint);
	joint->set_pin_local_b(p_local_b);
}

Vector3 Box3DPhysicsServer3D::pin_joint_get_local_b(RID p_joint) const {
	const Box3DJoint3D *joint = joint_owner.get_or_null(p_joint);
	ERR_FAIL_NULL_V(joint, Vector3());
	return joint->get_pin_local_b();
}

void Box3DPhysicsServer3D::hinge_joint_set_param(RID p_joint, HingeJointParam p_param, real_t p_value) {
	Box3DJoint3D *joint = joint_owner.get_or_null(p_joint);
	ERR_FAIL_NULL(joint);
	joint->set_hinge_param(p_param, p_value);
}

real_t Box3DPhysicsServer3D::hinge_joint_get_param(RID p_joint, HingeJointParam p_param) const {
	const Box3DJoint3D *joint = joint_owner.get_or_null(p_joint);
	ERR_FAIL_NULL_V(joint, 0);
	return joint->get_hinge_param(p_param);
}

void Box3DPhysicsServer3D::hinge_joint_set_flag(RID p_joint, HingeJointFlag p_flag, bool p_value) {
	Box3DJoint3D *joint = joint_owner.get_or_null(p_joint);
	ERR_FAIL_NULL(joint);
	joint->set_hinge_flag(p_flag, p_value);
}

bool Box3DPhysicsServer3D::hinge_joint_get_flag(RID p_joint, HingeJointFlag p_flag) const {
	const Box3DJoint3D *joint = joint_owner.get_or_null(p_joint);
	ERR_FAIL_NULL_V(joint, false);
	return joint->get_hinge_flag(p_flag);
}

void Box3DPhysicsServer3D::slider_joint_set_param(RID p_joint, SliderJointParam p_param, real_t p_value) {
	Box3DJoint3D *joint = joint_owner.get_or_null(p_joint);
	ERR_FAIL_NULL(joint);
	joint->set_slider_param(p_param, p_value);
}

real_t Box3DPhysicsServer3D::slider_joint_get_param(RID p_joint, SliderJointParam p_param) const {
	const Box3DJoint3D *joint = joint_owner.get_or_null(p_joint);
	ERR_FAIL_NULL_V(joint, 0);
	return joint->get_slider_param(p_param);
}

void Box3DPhysicsServer3D::cone_twist_joint_set_param(RID p_joint, ConeTwistJointParam p_param, real_t p_value) {
	Box3DJoint3D *joint = joint_owner.get_or_null(p_joint);
	ERR_FAIL_NULL(joint);
	joint->set_cone_twist_param(p_param, p_value);
}

real_t Box3DPhysicsServer3D::cone_twist_joint_get_param(RID p_joint, ConeTwistJointParam p_param) const {
	const Box3DJoint3D *joint = joint_owner.get_or_null(p_joint);
	ERR_FAIL_NULL_V(joint, 0);
	return joint->get_cone_twist_param(p_param);
}

/* AREAS */

RID Box3DPhysicsServer3D::area_create() {
	Box3DArea3D *area = memnew(Box3DArea3D);
	RID rid = area_owner.make_rid(area);
	area->set_self(rid);
	return rid;
}

void Box3DPhysicsServer3D::area_set_space(RID p_area, RID p_space) {
	Box3DArea3D *area = area_owner.get_or_null(p_area);
	ERR_FAIL_NULL(area);

	Box3DSpace3D *space = nullptr;
	if (p_space.is_valid()) {
		space = space_owner.get_or_null(p_space);
		ERR_FAIL_NULL(space);
	}
	area->set_space(space);
}

RID Box3DPhysicsServer3D::area_get_space(RID p_area) const {
	const Box3DArea3D *area = area_owner.get_or_null(p_area);
	ERR_FAIL_NULL_V(area, RID());
	const Box3DSpace3D *space = area->get_space();
	return space != nullptr ? space->get_self() : RID();
}

void Box3DPhysicsServer3D::area_add_shape(RID p_area, RID p_shape, const Transform3D &p_transform, bool p_disabled) {
	Box3DArea3D *area = area_owner.get_or_null(p_area);
	ERR_FAIL_NULL(area);
	Box3DShape3D *shape = shape_owner.get_or_null(p_shape);
	ERR_FAIL_NULL(shape);
	area->add_shape(shape, p_transform, p_disabled);
}

void Box3DPhysicsServer3D::area_set_shape(RID p_area, int p_shape_idx, RID p_shape) {
	Box3DArea3D *area = area_owner.get_or_null(p_area);
	ERR_FAIL_NULL(area);
	Box3DShape3D *shape = shape_owner.get_or_null(p_shape);
	ERR_FAIL_NULL(shape);
	area->set_shape(p_shape_idx, shape);
}

void Box3DPhysicsServer3D::area_set_shape_transform(RID p_area, int p_shape_idx, const Transform3D &p_transform) {
	Box3DArea3D *area = area_owner.get_or_null(p_area);
	ERR_FAIL_NULL(area);
	area->set_shape_transform(p_shape_idx, p_transform);
}

void Box3DPhysicsServer3D::area_set_shape_disabled(RID p_area, int p_shape_idx, bool p_disabled) {
	Box3DArea3D *area = area_owner.get_or_null(p_area);
	ERR_FAIL_NULL(area);
	area->set_shape_disabled(p_shape_idx, p_disabled);
}

int Box3DPhysicsServer3D::area_get_shape_count(RID p_area) const {
	const Box3DArea3D *area = area_owner.get_or_null(p_area);
	ERR_FAIL_NULL_V(area, 0);
	return area->get_shape_count();
}

RID Box3DPhysicsServer3D::area_get_shape(RID p_area, int p_shape_idx) const {
	const Box3DArea3D *area = area_owner.get_or_null(p_area);
	ERR_FAIL_NULL_V(area, RID());
	const Box3DShape3D *shape = area->get_shape(p_shape_idx);
	return shape != nullptr ? shape->get_self() : RID();
}

Transform3D Box3DPhysicsServer3D::area_get_shape_transform(RID p_area, int p_shape_idx) const {
	const Box3DArea3D *area = area_owner.get_or_null(p_area);
	ERR_FAIL_NULL_V(area, Transform3D());
	return area->get_shape_transform(p_shape_idx);
}

void Box3DPhysicsServer3D::area_remove_shape(RID p_area, int p_shape_idx) {
	Box3DArea3D *area = area_owner.get_or_null(p_area);
	ERR_FAIL_NULL(area);
	area->remove_shape(p_shape_idx);
}

void Box3DPhysicsServer3D::area_clear_shapes(RID p_area) {
	Box3DArea3D *area = area_owner.get_or_null(p_area);
	ERR_FAIL_NULL(area);
	area->clear_shapes();
}

void Box3DPhysicsServer3D::area_attach_object_instance_id(RID p_area, ObjectID p_id) {
	Box3DArea3D *area = area_owner.get_or_null(p_area);
	ERR_FAIL_NULL(area);
	area->set_instance_id(p_id);
}

ObjectID Box3DPhysicsServer3D::area_get_object_instance_id(RID p_area) const {
	const Box3DArea3D *area = area_owner.get_or_null(p_area);
	ERR_FAIL_NULL_V(area, ObjectID());
	return area->get_instance_id();
}

void Box3DPhysicsServer3D::area_set_param(RID p_area, AreaParameter p_param, const Variant &p_value) {
	// Godot addresses a space's default area by the space's own RID, which is how the
	// world's gravity arrives before any Area3D exists.
	if (Box3DSpace3D *space = space_owner.get_or_null(p_area)) {
		Box3DArea3D *area = space->get_default_area();
		ERR_FAIL_NULL(area);
		area->set_param(p_param, p_value);
		return;
	}

	Box3DArea3D *area = area_owner.get_or_null(p_area);
	ERR_FAIL_NULL(area);
	area->set_param(p_param, p_value);
}

Variant Box3DPhysicsServer3D::area_get_param(RID p_area, AreaParameter p_param) const {
	if (const Box3DSpace3D *space = space_owner.get_or_null(p_area)) {
		const Box3DArea3D *area = space->get_default_area();
		ERR_FAIL_NULL_V(area, Variant());
		return area->get_param(p_param);
	}

	const Box3DArea3D *area = area_owner.get_or_null(p_area);
	ERR_FAIL_NULL_V(area, Variant());
	return area->get_param(p_param);
}

void Box3DPhysicsServer3D::area_set_transform(RID p_area, const Transform3D &p_transform) {
	Box3DArea3D *area = area_owner.get_or_null(p_area);
	ERR_FAIL_NULL(area);
	area->set_transform(p_transform);
}

Transform3D Box3DPhysicsServer3D::area_get_transform(RID p_area) const {
	const Box3DArea3D *area = area_owner.get_or_null(p_area);
	ERR_FAIL_NULL_V(area, Transform3D());
	return area->get_transform();
}

void Box3DPhysicsServer3D::area_set_collision_layer(RID p_area, uint32_t p_layer) {
	Box3DArea3D *area = area_owner.get_or_null(p_area);
	ERR_FAIL_NULL(area);
	area->set_collision_layer(p_layer);
}

uint32_t Box3DPhysicsServer3D::area_get_collision_layer(RID p_area) const {
	const Box3DArea3D *area = area_owner.get_or_null(p_area);
	ERR_FAIL_NULL_V(area, 0);
	return area->get_collision_layer();
}

void Box3DPhysicsServer3D::area_set_collision_mask(RID p_area, uint32_t p_mask) {
	Box3DArea3D *area = area_owner.get_or_null(p_area);
	ERR_FAIL_NULL(area);
	area->set_collision_mask(p_mask);
}

uint32_t Box3DPhysicsServer3D::area_get_collision_mask(RID p_area) const {
	const Box3DArea3D *area = area_owner.get_or_null(p_area);
	ERR_FAIL_NULL_V(area, 0);
	return area->get_collision_mask();
}

void Box3DPhysicsServer3D::area_set_monitorable(RID p_area, bool p_monitorable) {
	Box3DArea3D *area = area_owner.get_or_null(p_area);
	ERR_FAIL_NULL(area);
	area->set_monitorable(p_monitorable);
}

void Box3DPhysicsServer3D::area_set_ray_pickable(RID p_area, bool p_enable) {
	Box3DArea3D *area = area_owner.get_or_null(p_area);
	ERR_FAIL_NULL(area);
	area->set_ray_pickable(p_enable);
}

void Box3DPhysicsServer3D::area_set_monitor_callback(RID p_area, const Callable &p_callback) {
	Box3DArea3D *area = area_owner.get_or_null(p_area);
	ERR_FAIL_NULL(area);
	area->set_monitor_callback(p_callback);
}

void Box3DPhysicsServer3D::area_set_area_monitor_callback(RID p_area, const Callable &p_callback) {
	Box3DArea3D *area = area_owner.get_or_null(p_area);
	ERR_FAIL_NULL(area);
	area->set_area_monitor_callback(p_callback);
}

Box3DPhysicsServer3D *Box3DPhysicsServer3D::singleton = nullptr;

Box3DPhysicsServer3D::Box3DPhysicsServer3D() {
	singleton = this;
}

Box3DPhysicsServer3D::~Box3DPhysicsServer3D() {
	if (singleton == this) {
		singleton = nullptr;
	}
}

// Collects the collision planes b3SolvePlanes needs for a capsule mover.
struct MoverPlanes {
	Box3DQueryContext filter;
	b3CollisionPlane planes[8];
	// The object each plane came from, so the reported collision can name a collider.
	// Without it Godot receives an empty RID and every get_slide_collision() lookup
	// asks the server for the state of a body that does not exist.
	Box3DCollisionObject3D *objects[8] = {};
	int count = 0;
};

static bool mover_plane_fcn(b3ShapeId p_shape, const b3PlaneResult *p_plane, int p_plane_count, void *p_context) {
	MoverPlanes *ctx = static_cast<MoverPlanes *>(p_context);
	Box3DCollisionObject3D *object = ctx->filter.resolve(p_shape);
	if (object == nullptr) {
		return true;
	}
	for (int i = 0; i < p_plane_count && ctx->count < 8; i++) {
		ctx->objects[ctx->count] = object;
		b3CollisionPlane &plane = ctx->planes[ctx->count++];
		plane.plane = p_plane[i].plane;
		// FLT_MAX makes the plane maximally rigid, which is what a character standing
		// on the ground wants; a soft plane would let it sink.
		plane.pushLimit = FLT_MAX;
		plane.push = 0.0f;
		plane.clipVelocity = true;
	}
	return ctx->count < 8;
}

// Capsule bodies go through Box3D's own character-mover path.
//
// A plain shape cast cannot serve move_and_slide on its own: Box3D reports a body that
// is already interpenetrating at fraction zero with a degenerate zero-length normal,
// which tells Godot nothing, and a character always ends up slightly interpenetrating
// after its first landing. Treating that as a blocking hit freezes the character in
// place; ignoring it lets the character tunnel through the floor. Neither is a bug in
// the cast - the missing piece is depenetration, and b3World_CollideMover plus
// b3SolvePlanes is precisely the primitive Box3D provides for it: the planes carry the
// push needed to separate, and the solver returns a delta that is both depenetrated
// and clipped against them.
//
// The mover is capsule-only, which is why this is a special case rather than the whole
// implementation. It covers CharacterBody3D as it is normally authored.
bool Box3DPhysicsServer3D::_test_motion_mover(Box3DBody3D *p_body, Box3DShape3D *p_shape,
		const MotionParameters &p_parameters, MotionResult *r_result) {
	Box3DSpace3D *space = p_body->get_space();

	const Transform3D shape_transform = p_parameters.from * p_body->get_shape_transform(0);
	const Dictionary data = p_shape->get_data();
	const real_t radius = data["radius"];
	const real_t height = data["height"];
	const real_t half_segment = MAX((real_t)0.0, height * 0.5 - radius);
	const Vector3 axis = shape_transform.basis.get_column(Vector3::AXIS_Y) * half_segment;

	b3Capsule capsule = {
		to_b3(shape_transform.origin + axis),
		to_b3(shape_transform.origin - axis),
		(float)radius,
	};

	// A mover is a free capsule rather than a shape on a body, so it would otherwise
	// collide with the character's own shapes and gather a degenerate self-plane.
	HashSet<RID> exclude;
	for (const RID &rid : p_parameters.exclude_bodies) {
		exclude.insert(rid);
	}
	exclude.insert(p_body->get_self());

	MoverPlanes ctx;
	ctx.filter.exclude = &exclude;
	ctx.filter.collide_with_bodies = true;
	ctx.filter.collide_with_areas = false;

	b3World_CollideMover(space->get_world_id(), to_b3_pos(Vector3()), &capsule,
			box3d_make_query_filter(p_body->get_collision_mask()), mover_plane_fcn, &ctx);

	if (ctx.count == 0) {
		return false;
	}

	const b3PlaneSolverResult solved = b3SolvePlanes(to_b3(p_parameters.motion), ctx.planes, ctx.count);
	const Vector3 travel = to_godot(solved.delta);

	// With nothing in the way the solver returns the motion unchanged, which is not a
	// collision however many planes were gathered.
	if (travel.is_equal_approx(p_parameters.motion)) {
		return false;
	}

	if (r_result != nullptr) {
		r_result->travel = travel;
		r_result->remainder = p_parameters.motion - travel;
		const real_t motion_length = p_parameters.motion.length();
		const real_t fraction = motion_length > 0 ? travel.length() / motion_length : 0;
		r_result->collision_safe_fraction = fraction;
		r_result->collision_unsafe_fraction = fraction;

		// Godot wants one representative contact, and the plane most opposed to the
		// motion is the one that actually stopped it - reporting any other would make
		// move_and_slide classify the surface wrongly and break is_on_floor().
		int best = 0;
		real_t best_dot = 1.0;
		for (int i = 0; i < ctx.count; i++) {
			const Vector3 normal = to_godot(ctx.planes[i].plane.normal);
			const real_t dot = normal.dot(p_parameters.motion.normalized());
			if (dot < best_dot) {
				best_dot = dot;
				best = i;
			}
		}

		MotionCollision &collision = r_result->collisions[0];
		collision.normal = to_godot(ctx.planes[best].plane.normal);
		collision.position = p_parameters.from.origin;
		collision.collider_velocity = ctx.objects[best]->is_area()
				? Vector3()
				: static_cast<Box3DBody3D *>(ctx.objects[best])->get_linear_velocity();
		collision.collider_angular_velocity = ctx.objects[best]->is_area()
				? Vector3()
				: static_cast<Box3DBody3D *>(ctx.objects[best])->get_angular_velocity();
		collision.depth = MAX((real_t)0.0, (real_t)ctx.planes[best].push);
		collision.local_shape = 0;
		collision.collider_id = ctx.objects[best]->get_instance_id();
		collision.collider = ctx.objects[best]->get_self();
		collision.collider_shape = 0;
		r_result->collision_count = 1;
	}
	return true;
}

bool Box3DPhysicsServer3D::body_test_motion(RID p_body, const MotionParameters &p_parameters, MotionResult *r_result) {
	Box3DBody3D *body = body_owner.get_or_null(p_body);
	ERR_FAIL_NULL_V(body, false);
	Box3DSpace3D *space = body->get_space();
	ERR_FAIL_NULL_V(space, false);

	if (r_result != nullptr) {
		*r_result = MotionResult();
		r_result->travel = p_parameters.motion;
		r_result->remainder = Vector3();
		r_result->collision_safe_fraction = 1.0;
		r_result->collision_unsafe_fraction = 1.0;
	}

	// This is what CharacterBody3D's move_and_slide and move_and_collide are built on:
	// sweep the body's own shapes along the motion and report the first thing hit. The
	// body's shapes are swept one at a time and the earliest impact wins, because
	// Box3D casts a single convex proxy per call and a Godot body may carry several.
	// A single-capsule body is the shape CharacterBody3D almost always has, and it is
	// the only shape Box3D's mover accepts.
	if (body->get_shape_count() == 1) {
		Box3DShape3D *shape = body->get_shape(0);
		if (shape != nullptr && shape->get_type() == Box3DShape3D::TYPE_CAPSULE) {
			return _test_motion_mover(body, shape, p_parameters, r_result);
		}
	}

	PhysicsDirectSpaceState3D::ShapeParameters params;
	params.transform = p_parameters.from;
	params.motion = p_parameters.motion;
	params.margin = p_parameters.margin;
	params.exclude = p_parameters.exclude_bodies;
	params.exclude.insert(p_body);
	params.collision_mask = body->get_collision_mask();
	params.collide_with_bodies = true;
	params.collide_with_areas = false;

	Box3DDirectSpaceState3D *state = space->get_direct_state();
	ERR_FAIL_NULL_V(state, false);

	bool hit = false;
	real_t best_fraction = 1.0;
	PhysicsDirectSpaceState3D::ShapeRestInfo best_info;

	for (int i = 0; i < body->get_shape_count(); i++) {
		Box3DShape3D *shape = body->get_shape(i);
		if (shape == nullptr || !shape->is_valid() || shape->is_static_only()) {
			continue;
		}
		params.shape_rid = shape->get_self();
		// The shape's own offset on the body has to ride along, or a multi-shape body
		// would sweep every shape from the body origin.
		params.transform = p_parameters.from * body->get_shape_transform(i);

		real_t safe = 1.0;
		real_t unsafe = 1.0;
		PhysicsDirectSpaceState3D::ShapeRestInfo info;
		if (!state->cast_motion(params, safe, unsafe, &info)) {
			continue;
		}
		if (!hit || safe < best_fraction) {
			hit = true;
			best_fraction = safe;
			best_info = info;
		}
	}

	if (!hit) {
		return false;
	}

	if (r_result != nullptr) {
		r_result->travel = p_parameters.motion * best_fraction;
		r_result->remainder = p_parameters.motion - r_result->travel;
		r_result->collision_safe_fraction = best_fraction;
		r_result->collision_unsafe_fraction = best_fraction;
		r_result->collision_depth = 0.0;

		MotionCollision &collision = r_result->collisions[0];
		collision.position = best_info.point;
		collision.normal = best_info.normal;
		collision.collider_velocity = best_info.linear_velocity;
		collision.collider_angular_velocity = Vector3();
		collision.depth = 0.0;
		collision.local_shape = 0;
		collision.collider_id = best_info.collider_id;
		collision.collider = best_info.rid;
		collision.collider_shape = best_info.shape;
		r_result->collision_count = 1;
	}
	return true;
}

/* BODY: CONTACTS, FORCES, LOCKS AND EXCEPTIONS */

void Box3DPhysicsServer3D::body_set_max_contacts_reported(RID p_body, int p_amount) {
	Box3DBody3D *body = body_owner.get_or_null(p_body);
	ERR_FAIL_NULL(body);
	body->set_max_contacts_reported(p_amount);
}

int Box3DPhysicsServer3D::body_get_max_contacts_reported(RID p_body) const {
	const Box3DBody3D *body = body_owner.get_or_null(p_body);
	ERR_FAIL_NULL_V(body, 0);
	return body->get_max_contacts_reported();
}

void Box3DPhysicsServer3D::body_set_contacts_reported_depth_threshold(RID p_body, real_t p_threshold) {
	Box3DBody3D *body = body_owner.get_or_null(p_body);
	ERR_FAIL_NULL(body);
	body->set_contact_depth_threshold(p_threshold);
}

real_t Box3DPhysicsServer3D::body_get_contacts_reported_depth_threshold(RID p_body) const {
	const Box3DBody3D *body = body_owner.get_or_null(p_body);
	ERR_FAIL_NULL_V(body, 0);
	return body->get_contact_depth_threshold();
}

void Box3DPhysicsServer3D::body_add_constant_central_force(RID p_body, const Vector3 &p_force) {
	Box3DBody3D *body = body_owner.get_or_null(p_body);
	ERR_FAIL_NULL(body);
	body->set_constant_force(body->get_constant_force() + p_force);
}

void Box3DPhysicsServer3D::body_add_constant_force(RID p_body, const Vector3 &p_force, const Vector3 &p_position) {
	Box3DBody3D *body = body_owner.get_or_null(p_body);
	ERR_FAIL_NULL(body);
	body->set_constant_force(body->get_constant_force() + p_force);
	const Vector3 arm = p_position - body->get_center_of_mass_local();
	body->set_constant_torque(body->get_constant_torque() + arm.cross(p_force));
}

void Box3DPhysicsServer3D::body_add_constant_torque(RID p_body, const Vector3 &p_torque) {
	Box3DBody3D *body = body_owner.get_or_null(p_body);
	ERR_FAIL_NULL(body);
	body->set_constant_torque(body->get_constant_torque() + p_torque);
}

void Box3DPhysicsServer3D::body_set_constant_force(RID p_body, const Vector3 &p_force) {
	Box3DBody3D *body = body_owner.get_or_null(p_body);
	ERR_FAIL_NULL(body);
	body->set_constant_force(p_force);
}

Vector3 Box3DPhysicsServer3D::body_get_constant_force(RID p_body) const {
	const Box3DBody3D *body = body_owner.get_or_null(p_body);
	ERR_FAIL_NULL_V(body, Vector3());
	return body->get_constant_force();
}

void Box3DPhysicsServer3D::body_set_constant_torque(RID p_body, const Vector3 &p_torque) {
	Box3DBody3D *body = body_owner.get_or_null(p_body);
	ERR_FAIL_NULL(body);
	body->set_constant_torque(p_torque);
}

Vector3 Box3DPhysicsServer3D::body_get_constant_torque(RID p_body) const {
	const Box3DBody3D *body = body_owner.get_or_null(p_body);
	ERR_FAIL_NULL_V(body, Vector3());
	return body->get_constant_torque();
}

void Box3DPhysicsServer3D::body_set_axis_velocity(RID p_body, const Vector3 &p_axis_velocity) {
	Box3DBody3D *body = body_owner.get_or_null(p_body);
	ERR_FAIL_NULL(body);
	// Replaces the component of the current velocity along the given axis and leaves
	// the rest alone - the jump-without-losing-run-speed primitive.
	const Vector3 axis = p_axis_velocity.normalized();
	Vector3 velocity = body->get_linear_velocity();
	velocity -= axis * axis.dot(velocity);
	velocity += p_axis_velocity;
	body->set_linear_velocity(velocity);
}

void Box3DPhysicsServer3D::body_set_axis_lock(RID p_body, BodyAxis p_axis, bool p_lock) {
	Box3DBody3D *body = body_owner.get_or_null(p_body);
	ERR_FAIL_NULL(body);
	body->set_axis_lock(p_axis, p_lock);
}

bool Box3DPhysicsServer3D::body_is_axis_locked(RID p_body, BodyAxis p_axis) const {
	const Box3DBody3D *body = body_owner.get_or_null(p_body);
	ERR_FAIL_NULL_V(body, false);
	return body->is_axis_locked(p_axis);
}

void Box3DPhysicsServer3D::body_add_collision_exception(RID p_body, RID p_body_b) {
	Box3DBody3D *body = body_owner.get_or_null(p_body);
	ERR_FAIL_NULL(body);
	body->add_collision_exception(p_body_b);
}

void Box3DPhysicsServer3D::body_remove_collision_exception(RID p_body, RID p_body_b) {
	Box3DBody3D *body = body_owner.get_or_null(p_body);
	ERR_FAIL_NULL(body);
	body->remove_collision_exception(p_body_b);
}

void Box3DPhysicsServer3D::body_get_collision_exceptions(RID p_body, List<RID> *p_exceptions) {
	const Box3DBody3D *body = body_owner.get_or_null(p_body);
	ERR_FAIL_NULL(body);
	ERR_FAIL_NULL(p_exceptions);
	for (const RID &rid : body->get_collision_exceptions()) {
		p_exceptions->push_back(rid);
	}
}

void Box3DPhysicsServer3D::body_reset_mass_properties(RID p_body) {
	Box3DBody3D *body = body_owner.get_or_null(p_body);
	ERR_FAIL_NULL(body);
	body->reset_mass_properties();
}
