/**************************************************************************/
/*  box3d_shape_3d.cpp                                                    */
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

#include "box3d_shape_3d.h"

#include "box3d_conversions.h"

#include "core/error/error_macros.h"
#include "core/templates/local_vector.h"
#include "core/typedefs.h"

void Box3DShape3D::set_data(const Variant &p_data) {
	data = p_data;

	switch (type) {
		case TYPE_SPHERE: {
			radius = p_data;
		} break;

		case TYPE_BOX: {
			half_extents = p_data;
		} break;

		case TYPE_CAPSULE:
		case TYPE_CYLINDER: {
			Dictionary d = p_data;
			ERR_FAIL_COND(!d.has("radius"));
			ERR_FAIL_COND(!d.has("height"));
			radius = d["radius"];
			height = d["height"];
		} break;

		case TYPE_CONVEX: {
			points = p_data;
		} break;

		case TYPE_CONCAVE: {
			Dictionary d = p_data;
			ERR_FAIL_COND(!d.has("faces"));
			faces = d["faces"];
		} break;

		case TYPE_UNSUPPORTED: {
		} break;
	}
}

b3ShapeId Box3DShape3D::instantiate(b3BodyId p_body, const b3ShapeDef &p_def, const Transform3D &p_transform,
		b3HullData **r_owned_hull, b3MeshData **r_owned_mesh) const {
	*r_owned_hull = nullptr;
	*r_owned_mesh = nullptr;

	switch (type) {
		case TYPE_SPHERE: {
			// A sphere is rotation-invariant, so only the translation of the local
			// transform can survive into the b3Sphere's center.
			b3Sphere sphere = { to_b3(p_transform.origin), (float)radius };
			return b3CreateSphereShape(p_body, &p_def, &sphere);
		}

		case TYPE_CAPSULE: {
			// Godot's capsule height is the full height including both caps, so the
			// segment between the sphere centers is height/2 - radius. Godot capsules
			// are Y-axis aligned; the local transform's basis is what tilts them.
			const real_t half_segment = MAX((real_t)0.0, height * 0.5 - radius);
			const Vector3 axis = p_transform.basis.get_column(Vector3::AXIS_Y) * half_segment;
			b3Capsule capsule = {
				to_b3(p_transform.origin + axis),
				to_b3(p_transform.origin - axis),
				(float)radius,
			};
			return b3CreateCapsuleShape(p_body, &p_def, &capsule);
		}

		case TYPE_BOX: {
			// b3MakeBoxHull returns a b3BoxHull by value with the b3HullData embedded
			// as its first member, so it needs no heap allocation and must never be
			// passed to b3DestroyHull. b3CreateTransformedHullShape clones it.
			b3BoxHull box = b3MakeBoxHull((float)half_extents.x, (float)half_extents.y, (float)half_extents.z);
			return b3CreateTransformedHullShape(p_body, &p_def, &box.base, to_b3(p_transform), b3Vec3{ 1.0f, 1.0f, 1.0f });
		}

		case TYPE_CYLINDER: {
			// b3CreateCylinder builds the hull spanning y in [yOffset, yOffset + height]
			// (hull.c:1789-1790), so it is base-anchored. Godot's CylinderShape3D is
			// centered on its origin, which is what -height/2 recovers; passing 0 here
			// sinks the shape by half its height, which is exactly what it looked like.
			b3HullData *hull = b3CreateCylinder((float)height, (float)radius, (float)(-height * 0.5), CYLINDER_SIDES);
			ERR_FAIL_NULL_V(hull, b3_nullShapeId);
			b3ShapeId id = b3CreateTransformedHullShape(p_body, &p_def, hull, to_b3(p_transform), b3Vec3{ 1.0f, 1.0f, 1.0f });
			// The hull is cloned into the shape, so the source can go immediately.
			b3DestroyHull(hull);
			return id;
		}

		case TYPE_CONVEX: {
			ERR_FAIL_COND_V_MSG(points.size() < 4, b3_nullShapeId,
					"Box3D: a convex shape needs at least 4 points to form a hull.");
			LocalVector<b3Vec3> b3_points;
			b3_points.resize(points.size());
			for (int i = 0; i < points.size(); i++) {
				b3_points[i] = to_b3(points[i]);
			}
			// maxVertexCount 0 asks Box3D not to simplify the hull.
			b3HullData *hull = b3CreateHull(b3_points.ptr(), points.size(), 0);
			ERR_FAIL_NULL_V_MSG(hull, b3_nullShapeId, "Box3D: failed to build a convex hull from the given points.");
			b3ShapeId id = b3CreateTransformedHullShape(p_body, &p_def, hull, to_b3(p_transform), b3Vec3{ 1.0f, 1.0f, 1.0f });
			b3DestroyHull(hull);
			return id;
		}

		case TYPE_CONCAVE: {
			// Unlike hulls, b3CreateMeshShape does NOT clone: it documents that the
			// mesh "must remain valid for the lifetime of this shape". The b3MeshData
			// is therefore handed back to the caller to own and destroy alongside the
			// shape instance, which is also why the local transform is baked into the
			// vertices here - b3CreateMeshShape takes a scale but no transform, and a
			// per-instance bake is only correct because the mesh is per-instance too.
			const int face_count = faces.size();
			ERR_FAIL_COND_V_MSG(face_count < 3 || face_count % 3 != 0, b3_nullShapeId,
					"Box3D: a concave shape needs a whole number of triangles.");

			LocalVector<b3Vec3> vertices;
			LocalVector<int32_t> indices;
			vertices.resize(face_count);
			indices.resize(face_count);
			for (int i = 0; i < face_count; i++) {
				vertices[i] = to_b3(p_transform.xform(faces[i]));
				indices[i] = i;
			}

			// Box3D ships no b3DefaultMeshDef, so the struct is zero-initialised and
			// every field that matters is set explicitly below.
			b3MeshDef mesh_def = {};
			mesh_def.vertices = vertices.ptr();
			mesh_def.vertexCount = face_count;
			mesh_def.indices = indices.ptr();
			mesh_def.triangleCount = face_count / 3;
			// The triangle soup Godot hands over repeats shared vertices, so welding is
			// what recovers the edge adjacency Box3D needs to suppress internal-edge
			// collisions on a mesh floor.
			mesh_def.weldVertices = true;
			// mesh.c:1579 skips welding entirely unless the tolerance is strictly
			// positive, so leaving it at zero would silently defeat the line above.
			// 0.1 mm collapses the soup's duplicated corners without merging geometry
			// that is meant to be distinct at any sane world scale.
			mesh_def.weldTolerance = 1e-4f;
			mesh_def.identifyEdges = true;

			b3MeshData *mesh = b3CreateMesh(&mesh_def, nullptr, 0);
			ERR_FAIL_NULL_V_MSG(mesh, b3_nullShapeId, "Box3D: failed to build a mesh from the given faces.");

			b3ShapeId id = b3CreateMeshShape(p_body, &p_def, mesh, b3Vec3{ 1.0f, 1.0f, 1.0f });
			*r_owned_mesh = mesh;
			return id;
		}

		case TYPE_UNSUPPORTED: {
			return b3_nullShapeId;
		}
	}

	return b3_nullShapeId;
}

Box3DShape3D::~Box3DShape3D() {
	// Bodies unregister themselves as they release the shape, so anything left here is
	// a body outliving a shape it still references - which would dangle on the next
	// rebuild rather than fail loudly later.
	ERR_FAIL_COND_MSG(!dependents.is_empty(), "Box3D: a shape was freed while bodies still referenced it.");
	ERR_FAIL_COND_MSG(!area_dependents.is_empty(), "Box3D: a shape was freed while areas still referenced it.");
}

bool Box3DShape3D::build_proxy(const Transform3D &p_transform, real_t p_inset, LocalVector<b3Vec3> &r_points,
		float &r_radius) const {
	r_points.clear();
	r_radius = 0.0f;

	// p_inset grows the proxy when positive and shrinks it when negative. A swept cast
	// passes a negative inset so a body resting exactly on a surface does not begin the
	// sweep already overlapping - Box3D reports that as fraction zero with a degenerate
	// zero normal, which tells Godot nothing and leaves a character unable to step off
	// the floor it is standing on.
	const real_t inset_radius = MAX((real_t)0.0, radius + p_inset);
	const Vector3 inset_extents = Vector3(
			MAX((real_t)0.0, half_extents.x + p_inset),
			MAX((real_t)0.0, half_extents.y + p_inset),
			MAX((real_t)0.0, half_extents.z + p_inset));

	switch (type) {
		case TYPE_SPHERE: {
			// A sphere is one point swept by its radius, which is exactly the shape
			// b3ShapeProxy is built around.
			r_points.push_back(to_b3(p_transform.origin));
			r_radius = (float)inset_radius;
			return true;
		}

		case TYPE_CAPSULE: {
			const real_t half_segment = MAX((real_t)0.0, height * 0.5 - radius);
			const Vector3 axis = p_transform.basis.get_column(Vector3::AXIS_Y) * half_segment;
			r_points.push_back(to_b3(p_transform.origin + axis));
			r_points.push_back(to_b3(p_transform.origin - axis));
			r_radius = (float)inset_radius;
			return true;
		}

		case TYPE_BOX: {
			for (int i = 0; i < 8; i++) {
				const Vector3 corner(
						(i & 1) ? inset_extents.x : -inset_extents.x,
						(i & 2) ? inset_extents.y : -inset_extents.y,
						(i & 4) ? inset_extents.z : -inset_extents.z);
				r_points.push_back(to_b3(p_transform.xform(corner)));
			}
			return true;
		}

		case TYPE_CYLINDER: {
			// The proxy has to be the same tessellation the collision shape uses, or a
			// query would disagree with the contact it is predicting.
			const real_t half_height = MAX((real_t)0.0, height * 0.5 + p_inset);
			for (int i = 0; i < CYLINDER_SIDES; i++) {
				const real_t a = Math::TAU * (real_t)i / (real_t)CYLINDER_SIDES;
				const real_t x = inset_radius * Math::cos(a);
				const real_t z = inset_radius * Math::sin(a);
				r_points.push_back(to_b3(p_transform.xform(Vector3(x, half_height, z))));
				r_points.push_back(to_b3(p_transform.xform(Vector3(x, -half_height, z))));
			}
			return true;
		}

		case TYPE_CONVEX: {
			ERR_FAIL_COND_V(points.is_empty(), false);
			// Box3D caps a proxy's point count. Beyond that the cloud is subsampled
			// evenly rather than truncated, so the proxy still spans the whole shape
			// instead of collapsing onto whichever vertices happened to come first.
			const int count = points.size();
			const int step = MAX(1, (count + B3_MAX_SHAPE_CAST_POINTS - 1) / B3_MAX_SHAPE_CAST_POINTS);
			for (int i = 0; i < count && (int)r_points.size() < B3_MAX_SHAPE_CAST_POINTS; i += step) {
				r_points.push_back(to_b3(p_transform.xform(points[i])));
			}
			// A convex hull's points cannot be inset without recomputing the hull, so the
			// skin is applied as a proxy radius when growing and simply dropped when
			// shrinking. A convex-shaped character therefore keeps the degenerate
			// resting-contact behavior that inset would otherwise avoid.
			r_radius = (float)MAX((real_t)0.0, p_inset);
			if (step > 1) {
				WARN_PRINT_ONCE(vformat(
						"Box3D: a convex shape with %d points was subsampled to %d for a query proxy; "
						"Box3D allows at most %d (B3_MAX_SHAPE_CAST_POINTS). The query is conservative "
						"but not exact for this shape.",
						count, r_points.size(), B3_MAX_SHAPE_CAST_POINTS));
			}
			return true;
		}

		case TYPE_CONCAVE:
		case TYPE_UNSUPPORTED: {
			// A triangle soup has no convex proxy. Box3D can cast *against* one but not
			// *with* it, which matches Godot, where concave shapes are static geometry.
			return false;
		}
	}
	return false;
}
