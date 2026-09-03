/**************************************************************************/
/*  box3d_area_3d.h                                                       */
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

#include "box3d_collision_object_3d.h"
#include "box3d_shape_3d.h"

#include "core/math/transform_3d.h"
#include "core/object/object_id.h"
#include "core/templates/hash_map.h"
#include "core/templates/local_vector.h"
#include "core/templates/rid.h"
#include "core/variant/callable.h"
#include "servers/physics_3d/physics_server_3d.h"

#include <box3d/box3d.h>

class Box3DSpace3D;

// A Godot Area3D, backed by a Box3D body whose shapes are all sensors.
//
// Box3D has no area object; a sensor is a per-shape flag, and overlaps arrive as
// begin/end touch events after the step. Two details of that model shape this class:
//
//  - `enableSensorEvents` is false by default *and applies to both sides*
//    (types.h:489), so the visitor shape needs it too. Box3DBody3D therefore sets it
//    on every shape it creates; without that an area silently detects nothing.
//  - Sensors are not excluded from each other in the overlap filter (sensor.c:118-133
//    checks enableSensorEvents, same-body and the collision filter, but never
//    isSensor), so area-to-area monitoring works with no extra machinery.
//
// Godot delivers overlaps through two callbacks that each take five arguments -
// status, RID, ObjectID, shape index on their side, shape index on ours - and it
// expects them at most once per state change, which is why the overlap sets are
// diffed here rather than forwarded event by event.
class Box3DArea3D : public Box3DCollisionObject3D {
	struct ShapeSlot {
		Box3DShape3D *shape = nullptr;
		Transform3D transform;
		bool disabled = false;
		b3ShapeId id = b3_nullShapeId;
		b3MeshData *owned_mesh = nullptr;
		b3HullData *owned_hull = nullptr;
	};

	Box3DSpace3D *space = nullptr;
	b3BodyId body_id = b3_nullBodyId;

	Transform3D transform;
	uint32_t collision_layer = 1;
	uint32_t collision_mask = 1;
	bool monitorable = true;
	bool ray_pickable = true;

	Callable monitor_callback;
	Callable area_monitor_callback;

	// Godot's area parameters are stored whole so they read back faithfully, but only
	// gravity and damping are acted on, and only for the space's default area - see
	// the note on apply_default_area_params().
	HashMap<int, Variant> params;

	LocalVector<ShapeSlot> shapes;

	void _build();
	void _destroy();
	void _rebuild_shapes();
	void _destroy_shape_slot(ShapeSlot &p_slot);

public:
	Box3DArea3D() :
			Box3DCollisionObject3D(TYPE_AREA) {}

	b3BodyId get_body_id() const { return body_id; }
	Box3DSpace3D *get_space() const { return space; }
	void set_space(Box3DSpace3D *p_space);

	void set_transform(const Transform3D &p_transform);
	Transform3D get_transform() const { return transform; }

	void set_param(PhysicsServer3D::AreaParameter p_param, const Variant &p_value);
	Variant get_param(PhysicsServer3D::AreaParameter p_param) const;

	void set_collision_layer(uint32_t p_layer);
	uint32_t get_collision_layer() const { return collision_layer; }
	void set_collision_mask(uint32_t p_mask);
	uint32_t get_collision_mask() const { return collision_mask; }

	void set_monitorable(bool p_monitorable);
	bool is_monitorable() const { return monitorable; }
	void set_ray_pickable(bool p_pickable) { ray_pickable = p_pickable; }
	bool is_ray_pickable() const { return ray_pickable; }

	void set_monitor_callback(const Callable &p_callback) { monitor_callback = p_callback; }
	void set_area_monitor_callback(const Callable &p_callback) { area_monitor_callback = p_callback; }
	bool has_monitor_callback() const { return monitor_callback.is_valid(); }
	bool has_area_monitor_callback() const { return area_monitor_callback.is_valid(); }

	// Invoked by the space once per state change, with Godot's five-argument layout.
	void report_body(PhysicsServer3D::AreaBodyStatus p_status, const RID &p_rid, ObjectID p_instance, int p_body_shape, int p_area_shape);
	void report_area(PhysicsServer3D::AreaBodyStatus p_status, const RID &p_rid, ObjectID p_instance, int p_area_shape, int p_self_shape);

	void add_shape(Box3DShape3D *p_shape, const Transform3D &p_transform, bool p_disabled);
	void set_shape(int p_index, Box3DShape3D *p_shape);
	void set_shape_transform(int p_index, const Transform3D &p_transform);
	void set_shape_disabled(int p_index, bool p_disabled);
	void remove_shape(int p_index);
	void clear_shapes();
	int get_shape_count() const { return shapes.size(); }
	Box3DShape3D *get_shape(int p_index) const;
	Transform3D get_shape_transform(int p_index) const;
	// Maps a Box3D shape id back to the Godot shape index the callbacks report.
	int find_shape_index(b3ShapeId p_id) const;

	void shape_changed(Box3DShape3D *p_shape);

	~Box3DArea3D();
};
