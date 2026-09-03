/**************************************************************************/
/*  box3d_query_3d.cpp                                                    */
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

#include "box3d_query_3d.h"

b3QueryFilter box3d_make_query_filter(uint32_t p_collision_mask) {
	b3QueryFilter filter = b3DefaultQueryFilter();
	filter.categoryBits = UINT64_MAX;
	filter.maskBits = p_collision_mask;
	return filter;
}

Box3DCollisionObject3D *Box3DQueryContext::resolve(b3ShapeId p_shape) const {
	if (!b3Shape_IsValid(p_shape)) {
		return nullptr;
	}
	const b3BodyId body = b3Shape_GetBody(p_shape);
	if (!B3_IS_NON_NULL(body)) {
		return nullptr;
	}

	Box3DCollisionObject3D *object = static_cast<Box3DCollisionObject3D *>(b3Body_GetUserData(body));
	if (object == nullptr) {
		return nullptr;
	}
	if (object->is_area() ? !collide_with_areas : !collide_with_bodies) {
		return nullptr;
	}
	if (exclude != nullptr && exclude->has(object->get_self())) {
		return nullptr;
	}
	return object;
}
