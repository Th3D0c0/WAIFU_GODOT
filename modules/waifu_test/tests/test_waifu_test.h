/**************************************************************************/
/*  test_waifu_test.h                                                     */
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

#include "../waifu_test.h"

#include "core/object/class_db.h"
#include "core/object/method_bind.h"
#include "core/string/ustring.h"
#include "tests/test_macros.h"

#include "tests/signal_watcher.h"

namespace TestWaifuTest {

TEST_CASE("[Modules][WaifuTest] Default state") {
	Ref<WaifuTest> waifu;
	waifu.instantiate();

	CHECK(waifu->get_greeting() == "Hello");
	CHECK(waifu->get_mood() == WaifuTest::MOOD_HAPPY);
	CHECK(waifu->get_counter() == 0);
}

TEST_CASE("[Modules][WaifuTest] Greeting depends on mood") {
	Ref<WaifuTest> waifu;
	waifu.instantiate();

	CHECK(waifu->greet("Godot") == "Hello, Godot!");

	waifu->set_mood(WaifuTest::MOOD_NEUTRAL);
	CHECK(waifu->greet("Godot") == "Hello, Godot.");

	waifu->set_mood(WaifuTest::MOOD_GRUMPY);
	CHECK(waifu->greet("Godot") == "Hello, Godot...");

	waifu->set_greeting("Good morning");
	CHECK(waifu->get_greeting() == "Good morning");
	CHECK(waifu->greet("Godot") == "Good morning, Godot...");

	// Unicode round-trip, to make sure `String` is handled properly.
	waifu->set_mood(WaifuTest::MOOD_HAPPY);
	waifu->set_greeting(U"こんにちは");
	CHECK(waifu->greet(U"ゴドー") == U"こんにちは, ゴドー!");

	// Out-of-range values must be rejected and leave the mood untouched.
	ERR_PRINT_OFF;
	waifu->set_mood((WaifuTest::Mood)42);
	ERR_PRINT_ON;
	CHECK(waifu->get_mood() == WaifuTest::MOOD_HAPPY);
}

TEST_CASE("[Modules][WaifuTest] Counter accumulation") {
	Ref<WaifuTest> waifu;
	waifu.instantiate();

	CHECK(waifu->accumulate(5) == 5);
	CHECK(waifu->accumulate(-2) == 3);
	CHECK(waifu->get_counter() == 3);

	waifu->reset();
	CHECK(waifu->get_counter() == 0);
}

TEST_CASE("[Modules][WaifuTest] Counter signal") {
	Ref<WaifuTest> waifu;
	waifu.instantiate();

	SIGNAL_WATCH(waifu.ptr(), "counter_changed");

	waifu->accumulate(2);
	const Array first_args = { { 2 } };
	SIGNAL_CHECK("counter_changed", first_args);

	waifu->accumulate(3);
	const Array second_args = { { 5 } };
	SIGNAL_CHECK("counter_changed", second_args);

	waifu->reset();
	const Array reset_args = { { 0 } };
	SIGNAL_CHECK("counter_changed", reset_args);

	// Resetting an already reset counter must not emit anything.
	waifu->reset();
	SIGNAL_CHECK_FALSE("counter_changed");

	SIGNAL_UNWATCH(waifu.ptr(), "counter_changed");
}

TEST_CASE("[Modules][WaifuTest] Static Fibonacci helper") {
	CHECK(WaifuTest::fibonacci(0) == 0);
	CHECK(WaifuTest::fibonacci(1) == 1);
	CHECK(WaifuTest::fibonacci(2) == 1);
	CHECK(WaifuTest::fibonacci(10) == 55);
	CHECK(WaifuTest::fibonacci(20) == 6765);

	ERR_PRINT_OFF;
	CHECK(WaifuTest::fibonacci(-1) == 0);
	ERR_PRINT_ON;
}

TEST_CASE("[Modules][WaifuTest] ClassDB registration") {
	REQUIRE(ClassDB::class_exists("WaifuTest"));
	CHECK(ClassDB::get_parent_class("WaifuTest") == "RefCounted");
	CHECK(ClassDB::can_instantiate("WaifuTest"));

	CHECK(ClassDB::has_method("WaifuTest", "greet", true));
	CHECK(ClassDB::has_method("WaifuTest", "accumulate", true));
	CHECK(ClassDB::has_method("WaifuTest", "fibonacci", true));
	CHECK(ClassDB::has_signal("WaifuTest", "counter_changed", true));

	bool constant_found = false;
	CHECK(ClassDB::get_integer_constant("WaifuTest", "MOOD_GRUMPY", &constant_found) == (int)WaifuTest::MOOD_GRUMPY);
	CHECK(constant_found);
	CHECK(ClassDB::get_integer_constant_enum("WaifuTest", "MOOD_GRUMPY") == StringName("Mood"));
}

TEST_CASE("[Modules][WaifuTest] Scripting API round-trip") {
	Object *object = ClassDB::instantiate("WaifuTest");
	REQUIRE(object != nullptr);

	Ref<WaifuTest> waifu = Ref<WaifuTest>(Object::cast_to<WaifuTest>(object));
	REQUIRE(waifu.is_valid());

	// Properties exposed through `ADD_PROPERTY`.
	waifu->set("greeting", "Hi");
	waifu->set("mood", (int)WaifuTest::MOOD_NEUTRAL);
	CHECK(waifu->get("greeting") == Variant("Hi"));
	CHECK(waifu->get("mood") == Variant((int)WaifuTest::MOOD_NEUTRAL));
	CHECK(waifu->get_greeting() == "Hi");
	CHECK(waifu->get_mood() == WaifuTest::MOOD_NEUTRAL);

	// Methods called through the Variant binder.
	Callable::CallError error;
	const Variant name = "Godot";
	const Variant *greet_args[] = { &name };
	const Variant greeting = waifu->callp("greet", greet_args, 1, error);
	CHECK(error.error == Callable::CallError::CALL_OK);
	CHECK(greeting == Variant("Hi, Godot."));

	// The `amount` parameter defaults to 1.
	CHECK(waifu->call("accumulate") == Variant(1));
	CHECK(waifu->call("accumulate", 4) == Variant(5));
	CHECK(waifu->call("get_counter") == Variant(5));

	// Static methods are callable without an instance.
	MethodBind *fibonacci_bind = ClassDB::get_method("WaifuTest", "fibonacci");
	REQUIRE(fibonacci_bind != nullptr);
	CHECK(fibonacci_bind->is_static());

	const Variant index = 12;
	const Variant *fibonacci_args[] = { &index };
	Callable::CallError static_error;
	CHECK(fibonacci_bind->call(nullptr, fibonacci_args, 1, static_error) == Variant(144));
	CHECK(static_error.error == Callable::CallError::CALL_OK);
}

} // namespace TestWaifuTest
