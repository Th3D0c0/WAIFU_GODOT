/**************************************************************************/
/*  box3d_direct_body_state_3d.cpp                                        */
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

#include "box3d_direct_body_state_3d.h"

#include "box3d_body_3d.h"
#include "box3d_direct_space_state_3d.h"
#include "box3d_space_3d.h"

#include "core/error/error_macros.h"

// The remaining entry points are in box3d_direct_body_state_3d_todo.cpp.
//
// This object is a thin view onto a Box3DBody3D rather than a snapshot: Godot hands
// it to the state sync callback during flush_queries, at which point the step is over
// and reading straight through to Box3D gives the post-step values the scene wants.

Transform3D Box3DDirectBodyState3D::get_transform() const {
	ERR_FAIL_NULL_V(body, Transform3D());
	return body->get_transform();
}

void Box3DDirectBodyState3D::set_transform(const Transform3D &p_transform) {
	ERR_FAIL_NULL(body);
	body->set_transform(p_transform);
}

Vector3 Box3DDirectBodyState3D::get_linear_velocity() const {
	ERR_FAIL_NULL_V(body, Vector3());
	return body->get_linear_velocity();
}

void Box3DDirectBodyState3D::set_linear_velocity(const Vector3 &p_velocity) {
	ERR_FAIL_NULL(body);
	body->set_linear_velocity(p_velocity);
}

Vector3 Box3DDirectBodyState3D::get_angular_velocity() const {
	ERR_FAIL_NULL_V(body, Vector3());
	return body->get_angular_velocity();
}

void Box3DDirectBodyState3D::set_angular_velocity(const Vector3 &p_velocity) {
	ERR_FAIL_NULL(body);
	body->set_angular_velocity(p_velocity);
}

real_t Box3DDirectBodyState3D::get_inverse_mass() const {
	ERR_FAIL_NULL_V(body, 0);
	// Static and kinematic bodies are infinitely massive to the solver, which Godot
	// spells as an inverse mass of zero rather than as a division by infinity.
	if (body->get_mode() == PhysicsServer3D::BODY_MODE_STATIC || body->get_mode() == PhysicsServer3D::BODY_MODE_KINEMATIC) {
		return 0;
	}
	const real_t mass = body->get_mass();
	return mass > 0 ? 1 / mass : 0;
}

Vector3 Box3DDirectBodyState3D::get_total_gravity() const {
	ERR_FAIL_NULL_V(body, Vector3());
	const Box3DSpace3D *space = body->get_space();
	if (space == nullptr) {
		return Vector3();
	}
	// Areas can override gravity per-region in Godot, but this backend has no area
	// objects yet, so the space's gravity scaled by the body's own factor is the
	// whole story - which is exactly what Box3D applies during the step.
	return space->get_gravity_vector() * space->get_gravity_magnitude() * body->get_gravity_scale();
}

real_t Box3DDirectBodyState3D::get_total_linear_damp() const {
	ERR_FAIL_NULL_V(body, 0);
	return body->get_linear_damp();
}

real_t Box3DDirectBodyState3D::get_total_angular_damp() const {
	ERR_FAIL_NULL_V(body, 0);
	return body->get_angular_damp();
}

void Box3DDirectBodyState3D::apply_central_impulse(const Vector3 &p_impulse) {
	ERR_FAIL_NULL(body);
	body->apply_central_impulse(p_impulse);
}

void Box3DDirectBodyState3D::apply_impulse(const Vector3 &p_impulse, const Vector3 &p_position) {
	ERR_FAIL_NULL(body);
	body->apply_impulse(p_impulse, p_position);
}

void Box3DDirectBodyState3D::apply_torque_impulse(const Vector3 &p_impulse) {
	ERR_FAIL_NULL(body);
	body->apply_torque_impulse(p_impulse);
}

void Box3DDirectBodyState3D::apply_central_force(const Vector3 &p_force) {
	ERR_FAIL_NULL(body);
	body->apply_central_force(p_force);
}

void Box3DDirectBodyState3D::apply_force(const Vector3 &p_force, const Vector3 &p_position) {
	ERR_FAIL_NULL(body);
	body->apply_force(p_force, p_position);
}

void Box3DDirectBodyState3D::apply_torque(const Vector3 &p_torque) {
	ERR_FAIL_NULL(body);
	body->apply_torque(p_torque);
}

bool Box3DDirectBodyState3D::is_sleeping() const {
	ERR_FAIL_NULL_V(body, false);
	return body->is_sleeping();
}

void Box3DDirectBodyState3D::set_sleep_state(bool p_sleep) {
	ERR_FAIL_NULL(body);
	body->set_sleeping(p_sleep);
}

real_t Box3DDirectBodyState3D::get_step() const {
	ERR_FAIL_NULL_V(body, 0);
	const Box3DSpace3D *space = body->get_space();
	return space != nullptr ? space->get_last_step() : 0;
}

RequiredResult<PhysicsDirectSpaceState3D> Box3DDirectBodyState3D::get_space_state() {
	ERR_FAIL_NULL_V(body, nullptr);
	Box3DSpace3D *space = body->get_space();
	ERR_FAIL_NULL_V(space, nullptr);
	return space->get_direct_state();
}

Vector3 Box3DDirectBodyState3D::get_center_of_mass() const {
	ERR_FAIL_NULL_V(body, Vector3());
	// Godot's "center of mass" on the state is the offset from the body origin; the
	// world position is get_center_of_mass() on the node, not here.
	return body->get_center_of_mass() - body->get_transform().origin;
}

Vector3 Box3DDirectBodyState3D::get_center_of_mass_local() const {
	ERR_FAIL_NULL_V(body, Vector3());
	return body->get_center_of_mass_local();
}

Basis Box3DDirectBodyState3D::get_principal_inertia_axes() const {
	ERR_FAIL_NULL_V(body, Basis());
	// Box3D keeps a full inertia tensor rather than diagonalising it, so there is no
	// stored principal frame. Identity says "the tensor is already expressed in the
	// body frame", which is what the tensor accessors below return it in.
	return Basis();
}

Vector3 Box3DDirectBodyState3D::get_inverse_inertia() const {
	ERR_FAIL_NULL_V(body, Vector3());
	const Basis inverse = body->get_inverse_inertia_tensor();
	// The diagonal is the inverse inertia about each body axis, which is what Godot
	// means by the vector form when the tensor is expressed in the body frame.
	return Vector3(inverse[0][0], inverse[1][1], inverse[2][2]);
}

Basis Box3DDirectBodyState3D::get_inverse_inertia_tensor() const {
	ERR_FAIL_NULL_V(body, Basis());
	return body->get_inverse_inertia_tensor();
}

Vector3 Box3DDirectBodyState3D::get_velocity_at_local_position(const Vector3 &p_position) const {
	ERR_FAIL_NULL_V(body, Vector3());
	return body->get_velocity_at_local_position(p_position);
}

void Box3DDirectBodyState3D::add_constant_central_force(const Vector3 &p_force) {
	ERR_FAIL_NULL(body);
	body->set_constant_force(body->get_constant_force() + p_force);
}

void Box3DDirectBodyState3D::add_constant_force(const Vector3 &p_force, const Vector3 &p_position) {
	ERR_FAIL_NULL(body);
	body->set_constant_force(body->get_constant_force() + p_force);
	// A force applied off-center also produces a torque about the center of mass.
	const Vector3 arm = p_position - body->get_center_of_mass_local();
	body->set_constant_torque(body->get_constant_torque() + arm.cross(p_force));
}

void Box3DDirectBodyState3D::add_constant_torque(const Vector3 &p_torque) {
	ERR_FAIL_NULL(body);
	body->set_constant_torque(body->get_constant_torque() + p_torque);
}

void Box3DDirectBodyState3D::set_constant_force(const Vector3 &p_force) {
	ERR_FAIL_NULL(body);
	body->set_constant_force(p_force);
}

Vector3 Box3DDirectBodyState3D::get_constant_force() const {
	ERR_FAIL_NULL_V(body, Vector3());
	return body->get_constant_force();
}

void Box3DDirectBodyState3D::set_constant_torque(const Vector3 &p_torque) {
	ERR_FAIL_NULL(body);
	body->set_constant_torque(p_torque);
}

Vector3 Box3DDirectBodyState3D::get_constant_torque() const {
	ERR_FAIL_NULL_V(body, Vector3());
	return body->get_constant_torque();
}

void Box3DDirectBodyState3D::set_collision_layer(uint32_t p_layer) {
	ERR_FAIL_NULL(body);
	body->set_collision_layer(p_layer);
}

uint32_t Box3DDirectBodyState3D::get_collision_layer() const {
	ERR_FAIL_NULL_V(body, 0);
	return body->get_collision_layer();
}

void Box3DDirectBodyState3D::set_collision_mask(uint32_t p_mask) {
	ERR_FAIL_NULL(body);
	body->set_collision_mask(p_mask);
}

uint32_t Box3DDirectBodyState3D::get_collision_mask() const {
	ERR_FAIL_NULL_V(body, 0);
	return body->get_collision_mask();
}

/* CONTACTS */
//
// Godot asks for contacts one accessor at a time, all against the same index, so the
// list is resolved once by get_contacts() and every accessor below indexes into it.

int Box3DDirectBodyState3D::get_contact_count() const {
	ERR_FAIL_NULL_V(body, 0);
	return (int)body->get_contacts().size();
}

Vector3 Box3DDirectBodyState3D::get_contact_local_position(int p_contact_idx) const {
	ERR_FAIL_NULL_V(body, Vector3());
	const LocalVector<Box3DBody3D::Contact> &list = body->get_contacts();
	ERR_FAIL_INDEX_V(p_contact_idx, (int)list.size(), Vector3());
	return list[p_contact_idx].local_position;
}

Vector3 Box3DDirectBodyState3D::get_contact_local_normal(int p_contact_idx) const {
	ERR_FAIL_NULL_V(body, Vector3());
	const LocalVector<Box3DBody3D::Contact> &list = body->get_contacts();
	ERR_FAIL_INDEX_V(p_contact_idx, (int)list.size(), Vector3());
	return list[p_contact_idx].local_normal;
}

Vector3 Box3DDirectBodyState3D::get_contact_impulse(int p_contact_idx) const {
	ERR_FAIL_NULL_V(body, Vector3());
	const LocalVector<Box3DBody3D::Contact> &list = body->get_contacts();
	ERR_FAIL_INDEX_V(p_contact_idx, (int)list.size(), Vector3());
	return list[p_contact_idx].impulse;
}

int Box3DDirectBodyState3D::get_contact_local_shape(int p_contact_idx) const {
	ERR_FAIL_NULL_V(body, 0);
	const LocalVector<Box3DBody3D::Contact> &list = body->get_contacts();
	ERR_FAIL_INDEX_V(p_contact_idx, (int)list.size(), 0);
	return list[p_contact_idx].local_shape;
}

Vector3 Box3DDirectBodyState3D::get_contact_local_velocity_at_position(int p_contact_idx) const {
	ERR_FAIL_NULL_V(body, Vector3());
	const LocalVector<Box3DBody3D::Contact> &list = body->get_contacts();
	ERR_FAIL_INDEX_V(p_contact_idx, (int)list.size(), Vector3());
	return body->get_velocity_at_local_position(list[p_contact_idx].local_position);
}

RID Box3DDirectBodyState3D::get_contact_collider(int p_contact_idx) const {
	ERR_FAIL_NULL_V(body, RID());
	const LocalVector<Box3DBody3D::Contact> &list = body->get_contacts();
	ERR_FAIL_INDEX_V(p_contact_idx, (int)list.size(), RID());
	return list[p_contact_idx].collider;
}

Vector3 Box3DDirectBodyState3D::get_contact_collider_position(int p_contact_idx) const {
	ERR_FAIL_NULL_V(body, Vector3());
	const LocalVector<Box3DBody3D::Contact> &list = body->get_contacts();
	ERR_FAIL_INDEX_V(p_contact_idx, (int)list.size(), Vector3());
	return list[p_contact_idx].collider_position;
}

ObjectID Box3DDirectBodyState3D::get_contact_collider_id(int p_contact_idx) const {
	ERR_FAIL_NULL_V(body, ObjectID());
	const LocalVector<Box3DBody3D::Contact> &list = body->get_contacts();
	ERR_FAIL_INDEX_V(p_contact_idx, (int)list.size(), ObjectID());
	return list[p_contact_idx].collider_id;
}

int Box3DDirectBodyState3D::get_contact_collider_shape(int p_contact_idx) const {
	ERR_FAIL_NULL_V(body, 0);
	const LocalVector<Box3DBody3D::Contact> &list = body->get_contacts();
	ERR_FAIL_INDEX_V(p_contact_idx, (int)list.size(), 0);
	return list[p_contact_idx].collider_shape;
}

Vector3 Box3DDirectBodyState3D::get_contact_collider_velocity_at_position(int p_contact_idx) const {
	ERR_FAIL_NULL_V(body, Vector3());
	const LocalVector<Box3DBody3D::Contact> &list = body->get_contacts();
	ERR_FAIL_INDEX_V(p_contact_idx, (int)list.size(), Vector3());
	return list[p_contact_idx].collider_velocity_at_position;
}
