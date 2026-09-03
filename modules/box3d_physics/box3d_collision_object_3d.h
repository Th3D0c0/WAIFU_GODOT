/**************************************************************************/
/*  box3d_collision_object_3d.h                                           */
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

#include "core/object/object_id.h"
#include "core/templates/rid.h"

// Common base for the two things that occupy a Box3D body's userData slot.
//
// Box3D hands back a `void*` in its move and sensor events, and both bodies and areas
// are backed by b3 bodies - an area is a kinematic body whose shapes are sensors. That
// means a move event can name either one, and casting blindly to the wrong type reads
// a Callable out of the middle of a different object. This tag is what makes the
// pointer identifiable before it is cast.
//
// Kept deliberately small and non-virtual: it exists to be safe to inspect, not to be
// an abstraction. Both subclasses stay independent otherwise.
class Box3DCollisionObject3D {
public:
	enum Type {
		TYPE_BODY,
		TYPE_AREA,
	};

private:
	const Type type;

protected:
	RID self;
	ObjectID instance_id;

	// Godot's layer/mask live here rather than on the subclasses because the pair
	// filter has to read them off either kind of object without knowing which it is.
	uint32_t collision_layer = 1;
	uint32_t collision_mask = 1;

	explicit Box3DCollisionObject3D(Type p_type) :
			type(p_type) {}

public:
	Type get_object_type() const { return type; }
	bool is_area() const { return type == TYPE_AREA; }

	void set_self(const RID &p_self) { self = p_self; }
	RID get_self() const { return self; }

	void set_instance_id(ObjectID p_id) { instance_id = p_id; }
	ObjectID get_instance_id() const { return instance_id; }

	uint32_t get_collision_layer() const { return collision_layer; }
	uint32_t get_collision_mask() const { return collision_mask; }

	~Box3DCollisionObject3D() {}
};
