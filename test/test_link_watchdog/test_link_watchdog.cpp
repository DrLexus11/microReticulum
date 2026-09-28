// Link watchdog: establishment timeout, keepalive, stale close.
//
// The watchdog was never ported from Python, so a link whose peer vanished
// stayed open for ever. These drive one outbound link, whose request nobody
// answers, through each state by setting its timestamps, and check what one
// Link::tick_watchdog() pass does with it.

#include <unity.h>

#include <microStore/Adapters/UniversalFileSystem.h>

#include "microReticulum.h"

// Sends go nowhere: no peer ever answers, which is the case under test.
class NullInterface : public RNS::InterfaceImpl {
public:
	NullInterface() : RNS::InterfaceImpl("NullInterface") { _OUT = true; _IN = true; }
	virtual bool send_outgoing(const RNS::Bytes& data) { InterfaceImpl::handle_outgoing(data); return true; }
};

static RNS::Reticulum reticulum({RNS::Type::NONE});
static RNS::Interface null_interface(new NullInterface());
static RNS::Destination remote({RNS::Type::NONE});
static int closed_calls = 0;

static void on_closed(RNS::Link& link) { closed_calls++; }

static RNS::Link new_link() {
	closed_calls = 0;
	RNS::Link link(remote, nullptr, on_closed);
	TEST_ASSERT_EQUAL(RNS::Type::Link::PENDING, link.status());
	return link;
}

void setUp(void) {
	if (reticulum) return;
	static microStore::FileSystem filesystem{microStore::Adapters::UniversalFileSystem()};
	filesystem.init();
	RNS::Utilities::OS::register_filesystem(filesystem);
	RNS::Transport::register_interface(null_interface);
	reticulum = RNS::Reticulum();
	reticulum.transport_enabled(false);
	reticulum.start();
	// A peer we know but that never answers
	RNS::Identity peer(true);
	remote = RNS::Destination(peer, RNS::Type::Destination::OUT, RNS::Type::Destination::SINGLE, "watchdog", "test");
}

void tearDown(void) {}

void test_pending_link_left_alone_before_its_timeout() {
	RNS::Link link = new_link();
	link.tick_watchdog();
	TEST_ASSERT_EQUAL(RNS::Type::Link::PENDING, link.status());
	TEST_ASSERT_EQUAL(0, closed_calls);
}

void test_pending_link_times_out() {
	RNS::Link link = new_link();
	link.request_time(RNS::Utilities::OS::time() - link.establishment_timeout() - 1);
	link.tick_watchdog();
	TEST_ASSERT_EQUAL(RNS::Type::Link::CLOSED, link.status());
	TEST_ASSERT_EQUAL(RNS::Type::Link::TIMEOUT, link.teardown_reason());
	TEST_ASSERT_EQUAL(1, closed_calls);
}

// The test link never completed a handshake, so it has no keys and every
// send on it throws. That stands in for any failed send: the watchdog must
// carry on, since the same pass culls every other link. The keepalive itself
// is exercised on hardware.
void test_quiet_link_due_a_keepalive_stays_active_when_the_send_fails() {
	RNS::Link link = new_link();
	const double now = RNS::Utilities::OS::time();
	link.status(RNS::Type::Link::ACTIVE);
	link.last_inbound(now - RNS::Type::Link::KEEPALIVE - 1);
	link.last_outbound(now - RNS::Type::Link::KEEPALIVE - 1);
	link.tick_watchdog();
	TEST_ASSERT_EQUAL(RNS::Type::Link::ACTIVE, link.status());
	TEST_ASSERT_EQUAL(0, closed_calls);
	link.status(RNS::Type::Link::PENDING);
	link.teardown();
}

void test_silent_link_goes_stale_then_closes() {
	RNS::Link link = new_link();
	const double now = RNS::Utilities::OS::time();
	link.status(RNS::Type::Link::ACTIVE);
	link.last_inbound(now - RNS::Type::Link::STALE_TIME - 1);
	link.tick_watchdog();
	TEST_ASSERT_EQUAL(RNS::Type::Link::STALE, link.status());
	TEST_ASSERT_EQUAL(0, closed_calls);

	// Still inside the grace (RTT * factor + STALE_GRACE; RTT is 0 here)
	link.tick_watchdog();
	TEST_ASSERT_EQUAL(RNS::Type::Link::STALE, link.status());

	// Past it: closed, although its LINKCLOSE cannot be sent (no keys)
	RNS::Utilities::OS::sleep(RNS::Type::Link::STALE_GRACE + 0.2);
	link.tick_watchdog();
	TEST_ASSERT_EQUAL(RNS::Type::Link::CLOSED, link.status());
	TEST_ASSERT_EQUAL(RNS::Type::Link::TIMEOUT, link.teardown_reason());
	TEST_ASSERT_EQUAL(1, closed_calls);
}

void test_closed_link_is_left_alone() {
	RNS::Link link = new_link();
	link.teardown();
	TEST_ASSERT_EQUAL(1, closed_calls);
	link.request_time(0);
	link.tick_watchdog();
	TEST_ASSERT_EQUAL(1, closed_calls);
}

int runUnityTests(void) {
	UNITY_BEGIN();
	RUN_TEST(test_pending_link_left_alone_before_its_timeout);
	RUN_TEST(test_pending_link_times_out);
	RUN_TEST(test_quiet_link_due_a_keepalive_stays_active_when_the_send_fails);
	RUN_TEST(test_silent_link_goes_stale_then_closes);
	RUN_TEST(test_closed_link_is_left_alone);
	return UNITY_END();
}

int main(void) {
	return runUnityTests();
}
