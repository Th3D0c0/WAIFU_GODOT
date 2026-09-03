/**************************************************************************/
/*  register_types.cpp                                                    */
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

#include "register_types.h"

#include "box3d_physics_server_3d.h"

#include "core/object/callable_mp.h"
#include "servers/physics_3d/physics_server_3d_wrap_mt.h"

static PhysicsServer3D *create_box3d_physics_server() {
	// Box3D is not thread-safe for external callers - b3World_Step multithreads
	// internally through its own task callbacks, but the API around it must be driven
	// from one thread (docs/faq.md). So unlike the Jolt backend this one does not read
	// physics/3d/run_on_separate_thread; it is always wrapped single-threaded, and the
	// engine's parallelism comes from handing Box3D a worker pool, not from stepping
	// the server off-thread.
	Box3DPhysicsServer3D *physics_server = memnew(Box3DPhysicsServer3D);

	return memnew(PhysicsServer3DWrapMT(physics_server, false));
}

void initialize_box3d_physics_module(ModuleInitializationLevel p_level) {
	if (p_level != MODULE_INITIALIZATION_LEVEL_SERVERS) {
		return;
	}

	// Registered but not made default: set_default_server is deliberately not called
	// here, so an existing project keeps whatever physics/3d/physics_engine already
	// says. Selecting "Box3D" in Project Settings is the only way to get this backend,
	// which is what makes the experiment reversible from the game side too.
	PhysicsServer3DManager::get_singleton()->register_server("Box3D", callable_mp_static(&create_box3d_physics_server));
}

void uninitialize_box3d_physics_module(ModuleInitializationLevel p_level) {
	if (p_level != MODULE_INITIALIZATION_LEVEL_SERVERS) {
		return;
	}
}
