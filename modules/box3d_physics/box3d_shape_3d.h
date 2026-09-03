/**************************************************************************/
/*  box3d_shape_3d.h                                                      */
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
#include "core/templates/hash_set.h"
#include "core/templates/local_vector.h"
#include "core/templates/rid.h"
#include "core/variant/variant.h"

#include <box3d/box3d.h>
#include <box3d/collision.h>

class Box3DArea3D;
class Box3DBody3D;

// A Godot collision shape, which is not the same object a Box3D shape is.
//
// Godot creates shapes as free-standing RIDs and then attaches one to any number of
// bodies with a per-attachment local transform. Box3D has no free-standing shape at
// all: b3CreateSphereShape and friends take a b3BodyId and return a shape already
// bound to that body. So this class holds the *description* rather than a Box3D
// object, and every body that uses it instantiates its own b3ShapeId from it.
//
// That inversion is also why the class tracks its dependent bodies. Godot allows
// shape_set_data() on a shape already attached to bodies, and the only way to honor
// that is to destroy and rebuild each instance, so the shape has to know who is using
// it. Bodies register and unregister themselves.
class Box3DShape3D {
public:
	enum Type {
		TYPE_SPHERE,
		TYPE_BOX,
		TYPE_CAPSULE,
		TYPE_CYLINDER,
		TYPE_CONVEX,
		TYPE_CONCAVE,
		TYPE_UNSUPPORTED,
	};

	// Number of sides used when approximating a cylinder.
	//
	// Box3D has no analytic cylinder; b3CreateCylinder tessellates one into a convex
	// hull, so the count is a fidelity/cost trade rather than a detail. Sixteen keeps
	// the radial error under 2% (1 - cos(pi/16)), which is below the contact skin most
	// collisions resolve within, while staying cheap enough for the face counts SAT
	// walks. Shared with build_proxy() so a query agrees with the collision shape.
	static constexpr int CYLINDER_SIDES = 16;

private:
	// Each object carries the RID it was allocated under, so the server can answer
	// "which RID is this?" in O(1). RID_PtrOwner only maps RID -> pointer, and the
	// reverse walk it would otherwise take is linear in every live object.
	RID self;
	Type type = TYPE_UNSUPPORTED;
	Variant data;
	real_t margin = 0.04;

	// Parsed from `data` once at set_data() time rather than on every instantiation.
	real_t radius = 0.5;
	real_t height = 2.0;
	Vector3 half_extents = Vector3(0.5, 0.5, 0.5);
	Vector<Vector3> points;
	Vector<Vector3> faces;

	HashSet<Box3DBody3D *> dependents;
	HashSet<Box3DArea3D *> area_dependents;

public:
	void set_self(const RID &p_self) { self = p_self; }
	RID get_self() const { return self; }

	void set_type(Type p_type) { type = p_type; }
	Type get_type() const { return type; }
	// True when this shape can actually be baked into Box3D geometry.
	//
	// A supported type is not enough. Godot creates a shape RID first and sets its data
	// afterwards, so between those two calls a shape legitimately exists with no
	// geometry at all - and it is already attached to a body by then, because
	// CollisionShape3D adds the shape before the resource has computed itself. A
	// CSGShape3D or a runtime-generated ConcavePolygonShape3D can also be handed an
	// empty face array and filled in on a later frame.
	//
	// So an empty shape is a transient state to skip, not an error to report: the
	// dependent rebuild triggered by shape_set_data is what brings it in once the data
	// lands. Treating it as valid instead means trying to bake a mesh with no triangles
	// on every such shape during scene setup.
	bool is_valid() const { return type != TYPE_UNSUPPORTED && has_usable_data(); }

	// Whether the geometry this shape describes is non-degenerate. Split out from
	// is_valid() so the distinction between "unsupported type" and "not filled in yet"
	// stays readable at the call sites that care.
	bool has_usable_data() const;

	// Concave geometry can only ever collide as a static body: b3CreateMeshShape is
	// documented as only creating contacts on static bodies, exactly as Godot's own
	// ConcavePolygonShape3D is only valid on StaticBody3D.
	bool is_static_only() const { return type == TYPE_CONCAVE; }

	void set_data(const Variant &p_data);
	Variant get_data() const { return data; }

	void set_margin(real_t p_margin) { margin = p_margin; }
	real_t get_margin() const { return margin; }

	void add_dependent(Box3DBody3D *p_body) { dependents.insert(p_body); }
	void remove_dependent(Box3DBody3D *p_body) { dependents.erase(p_body); }
	const HashSet<Box3DBody3D *> &get_dependents() const { return dependents; }

	void add_area_dependent(Box3DArea3D *p_area) { area_dependents.insert(p_area); }
	void remove_area_dependent(Box3DArea3D *p_area) { area_dependents.erase(p_area); }
	const HashSet<Box3DArea3D *> &get_area_dependents() const { return area_dependents; }

	// Instantiates this shape onto a body at a local transform, returning the Box3D
	// shape id. `r_owned_hull` and `r_owned_mesh` receive any heap geometry the caller
	// must keep alive and destroy later - see the ownership note in the .cpp.
	b3ShapeId instantiate(b3BodyId p_body, const b3ShapeDef &p_def, const Transform3D &p_transform,
			b3HullData **r_owned_hull, b3MeshData **r_owned_mesh) const;

	// Builds the point-cloud-plus-radius form Box3D's overlap and shape-cast queries
	// take. The points are written in the frame p_transform puts the shape in, since
	// b3ShapeProxy carries no transform of its own. Returns false for shapes with no
	// convex proxy - concave meshes and height fields, which Box3D cannot cast *with*
	// (only against). Points are capped at B3_MAX_SHAPE_CAST_POINTS.
	//
	// p_inset grows the proxy when positive and shrinks it when negative; sweeps pass a
	// negative inset so resting contact is not an initial overlap.
	bool build_proxy(const Transform3D &p_transform, real_t p_inset, LocalVector<b3Vec3> &r_points,
			float &r_radius) const;

	~Box3DShape3D();
};
