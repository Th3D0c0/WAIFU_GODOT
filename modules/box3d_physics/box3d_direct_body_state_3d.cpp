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

Vector3 Box3DDirectBodyState3D::get_total_gravity() const {
	B3_TODO();
	return Vector3();
}

real_t Box3DDirectBodyState3D::get_total_angular_damp() const {
	B3_TODO();
	return 0;
}

real_t Box3DDirectBodyState3D::get_total_linear_damp() const {
	B3_TODO();
	return 0;
}

Vector3 Box3DDirectBodyState3D::get_center_of_mass() const {
	B3_TODO();
	return Vector3();
}

Vector3 Box3DDirectBodyState3D::get_center_of_mass_local() const {
	B3_TODO();
	return Vector3();
}

Basis Box3DDirectBodyState3D::get_principal_inertia_axes() const {
	B3_TODO();
	return Basis();
}

real_t Box3DDirectBodyState3D::get_inverse_mass() const {
	B3_TODO();
	return 0;
}

Vector3 Box3DDirectBodyState3D::get_inverse_inertia() const {
	B3_TODO();
	return Vector3();
}

Basis Box3DDirectBodyState3D::get_inverse_inertia_tensor() const {
	B3_TODO();
	return Basis();
}

void Box3DDirectBodyState3D::set_linear_velocity(const Vector3 &) {
	B3_TODO();
}

Vector3 Box3DDirectBodyState3D::get_linear_velocity() const {
	B3_TODO();
	return Vector3();
}

void Box3DDirectBodyState3D::set_angular_velocity(const Vector3 &) {
	B3_TODO();
}

Vector3 Box3DDirectBodyState3D::get_angular_velocity() const {
	B3_TODO();
	return Vector3();
}

void Box3DDirectBodyState3D::set_transform(const Transform3D &) {
	B3_TODO();
}

Transform3D Box3DDirectBodyState3D::get_transform() const {
	B3_TODO();
	return Transform3D();
}

Vector3 Box3DDirectBodyState3D::get_velocity_at_local_position(const Vector3 &) const {
	B3_TODO();
	return Vector3();
}

void Box3DDirectBodyState3D::apply_central_impulse(const Vector3 &) {
	B3_TODO();
}

void Box3DDirectBodyState3D::apply_impulse(const Vector3 &, const Vector3 &) {
	B3_TODO();
}

void Box3DDirectBodyState3D::apply_torque_impulse(const Vector3 &) {
	B3_TODO();
}

void Box3DDirectBodyState3D::apply_central_force(const Vector3 &) {
	B3_TODO();
}

void Box3DDirectBodyState3D::apply_force(const Vector3 &, const Vector3 &) {
	B3_TODO();
}

void Box3DDirectBodyState3D::apply_torque(const Vector3 &) {
	B3_TODO();
}

void Box3DDirectBodyState3D::add_constant_central_force(const Vector3 &) {
	B3_TODO();
}

void Box3DDirectBodyState3D::add_constant_force(const Vector3 &, const Vector3 &) {
	B3_TODO();
}

void Box3DDirectBodyState3D::add_constant_torque(const Vector3 &) {
	B3_TODO();
}

void Box3DDirectBodyState3D::set_constant_force(const Vector3 &) {
	B3_TODO();
}

Vector3 Box3DDirectBodyState3D::get_constant_force() const {
	B3_TODO();
	return Vector3();
}

void Box3DDirectBodyState3D::set_constant_torque(const Vector3 &) {
	B3_TODO();
}

Vector3 Box3DDirectBodyState3D::get_constant_torque() const {
	B3_TODO();
	return Vector3();
}

void Box3DDirectBodyState3D::set_sleep_state(bool) {
	B3_TODO();
}

bool Box3DDirectBodyState3D::is_sleeping() const {
	B3_TODO();
	return false;
}

void Box3DDirectBodyState3D::set_collision_layer(uint32_t) {
	B3_TODO();
}

uint32_t Box3DDirectBodyState3D::get_collision_layer() const {
	B3_TODO();
	return 0;
}

void Box3DDirectBodyState3D::set_collision_mask(uint32_t) {
	B3_TODO();
}

uint32_t Box3DDirectBodyState3D::get_collision_mask() const {
	B3_TODO();
	return 0;
}

int Box3DDirectBodyState3D::get_contact_count() const {
	B3_TODO();
	return 0;
}

Vector3 Box3DDirectBodyState3D::get_contact_local_position(int) const {
	B3_TODO();
	return Vector3();
}

Vector3 Box3DDirectBodyState3D::get_contact_local_normal(int) const {
	B3_TODO();
	return Vector3();
}

Vector3 Box3DDirectBodyState3D::get_contact_impulse(int) const {
	B3_TODO();
	return Vector3();
}

int Box3DDirectBodyState3D::get_contact_local_shape(int) const {
	B3_TODO();
	return 0;
}

Vector3 Box3DDirectBodyState3D::get_contact_local_velocity_at_position(int) const {
	B3_TODO();
	return Vector3();
}

RID Box3DDirectBodyState3D::get_contact_collider(int) const {
	B3_TODO();
	return RID();
}

Vector3 Box3DDirectBodyState3D::get_contact_collider_position(int) const {
	B3_TODO();
	return Vector3();
}

ObjectID Box3DDirectBodyState3D::get_contact_collider_id(int) const {
	B3_TODO();
	return ObjectID();
}

int Box3DDirectBodyState3D::get_contact_collider_shape(int) const {
	B3_TODO();
	return 0;
}

Vector3 Box3DDirectBodyState3D::get_contact_collider_velocity_at_position(int) const {
	B3_TODO();
	return Vector3();
}

real_t Box3DDirectBodyState3D::get_step() const {
	B3_TODO();
	return 0;
}

RequiredResult<PhysicsDirectSpaceState3D> Box3DDirectBodyState3D::get_space_state() {
	B3_TODO();
	return RequiredResult<PhysicsDirectSpaceState3D>();
}
