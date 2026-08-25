#include <unity.h>

#include "microReticulum.h"

static RNS::Bytes captured_outgoing;
static RNS::Bytes captured_incoming;
static unsigned int incoming_callbacks = 0;

class CaptureInterface : public RNS::InterfaceImpl {
public:
	CaptureInterface() : RNS::InterfaceImpl("IFACVectorInterface") {
		_IN = true;
		_OUT = true;
	}

protected:
	bool send_outgoing(const RNS::Bytes& data) override {
		captured_outgoing = data;
		return true;
	}
};

static void capture_incoming(const RNS::Bytes& raw,
								 const RNS::Interface&) {
	captured_incoming = raw;
	++incoming_callbacks;
}

static RNS::Interface vector_interface() {
	RNS::Interface interface(new CaptureInterface());
	TEST_ASSERT_TRUE(interface.enable_ifac("IFAC interoperability test",
														 "not-a-secret", 8));
	return interface;
}

static RNS::Interface vector_interface_16() {
	RNS::Interface interface(new CaptureInterface());
	TEST_ASSERT_TRUE(interface.enable_ifac("IFAC interoperability test",
												 "not-a-secret", 16));
	return interface;
}

static RNS::Bytes vector_raw() {
	RNS::Bytes raw;
	raw.assignHex("0100112233445566778899aabbccddeeff2a48656c6c6f2049464143");
	return raw;
}

void test_ifac_key_derivation_matches_python_rns() {
	RNS::Interface interface = vector_interface();
	TEST_ASSERT_EQUAL_STRING(
		"9213858a8ddc452925f22e758377f97dd7fe28103d3c5093c14c1c4951291667"
		"f78f058332cdb942924002513f0e2abaa68b2fdcd2a460c5a025402c70888992",
		interface.ifac_key().toHex().c_str());
	TEST_ASSERT_EQUAL_UINT8(8, interface.ifac_size());
	TEST_ASSERT_EQUAL_STRING("IFAC interoperability test",
		interface.ifac_netname().c_str());
}

void test_ifac_outbound_frame_matches_python_rns() {
	RNS::Interface interface = vector_interface();
	captured_outgoing = RNS::Bytes(RNS::Type::NONE);

	TEST_ASSERT_TRUE(RNS::Transport::transmit(interface, vector_raw()));
	TEST_ASSERT_EQUAL_STRING(
		"c48f73ed6d83828b5f017e7f2632dd0f857cbf7e7ccb21791ef6078024a36861"
		"4af42e6b",
		captured_outgoing.toHex().c_str());
}

void test_ifac_inbound_frame_matches_python_rns() {
	RNS::Interface interface = vector_interface();
	RNS::Bytes wire;
	wire.assignHex(
		"c48f73ed6d83828b5f017e7f2632dd0f857cbf7e7ccb21791ef6078024a36861"
		"4af42e6b");
	captured_incoming = RNS::Bytes(RNS::Type::NONE);
	incoming_callbacks = 0;
	RNS::Transport::set_receive_packet_callback(capture_incoming);

	RNS::Transport::inbound(wire, interface);

	TEST_ASSERT_EQUAL_UINT(1, incoming_callbacks);
	TEST_ASSERT_EQUAL_STRING(vector_raw().toHex().c_str(),
		captured_incoming.toHex().c_str());
}

void test_ifac_16_byte_frame_matches_python_rns() {
	RNS::Interface interface = vector_interface_16();
	captured_outgoing = RNS::Bytes(RNS::Type::NONE);

	TEST_ASSERT_TRUE(RNS::Transport::transmit(interface, vector_raw()));
	TEST_ASSERT_EQUAL_STRING(
		"c31971a3134e74f6fadd73ed6d83828b5f014d5391038ecec91d8739d879c340"
		"1e878271525080b1c72fd69c",
		captured_outgoing.toHex().c_str());

	captured_incoming = RNS::Bytes(RNS::Type::NONE);
	incoming_callbacks = 0;
	RNS::Transport::set_receive_packet_callback(capture_incoming);
	RNS::Transport::inbound(captured_outgoing, interface);

	TEST_ASSERT_EQUAL_UINT(1, incoming_callbacks);
	TEST_ASSERT_EQUAL_STRING(vector_raw().toHex().c_str(),
		captured_incoming.toHex().c_str());
}

void test_ifac_rejects_tampering_and_wrong_network() {
	RNS::Interface interface = vector_interface();
	RNS::Bytes wire;
	wire.assignHex(
		"c48f73ed6d83828b5f017e7f2632dd0f857cbf7e7ccb21791ef6078024a36861"
		"4af42e6b");
	wire.writable(wire.size())[wire.size() - 1] ^= 0x01;
	incoming_callbacks = 0;
	uint32_t packets_before = RNS::Transport::packets_received();
	RNS::Transport::set_receive_packet_callback(capture_incoming);
	RNS::Transport::inbound(wire, interface);
	TEST_ASSERT_EQUAL_UINT(0, incoming_callbacks);
	TEST_ASSERT_EQUAL_UINT32(packets_before, RNS::Transport::packets_received());

	RNS::Interface wrong(new CaptureInterface());
	TEST_ASSERT_TRUE(wrong.enable_ifac("different network", "not-a-secret", 8));
	RNS::Transport::inbound(captured_outgoing, wrong);
	TEST_ASSERT_EQUAL_UINT(0, incoming_callbacks);
	TEST_ASSERT_EQUAL_UINT32(packets_before, RNS::Transport::packets_received());
}

void test_ifac_enforces_closed_and_open_interfaces() {
	RNS::Interface closed = vector_interface();
	RNS::Interface open(new CaptureInterface());
	RNS::Bytes protected_frame;
	protected_frame.assignHex(
		"c48f73ed6d83828b5f017e7f2632dd0f857cbf7e7ccb21791ef6078024a36861"
		"4af42e6b");
	incoming_callbacks = 0;
	RNS::Transport::set_receive_packet_callback(capture_incoming);

	RNS::Transport::inbound(vector_raw(), closed);
	RNS::Transport::inbound(protected_frame, open);
	TEST_ASSERT_EQUAL_UINT(0, incoming_callbacks);
}

void test_ifac_configuration_validation_is_transactional() {
	RNS::Interface interface = vector_interface();
	RNS::Bytes original_key = interface.ifac_key();
	TEST_ASSERT_FALSE(interface.enable_ifac("", "", 8));
	TEST_ASSERT_FALSE(interface.enable_ifac("network", "secret", 0));
	TEST_ASSERT_FALSE(interface.enable_ifac("network", "secret", 65));
	TEST_ASSERT_TRUE(interface.ifac_enabled());
	TEST_ASSERT_TRUE(original_key == interface.ifac_key());

	interface.disable_ifac();
	TEST_ASSERT_FALSE(interface.ifac_enabled());
	TEST_ASSERT_EQUAL_UINT8(0, interface.ifac_size());
	TEST_ASSERT_TRUE(interface.ifac_key().empty());
}

void test_ifac_required_without_a_key_fails_closed() {
	RNS::Interface interface(new CaptureInterface());
	interface.require_ifac(true);
	captured_outgoing = RNS::Bytes(RNS::Type::NONE);
	incoming_callbacks = 0;
	RNS::Transport::set_receive_packet_callback(capture_incoming);

	TEST_ASSERT_FALSE(RNS::Transport::transmit(interface, vector_raw()));
	RNS::Transport::inbound(vector_raw(), interface);
	TEST_ASSERT_FALSE(captured_outgoing);
	TEST_ASSERT_EQUAL_UINT(0, incoming_callbacks);
}

void setUp(void) {}
void tearDown(void) {
	RNS::Transport::set_receive_packet_callback(nullptr);
}

int main(void) {
	UNITY_BEGIN();
	RUN_TEST(test_ifac_key_derivation_matches_python_rns);
	RUN_TEST(test_ifac_outbound_frame_matches_python_rns);
	RUN_TEST(test_ifac_inbound_frame_matches_python_rns);
	RUN_TEST(test_ifac_16_byte_frame_matches_python_rns);
	RUN_TEST(test_ifac_rejects_tampering_and_wrong_network);
	RUN_TEST(test_ifac_enforces_closed_and_open_interfaces);
	RUN_TEST(test_ifac_configuration_validation_is_transactional);
	RUN_TEST(test_ifac_required_without_a_key_fails_closed);
	return UNITY_END();
}
