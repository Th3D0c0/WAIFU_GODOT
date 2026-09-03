/**************************************************************************/
/*  box3d_space_3d.cpp                                                    */
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

#include "box3d_space_3d.h"

#include "box3d_area_3d.h"
#include "box3d_body_3d.h"
#include "box3d_conversions.h"
#include "box3d_direct_body_state_3d.h"
#include "box3d_direct_space_state_3d.h"

#include "core/os/os.h"

Box3DSpace3D::Box3DSpace3D() {
	b3WorldDef world_def = b3DefaultWorldDef();
	world_def.gravity = to_b3(gravity_vector * gravity_magnitude);

	// Box3D is handed a worker count but no enqueue/finish callbacks, which
	// physics_world.c:364 reads as "multithread using your own scheduler". That is
	// deliberate rather than unfinished: b3World_Step blocks on the tasks it spawns
	// and holds its stack across every fork/join, so driving it from inside a
	// WorkerThreadPool job risks the deadlock the task-callback docs warn about,
	// whereas Box3D's in-tree scheduler runs pending tasks on the waiting thread.
	// Swapping to WorkerThreadPool is a later optimization, not a prerequisite.
	world_def.workerCount = CLAMP(OS::get_singleton()->get_processor_count(), 1, B3_MAX_WORKERS);

	world_id = b3CreateWorld(&world_def);
}

Box3DSpace3D::~Box3DSpace3D() {
	if (direct_state != nullptr) {
		memdelete(direct_state);
		direct_state = nullptr;
	}
	if (B3_IS_NON_NULL(world_id)) {
		b3DestroyWorld(world_id);
		world_id = b3_nullWorldId;
	}
}

void Box3DSpace3D::_apply_gravity() {
	if (B3_IS_NON_NULL(world_id)) {
		b3World_SetGravity(world_id, to_b3(gravity_vector * gravity_magnitude));
	}
}

void Box3DSpace3D::set_gravity_vector(const Vector3 &p_vector) {
	gravity_vector = p_vector;
	_apply_gravity();
}

void Box3DSpace3D::set_gravity_magnitude(real_t p_magnitude) {
	gravity_magnitude = p_magnitude;
	_apply_gravity();
}

void Box3DSpace3D::set_param(PhysicsServer3D::SpaceParameter p_param, real_t p_value) {
	switch (p_param) {
		case PhysicsServer3D::SPACE_PARAM_SOLVER_ITERATIONS: {
			// Godot's "solver iterations" is the closest thing it has to Box3D's
			// substep count, and both mean "resolve this many times per step", so the
			// knob is mapped rather than ignored.
			substep_count = MAX(1, (int)p_value);
		} break;

		default: {
			// The remaining space parameters describe thresholds of Godot's own
			// solver (contact bias, separation, sleep margins) that Box3D derives
			// internally from contactHertz and contactDampingRatio instead. Dropping
			// them is correct; there is nothing to forward them to.
		} break;
	}
}

real_t Box3DSpace3D::get_param(PhysicsServer3D::SpaceParameter p_param) const {
	switch (p_param) {
		case PhysicsServer3D::SPACE_PARAM_SOLVER_ITERATIONS:
			return (real_t)substep_count;
		default:
			return 0;
	}
}

void Box3DSpace3D::step(real_t p_delta) {
	last_step = p_delta;
	if (!active || !B3_IS_NON_NULL(world_id)) {
		return;
	}
	b3World_Step(world_id, (float)p_delta, substep_count);
}

void Box3DSpace3D::call_queries() {
	if (!B3_IS_NON_NULL(world_id)) {
		return;
	}

	// Godot pulls a body's transform through its state sync callback rather than
	// polling every body, so only bodies Box3D actually moved need touching. That is
	// exactly what the move-event list is: b3World_GetBodyEvents returns one entry per
	// body whose transform changed during the step, so a world of mostly sleeping
	// bodies costs nothing here.
	_flush_sensor_events();

	const b3BodyEvents events = b3World_GetBodyEvents(world_id);
	for (int i = 0; i < events.moveCount; i++) {
		const b3BodyMoveEvent &event = events.moveEvents[i];
		Box3DCollisionObject3D *object = static_cast<Box3DCollisionObject3D *>(event.userData);
		// Areas are kinematic bodies, so they produce move events too. Reading one as a
		// Box3DBody3D pulls a Callable out of the middle of a different object, which
		// is why the pointer is tagged rather than assumed.
		if (object == nullptr || object->is_area()) {
			continue;
		}
		Box3DBody3D *body = static_cast<Box3DBody3D *>(object);

		const Callable &callback = body->get_state_sync_callback();
		if (callback.is_valid()) {
			callback.call(body->get_direct_state());
		}
	}
}

// Turns Box3D's sensor begin/end touch events into Godot's two area callbacks.
//
// Box3D reports overlaps per *shape pair*, which is the same granularity Godot's
// callbacks take, so the events map across one for one. The work here is resolving
// each side back to the object that owns it and deciding which of the two callbacks
// it belongs on: a visitor that is itself an area goes to the area monitor, anything
// else to the body monitor.
void Box3DSpace3D::_flush_sensor_events() {
	const b3SensorEvents events = b3World_GetSensorEvents(world_id);

	for (int i = 0; i < events.beginCount + events.endCount; i++) {
		const bool entering = i < events.beginCount;
		const b3ShapeId sensor_shape = entering ? events.beginEvents[i].sensorShapeId
												: events.endEvents[i - events.beginCount].sensorShapeId;
		const b3ShapeId visitor_shape = entering ? events.beginEvents[i].visitorShapeId
												 : events.endEvents[i - events.beginCount].visitorShapeId;

		// An end event is explicitly allowed to name a shape that has since been
		// destroyed (types.h), so both sides are checked before being dereferenced.
		if (!b3Shape_IsValid(sensor_shape) || !b3Shape_IsValid(visitor_shape)) {
			continue;
		}

		const b3BodyId sensor_body = b3Shape_GetBody(sensor_shape);
		const b3BodyId visitor_body = b3Shape_GetBody(visitor_shape);
		if (!B3_IS_NON_NULL(sensor_body) || !B3_IS_NON_NULL(visitor_body)) {
			continue;
		}

		// Only an area owns sensor shapes, so the sensor side resolves to one.
		Box3DCollisionObject3D *sensor_object = static_cast<Box3DCollisionObject3D *>(b3Body_GetUserData(sensor_body));
		if (sensor_object == nullptr || !sensor_object->is_area()) {
			continue;
		}
		Box3DArea3D *area = static_cast<Box3DArea3D *>(sensor_object);

		Box3DCollisionObject3D *visitor = static_cast<Box3DCollisionObject3D *>(b3Body_GetUserData(visitor_body));
		if (visitor == nullptr) {
			continue;
		}

		const PhysicsServer3D::AreaBodyStatus status =
				entering ? PhysicsServer3D::AREA_BODY_ADDED : PhysicsServer3D::AREA_BODY_REMOVED;
		const int area_shape_index = area->find_shape_index(sensor_shape);

		if (visitor->is_area()) {
			Box3DArea3D *other = static_cast<Box3DArea3D *>(visitor);
			// Godot's monitorable flag is about whether other areas may see this one.
			// Box3D has no equivalent, so it is enforced at report time.
			if (!other->is_monitorable()) {
				continue;
			}
			area->report_area(status, other->get_self(), other->get_instance_id(),
					other->find_shape_index(visitor_shape), area_shape_index);
		} else {
			Box3DBody3D *body = static_cast<Box3DBody3D *>(visitor);
			area->report_body(status, body->get_self(), body->get_instance_id(),
					body->find_shape_index(visitor_shape), area_shape_index);
		}
	}
}

Box3DDirectSpaceState3D *Box3DSpace3D::get_direct_state() {
	if (direct_state == nullptr) {
		direct_state = memnew(Box3DDirectSpaceState3D(this));
	}
	return direct_state;
}
