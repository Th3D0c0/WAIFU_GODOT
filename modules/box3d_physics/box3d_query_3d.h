/**************************************************************************/
/*  box3d_query_3d.h                                                      */
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

#include "core/templates/hash_set.h"
#include "core/templates/local_vector.h"
#include "core/templates/rid.h"

#include <box3d/box3d.h>

// Shared plumbing for the shape-based space queries.
//
// Box3D's overlap and cast entry points take a filter that can only express layer
// bits, and hand results back through a C callback with a void* context. Everything
// Godot additionally filters on - excluded RIDs, whether areas or bodies are eligible,
// which shape index was hit - has to be resolved on our side inside that callback.
// This is the context those callbacks share so the rules are written once.
struct Box3DQueryContext {
	const HashSet<RID> *exclude = nullptr;
	bool collide_with_bodies = true;
	bool collide_with_areas = false;

	// Resolves a hit shape to the object that owns it, applying every filter Godot
	// asks for and Box3D cannot. Returns nullptr when the shape should be ignored.
	Box3DCollisionObject3D *resolve(b3ShapeId p_shape) const;
};

// Builds the layer/mask filter for a query. Godot queries carry only a mask and expect
// to hit anything whose layer intersects it, so the query presents itself as belonging
// to every category and the shape's own mask is left out of the test.
b3QueryFilter box3d_make_query_filter(uint32_t p_collision_mask);
