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

// Godot's and Box3D's layer rules are not the same rule, and the difference is not a
// tuning detail - it decides whether the scene collides at all.
//
//   Godot and Jolt:  (maskA & layerB) != 0 || (maskB & layerA) != 0
//   Box3D:           (maskA & catB)   != 0 && (catA & maskB)   != 0
//
// Godot's is an OR: one object scanning the other is enough. Box3D's is an AND: both
// have to opt in. So a floor on layer 1 / mask 1 and a crate on layer 2 / mask 7
// collide under Godot - the crate scans the floor - and silently do not under Box3D.
// Almost every Godot project is authored that way, with static geometry left on the
// default layer and each moving object masking what it cares about, so the AND rule
// drops the ground out from under the whole scene.
//
// No assignment of category and mask bits can turn Box3D's AND into Godot's OR, so the
// real test moves into the world's custom filter callback and the bit filter is reduced
// to something that can never reject a pair the OR rule would accept.
//
// That leaves one conflict. Box3D uses `categoryBits` for two different jobs: the pair
// test above and `b3ShouldQueryCollide`, where a shape is hit when its category
// intersects the query's mask - which is exactly Godot's query rule and must keep
// working. So the category cannot simply be widened. Instead it keeps the Godot layer
// and gains one reserved bit that no query mask can contain, because Godot masks are
// 32-bit and this bit is the 64th. The pair test then always passes and defers to the
// callback, while queries keep matching on the low 32 bits alone.
constexpr uint64_t B3_GODOT_PAIR_BIT = (uint64_t)1 << 63;

// The shape-side filter: the Godot layer plus the reserved bit, and a mask of all ones
// so Box3D's AND test cannot reject anything. Every shape built with this must also set
// `enableCustomFiltering`, or the pair rule is never applied and everything collides.
b3Filter box3d_make_shape_filter(uint32_t p_collision_layer, uint32_t p_collision_mask);

// Godot's actual pair rule, applied from the custom filter callback.
_FORCE_INLINE_ bool box3d_layers_should_collide(uint32_t p_layer_a, uint32_t p_mask_a, uint32_t p_layer_b, uint32_t p_mask_b) {
	return (p_mask_a & p_layer_b) != 0 || (p_mask_b & p_layer_a) != 0;
}
