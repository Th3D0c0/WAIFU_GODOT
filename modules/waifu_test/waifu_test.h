/**************************************************************************/
/*  waifu_test.h                                                          */
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

#include "core/object/ref_counted.h"
#include "core/string/ustring.h"
#include "core/variant/type_info.h"

// Minimal engine module used to validate that a custom C++ module compiles,
// links, registers itself with ClassDB and passes its own unit tests.
class WaifuTest : public RefCounted {
	GDCLASS(WaifuTest, RefCounted);

public:
	enum Mood {
		MOOD_HAPPY,
		MOOD_NEUTRAL,
		MOOD_GRUMPY,
		MOOD_MAX,
	};

private:
	String greeting = "Hello";
	Mood mood = MOOD_HAPPY;
	int counter = 0;

protected:
	static void _bind_methods();

public:
	void set_greeting(const String &p_greeting);
	String get_greeting() const;

	void set_mood(Mood p_mood);
	Mood get_mood() const;

	String greet(const String &p_name) const;

	int accumulate(int p_amount);
	int get_counter() const;
	void reset();

	static int fibonacci(int p_n);
};

VARIANT_ENUM_CAST(WaifuTest::Mood);
