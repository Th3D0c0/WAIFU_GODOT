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

bool Box3DDirectSpaceState3D::intersect_ray(const RayParameters &, RayResult &r_result) {
	B3_TODO();
	return false;
}

int Box3DDirectSpaceState3D::intersect_point(const PointParameters &, ShapeResult *r_results, int) {
	B3_TODO();
	return 0;
}

int Box3DDirectSpaceState3D::intersect_shape(const ShapeParameters &, ShapeResult *r_results, int) {
	B3_TODO();
	return 0;
}

bool Box3DDirectSpaceState3D::cast_motion(const ShapeParameters &, real_t &, real_t &, ShapeRestInfo *r_info) {
	B3_TODO();
	return false;
}

bool Box3DDirectSpaceState3D::collide_shape(const ShapeParameters &, Vector3 *r_results, int, int &r_result_count) {
	B3_TODO();
	return false;
}

bool Box3DDirectSpaceState3D::rest_info(const ShapeParameters &, ShapeRestInfo *r_info) {
	B3_TODO();
	return false;
}

Vector3 Box3DDirectSpaceState3D::get_closest_point_to_object_volume(RID, const Vector3) const {
	B3_TODO();
	return Vector3();
}
