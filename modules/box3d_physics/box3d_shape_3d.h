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
#include "core/templates/rid.h"
#include "core/variant/variant.h"

#include <box3d/box3d.h>
#include <box3d/collision.h>

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

public:
	void set_self(const RID &p_self) { self = p_self; }
	RID get_self() const { return self; }

	void set_type(Type p_type) { type = p_type; }
	Type get_type() const { return type; }
	bool is_valid() const { return type != TYPE_UNSUPPORTED; }

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

	// Instantiates this shape onto a body at a local transform, returning the Box3D
	// shape id. `r_owned_hull` and `r_owned_mesh` receive any heap geometry the caller
	// must keep alive and destroy later - see the ownership note in the .cpp.
	b3ShapeId instantiate(b3BodyId p_body, const b3ShapeDef &p_def, const Transform3D &p_transform,
			b3HullData **r_owned_hull, b3MeshData **r_owned_mesh) const;

	~Box3DShape3D();
};
