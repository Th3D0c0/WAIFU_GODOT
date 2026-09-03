/**************************************************************************/
/*  box3d_area_3d.cpp                                                     */
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

#include "box3d_area_3d.h"

#include "box3d_conversions.h"
#include "box3d_space_3d.h"

#include "core/error/error_macros.h"

void Box3DArea3D::_build() {
	ERR_FAIL_NULL(space);
	ERR_FAIL_COND(B3_IS_NON_NULL(body_id));

	b3BodyDef body_def = b3DefaultBodyDef();
	// Kinematic rather than static: an Area3D is routinely moved every frame (a grab
	// volume follows a hand), and Box3D expects a moving body to be kinematic so its
	// broad-phase proxy is refreshed. A static body that teleports would still work
	// but pays a full proxy rebuild each time.
	body_def.type = b3_kinematicBody;
	body_def.position = to_b3_pos(transform.origin);
	body_def.rotation = to_b3(transform.basis.get_rotation_quaternion());
	body_def.userData = static_cast<Box3DCollisionObject3D *>(this);
	// An area that stops being simulated stops reporting overlaps, and Godot expects
	// a stationary area to keep detecting things that move into it.
	body_def.enableSleep = false;

	body_id = b3CreateBody(space->get_world_id(), &body_def);
	ERR_FAIL_COND(!B3_IS_NON_NULL(body_id));

	_rebuild_shapes();
}

void Box3DArea3D::_destroy_shape_slot(ShapeSlot &p_slot) {
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

void Box3DArea3D::_destroy() {
	if (!B3_IS_NON_NULL(body_id)) {
		return;
	}
	b3DestroyBody(body_id);
	body_id = b3_nullBodyId;
	for (ShapeSlot &slot : shapes) {
		_destroy_shape_slot(slot);
	}
}

void Box3DArea3D::_rebuild_shapes() {
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

		b3ShapeDef shape_def = b3DefaultShapeDef();
		shape_def.isSensor = true;
		// False by default even for sensors (types.h:485-489), so this is what makes
		// the area report anything at all.
		shape_def.enableSensorEvents = true;
		// A sensor with mass would drag the kinematic body's inertia around for no
		// reason; areas are volumes, not matter.
		shape_def.density = 0.0f;
		shape_def.filter.categoryBits = collision_layer;
		shape_def.filter.maskBits = collision_mask;
		shape_def.userData = this;
		shape_def.updateBodyMass = false;

		slot.id = slot.shape->instantiate(body_id, shape_def, slot.transform, &slot.owned_hull, &slot.owned_mesh);
	}
}

void Box3DArea3D::set_space(Box3DSpace3D *p_space) {
	if (space == p_space) {
		return;
	}
	if (space != nullptr) {
		_destroy();
		space->remove_area(this);
	}
	space = p_space;
	if (space != nullptr) {
		space->add_area(this);
		_build();
	}
}

void Box3DArea3D::set_transform(const Transform3D &p_transform) {
	transform = p_transform;
	if (B3_IS_NON_NULL(body_id)) {
		b3Body_SetTransform(body_id, to_b3_pos(p_transform.origin), to_b3(p_transform.basis.get_rotation_quaternion()));
	}
}

void Box3DArea3D::set_param(PhysicsServer3D::AreaParameter p_param, const Variant &p_value) {
	params[(int)p_param] = p_value;

	// Godot gives every space a default area and routes the world's gravity through
	// it, so these two have to reach the world or a project's gravity setting never
	// arrives. Per-area gravity and damping overrides for bodies *inside* a non-default
	// area are a separate mechanism that is not implemented; those areas still report
	// overlaps correctly, they just do not alter the physics of what is inside them.
	if (space == nullptr || space->get_default_area() != this) {
		return;
	}
	switch (p_param) {
		case PhysicsServer3D::AREA_PARAM_GRAVITY_VECTOR:
			space->set_gravity_vector(p_value);
			break;
		case PhysicsServer3D::AREA_PARAM_GRAVITY:
			space->set_gravity_magnitude(p_value);
			break;
		default:
			break;
	}
}

Variant Box3DArea3D::get_param(PhysicsServer3D::AreaParameter p_param) const {
	const Variant *value = params.getptr((int)p_param);
	return value != nullptr ? *value : Variant();
}

void Box3DArea3D::set_collision_layer(uint32_t p_layer) {
	collision_layer = p_layer;
	_rebuild_shapes();
}

void Box3DArea3D::set_collision_mask(uint32_t p_mask) {
	collision_mask = p_mask;
	_rebuild_shapes();
}

void Box3DArea3D::set_monitorable(bool p_monitorable) {
	if (monitorable == p_monitorable) {
		return;
	}
	monitorable = p_monitorable;
	// Monitorability is about whether *other* areas may see this one. Box3D has no
	// such notion, so it is enforced when the overlap is reported rather than by
	// changing the shape - see Box3DSpace3D::_flush_sensor_events.
}

void Box3DArea3D::report_body(PhysicsServer3D::AreaBodyStatus p_status, const RID &p_rid, ObjectID p_instance, int p_body_shape, int p_area_shape) {
	if (!monitor_callback.is_valid()) {
		return;
	}
	const Variant args[5] = { p_status, p_rid, p_instance, p_body_shape, p_area_shape };
	const Variant *argp[5] = { &args[0], &args[1], &args[2], &args[3], &args[4] };
	Callable::CallError ce;
	Variant ret;
	monitor_callback.callp(argp, 5, ret, ce);
	if (ce.error != Callable::CallError::CALL_OK) {
		ERR_PRINT_ONCE("Box3D: error calling area monitor callback " + Variant::get_callable_error_text(monitor_callback, argp, 5, ce));
	}
}

void Box3DArea3D::report_area(PhysicsServer3D::AreaBodyStatus p_status, const RID &p_rid, ObjectID p_instance, int p_area_shape, int p_self_shape) {
	if (!area_monitor_callback.is_valid()) {
		return;
	}
	const Variant args[5] = { p_status, p_rid, p_instance, p_area_shape, p_self_shape };
	const Variant *argp[5] = { &args[0], &args[1], &args[2], &args[3], &args[4] };
	Callable::CallError ce;
	Variant ret;
	area_monitor_callback.callp(argp, 5, ret, ce);
	if (ce.error != Callable::CallError::CALL_OK) {
		ERR_PRINT_ONCE("Box3D: error calling area area-monitor callback " + Variant::get_callable_error_text(area_monitor_callback, argp, 5, ce));
	}
}

void Box3DArea3D::add_shape(Box3DShape3D *p_shape, const Transform3D &p_transform, bool p_disabled) {
	ShapeSlot slot;
	slot.shape = p_shape;
	slot.transform = p_transform;
	slot.disabled = p_disabled;
	shapes.push_back(slot);
	if (p_shape != nullptr) {
		p_shape->add_area_dependent(this);
	}
	_rebuild_shapes();
}

void Box3DArea3D::set_shape(int p_index, Box3DShape3D *p_shape) {
	ERR_FAIL_INDEX(p_index, (int)shapes.size());
	if (shapes[p_index].shape != nullptr) {
		shapes[p_index].shape->remove_area_dependent(this);
	}
	shapes[p_index].shape = p_shape;
	if (p_shape != nullptr) {
		p_shape->add_area_dependent(this);
	}
	_rebuild_shapes();
}

void Box3DArea3D::set_shape_transform(int p_index, const Transform3D &p_transform) {
	ERR_FAIL_INDEX(p_index, (int)shapes.size());
	shapes[p_index].transform = p_transform;
	_rebuild_shapes();
}

void Box3DArea3D::set_shape_disabled(int p_index, bool p_disabled) {
	ERR_FAIL_INDEX(p_index, (int)shapes.size());
	shapes[p_index].disabled = p_disabled;
	_rebuild_shapes();
}

void Box3DArea3D::remove_shape(int p_index) {
	ERR_FAIL_INDEX(p_index, (int)shapes.size());
	if (B3_IS_NON_NULL(shapes[p_index].id)) {
		b3DestroyShape(shapes[p_index].id, false);
	}
	_destroy_shape_slot(shapes[p_index]);
	if (shapes[p_index].shape != nullptr) {
		shapes[p_index].shape->remove_area_dependent(this);
	}
	shapes.remove_at(p_index);
	_rebuild_shapes();
}

void Box3DArea3D::clear_shapes() {
	while (!shapes.is_empty()) {
		remove_shape(shapes.size() - 1);
	}
}

Box3DShape3D *Box3DArea3D::get_shape(int p_index) const {
	ERR_FAIL_INDEX_V(p_index, (int)shapes.size(), nullptr);
	return shapes[p_index].shape;
}

Transform3D Box3DArea3D::get_shape_transform(int p_index) const {
	ERR_FAIL_INDEX_V(p_index, (int)shapes.size(), Transform3D());
	return shapes[p_index].transform;
}

int Box3DArea3D::find_shape_index(b3ShapeId p_id) const {
	for (uint32_t i = 0; i < shapes.size(); i++) {
		if (B3_ID_EQUALS(shapes[i].id, p_id)) {
			return (int)i;
		}
	}
	return 0;
}

void Box3DArea3D::shape_changed(Box3DShape3D *p_shape) {
	_rebuild_shapes();
}

Box3DArea3D::~Box3DArea3D() {
	_destroy();
	for (ShapeSlot &slot : shapes) {
		if (slot.shape != nullptr) {
			slot.shape->remove_area_dependent(this);
		}
	}
}
