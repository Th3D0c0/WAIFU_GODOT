/**************************************************************************/
/*  box3d_joint_3d.h                                                      */
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

#include "core/math/transform_3d.h"
#include "core/templates/rid.h"
#include "servers/physics_3d/physics_server_3d.h"

#include <box3d/box3d.h>

class Box3DBody3D;
class Box3DSpace3D;

// A Godot joint and the Box3D constraint backing it.
//
// Like Box3DBody3D, this is the authority and Box3D is the cache. Godot builds a
// joint in stages - joint_create(), then joint_make_*(), then a run of
// <type>_joint_set_param() calls - and the bodies involved may not be in a space at
// any given point, so the parameters are accumulated here and pushed into Box3D once
// a constraint can exist. Parameters that Box3D exposes a runtime setter for are also
// applied immediately; the rest are folded in on the next rebuild.
class Box3DJoint3D {
public:
	// Godot's JOINT_TYPE_MAX doubles as "created but not yet given a type", which is
	// the state joint_create() leaves a joint in and joint_clear() returns it to.
	static constexpr PhysicsServer3D::JointType TYPE_NONE = PhysicsServer3D::JOINT_TYPE_MAX;

private:
	RID self;
	PhysicsServer3D::JointType type = TYPE_NONE;

	Box3DBody3D *body_a = nullptr;
	Box3DBody3D *body_b = nullptr;
	Transform3D frame_a;
	Transform3D frame_b;

	bool collide_connected = true;
	int solver_priority = 1;

	b3JointId joint_id = b3_nullJointId;

	// --- Generic6DOF state, per Vector3::Axis ---------------------------------
	//
	// Godot drives a 6DOF joint one axis at a time, so all 18 parameters and 18 flags
	// are held per axis even though Box3D's motor joint takes whole-vector values.
	// _collapse_* below is where the two models are reconciled.
	Vector3 g6dof_linear_lower;
	Vector3 g6dof_linear_upper;
	Vector3 g6dof_angular_lower;
	Vector3 g6dof_angular_upper;
	Vector3 g6dof_linear_spring_stiffness;
	Vector3 g6dof_linear_spring_damping;
	Vector3 g6dof_linear_equilibrium;
	Vector3 g6dof_angular_spring_stiffness;
	Vector3 g6dof_angular_spring_damping;
	Vector3 g6dof_angular_equilibrium;
	Vector3 g6dof_linear_motor_velocity;
	Vector3 g6dof_linear_motor_force_limit;
	Vector3 g6dof_angular_motor_velocity;
	Vector3 g6dof_angular_motor_force_limit;
	bool g6dof_flags[3][PhysicsServer3D::G6DOF_JOINT_FLAG_MAX] = {};

	// --- Single-axis joint state ----------------------------------------------
	real_t hinge_lower = 0.0;
	real_t hinge_upper = 0.0;
	real_t hinge_motor_velocity = 0.0;
	real_t hinge_motor_max_impulse = 0.0;
	bool hinge_use_limit = false;
	bool hinge_enable_motor = false;

	real_t slider_lower = 0.0;
	real_t slider_upper = 0.0;

	real_t cone_swing_span = Math::PI / 4;
	real_t cone_twist_span = Math::PI;

	void _set_bodies(Box3DBody3D *p_body_a, Box3DBody3D *p_body_b);
	void _destroy();
	void _build();
	b3WorldId _resolve_world() const;

	void _fill_base(b3JointDef &r_base) const;
	void _build_motor_joint(b3WorldId p_world);
	void _build_spherical_joint(b3WorldId p_world, bool p_limited);
	void _build_revolute_joint(b3WorldId p_world);
	void _build_prismatic_joint(b3WorldId p_world);

	// Box3D's motor joint carries one scalar where Godot carries three. The stiffest
	// axis wins: a user who sets all three the same gets exactly what they asked for,
	// and a user who sets them differently gets the joint holding rather than sagging
	// on its softest axis, which is the less surprising failure.
	static real_t _collapse_max(const Vector3 &p_value);

	// The mass the spring is actually working against: the lighter dynamic endpoint.
	// Both conversions below need it, which is why neither can happen at set time.
	real_t _driven_mass() const;

	// Godot's spring parameter is a stiffness k, Box3D's is a frequency in hertz.
	// f = sqrt(k/m) / 2*pi is the exact relation, so the conversion needs the driven
	// body's mass - which is why it happens at build time rather than at set time.
	real_t _stiffness_to_hertz(real_t p_stiffness, bool p_angular) const;

	// Godot's damping is a coefficient, Box3D's is a damping ratio: zeta = c / 2*sqrt(k*m).
	real_t _damping_to_ratio(real_t p_damping, real_t p_stiffness) const;

public:
	void set_self(const RID &p_self) { self = p_self; }
	RID get_self() const { return self; }

	PhysicsServer3D::JointType get_type() const { return type; }
	b3JointId get_joint_id() const { return joint_id; }

	void clear();
	void make_pin(Box3DBody3D *p_body_a, const Vector3 &p_local_a, Box3DBody3D *p_body_b, const Vector3 &p_local_b);
	void make_hinge(Box3DBody3D *p_body_a, const Transform3D &p_frame_a, Box3DBody3D *p_body_b, const Transform3D &p_frame_b);
	void make_slider(Box3DBody3D *p_body_a, const Transform3D &p_frame_a, Box3DBody3D *p_body_b, const Transform3D &p_frame_b);
	void make_cone_twist(Box3DBody3D *p_body_a, const Transform3D &p_frame_a, Box3DBody3D *p_body_b, const Transform3D &p_frame_b);
	void make_generic_6dof(Box3DBody3D *p_body_a, const Transform3D &p_frame_a, Box3DBody3D *p_body_b, const Transform3D &p_frame_b);

	void set_collide_connected(bool p_collide);
	bool is_collide_connected() const { return collide_connected; }
	void set_solver_priority(int p_priority) { solver_priority = p_priority; }
	int get_solver_priority() const { return solver_priority; }

	// A pin's anchors are the whole of its geometry - there is no frame basis to
	// speak of - so these replace the translation and leave the identity basis alone.
	void set_pin_local_a(const Vector3 &p_local_a);
	Vector3 get_pin_local_a() const { return frame_a.origin; }
	void set_pin_local_b(const Vector3 &p_local_b);
	Vector3 get_pin_local_b() const { return frame_b.origin; }

	// Bias, damping and impulse clamp describe Bullet's sequential-impulse solver and
	// have no counterpart in Box3D's soft-constraint solver, so nothing is stored:
	// the getter reports Godot's default and the setter warns only when a value that
	// would actually change behavior is asked for. This is what the Jolt backend does
	// with the same three parameters.
	void set_pin_param(PhysicsServer3D::PinJointParam p_param, real_t p_value);
	real_t get_pin_param(PhysicsServer3D::PinJointParam p_param) const;

	void set_g6dof_param(Vector3::Axis p_axis, PhysicsServer3D::G6DOFJointAxisParam p_param, real_t p_value);
	real_t get_g6dof_param(Vector3::Axis p_axis, PhysicsServer3D::G6DOFJointAxisParam p_param) const;
	void set_g6dof_flag(Vector3::Axis p_axis, PhysicsServer3D::G6DOFJointAxisFlag p_flag, bool p_enabled);
	bool get_g6dof_flag(Vector3::Axis p_axis, PhysicsServer3D::G6DOFJointAxisFlag p_flag) const;

	void set_hinge_param(PhysicsServer3D::HingeJointParam p_param, real_t p_value);
	real_t get_hinge_param(PhysicsServer3D::HingeJointParam p_param) const;
	void set_hinge_flag(PhysicsServer3D::HingeJointFlag p_flag, bool p_enabled);
	bool get_hinge_flag(PhysicsServer3D::HingeJointFlag p_flag) const;

	void set_slider_param(PhysicsServer3D::SliderJointParam p_param, real_t p_value);
	real_t get_slider_param(PhysicsServer3D::SliderJointParam p_param) const;

	void set_cone_twist_param(PhysicsServer3D::ConeTwistJointParam p_param, real_t p_value);
	real_t get_cone_twist_param(PhysicsServer3D::ConeTwistJointParam p_param) const;

	// Called when one of the connected bodies enters or leaves a space, since the
	// constraint can only exist while both endpoints do.
	void bodies_changed();

	~Box3DJoint3D();
};
