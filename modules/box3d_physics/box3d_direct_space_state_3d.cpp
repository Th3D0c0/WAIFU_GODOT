/**************************************************************************/
/*  box3d_direct_space_state_3d.cpp                                       */
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

#include "box3d_direct_space_state_3d.h"

#include "box3d_body_3d.h"
#include "box3d_conversions.h"
#include "box3d_space_3d.h"

#include "core/error/error_macros.h"
#include "core/object/object.h"

// The remaining query entry points are in box3d_direct_space_state_3d_todo.cpp.

bool Box3DDirectSpaceState3D::intersect_ray(const RayParameters &p_parameters, RayResult &r_result) {
	ERR_FAIL_NULL_V(space, false);
	const b3WorldId world = space->get_world_id();
	if (!B3_IS_NON_NULL(world)) {
		return false;
	}

	// Box3D casts a ray as an origin plus a translation, not as a second endpoint, so
	// the segment length is carried by the vector itself and a zero-length ray simply
	// has nothing to hit.
	const Vector3 translation = p_parameters.to - p_parameters.from;
	if (translation.is_zero_approx()) {
		return false;
	}

	b3QueryFilter filter = b3DefaultQueryFilter();
	// Godot's ray carries only a mask; it wants to hit anything whose layer intersects
	// it. Presenting the ray as belonging to every category is what makes the shape's
	// own maskBits irrelevant to the test, matching Godot's one-sided semantics.
	filter.categoryBits = UINT64_MAX;
	filter.maskBits = p_parameters.collision_mask;

	const b3RayResult hit = b3World_CastRayClosest(world, to_b3_pos(p_parameters.from), to_b3(translation), filter);
	if (!hit.hit) {
		return false;
	}

	// A shape knows its body through Box3D, and the body's userData is the wrapper
	// that owns the Godot-side identity.
	const b3BodyId hit_body = b3Shape_GetBody(hit.shapeId);
	Box3DBody3D *body = B3_IS_NON_NULL(hit_body) ? static_cast<Box3DBody3D *>(b3Body_GetUserData(hit_body)) : nullptr;
	if (body == nullptr) {
		return false;
	}

	// Godot filters by RID after the fact rather than during traversal, and pick rays
	// additionally honor the per-body pickable flag.
	if (p_parameters.exclude.has(body->get_self())) {
		return false;
	}
	if (p_parameters.pick_ray && !body->is_ray_pickable()) {
		return false;
	}

	r_result.position = to_godot(hit.point);
	r_result.normal = to_godot(hit.normal);
	r_result.rid = body->get_self();
	r_result.collider_id = body->get_instance_id();
	r_result.collider = r_result.collider_id.is_valid() ? ObjectDB::get_instance(r_result.collider_id) : nullptr;
	// Box3D reports which child shape was hit, not which of Godot's shape slots it
	// came from; the two indices coincide only for single-shape bodies, so this is
	// left at zero rather than reported as if it were the same thing.
	r_result.shape = 0;
	r_result.face_index = hit.triangleIndex;
	return true;
}
