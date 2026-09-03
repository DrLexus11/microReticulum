/*
 * Copyright (c) 2023 Chad Attermann
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at:
 *
 *     http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 */

#include "OS.h"

#include "../Type.h"
#include "../Log.h"

#include <new>

using namespace RNS;
using namespace RNS::Utilities;

/*static*/ microStore::FileSystem OS::_filesystem;
/*static*/ uint64_t OS::_time_offset = 0;
/*static*/ int64_t OS::_wall_time_offset = 0;
#ifdef ARDUINO
/*static*/ bool OS::_wall_time_known = false;
/*static*/ OS::WallTimeSource OS::_wall_time_source = OS::WallTimeSource::UNKNOWN;
/*static*/ OS::WallTimeSource OS::_wall_time_last_live_source = OS::WallTimeSource::UNKNOWN;
#else
/*static*/ bool OS::_wall_time_known = true;
/*static*/ OS::WallTimeSource OS::_wall_time_source = OS::WallTimeSource::SYSTEM;
/*static*/ OS::WallTimeSource OS::_wall_time_last_live_source = OS::WallTimeSource::SYSTEM;
#endif
/*static*/ uint64_t OS::_wall_time_adopted_at = 0;
/*static*/ int64_t OS::_wall_time_last_correction = 0;
/*static*/ OS::LoopCallback OS::_on_loop = nullptr;

namespace {
	// Reject seconds accidentally supplied as milliseconds and obviously corrupt
	// epochs. This range is intentionally broad; policy belongs to the caller.
	constexpr uint64_t WALL_TIME_MIN_MS = 946684800000ULL;   // 2000-01-01
	constexpr uint64_t WALL_TIME_MAX_MS = 7258118400000ULL;  // 2200-01-01
}

/*static*/ uint64_t OS::monotonic_time_millis() {
#ifdef ARDUINO
	return ltime();
#else
	timespec now;
	::clock_gettime(CLOCK_MONOTONIC, &now);
	return (uint64_t)now.tv_sec * 1000ULL + (uint64_t)now.tv_nsec / 1000000ULL;
#endif
}

/*static*/ double OS::monotonic_time() {
#ifdef ARDUINO
	return time();
#else
	timespec now;
	::clock_gettime(CLOCK_MONOTONIC, &now);
	return (double)now.tv_sec + (double)now.tv_nsec / 1000000000.0;
#endif
}

/*static*/ bool OS::wall_time_known() {
	return _wall_time_known;
}

/*static*/ uint64_t OS::wall_time_millis() {
#ifndef ARDUINO
	timeval now;
	::gettimeofday(&now, nullptr);
	return (uint64_t)now.tv_sec * 1000ULL + (uint64_t)now.tv_usec / 1000ULL;
#else
	if (!_wall_time_known) return 0;
	const int64_t wall = (int64_t)monotonic_time_millis() + _wall_time_offset;
	return wall > 0 ? (uint64_t)wall : 0;
#endif
}

/*static*/ double OS::wall_time() {
	return (double)wall_time_millis() / 1000.0;
}

/*static*/ OS::WallTimeSource OS::wall_time_source() {
	return _wall_time_source;
}

/*static*/ OS::WallTimeSource OS::wall_time_last_live_source() {
	return _wall_time_last_live_source;
}

/*static*/ const char* OS::wall_time_source_name(WallTimeSource source) {
	switch (source) {
		case WallTimeSource::PERSISTED: return "persisted";
		case WallTimeSource::NTP: return "ntp";
		case WallTimeSource::AUTHENTICATED_CLIENT: return "authenticated-client";
		case WallTimeSource::GNSS: return "gnss";
		case WallTimeSource::RTC: return "rtc";
		case WallTimeSource::SYSTEM: return "system";
		default: return "unknown";
	}
}

/*static*/ uint64_t OS::wall_time_adopted_at() {
	return _wall_time_adopted_at;
}

/*static*/ int64_t OS::wall_time_last_correction() {
	return _wall_time_last_correction;
}

/*static*/ OS::WallTimeResult OS::adopt_wall_time(
	uint64_t unix_time_ms, WallTimeSource source, uint64_t max_forward_step_ms) {
	if (unix_time_ms < WALL_TIME_MIN_MS || unix_time_ms >= WALL_TIME_MAX_MS ||
	    source == WallTimeSource::UNKNOWN || source == WallTimeSource::PERSISTED) {
		return WallTimeResult::INVALID;
	}

	const uint64_t monotonic_now = monotonic_time_millis();
	int64_t correction = 0;
	if (_wall_time_known) {
		const uint64_t current = wall_time_millis();
		if (unix_time_ms < current) return WallTimeResult::BACKWARDS;
		const uint64_t forward = unix_time_ms - current;
		if (forward > max_forward_step_ms) return WallTimeResult::JUMP_TOO_LARGE;
		correction = (int64_t)forward;
	}

	_wall_time_offset = (int64_t)unix_time_ms - (int64_t)monotonic_now;
	_wall_time_known = true;
	_wall_time_source = source;
	_wall_time_last_live_source = source;
	_wall_time_adopted_at = monotonic_now;
	_wall_time_last_correction = correction;
	return WallTimeResult::ACCEPTED;
}

/*static*/ bool OS::restore_wall_time(uint64_t unix_time_ms,
	WallTimeSource source, uint64_t adopted_at, int64_t last_correction) {
	if (unix_time_ms < WALL_TIME_MIN_MS || unix_time_ms >= WALL_TIME_MAX_MS ||
	    source == WallTimeSource::UNKNOWN || source == WallTimeSource::PERSISTED ||
	    static_cast<uint8_t>(source) > static_cast<uint8_t>(WallTimeSource::SYSTEM)) {
		return false;
	}
	_wall_time_offset = (int64_t)unix_time_ms - (int64_t)monotonic_time_millis();
	_wall_time_known = true;
	// A stored sample cannot account for time spent powered off. Expose that
	// explicitly so a live trusted source can apply a larger forward correction.
	_wall_time_source = WallTimeSource::PERSISTED;
	_wall_time_last_live_source = source;
	_wall_time_adopted_at = adopted_at;
	_wall_time_last_correction = last_correction;
	return true;
}

/*static*/ void OS::clear_wall_time() {
#ifdef ARDUINO
	_wall_time_known = false;
	_wall_time_source = WallTimeSource::UNKNOWN;
	_wall_time_last_live_source = WallTimeSource::UNKNOWN;
	_wall_time_offset = 0;
	_wall_time_adopted_at = 0;
	_wall_time_last_correction = 0;
#endif
}
