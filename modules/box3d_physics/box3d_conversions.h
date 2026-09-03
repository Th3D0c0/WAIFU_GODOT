/**************************************************************************/
/*  box3d_conversions.h                                                   */
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

#include "core/math/basis.h"
#include "core/math/quaternion.h"
#include "core/math/transform_3d.h"
#include "core/math/vector3.h"

#include <box3d/box3d.h>

// Godot <-> Box3D type conversions.
//
// Box3D is float-only (b3Vec3 is three floats, b3Quat is a float vector plus a float
// scalar), while Godot's real_t is double in a precision=double build. Every conversion
// therefore goes through an explicit float cast rather than an implicit narrowing one,
// so a double build produces the same code without warnings.
//
// b3Pos is the world position type. In the default single-precision build it is a
// typedef of b3Vec3; under BOX3D_DOUBLE_PRECISION it becomes three doubles. Keeping
// the position conversions separate from the vector ones means enabling that flag is a
// change to these two functions and nothing else.

_FORCE_INLINE_ b3Vec3 to_b3(const Vector3 &p_vec) {
	return b3Vec3{ (float)p_vec.x, (float)p_vec.y, (float)p_vec.z };
}

_FORCE_INLINE_ Vector3 to_godot(const b3Vec3 &p_vec) {
	return Vector3((real_t)p_vec.x, (real_t)p_vec.y, (real_t)p_vec.z);
}

#if defined(BOX3D_DOUBLE_PRECISION)

// Under large-world mode b3Pos is three doubles and so a distinct type that needs its
// own overloads. In the default build it is a plain typedef of b3Vec3, and declaring
// these separately would redefine the b3Vec3 pair above.
_FORCE_INLINE_ b3Pos to_b3_pos(const Vector3 &p_vec) {
	return b3Pos{ (double)p_vec.x, (double)p_vec.y, (double)p_vec.z };
}

_FORCE_INLINE_ Vector3 to_godot(const b3Pos &p_pos) {
	return Vector3((real_t)p_pos.x, (real_t)p_pos.y, (real_t)p_pos.z);
}

#else

_FORCE_INLINE_ b3Pos to_b3_pos(const Vector3 &p_vec) {
	return to_b3(p_vec);
}

#endif

// Godot's Quaternion stores (x, y, z, w); Box3D splits it into a vector part and a
// scalar, so the mapping is w <-> s and xyz <-> v rather than a field-order shuffle.
_FORCE_INLINE_ b3Quat to_b3(const Quaternion &p_quat) {
	return b3Quat{ b3Vec3{ (float)p_quat.x, (float)p_quat.y, (float)p_quat.z }, (float)p_quat.w };
}

_FORCE_INLINE_ Quaternion to_godot(const b3Quat &p_quat) {
	return Quaternion((real_t)p_quat.v.x, (real_t)p_quat.v.y, (real_t)p_quat.v.z, (real_t)p_quat.s);
}

// Transform3D carries a full Basis, which can hold scale and shear that a rigid
// quaternion cannot. Callers that need the scale must pull it off the Basis before
// converting - the physics side only ever sees the orthonormalized rotation.
_FORCE_INLINE_ b3Transform to_b3(const Transform3D &p_transform) {
	return b3Transform{ to_b3(p_transform.origin), to_b3(p_transform.basis.get_rotation_quaternion()) };
}

_FORCE_INLINE_ b3WorldTransform to_b3_world(const Transform3D &p_transform) {
	return b3WorldTransform{ to_b3_pos(p_transform.origin), to_b3(p_transform.basis.get_rotation_quaternion()) };
}

_FORCE_INLINE_ Transform3D to_godot(const b3WorldTransform &p_transform) {
	return Transform3D(Basis(to_godot(p_transform.q)), to_godot(p_transform.p));
}
