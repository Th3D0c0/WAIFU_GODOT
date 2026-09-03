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

#include "box3d_area_3d.h"
#include "box3d_body_3d.h"
#include "box3d_conversions.h"
#include "box3d_physics_server_3d.h"
#include "box3d_query_3d.h"
#include "box3d_shape_3d.h"
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
	Box3DCollisionObject3D *object = B3_IS_NON_NULL(hit_body)
			? static_cast<Box3DCollisionObject3D *>(b3Body_GetUserData(hit_body))
			: nullptr;
	// Godot's ray defaults to bodies only, and areas share the same userData slot, so
	// the tag decides whether this hit is eligible at all.
	if (object == nullptr) {
		return false;
	}
	if (object->is_area() && !p_parameters.collide_with_areas) {
		return false;
	}
	if (!object->is_area() && !p_parameters.collide_with_bodies) {
		return false;
	}
	Box3DBody3D *body = object->is_area() ? nullptr : static_cast<Box3DBody3D *>(object);

	// Godot filters by RID after the fact rather than during traversal, and pick rays
	// additionally honor the per-body pickable flag.
	if (p_parameters.exclude.has(object->get_self())) {
		return false;
	}
	if (p_parameters.pick_ray && body != nullptr && !body->is_ray_pickable()) {
		return false;
	}

	r_result.position = to_godot(hit.point);
	r_result.normal = to_godot(hit.normal);
	r_result.rid = object->get_self();
	r_result.collider_id = object->get_instance_id();
	r_result.collider = r_result.collider_id.is_valid() ? ObjectDB::get_instance(r_result.collider_id) : nullptr;
	// Box3D reports which child shape was hit, not which of Godot's shape slots it
	// came from; the two indices coincide only for single-shape bodies, so this is
	// left at zero rather than reported as if it were the same thing.
	r_result.shape = 0;
	r_result.face_index = hit.triangleIndex;
	return true;
}

// Resolves a Godot ShapeParameters into the Box3D proxy the overlap and cast entry
// points take. Returns false when the shape has no convex proxy, which is not an
// error - Godot allows a concave shape RID here and simply cannot query with it.
bool Box3DDirectSpaceState3D::_make_proxy(const ShapeParameters &p_parameters, LocalVector<b3Vec3> &r_points,
		b3ShapeProxy &r_proxy, bool p_inflate_by_margin) const {
	Box3DPhysicsServer3D *server = Box3DPhysicsServer3D::get_singleton();
	ERR_FAIL_NULL_V(server, false);
	const Box3DShape3D *shape = server->get_shape(p_parameters.shape_rid);
	ERR_FAIL_NULL_V(shape, false);

	// The margin grows the shape for an overlap test, where it is a tolerance, and
	// shrinks it for a sweep, where it is the skin that keeps resting contact from
	// registering as an initial overlap.
	const real_t inset = p_inflate_by_margin ? p_parameters.margin : -p_parameters.margin;

	float radius = 0.0f;
	if (!shape->build_proxy(p_parameters.transform, inset, r_points, radius)) {
		return false;
	}
	r_proxy.points = r_points.ptr();
	r_proxy.count = (int)r_points.size();
	r_proxy.radius = radius;
	return true;
}

struct OverlapCollect {
	Box3DQueryContext filter;
	PhysicsDirectSpaceState3D::ShapeResult *results = nullptr;
	int max = 0;
	int count = 0;
	// Godot reports one result per collider, while Box3D reports one per shape, so a
	// multi-shape body would otherwise fill the caller's buffer with duplicates.
	HashSet<RID> seen;
};

static bool overlap_collect_fcn(b3ShapeId p_shape, void *p_context) {
	OverlapCollect *ctx = static_cast<OverlapCollect *>(p_context);
	Box3DCollisionObject3D *object = ctx->filter.resolve(p_shape);
	if (object == nullptr) {
		return true;
	}
	if (ctx->seen.has(object->get_self())) {
		return true;
	}
	ctx->seen.insert(object->get_self());

	PhysicsDirectSpaceState3D::ShapeResult &result = ctx->results[ctx->count];
	result.rid = object->get_self();
	result.collider_id = object->get_instance_id();
	result.collider = result.collider_id.is_valid() ? ObjectDB::get_instance(result.collider_id) : nullptr;
	result.shape = 0;
	ctx->count++;
	// Returning false stops the traversal, which is what a full buffer calls for.
	return ctx->count < ctx->max;
}

int Box3DDirectSpaceState3D::intersect_shape(const ShapeParameters &p_parameters, ShapeResult *r_results, int p_result_max) {
	ERR_FAIL_NULL_V(space, 0);
	if (p_result_max <= 0) {
		return 0;
	}

	LocalVector<b3Vec3> points;
	b3ShapeProxy proxy = {};
	if (!_make_proxy(p_parameters, points, proxy, true)) {
		return 0;
	}

	OverlapCollect ctx;
	ctx.filter.exclude = &p_parameters.exclude;
	ctx.filter.collide_with_bodies = p_parameters.collide_with_bodies;
	ctx.filter.collide_with_areas = p_parameters.collide_with_areas;
	ctx.results = r_results;
	ctx.max = p_result_max;

	// The proxy points are already in world space, so the query origin is the world
	// origin - see the note on b3World_OverlapShape.
	b3World_OverlapShape(space->get_world_id(), to_b3_pos(Vector3()), &proxy,
			box3d_make_query_filter(p_parameters.collision_mask), overlap_collect_fcn, &ctx);
	return ctx.count;
}

int Box3DDirectSpaceState3D::intersect_point(const PointParameters &p_parameters, ShapeResult *r_results, int p_result_max) {
	ERR_FAIL_NULL_V(space, 0);
	if (p_result_max <= 0) {
		return 0;
	}

	// A point is a zero-radius one-point proxy; Box3D needs no special case for it.
	const b3Vec3 point = to_b3(p_parameters.position);
	b3ShapeProxy proxy = {};
	proxy.points = &point;
	proxy.count = 1;
	proxy.radius = 0.0f;

	OverlapCollect ctx;
	ctx.filter.exclude = &p_parameters.exclude;
	ctx.filter.collide_with_bodies = p_parameters.collide_with_bodies;
	ctx.filter.collide_with_areas = p_parameters.collide_with_areas;
	ctx.results = r_results;
	ctx.max = p_result_max;

	b3World_OverlapShape(space->get_world_id(), to_b3_pos(Vector3()), &proxy,
			box3d_make_query_filter(p_parameters.collision_mask), overlap_collect_fcn, &ctx);
	return ctx.count;
}

struct CastClosest {
	Box3DQueryContext filter;
	float fraction = 1.0f;
	bool hit = false;
	b3ShapeId shape = b3_nullShapeId;
	Box3DCollisionObject3D *object = nullptr;
	b3Pos point = {};
	b3Vec3 normal = {};
};

static float cast_closest_fcn(b3ShapeId p_shape, b3Pos p_point, b3Vec3 p_normal, float p_fraction,
		uint64_t p_material, int p_triangle_index, int p_child_index, void *p_context) {
	CastClosest *ctx = static_cast<CastClosest *>(p_context);
	Box3DCollisionObject3D *object = ctx->filter.resolve(p_shape);
	if (object == nullptr) {
		// -1 is Box3D's "filter this hit" return (types.h:112): the traversal carries
		// on and this shape neither records a hit nor clips the cast.
		return -1.0f;
	}

	// A shape already interpenetrating at t=0 is reported at fraction zero with a
	// degenerate zero-length normal - Box3D saying "already touching", not "you hit a
	// wall here". Godot cannot use it: it classifies neither as floor nor wall, so
	// move_and_slide can neither stand on it nor slide along it, and a character that
	// settles a fraction into the ground freezes permanently.
	//
	// Resolving it properly means depenetration, which this backend does not implement
	// yet. Filtering the hit lets the sweep report the first *real* impact instead,
	// which is the strictly better failure: an approaching surface always produces a
	// genuine normal before contact, so walls still stop a character, and a body that
	// is already overlapping is allowed to move rather than being frozen by contact it
	// cannot resolve.
	if (p_fraction <= 0.0f && b3LengthSquared(p_normal) < 1e-12f) {
		return -1.0f;
	}

	if (!ctx->hit || p_fraction < ctx->fraction) {
		ctx->hit = true;
		ctx->fraction = p_fraction;
		ctx->shape = p_shape;
		ctx->object = object;
		ctx->point = p_point;
		ctx->normal = p_normal;
	}
	// Clipping the cast to the closest hit so far is what makes this O(hits) rather
	// than a full traversal.
	return ctx->fraction;
}

bool Box3DDirectSpaceState3D::cast_motion(const ShapeParameters &p_parameters, real_t &p_closest_safe,
		real_t &p_closest_unsafe, ShapeRestInfo *r_info) {
	ERR_FAIL_NULL_V(space, false);
	p_closest_safe = 1.0;
	p_closest_unsafe = 1.0;

	LocalVector<b3Vec3> points;
	b3ShapeProxy proxy = {};
	if (!_make_proxy(p_parameters, points, proxy, false)) {
		return false;
	}

	CastClosest ctx;
	ctx.filter.exclude = &p_parameters.exclude;
	ctx.filter.collide_with_bodies = p_parameters.collide_with_bodies;
	ctx.filter.collide_with_areas = p_parameters.collide_with_areas;

	b3World_CastShape(space->get_world_id(), to_b3_pos(Vector3()), &proxy, to_b3(p_parameters.motion),
			box3d_make_query_filter(p_parameters.collision_mask), cast_closest_fcn, &ctx);

	if (!ctx.hit) {
		return false;
	}

	// Godot distinguishes the last safe fraction from the first unsafe one so a caller
	// can back off before the contact. Box3D returns a single time of impact, so the
	// two coincide - the shape is exactly touching at that fraction, not yet overlapping.
	p_closest_safe = (real_t)ctx.fraction;
	p_closest_unsafe = (real_t)ctx.fraction;

	if (r_info != nullptr) {
		r_info->point = to_godot(ctx.point);
		r_info->normal = to_godot(ctx.normal);
		r_info->rid = ctx.object->get_self();
		r_info->collider_id = ctx.object->get_instance_id();
		r_info->shape = 0;
		r_info->linear_velocity = ctx.object->is_area()
				? Vector3()
				: static_cast<Box3DBody3D *>(ctx.object)->get_linear_velocity();
	}
	return true;
}

bool Box3DDirectSpaceState3D::rest_info(const ShapeParameters &p_parameters, ShapeRestInfo *r_info) {
	ERR_FAIL_NULL_V(space, false);
	ERR_FAIL_NULL_V(r_info, false);

	// Godot's rest_info asks where a shape would come to rest along its motion. A zero
	// motion is the common case (XR grab probes use it), and a shape cast with no
	// translation reports nothing, so a tiny probe along the motion is used instead.
	ShapeParameters params = p_parameters;
	if (params.motion.is_zero_approx()) {
		real_t safe = 0;
		real_t unsafe = 0;
		params.motion = Vector3(0, -CMP_EPSILON, 0);
		return cast_motion(params, safe, unsafe, r_info);
	}

	real_t safe = 0;
	real_t unsafe = 0;
	return cast_motion(params, safe, unsafe, r_info);
}
