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

#include "box3d_body_3d.h"
#include "box3d_conversions.h"
#include "box3d_direct_body_state_3d.h"
#include "box3d_direct_space_state_3d.h"
#include "box3d_shape_3d.h"
#include "box3d_space_3d.h"

#include "core/error/error_macros.h"

// Everything not defined here lives in box3d_physics_server_3d_todo.cpp, which is
// generated and shrinks as work moves into this file. See box3d_todo.h.

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
		shape_owner.free(p_rid);
		memdelete(shape);
		return;
	}

	if (Box3DBody3D *body = body_owner.get_or_null(p_rid)) {
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
