/**************************************************************************/
/*  box3d_joint_3d.cpp                                                    */
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

#include "box3d_joint_3d.h"

#include "box3d_body_3d.h"
#include "box3d_conversions.h"
#include "box3d_space_3d.h"

#include "core/error/error_macros.h"

b3WorldId Box3DJoint3D::_resolve_world() const {
	// A constraint needs both endpoints resident in the same world. Godot allows one
	// side to be a null RID, meaning "pin to the world", but Box3D has no static world
	// body to attach to, so both bodies are required here.
	if (body_a == nullptr || body_b == nullptr) {
		return b3_nullWorldId;
	}
	const Box3DSpace3D *space_a = body_a->get_space();
	const Box3DSpace3D *space_b = body_b->get_space();
	if (space_a == nullptr || space_a != space_b) {
		return b3_nullWorldId;
	}
	if (!B3_IS_NON_NULL(body_a->get_body_id()) || !B3_IS_NON_NULL(body_b->get_body_id())) {
		return b3_nullWorldId;
	}
	return space_a->get_world_id();
}

void Box3DJoint3D::_destroy() {
	if (B3_IS_NON_NULL(joint_id)) {
		b3DestroyJoint(joint_id, true);
		joint_id = b3_nullJointId;
	}
}

real_t Box3DJoint3D::_collapse_max(const Vector3 &p_value) {
	return MAX(p_value.x, MAX(p_value.y, p_value.z));
}

real_t Box3DJoint3D::_driven_mass() const {
	// Pick whichever endpoint actually moves. If both are dynamic the lighter one
	// dominates the reduced mass, so it is the conservative choice for stability.
	real_t inertia = 0;
	for (const Box3DBody3D *body : { body_a, body_b }) {
		if (body == nullptr || body->get_mode() == PhysicsServer3D::BODY_MODE_STATIC ||
				body->get_mode() == PhysicsServer3D::BODY_MODE_KINEMATIC) {
			continue;
		}
		const real_t m = body->get_mass();
		if (m > 0 && (inertia == 0 || m < inertia)) {
			inertia = m;
		}
	}
	return inertia;
}

// Godot's spring damping is a coefficient - newton-seconds per meter for a linear
// axis, newton-meter-seconds per radian for an angular one - while Box3D's is the
// dimensionless damping ratio zeta, where 1 is critical. The two are related by
// c = 2 * zeta * sqrt(k * m), so the coefficient has to be divided back out against
// the same stiffness and mass the frequency was derived from.
//
// Handing the raw coefficient over as if it were a ratio is not a small error: a rig
// authored for critical damping carries values in the thousands, which as a ratio is
// an enormously overdamped spring that creeps toward its target over seconds instead
// of snapping to it. That looks like a spring that is too weak rather than one that is
// too damped, which is what makes it worth naming here.
real_t Box3DJoint3D::_damping_to_ratio(real_t p_damping, real_t p_stiffness) const {
	if (p_damping <= 0 || p_stiffness <= 0) {
		return 0;
	}
	const real_t inertia = _driven_mass();
	if (inertia <= 0) {
		return 0;
	}
	return p_damping / (2 * Math::sqrt(p_stiffness * inertia));
}

real_t Box3DJoint3D::_stiffness_to_hertz(real_t p_stiffness, bool p_angular) const {
	if (p_stiffness <= 0) {
		return 0;
	}
	const real_t inertia = _driven_mass();
	if (inertia <= 0) {
		return 0;
	}
	// For the angular case the exact relation needs the inertia tensor about the joint
	// axis, which is direction-dependent and not known here, so the mass stands in for
	// it - equivalent to assuming a unit radius of gyration. That keeps the frequency
	// in the right order of magnitude without being the true value, and is the main
	// reason a 6DOF angular spring will not feel identical to the same numbers on Jolt.
	// The linear case has no such caveat: f = sqrt(k/m) / 2*pi is exact.
	(void)p_angular;
	return Math::sqrt(p_stiffness / inertia) / (real_t)Math::TAU;
}

// Box3D has no standalone b3DefaultJointDef; every per-type default returns a struct
// with `base` already populated, so the shared fields are written into that in place
// rather than built separately and copied over.
void Box3DJoint3D::_fill_base(b3JointDef &r_base) const {
	r_base.bodyIdA = body_a->get_body_id();
	r_base.bodyIdB = body_b->get_body_id();
	r_base.localFrameA = to_b3(frame_a);
	r_base.localFrameB = to_b3(frame_b);
	r_base.collideConnected = collide_connected;
	r_base.userData = const_cast<Box3DJoint3D *>(this);
}

void Box3DJoint3D::_build_motor_joint(b3WorldId p_world) {
	b3MotorJointDef def = b3DefaultMotorJointDef();
	_fill_base(def.base);

	// The spring drives frame B onto frame A, so Godot's per-axis equilibrium point is
	// expressed by displacing frame A rather than by a separate target.
	def.base.localFrameA.p = to_b3(frame_a.origin + g6dof_linear_equilibrium);

	const bool linear_spring =
			g6dof_flags[0][PhysicsServer3D::G6DOF_JOINT_FLAG_ENABLE_LINEAR_SPRING] ||
			g6dof_flags[1][PhysicsServer3D::G6DOF_JOINT_FLAG_ENABLE_LINEAR_SPRING] ||
			g6dof_flags[2][PhysicsServer3D::G6DOF_JOINT_FLAG_ENABLE_LINEAR_SPRING];
	const bool angular_spring =
			g6dof_flags[0][PhysicsServer3D::G6DOF_JOINT_FLAG_ENABLE_ANGULAR_SPRING] ||
			g6dof_flags[1][PhysicsServer3D::G6DOF_JOINT_FLAG_ENABLE_ANGULAR_SPRING] ||
			g6dof_flags[2][PhysicsServer3D::G6DOF_JOINT_FLAG_ENABLE_ANGULAR_SPRING];

	if (linear_spring) {
		const real_t linear_stiffness = _collapse_max(g6dof_linear_spring_stiffness);
		def.linearHertz = (float)_stiffness_to_hertz(linear_stiffness, false);
		def.linearDampingRatio = (float)_damping_to_ratio(_collapse_max(g6dof_linear_spring_damping), linear_stiffness);
		def.maxSpringForce = FLT_MAX;
	} else {
		// Hertz zero is Box3D's "rigid" setting, which is what a 6DOF axis with no
		// spring and no limit should be: welded on that degree of freedom.
		def.linearHertz = 0.0f;
	}

	if (angular_spring) {
		const real_t angular_stiffness = _collapse_max(g6dof_angular_spring_stiffness);
		def.angularHertz = (float)_stiffness_to_hertz(angular_stiffness, true);
		def.angularDampingRatio = (float)_damping_to_ratio(_collapse_max(g6dof_angular_spring_damping), angular_stiffness);
		def.maxSpringTorque = FLT_MAX;
	} else {
		def.angularHertz = 0.0f;
	}

	if (g6dof_flags[0][PhysicsServer3D::G6DOF_JOINT_FLAG_ENABLE_LINEAR_MOTOR] ||
			g6dof_flags[1][PhysicsServer3D::G6DOF_JOINT_FLAG_ENABLE_LINEAR_MOTOR] ||
			g6dof_flags[2][PhysicsServer3D::G6DOF_JOINT_FLAG_ENABLE_LINEAR_MOTOR]) {
		def.linearVelocity = to_b3(g6dof_linear_motor_velocity);
		def.maxVelocityForce = (float)_collapse_max(g6dof_linear_motor_force_limit);
	}
	if (g6dof_flags[0][PhysicsServer3D::G6DOF_JOINT_FLAG_ENABLE_MOTOR] ||
			g6dof_flags[1][PhysicsServer3D::G6DOF_JOINT_FLAG_ENABLE_MOTOR] ||
			g6dof_flags[2][PhysicsServer3D::G6DOF_JOINT_FLAG_ENABLE_MOTOR]) {
		def.angularVelocity = to_b3(g6dof_angular_motor_velocity);
		def.maxVelocityTorque = (float)_collapse_max(g6dof_angular_motor_force_limit);
	}

	for (int axis = 0; axis < 3; axis++) {
		if (g6dof_flags[axis][PhysicsServer3D::G6DOF_JOINT_FLAG_ENABLE_LINEAR_LIMIT] ||
				g6dof_flags[axis][PhysicsServer3D::G6DOF_JOINT_FLAG_ENABLE_ANGULAR_LIMIT]) {
			WARN_PRINT_ONCE(
					"Box3D: Generic6DOFJoint3D per-axis limits are not supported. Box3D's motor joint "
					"constrains all axes together and has no limit range, so the limit is ignored and "
					"the axis behaves as an unlimited spring or a rigid weld. Use a ConeTwistJoint3D, "
					"HingeJoint3D or SliderJoint3D where the limit matters.");
			break;
		}
	}

	joint_id = b3CreateMotorJoint(p_world, &def);
}

// Godot's ConeTwistJoint3D twists about its frame's X axis - godot_cone_twist_joint_3d.cpp
// takes basis column 0 as the twist axis - while Box3D centers both the cone and the
// twist on the frame's Z axis (types.h:881,887). This rotation re-labels the frame so
// the old X becomes the new Z, which is what makes the two agree; without it a
// cone-twist limits the wrong axis and a ragdoll bends sideways.
static Transform3D cone_twist_frame(const Transform3D &p_frame) {
	Basis relabel;
	relabel.set_column(Vector3::AXIS_X, Vector3(0, 1, 0));
	relabel.set_column(Vector3::AXIS_Y, Vector3(0, 0, 1));
	relabel.set_column(Vector3::AXIS_Z, Vector3(1, 0, 0));
	return Transform3D(p_frame.basis * relabel, p_frame.origin);
}

void Box3DJoint3D::_build_spherical_joint(b3WorldId p_world, bool p_limited) {
	b3SphericalJointDef def = b3DefaultSphericalJointDef();
	_fill_base(def.base);

	if (p_limited) {
		def.base.localFrameA = to_b3(cone_twist_frame(frame_a));
		def.base.localFrameB = to_b3(cone_twist_frame(frame_b));

		// Godot's swing span is a half-angle measured from the twist axis, which is
		// exactly what Box3D's cone angle means, so the value carries over directly.
		def.enableConeLimit = true;
		def.coneAngle = (float)CLAMP(cone_swing_span, (real_t)0.0, (real_t)Math::PI);
		// A Godot twist span is symmetric about zero; Box3D takes an explicit range,
		// clamped to just inside +/-pi as its own documentation requires.
		const float twist = (float)CLAMP(cone_twist_span, (real_t)0.0, (real_t)(0.99 * Math::PI));
		def.enableTwistLimit = true;
		def.lowerTwistAngle = -twist;
		def.upperTwistAngle = twist;
	}

	joint_id = b3CreateSphericalJoint(p_world, &def);
}

void Box3DJoint3D::_build_revolute_joint(b3WorldId p_world) {
	b3RevoluteJointDef def = b3DefaultRevoluteJointDef();
	_fill_base(def.base);

	def.enableLimit = hinge_use_limit;
	def.lowerAngle = (float)hinge_lower;
	def.upperAngle = (float)hinge_upper;
	def.enableMotor = hinge_enable_motor;
	def.motorSpeed = (float)hinge_motor_velocity;
	// Godot's hinge motor budget is a max *impulse*; Box3D wants a max torque. They
	// differ by the timestep, which is not known here, so the value is passed through
	// unscaled - a hinge motor will be stronger or weaker than the same number under
	// GodotPhysics3D by a factor of the step.
	def.maxMotorTorque = (float)hinge_motor_max_impulse;

	joint_id = b3CreateRevoluteJoint(p_world, &def);
}

void Box3DJoint3D::_build_prismatic_joint(b3WorldId p_world) {
	b3PrismaticJointDef def = b3DefaultPrismaticJointDef();
	_fill_base(def.base);

	// A Godot slider is always limited; lower > upper is how it spells "free".
	def.enableLimit = slider_lower <= slider_upper;
	def.lowerTranslation = (float)slider_lower;
	def.upperTranslation = (float)slider_upper;

	joint_id = b3CreatePrismaticJoint(p_world, &def);
}

void Box3DJoint3D::_build() {
	_destroy();

	if (type == TYPE_NONE) {
		return;
	}
	const b3WorldId world = _resolve_world();
	if (!B3_IS_NON_NULL(world)) {
		// Not an error: Godot routinely configures a joint before its bodies enter a
		// space. bodies_changed() brings the constraint into being when they do.
		return;
	}

	switch (type) {
		case PhysicsServer3D::JOINT_TYPE_PIN:
			_build_spherical_joint(world, false);
			break;
		case PhysicsServer3D::JOINT_TYPE_CONE_TWIST:
			_build_spherical_joint(world, true);
			break;
		case PhysicsServer3D::JOINT_TYPE_HINGE:
			_build_revolute_joint(world);
			break;
		case PhysicsServer3D::JOINT_TYPE_SLIDER:
			_build_prismatic_joint(world);
			break;
		case PhysicsServer3D::JOINT_TYPE_6DOF:
			_build_motor_joint(world);
			break;
		default:
			break;
	}

	// Creating a constraint does not wake its bodies, and a sleeping body will not be
	// moved by a spring or a motor however strong it is. Anything that just gained a
	// constraint has had its situation changed, so it gets a chance to respond -
	// otherwise a drive attached to a resting body silently does nothing until
	// something unrelated happens to wake it, which reads as the joint being ignored.
	if (B3_IS_NON_NULL(joint_id)) {
		b3Joint_WakeBodies(joint_id);
	}
}

// Rebinding endpoints in one place keeps the bodies' joint registrations from
// drifting out of step with body_a/body_b.
void Box3DJoint3D::_set_bodies(Box3DBody3D *p_body_a, Box3DBody3D *p_body_b) {
	if (body_a != nullptr) {
		body_a->remove_joint(this);
	}
	if (body_b != nullptr) {
		body_b->remove_joint(this);
	}
	body_a = p_body_a;
	body_b = p_body_b;
	if (body_a != nullptr) {
		body_a->add_joint(this);
	}
	if (body_b != nullptr) {
		body_b->add_joint(this);
	}
}

void Box3DJoint3D::clear() {
	_destroy();
	type = TYPE_NONE;
	_set_bodies(nullptr, nullptr);
}

void Box3DJoint3D::make_pin(Box3DBody3D *p_body_a, const Vector3 &p_local_a, Box3DBody3D *p_body_b, const Vector3 &p_local_b) {
	type = PhysicsServer3D::JOINT_TYPE_PIN;
	_set_bodies(p_body_a, p_body_b);
	// A pin is a point constraint, so only the anchor translation is meaningful; an
	// unlimited spherical joint leaves all three rotations free, which is the same
	// degree-of-freedom set Godot's PinJoint3D describes.
	frame_a = Transform3D(Basis(), p_local_a);
	frame_b = Transform3D(Basis(), p_local_b);
	_build();
}

void Box3DJoint3D::make_hinge(Box3DBody3D *p_body_a, const Transform3D &p_frame_a, Box3DBody3D *p_body_b, const Transform3D &p_frame_b) {
	type = PhysicsServer3D::JOINT_TYPE_HINGE;
	_set_bodies(p_body_a, p_body_b);
	frame_a = p_frame_a;
	frame_b = p_frame_b;
	_build();
}

void Box3DJoint3D::make_slider(Box3DBody3D *p_body_a, const Transform3D &p_frame_a, Box3DBody3D *p_body_b, const Transform3D &p_frame_b) {
	type = PhysicsServer3D::JOINT_TYPE_SLIDER;
	_set_bodies(p_body_a, p_body_b);
	frame_a = p_frame_a;
	frame_b = p_frame_b;
	_build();
}

void Box3DJoint3D::make_cone_twist(Box3DBody3D *p_body_a, const Transform3D &p_frame_a, Box3DBody3D *p_body_b, const Transform3D &p_frame_b) {
	type = PhysicsServer3D::JOINT_TYPE_CONE_TWIST;
	_set_bodies(p_body_a, p_body_b);
	frame_a = p_frame_a;
	frame_b = p_frame_b;
	_build();
}

void Box3DJoint3D::make_generic_6dof(Box3DBody3D *p_body_a, const Transform3D &p_frame_a, Box3DBody3D *p_body_b, const Transform3D &p_frame_b) {
	type = PhysicsServer3D::JOINT_TYPE_6DOF;
	_set_bodies(p_body_a, p_body_b);
	frame_a = p_frame_a;
	frame_b = p_frame_b;
	_build();
}

void Box3DJoint3D::set_collide_connected(bool p_collide) {
	collide_connected = p_collide;
	if (B3_IS_NON_NULL(joint_id)) {
		b3Joint_SetCollideConnected(joint_id, p_collide);
	}
}

void Box3DJoint3D::bodies_changed() {
	_build();
}

/* PIN */

namespace {
// Godot's own defaults for the three pin parameters, from PinJoint3D. Reported back
// verbatim because nothing here is configurable.
constexpr real_t PIN_DEFAULT_BIAS = 0.3;
constexpr real_t PIN_DEFAULT_DAMPING = 1.0;
constexpr real_t PIN_DEFAULT_IMPULSE_CLAMP = 0.0;
} // namespace

void Box3DJoint3D::set_pin_local_a(const Vector3 &p_local_a) {
	frame_a = Transform3D(Basis(), p_local_a);
	// An anchor is baked into the constraint at creation, so moving one means building
	// a new constraint rather than adjusting the existing one.
	_build();
}

void Box3DJoint3D::set_pin_local_b(const Vector3 &p_local_b) {
	frame_b = Transform3D(Basis(), p_local_b);
	_build();
}

void Box3DJoint3D::set_pin_param(PhysicsServer3D::PinJointParam p_param, real_t p_value) {
	// Warn only when the value would actually mean something. Godot's PinJoint3D pushes
	// all three parameters on every joint it creates, so warning unconditionally would
	// fire on scenes that never touched them.
	switch (p_param) {
		case PhysicsServer3D::PIN_JOINT_BIAS: {
			if (!Math::is_equal_approx(p_value, PIN_DEFAULT_BIAS)) {
				WARN_PRINT_ONCE("Box3D: pin joint bias is not supported; the value is ignored.");
			}
		} break;
		case PhysicsServer3D::PIN_JOINT_DAMPING: {
			if (!Math::is_equal_approx(p_value, PIN_DEFAULT_DAMPING)) {
				WARN_PRINT_ONCE("Box3D: pin joint damping is not supported; the value is ignored.");
			}
		} break;
		case PhysicsServer3D::PIN_JOINT_IMPULSE_CLAMP: {
			if (!Math::is_equal_approx(p_value, PIN_DEFAULT_IMPULSE_CLAMP)) {
				WARN_PRINT_ONCE("Box3D: pin joint impulse clamp is not supported; the value is ignored.");
			}
		} break;
		default:
			break;
	}
}

real_t Box3DJoint3D::get_pin_param(PhysicsServer3D::PinJointParam p_param) const {
	switch (p_param) {
		case PhysicsServer3D::PIN_JOINT_BIAS:
			return PIN_DEFAULT_BIAS;
		case PhysicsServer3D::PIN_JOINT_DAMPING:
			return PIN_DEFAULT_DAMPING;
		case PhysicsServer3D::PIN_JOINT_IMPULSE_CLAMP:
			return PIN_DEFAULT_IMPULSE_CLAMP;
		default:
			return 0;
	}
}

/* GENERIC 6DOF */

void Box3DJoint3D::set_g6dof_param(Vector3::Axis p_axis, PhysicsServer3D::G6DOFJointAxisParam p_param, real_t p_value) {
	ERR_FAIL_INDEX((int)p_axis, 3);

	switch (p_param) {
		case PhysicsServer3D::G6DOF_JOINT_LINEAR_LOWER_LIMIT:
			g6dof_linear_lower[p_axis] = p_value;
			break;
		case PhysicsServer3D::G6DOF_JOINT_LINEAR_UPPER_LIMIT:
			g6dof_linear_upper[p_axis] = p_value;
			break;
		case PhysicsServer3D::G6DOF_JOINT_ANGULAR_LOWER_LIMIT:
			g6dof_angular_lower[p_axis] = p_value;
			break;
		case PhysicsServer3D::G6DOF_JOINT_ANGULAR_UPPER_LIMIT:
			g6dof_angular_upper[p_axis] = p_value;
			break;

		case PhysicsServer3D::G6DOF_JOINT_LINEAR_SPRING_STIFFNESS:
			g6dof_linear_spring_stiffness[p_axis] = p_value;
			if (B3_IS_NON_NULL(joint_id)) {
				b3MotorJoint_SetLinearHertz(joint_id, (float)_stiffness_to_hertz(_collapse_max(g6dof_linear_spring_stiffness), false));
			}
			break;
		case PhysicsServer3D::G6DOF_JOINT_LINEAR_SPRING_DAMPING:
			g6dof_linear_spring_damping[p_axis] = p_value;
			if (B3_IS_NON_NULL(joint_id)) {
				b3MotorJoint_SetLinearDampingRatio(joint_id, (float)_collapse_max(g6dof_linear_spring_damping));
			}
			break;
		case PhysicsServer3D::G6DOF_JOINT_ANGULAR_SPRING_STIFFNESS:
			g6dof_angular_spring_stiffness[p_axis] = p_value;
			if (B3_IS_NON_NULL(joint_id)) {
				b3MotorJoint_SetAngularHertz(joint_id, (float)_stiffness_to_hertz(_collapse_max(g6dof_angular_spring_stiffness), true));
			}
			break;
		case PhysicsServer3D::G6DOF_JOINT_ANGULAR_SPRING_DAMPING:
			g6dof_angular_spring_damping[p_axis] = p_value;
			if (B3_IS_NON_NULL(joint_id)) {
				b3MotorJoint_SetAngularDampingRatio(joint_id, (float)_collapse_max(g6dof_angular_spring_damping));
			}
			break;

		case PhysicsServer3D::G6DOF_JOINT_LINEAR_SPRING_EQUILIBRIUM_POINT:
			g6dof_linear_equilibrium[p_axis] = p_value;
			// The equilibrium lives in the joint frame, which is baked at build time.
			_build();
			break;
		case PhysicsServer3D::G6DOF_JOINT_ANGULAR_SPRING_EQUILIBRIUM_POINT:
			g6dof_angular_equilibrium[p_axis] = p_value;
			_build();
			break;

		case PhysicsServer3D::G6DOF_JOINT_LINEAR_MOTOR_TARGET_VELOCITY:
			g6dof_linear_motor_velocity[p_axis] = p_value;
			if (B3_IS_NON_NULL(joint_id)) {
				b3MotorJoint_SetLinearVelocity(joint_id, to_b3(g6dof_linear_motor_velocity));
			}
			break;
		case PhysicsServer3D::G6DOF_JOINT_LINEAR_MOTOR_FORCE_LIMIT:
			g6dof_linear_motor_force_limit[p_axis] = p_value;
			if (B3_IS_NON_NULL(joint_id)) {
				b3MotorJoint_SetMaxVelocityForce(joint_id, (float)_collapse_max(g6dof_linear_motor_force_limit));
			}
			break;
		case PhysicsServer3D::G6DOF_JOINT_ANGULAR_MOTOR_TARGET_VELOCITY:
			g6dof_angular_motor_velocity[p_axis] = p_value;
			if (B3_IS_NON_NULL(joint_id)) {
				b3MotorJoint_SetAngularVelocity(joint_id, to_b3(g6dof_angular_motor_velocity));
			}
			break;
		case PhysicsServer3D::G6DOF_JOINT_ANGULAR_MOTOR_FORCE_LIMIT:
			g6dof_angular_motor_force_limit[p_axis] = p_value;
			if (B3_IS_NON_NULL(joint_id)) {
				b3MotorJoint_SetMaxVelocityTorque(joint_id, (float)_collapse_max(g6dof_angular_motor_force_limit));
			}
			break;

		default:
			// Softness, restitution, ERP and the plain damping terms describe Bullet's
			// solver, which is what Godot's 6DOF was modeled on. Box3D's soft
			// constraints are parameterized by hertz and damping ratio instead, so
			// there is nothing faithful to map these onto.
			break;
	}
}

real_t Box3DJoint3D::get_g6dof_param(Vector3::Axis p_axis, PhysicsServer3D::G6DOFJointAxisParam p_param) const {
	ERR_FAIL_INDEX_V((int)p_axis, 3, 0);

	switch (p_param) {
		case PhysicsServer3D::G6DOF_JOINT_LINEAR_LOWER_LIMIT:
			return g6dof_linear_lower[p_axis];
		case PhysicsServer3D::G6DOF_JOINT_LINEAR_UPPER_LIMIT:
			return g6dof_linear_upper[p_axis];
		case PhysicsServer3D::G6DOF_JOINT_ANGULAR_LOWER_LIMIT:
			return g6dof_angular_lower[p_axis];
		case PhysicsServer3D::G6DOF_JOINT_ANGULAR_UPPER_LIMIT:
			return g6dof_angular_upper[p_axis];
		case PhysicsServer3D::G6DOF_JOINT_LINEAR_SPRING_STIFFNESS:
			return g6dof_linear_spring_stiffness[p_axis];
		case PhysicsServer3D::G6DOF_JOINT_LINEAR_SPRING_DAMPING:
			return g6dof_linear_spring_damping[p_axis];
		case PhysicsServer3D::G6DOF_JOINT_ANGULAR_SPRING_STIFFNESS:
			return g6dof_angular_spring_stiffness[p_axis];
		case PhysicsServer3D::G6DOF_JOINT_ANGULAR_SPRING_DAMPING:
			return g6dof_angular_spring_damping[p_axis];
		case PhysicsServer3D::G6DOF_JOINT_LINEAR_SPRING_EQUILIBRIUM_POINT:
			return g6dof_linear_equilibrium[p_axis];
		case PhysicsServer3D::G6DOF_JOINT_ANGULAR_SPRING_EQUILIBRIUM_POINT:
			return g6dof_angular_equilibrium[p_axis];
		case PhysicsServer3D::G6DOF_JOINT_LINEAR_MOTOR_TARGET_VELOCITY:
			return g6dof_linear_motor_velocity[p_axis];
		case PhysicsServer3D::G6DOF_JOINT_LINEAR_MOTOR_FORCE_LIMIT:
			return g6dof_linear_motor_force_limit[p_axis];
		case PhysicsServer3D::G6DOF_JOINT_ANGULAR_MOTOR_TARGET_VELOCITY:
			return g6dof_angular_motor_velocity[p_axis];
		case PhysicsServer3D::G6DOF_JOINT_ANGULAR_MOTOR_FORCE_LIMIT:
			return g6dof_angular_motor_force_limit[p_axis];
		default:
			return 0;
	}
}

void Box3DJoint3D::set_g6dof_flag(Vector3::Axis p_axis, PhysicsServer3D::G6DOFJointAxisFlag p_flag, bool p_enabled) {
	ERR_FAIL_INDEX((int)p_axis, 3);
	ERR_FAIL_INDEX((int)p_flag, PhysicsServer3D::G6DOF_JOINT_FLAG_MAX);

	if (g6dof_flags[p_axis][p_flag] == p_enabled) {
		return;
	}
	g6dof_flags[p_axis][p_flag] = p_enabled;
	// Every flag changes which terms the constraint has, and Box3D's motor joint has
	// no runtime "enable" toggles - a zero hertz means rigid, not disabled - so the
	// joint is rebuilt rather than patched.
	_build();
}

bool Box3DJoint3D::get_g6dof_flag(Vector3::Axis p_axis, PhysicsServer3D::G6DOFJointAxisFlag p_flag) const {
	ERR_FAIL_INDEX_V((int)p_axis, 3, false);
	ERR_FAIL_INDEX_V((int)p_flag, PhysicsServer3D::G6DOF_JOINT_FLAG_MAX, false);
	return g6dof_flags[p_axis][p_flag];
}

/* HINGE */

void Box3DJoint3D::set_hinge_param(PhysicsServer3D::HingeJointParam p_param, real_t p_value) {
	switch (p_param) {
		case PhysicsServer3D::HINGE_JOINT_LIMIT_LOWER:
			hinge_lower = p_value;
			if (B3_IS_NON_NULL(joint_id)) {
				b3RevoluteJoint_SetLimits(joint_id, (float)hinge_lower, (float)hinge_upper);
			}
			break;
		case PhysicsServer3D::HINGE_JOINT_LIMIT_UPPER:
			hinge_upper = p_value;
			if (B3_IS_NON_NULL(joint_id)) {
				b3RevoluteJoint_SetLimits(joint_id, (float)hinge_lower, (float)hinge_upper);
			}
			break;
		case PhysicsServer3D::HINGE_JOINT_MOTOR_TARGET_VELOCITY:
			hinge_motor_velocity = p_value;
			if (B3_IS_NON_NULL(joint_id)) {
				b3RevoluteJoint_SetMotorSpeed(joint_id, (float)p_value);
			}
			break;
		case PhysicsServer3D::HINGE_JOINT_MOTOR_MAX_IMPULSE:
			hinge_motor_max_impulse = p_value;
			if (B3_IS_NON_NULL(joint_id)) {
				b3RevoluteJoint_SetMaxMotorTorque(joint_id, (float)p_value);
			}
			break;
		default:
			// Bias, softness and relaxation are Bullet solver tuning with no Box3D
			// counterpart; see the note in set_g6dof_param.
			break;
	}
}

real_t Box3DJoint3D::get_hinge_param(PhysicsServer3D::HingeJointParam p_param) const {
	switch (p_param) {
		case PhysicsServer3D::HINGE_JOINT_LIMIT_LOWER:
			return hinge_lower;
		case PhysicsServer3D::HINGE_JOINT_LIMIT_UPPER:
			return hinge_upper;
		case PhysicsServer3D::HINGE_JOINT_MOTOR_TARGET_VELOCITY:
			return hinge_motor_velocity;
		case PhysicsServer3D::HINGE_JOINT_MOTOR_MAX_IMPULSE:
			return hinge_motor_max_impulse;
		default:
			return 0;
	}
}

void Box3DJoint3D::set_hinge_flag(PhysicsServer3D::HingeJointFlag p_flag, bool p_enabled) {
	switch (p_flag) {
		case PhysicsServer3D::HINGE_JOINT_FLAG_USE_LIMIT:
			hinge_use_limit = p_enabled;
			if (B3_IS_NON_NULL(joint_id)) {
				b3RevoluteJoint_EnableLimit(joint_id, p_enabled);
			}
			break;
		case PhysicsServer3D::HINGE_JOINT_FLAG_ENABLE_MOTOR:
			hinge_enable_motor = p_enabled;
			if (B3_IS_NON_NULL(joint_id)) {
				b3RevoluteJoint_EnableMotor(joint_id, p_enabled);
			}
			break;
		default:
			break;
	}
}

bool Box3DJoint3D::get_hinge_flag(PhysicsServer3D::HingeJointFlag p_flag) const {
	switch (p_flag) {
		case PhysicsServer3D::HINGE_JOINT_FLAG_USE_LIMIT:
			return hinge_use_limit;
		case PhysicsServer3D::HINGE_JOINT_FLAG_ENABLE_MOTOR:
			return hinge_enable_motor;
		default:
			return false;
	}
}

/* SLIDER */

void Box3DJoint3D::set_slider_param(PhysicsServer3D::SliderJointParam p_param, real_t p_value) {
	switch (p_param) {
		case PhysicsServer3D::SLIDER_JOINT_LINEAR_LIMIT_LOWER:
			slider_lower = p_value;
			if (B3_IS_NON_NULL(joint_id)) {
				b3PrismaticJoint_SetLimits(joint_id, (float)slider_lower, (float)slider_upper);
			}
			break;
		case PhysicsServer3D::SLIDER_JOINT_LINEAR_LIMIT_UPPER:
			slider_upper = p_value;
			if (B3_IS_NON_NULL(joint_id)) {
				b3PrismaticJoint_SetLimits(joint_id, (float)slider_lower, (float)slider_upper);
			}
			break;
		default:
			// Godot's slider carries 22 parameters, 20 of which are Bullet softness,
			// restitution and damping terms for the limit, the free motion and the
			// orthogonal directions. Box3D's prismatic joint holds the orthogonal
			// directions rigidly and has no such tuning, so only the two limits map.
			break;
	}
}

real_t Box3DJoint3D::get_slider_param(PhysicsServer3D::SliderJointParam p_param) const {
	switch (p_param) {
		case PhysicsServer3D::SLIDER_JOINT_LINEAR_LIMIT_LOWER:
			return slider_lower;
		case PhysicsServer3D::SLIDER_JOINT_LINEAR_LIMIT_UPPER:
			return slider_upper;
		default:
			return 0;
	}
}

/* CONE TWIST */

void Box3DJoint3D::set_cone_twist_param(PhysicsServer3D::ConeTwistJointParam p_param, real_t p_value) {
	switch (p_param) {
		case PhysicsServer3D::CONE_TWIST_JOINT_SWING_SPAN:
			cone_swing_span = p_value;
			if (B3_IS_NON_NULL(joint_id)) {
				b3SphericalJoint_SetConeLimit(joint_id, (float)CLAMP(p_value, (real_t)0.0, (real_t)Math::PI));
			}
			break;
		case PhysicsServer3D::CONE_TWIST_JOINT_TWIST_SPAN: {
			cone_twist_span = p_value;
			if (B3_IS_NON_NULL(joint_id)) {
				const float twist = (float)CLAMP(p_value, (real_t)0.0, (real_t)(0.99 * Math::PI));
				b3SphericalJoint_SetTwistLimits(joint_id, -twist, twist);
			}
		} break;
		default:
			break;
	}
}

real_t Box3DJoint3D::get_cone_twist_param(PhysicsServer3D::ConeTwistJointParam p_param) const {
	switch (p_param) {
		case PhysicsServer3D::CONE_TWIST_JOINT_SWING_SPAN:
			return cone_swing_span;
		case PhysicsServer3D::CONE_TWIST_JOINT_TWIST_SPAN:
			return cone_twist_span;
		default:
			return 0;
	}
}

Box3DJoint3D::~Box3DJoint3D() {
	_destroy();
	_set_bodies(nullptr, nullptr);
}
