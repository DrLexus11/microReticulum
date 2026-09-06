#include <unity.h>

#include "microReticulum/Log.h"
#include "microReticulum/Utilities/OS.h"

using namespace RNS::Utilities;

void testTime()
{

    uint64_t start_ltime = OS::ltime();
    double start_dtime = OS::time();

    double sleep_time = 1.23456;
    OS::sleep(sleep_time);

    uint64_t end_ltime = OS::ltime();
    double end_dtime = OS::time();

    double diff_time;

    diff_time = (double)(end_ltime - start_ltime) / 1000.0;
	TRACEF("ltime diff: %f", diff_time);
    TEST_ASSERT_TRUE(diff_time > sleep_time * 0.99);
    TEST_ASSERT_TRUE(diff_time < sleep_time * 1.01);

    diff_time = end_dtime - start_dtime;
	TRACEF("dtime diff: %f", diff_time);
    TEST_ASSERT_TRUE(diff_time > sleep_time * 0.99);
    TEST_ASSERT_TRUE(diff_time < sleep_time * 1.01);

}

void testConvert() {
	//uint64_t time = ltime();
	//printf("time: %lld\n", time);

	uint64_t num = 1234567890;
	TEST_ASSERT_EQUAL_UINT64(1234567890, num);

	char str[16];
	snprintf(str, 16, "%lld", num);
	TEST_ASSERT_EQUAL_STRING("1234567890", str);

	char* buf = (char*)&num;

	//uint64_t newnum = (uint64_t)(*buf);
	uint64_t newnum = *(uint64_t*)buf;
	TEST_ASSERT_EQUAL_UINT64(1234567890, newnum);
}

void testClockDomains() {
	const uint64_t monotonic_start = OS::monotonic_time_millis();
	OS::sleep(0.01f);
	TEST_ASSERT_GREATER_OR_EQUAL_UINT64(monotonic_start,
	                                   OS::monotonic_time_millis());
	TEST_ASSERT_EQUAL_STRING("unknown", OS::wall_time_source_name(OS::WallTimeSource::UNKNOWN));
	TEST_ASSERT_EQUAL_STRING("persisted", OS::wall_time_source_name(OS::WallTimeSource::PERSISTED));
	TEST_ASSERT_EQUAL_STRING("ntp", OS::wall_time_source_name(OS::WallTimeSource::NTP));
	TEST_ASSERT_EQUAL_STRING("authenticated-client",
	                         OS::wall_time_source_name(OS::WallTimeSource::AUTHENTICATED_CLIENT));

#ifdef ARDUINO
	OS::clear_wall_time();
	TEST_ASSERT_FALSE(OS::wall_time_known());
	TEST_ASSERT_EQUAL_UINT64(0, OS::wall_time_millis());
	TEST_ASSERT_EQUAL(OS::WallTimeResult::INVALID,
	                  OS::adopt_wall_time(1234, OS::WallTimeSource::NTP, 1000));
	TEST_ASSERT_EQUAL(OS::WallTimeResult::ACCEPTED,
	                  OS::adopt_wall_time(1737849600000ULL,
	                                      OS::WallTimeSource::NTP, 1000));
	const uint64_t adopted = OS::wall_time_millis();
	TEST_ASSERT_TRUE(OS::wall_time_known());
	TEST_ASSERT_EQUAL(OS::WallTimeSource::NTP, OS::wall_time_source());
	TEST_ASSERT_EQUAL(OS::WallTimeSource::NTP, OS::wall_time_last_live_source());
	TEST_ASSERT_EQUAL(OS::WallTimeResult::BACKWARDS,
	                  OS::adopt_wall_time(adopted - 1,
	                                      OS::WallTimeSource::NTP, 1000));
	// An hour past a clock that only drifts forward with monotonic time. A
	// margin of one millisecond over the step made this depend on how long the
	// calls above took: wall_time_millis() advances between capturing `adopted`
	// and adopt_wall_time() reading it, which shrinks the computed step.
	TEST_ASSERT_EQUAL(OS::WallTimeResult::JUMP_TOO_LARGE,
	                  OS::adopt_wall_time(adopted + 3600000ULL,
	                                      OS::WallTimeSource::NTP, 1000));

	// Stratum gates adoption: NTP resolves to 1, an authenticated client to 2,
	// and a worse stratum cannot overwrite a better one no matter how small the
	// step. Equal stays allowed so a source can refresh its own value.
	TEST_ASSERT_EQUAL_UINT8(1, OS::wall_time_stratum());
	TEST_ASSERT_EQUAL(OS::WallTimeResult::WORSE_STRATUM,
	                  OS::adopt_wall_time(OS::wall_time_millis() + 10,
	                                      OS::WallTimeSource::AUTHENTICATED_CLIENT,
	                                      1000));
	TEST_ASSERT_EQUAL_UINT8(1, OS::wall_time_stratum());
	TEST_ASSERT_EQUAL(OS::WallTimeResult::ACCEPTED,
	                  OS::adopt_wall_time(OS::wall_time_millis() + 10,
	                                      OS::WallTimeSource::NTP, 1000));

	// Every source that can be persisted must be restorable. writeWallTime()
	// stores the last live source, so a beacon-synced node writes a 7.
	TEST_ASSERT_TRUE(OS::restore_wall_time(OS::wall_time_millis(),
	                                       OS::WallTimeSource::SIGNED_BEACON,
	                                       0, 0, 0));
	TEST_ASSERT_EQUAL(OS::WallTimeSource::PERSISTED, OS::wall_time_source());
	TEST_ASSERT_EQUAL(OS::WallTimeSource::SIGNED_BEACON, OS::wall_time_last_live_source());

	// A restored clock is only a lower bound -- it cannot account for time
	// spent powered off -- so it yields to a live source even a worse one.
	TEST_ASSERT_TRUE(OS::restore_wall_time(OS::wall_time_millis(),
	                                       OS::WallTimeSource::NTP, 0, 0, 1));
	TEST_ASSERT_EQUAL_UINT8(1, OS::wall_time_stratum());
	TEST_ASSERT_EQUAL(OS::WallTimeResult::ACCEPTED,
	                  OS::adopt_wall_time(OS::wall_time_millis() + 10,
	                                      OS::WallTimeSource::AUTHENTICATED_CLIENT,
	                                      1000));
	TEST_ASSERT_EQUAL_UINT8(2, OS::wall_time_stratum());
	OS::clear_wall_time();
#endif
}

void testOS() {
	HEAD("Running testOS...", RNS::LOG_TRACE);
	testTime();
	testConvert();
}


void setUp(void) {
	// set stuff up here before each test
}

void tearDown(void) {
	// clean stuff up here after each test
}

int runUnityTests(void) {
	UNITY_BEGIN();
	RUN_TEST(testTime);
	RUN_TEST(testConvert);
	RUN_TEST(testClockDomains);
	RUN_TEST(testOS);
	return UNITY_END();
}

// For native dev-platform or for some embedded frameworks
int main(void) {
	return runUnityTests();
}

#ifdef ARDUINO
// For Arduino framework
void setup() {
	// Wait ~2 seconds before the Unity test runner
	// establishes connection with a board Serial interface
	delay(2000);
	
	runUnityTests();
}
void loop() {}
#endif

// For ESP-IDF framework
void app_main() {
	runUnityTests();
}
