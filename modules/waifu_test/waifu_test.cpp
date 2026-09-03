/**************************************************************************/
/*  waifu_test.cpp                                                        */
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

#include "waifu_test.h"

#include "core/object/class_db.h"

void WaifuTest::set_greeting(const String &p_greeting) {
	greeting = p_greeting;
}

String WaifuTest::get_greeting() const {
	return greeting;
}

void WaifuTest::set_mood(int p_mood) {
	ERR_FAIL_INDEX(p_mood, MOOD_MAX);
	mood = (Mood)p_mood;
}

WaifuTest::Mood WaifuTest::get_mood() const {
	return mood;
}

String WaifuTest::greet(const String &p_name) const {
	String punctuation;
	switch (mood) {
		case MOOD_HAPPY:
			punctuation = "!";
			break;
		case MOOD_NEUTRAL:
			punctuation = ".";
			break;
		case MOOD_GRUMPY:
			punctuation = "...";
			break;
		case MOOD_MAX:
			break;
	}
	return greeting + ", " + p_name + punctuation;
}

int WaifuTest::accumulate(int p_amount) {
	counter += p_amount;
	emit_signal(SNAME("counter_changed"), counter);
	return counter;
}

int WaifuTest::get_counter() const {
	return counter;
}

void WaifuTest::reset() {
	if (counter == 0) {
		return;
	}
	counter = 0;
	emit_signal(SNAME("counter_changed"), counter);
}

int WaifuTest::fibonacci(int p_n) {
	ERR_FAIL_COND_V_MSG(p_n < 0, 0, "The Fibonacci sequence is not defined for negative indices.");

	int previous = 0;
	int current = 1;
	for (int i = 0; i < p_n; i++) {
		const int next = previous + current;
		previous = current;
		current = next;
	}
	return previous;
}

void WaifuTest::_bind_methods() {
	ClassDB::bind_method(D_METHOD("set_greeting", "greeting"), &WaifuTest::set_greeting);
	ClassDB::bind_method(D_METHOD("get_greeting"), &WaifuTest::get_greeting);
	ClassDB::bind_method(D_METHOD("set_mood", "mood"), &WaifuTest::set_mood);
	ClassDB::bind_method(D_METHOD("get_mood"), &WaifuTest::get_mood);

	ClassDB::bind_method(D_METHOD("greet", "name"), &WaifuTest::greet);
	ClassDB::bind_method(D_METHOD("accumulate", "amount"), &WaifuTest::accumulate, DEFVAL(1));
	ClassDB::bind_method(D_METHOD("get_counter"), &WaifuTest::get_counter);
	ClassDB::bind_method(D_METHOD("reset"), &WaifuTest::reset);

	ClassDB::bind_static_method("WaifuTest", D_METHOD("fibonacci", "n"), &WaifuTest::fibonacci);

	ADD_PROPERTY(PropertyInfo(Variant::STRING, "greeting"), "set_greeting", "get_greeting");
	ADD_PROPERTY(PropertyInfo(Variant::INT, "mood", PROPERTY_HINT_ENUM, "Happy,Neutral,Grumpy"), "set_mood", "get_mood");

	ADD_SIGNAL(MethodInfo("counter_changed", PropertyInfo(Variant::INT, "value")));

	BIND_ENUM_CONSTANT(MOOD_HAPPY);
	BIND_ENUM_CONSTANT(MOOD_NEUTRAL);
	BIND_ENUM_CONSTANT(MOOD_GRUMPY);
	BIND_ENUM_CONSTANT(MOOD_MAX);
}
