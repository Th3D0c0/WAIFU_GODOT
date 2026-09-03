/**************************************************************************/
/*  box3d_space_3d.h                                                      */
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

#include "core/templates/hash_set.h"
#include "core/templates/local_vector.h"
#include "core/templates/rid.h"
#include "servers/physics_3d/physics_server_3d.h"

#include <box3d/box3d.h>

class Box3DBody3D;
class Box3DDirectSpaceState3D;

// A Godot physics space and the b3World backing it.
//
// The space owns the Box3D world and every body currently assigned to it, and is the
// only place b3World_Step is called from. It also owns the worker-pool plumbing:
// Box3D multithreads inside the step through the enqueue/finish task callbacks in
// b3WorldDef, which is where this backend's parallelism is meant to come from, since
// the server itself is driven single-threaded.
class Box3DSpace3D {
	RID self;
	b3WorldId world_id = b3_nullWorldId;
	bool active = false;

	Vector3 gravity_vector = Vector3(0, -9.8, 0);
	real_t gravity_magnitude = 9.8;
	int substep_count = 4;
	real_t last_step = 0.0;

	HashSet<Box3DBody3D *> bodies;
	Box3DDirectSpaceState3D *direct_state = nullptr;

	void _apply_gravity();

public:
	void set_self(const RID &p_self) { self = p_self; }
	RID get_self() const { return self; }

	b3WorldId get_world_id() const { return world_id; }

	void set_active(bool p_active) { active = p_active; }
	bool is_active() const { return active; }

	void set_param(PhysicsServer3D::SpaceParameter p_param, real_t p_value);
	real_t get_param(PhysicsServer3D::SpaceParameter p_param) const;

	// Area-style gravity overrides land on the space, because Godot's default area is
	// what carries a world's gravity and the backend has no area objects yet.
	void set_gravity_vector(const Vector3 &p_vector);
	void set_gravity_magnitude(real_t p_magnitude);
	Vector3 get_gravity_vector() const { return gravity_vector; }
	real_t get_gravity_magnitude() const { return gravity_magnitude; }

	void add_body(Box3DBody3D *p_body) { bodies.insert(p_body); }
	void remove_body(Box3DBody3D *p_body) { bodies.erase(p_body); }
	const HashSet<Box3DBody3D *> &get_bodies() const { return bodies; }

	void step(real_t p_delta);
	// The delta of the most recent step, which is what a body's direct state reports
	// as get_step() during the force-integration callback.
	real_t get_last_step() const { return last_step; }
	// Pushes post-step transforms back to the scene tree through each body's state
	// sync callback. Separate from step() because Godot calls sync/flush_queries at a
	// different point in the frame than it calls step().
	void call_queries();

	Box3DDirectSpaceState3D *get_direct_state();

	Box3DSpace3D();
	~Box3DSpace3D();
};
