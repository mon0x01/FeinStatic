#include "test.hpp"

#include "network_harness.hpp"

#include "feinstat/common/bytes.hpp"
#include "feinstat/crypto/crypto.hpp"
#include "feinstat/network/bootstrap.hpp"
#include "feinstat/network/node_record.hpp"

#include <chrono>
#include <filesystem>
#include <iostream>
#include <string>
#include <thread>

using namespace feinstat;
using namespace feinstat::crypto;
using namespace feinstat::network;
using namespace feinstat::network::test;

namespace {

Bytes make_network_id() {
    Bytes nid(32, 0x42);
    return nid;
}

SignedNodeRecord make_record(Ed25519KeyProvider& provider,
                             const Bytes& network_id,
                             std::uint64_t sequence,
                             std::uint64_t expiry,
                             const std::vector<NodeEndpoint>& endpoints) {
    SignedNodeRecord record{};
    record.format_version = 1;
    record.network_identifier = network_id;
    record.node_id = provider.node_id();
    record.key_algorithm = 1;
    record.public_key = provider.public_key();
    record.sequence = sequence;
    record.protocol_versions = {8};
    record.capabilities = {"peer", "bootstrap"};
    record.endpoints = endpoints;
    record.expiry_unix_ms = expiry;
    ByteWriter dw;
    dw.string(SignedNodeRecord::kSignDomain);
    dw.blob(record.canonical_body());
    record.signature = provider.sign(std::move(dw).take());
    return record;
}

std::uint64_t now_ms() {
    return static_cast<std::uint64_t>(
        std::chrono::duration_cast<std::chrono::milliseconds>(
            std::chrono::system_clock::now().time_since_epoch()).count());
}

// Sign a record using a live fixture's own identity.
SignedNodeRecord make_record_from(NodeFixture& node,
                                  const Bytes& network_id,
                                  std::uint64_t sequence,
                                  std::uint64_t expiry,
                                  const std::vector<NodeEndpoint>& endpoints) {
    SignedNodeRecord record{};
    record.format_version = 1;
    record.network_identifier = network_id;
    record.node_id = node.node_id();
    record.key_algorithm = 1;
    record.public_key = node.public_key();
    record.sequence = sequence;
    record.protocol_versions = {8};
    record.capabilities = {"peer", "bootstrap"};
    record.endpoints = endpoints;
    record.expiry_unix_ms = expiry;
    ByteWriter dw;
    dw.string(SignedNodeRecord::kSignDomain);
    dw.blob(record.canonical_body());
    record.signature = node.sign(std::move(dw).take());
    return record;
}

} // namespace

//------------------------------------------------------------------------------
// Multi-node networking
//------------------------------------------------------------------------------

FS_TEST(harness_two_live_nodes_establish_authenticated_session) {
    TopologyConfig topology{make_network_id()};

    NodeFixture alice(topology);
    NodeFixture bob(topology);
    auto t0 = std::chrono::steady_clock::now();
    std::cerr << "[T] " << std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now()-t0).count() << " ms: start" << std::endl;
    alice.start();
    bob.start();
    std::cerr << "[T] " << std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now()-t0).count() << " ms: started" << std::endl;

    FS_REQUIRE(alice.node_id() != bob.node_id());
    FS_REQUIRE(alice.listen_port() != 0);
    FS_REQUIRE(bob.listen_port() != 0);
    FS_REQUIRE(alice.listen_port() != bob.listen_port());

    // Alice connects to Bob, requiring Bob to authenticate as bob.node_id().
    alice.connect_to(bob);
    std::cerr << "[T] " << std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now()-t0).count() << " ms: connect issued" << std::endl;
    auto r1 = alice.wait_for_peer(bob.node_id(), std::chrono::seconds(5));
    std::cerr << "[T] " << std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now()-t0).count() << " ms: wait1=" << static_cast<int>(r1) << std::endl;
    FS_REQUIRE(r1 == WaitResult::Success);
    FS_REQUIRE(bob.wait_for_peer(alice.node_id(), std::chrono::seconds(5)) ==
               WaitResult::Success);

    // Both sides observe an authenticated session.
    auto alice_session = alice.runtime().admission().session(bob.node_id());
    auto bob_session = bob.runtime().admission().session(alice.node_id());
    FS_REQUIRE(alice_session != nullptr);
    FS_REQUIRE(bob_session != nullptr);
    FS_REQUIRE(alice_session->is_authenticated());
    FS_REQUIRE(bob_session->is_authenticated());

    // Inbound/outbound bookkeeping: Alice is outbound, Bob is inbound.
    FS_REQUIRE(alice.runtime().admission().connection_count() == 1);
    FS_REQUIRE(bob.runtime().admission().connection_count() == 1);

    alice.stop();
    bob.stop();
}

FS_TEST(harness_three_node_topology_distinct_ids_storage) {
    TopologyConfig topology{make_network_id()};

    NodeFixture alice(topology);
    NodeFixture bob(topology);
    NodeFixture carol(topology);
    alice.start();
    bob.start();
    carol.start();

    FS_REQUIRE(alice.node_id() != bob.node_id());
    FS_REQUIRE(bob.node_id() != carol.node_id());
    FS_REQUIRE(alice.node_id() != carol.node_id());

    // Distinct storage files (distinct temp dirs).
    FS_REQUIRE(alice.temp_dir() != bob.temp_dir());
    FS_REQUIRE(bob.temp_dir() != carol.temp_dir());

    // Distinct dynamic ports.
    FS_REQUIRE(alice.listen_port() != bob.listen_port());
    FS_REQUIRE(bob.listen_port() != carol.listen_port());
    FS_REQUIRE(alice.listen_port() != carol.listen_port());

    alice.connect_to(bob);
    bob.connect_to(carol);

    FS_REQUIRE(alice.wait_for_peer(bob.node_id(), std::chrono::seconds(5)) ==
               WaitResult::Success);
    std::cerr << "DBG after first wait_for_peer" << std::endl;
    FS_REQUIRE(bob.wait_for_peer(alice.node_id(), std::chrono::seconds(5)) ==
               WaitResult::Success);
    FS_REQUIRE(bob.wait_for_peer(alice.node_id(), std::chrono::seconds(5)) ==
               WaitResult::Success);
    FS_REQUIRE(carol.wait_for_peer(bob.node_id(), std::chrono::seconds(5)) ==
               WaitResult::Success);

    alice.stop();
    bob.stop();
    carol.stop();
}

//------------------------------------------------------------------------------
// Message processing
//------------------------------------------------------------------------------

FS_TEST(harness_ping_pong_round_trip) {
    TopologyConfig topology{make_network_id()};
    NodeFixture alice(topology);
    NodeFixture bob(topology);
    alice.start();
    bob.start();

    alice.connect_to(bob);
    FS_REQUIRE(alice.wait_for_peer(bob.node_id(), std::chrono::seconds(5)) ==
               WaitResult::Success);

    Bytes ping = Bytes{0x01, 0x02, 0x03, 0x04};
    auto ec = alice.send(bob, PeerMessageType::kPing, ping);
    FS_REQUIRE(!ec);

    // Bob auto-responds with kPong; Alice's observer captures it.
    ReceivedMessage msg;
    FS_REQUIRE(alice.receive_next(msg, std::chrono::seconds(5)) == WaitResult::Success);
    FS_REQUIRE(msg.type == PeerMessageType::kPong);
    FS_REQUIRE(msg.peer_node_id == bob.node_id());
    FS_REQUIRE(msg.payload == ping);

    alice.stop();
    bob.stop();
}

FS_TEST(harness_custom_application_message_round_trip) {
    TopologyConfig topology{make_network_id()};
    NodeFixture alice(topology);
    NodeFixture bob(topology);
    alice.start();
    bob.start();

    alice.connect_to(bob);
    FS_REQUIRE(alice.wait_for_peer(bob.node_id(), std::chrono::seconds(5)) ==
               WaitResult::Success);

    Bytes app_msg = Bytes{'h', 'e', 'l', 'l', 'o'};
    auto ec = alice.send(bob, kTestAppMessage, app_msg);
    FS_REQUIRE(!ec);

    ReceivedMessage msg;
    FS_REQUIRE(bob.receive_next(msg, std::chrono::seconds(5)) == WaitResult::Success);
    FS_REQUIRE(msg.type == kTestAppMessage);
    FS_REQUIRE(msg.peer_node_id == alice.node_id());
    FS_REQUIRE(msg.payload == app_msg);

    alice.stop();
    bob.stop();
}

FS_TEST(harness_unknown_message_type_rejected) {
    TopologyConfig topology{make_network_id()};
    NodeFixture alice(topology);
    NodeFixture bob(topology);
    alice.start();
    bob.start();

    alice.connect_to(bob);
    FS_REQUIRE(alice.wait_for_peer(bob.node_id(), std::chrono::seconds(5)) ==
               WaitResult::Success);

    // An unhandled message type causes the receiver to close the session.
    auto ec = alice.send(bob, static_cast<PeerMessageType>(9999), Bytes{});
    FS_REQUIRE(!ec);

    FS_REQUIRE(bob.wait_for_no_peer(alice.node_id(), std::chrono::seconds(5)) ==
               WaitResult::Success);
    FS_REQUIRE(bob.is_running());

    alice.stop();
    bob.stop();
}

FS_TEST(harness_oversized_message_rejected) {
    TopologyConfig topology{make_network_id()};
    NodeFixture alice(topology);
    NodeFixture bob(topology);
    alice.start();
    bob.start();

    alice.connect_to(bob);
    FS_REQUIRE(alice.wait_for_peer(bob.node_id(), std::chrono::seconds(5)) ==
               WaitResult::Success);

    // Oversized payload exceeds kMaxPeerRecordBytes.
    Bytes oversized(kMaxPeerRecordBytes + 1, 0x41);
    auto ec = alice.send(bob, kTestAppMessage, oversized);
    FS_REQUIRE(!ec);

    FS_REQUIRE(bob.wait_for_no_peer(alice.node_id(), std::chrono::seconds(5)) ==
               WaitResult::Success);
    FS_REQUIRE(bob.is_running());

    alice.stop();
    bob.stop();
}

FS_TEST(harness_graceful_disconnect) {
    TopologyConfig topology{make_network_id()};
    NodeFixture alice(topology);
    NodeFixture bob(topology);
    alice.start();
    bob.start();

    alice.connect_to(bob);
    FS_REQUIRE(alice.wait_for_peer(bob.node_id(), std::chrono::seconds(5)) ==
               WaitResult::Success);

    auto ec = alice.send(bob, PeerMessageType::kDisconnect, Bytes{});
    FS_REQUIRE(!ec);

    FS_REQUIRE(bob.wait_for_no_peer(alice.node_id(), std::chrono::seconds(5)) ==
               WaitResult::Success);
    FS_REQUIRE(alice.wait_for_no_peer(bob.node_id(), std::chrono::seconds(5)) ==
               WaitResult::Success);
    FS_REQUIRE(alice.runtime().admission().connection_count() == 0);
    FS_REQUIRE(bob.runtime().admission().connection_count() == 0);

    alice.stop();
    bob.stop();
}

//------------------------------------------------------------------------------
// Peer discovery (wire-level, authenticated sessions)
//------------------------------------------------------------------------------

FS_TEST(harness_bootstrap_ingestion_and_auto_connect) {
    TopologyConfig topology{make_network_id()};
    std::uint64_t now = now_ms();
    const std::uint64_t expiry =
        now + std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::hours(1)).count();

    NodeFixture bob(topology);
    bob.start();

    // Bob signs his own bootstrap record advertising his listening endpoint.
    auto bob_record = make_record_from(bob, topology.network_identifier, 1, expiry,
                                       {{TransportType::Tcp, "127.0.0.1", bob.listen_port()}});

    NodeFixture alice(topology);
    alice.runtime().config().bootstrap_seed_records = {bob_record};
    alice.start();

    // Alice ingests, validates, persists Bob's record, and auto-connects.
    FS_REQUIRE(alice.wait_for_peer(bob.node_id(), std::chrono::seconds(5)) ==
               WaitResult::Success);
    FS_REQUIRE(alice.runtime().store().get(bob.node_id()).has_value());
    FS_REQUIRE(alice.runtime().admission().connection_count() == 1);

    alice.stop();
    bob.stop();
}

FS_TEST(harness_peer_record_request_response_discovery) {
    TopologyConfig topology{make_network_id()};

    // Start Bob and Carol first so their listening ports are known.
    NodeFixture bob(topology);
    NodeFixture carol(topology);
    bob.start();
    carol.start();

    // Carol signs her own record advertising her listening endpoint.
    auto carol_record = make_record_from(carol, topology.network_identifier, 1, 0,
                                         {{TransportType::Tcp, "127.0.0.1", carol.listen_port()}});

    // Bob stores Carol's record as verified (as if Bob learned it earlier).
    {
        std::uint64_t now = now_ms();
        auto ura = bob.runtime().store().upsert(carol_record, DiscoverySource::Bootstrap, now);
        FS_REQUIRE(ura == UpsertResult::Inserted);
        bob.runtime().store().mark_verified(carol.node_id(), now);
    }

    // Alice starts with no knowledge of Carol.
    NodeFixture alice(topology);
    alice.start();

    alice.connect_to(bob);
    FS_REQUIRE(alice.wait_for_peer(bob.node_id(), std::chrono::seconds(5)) ==
               WaitResult::Success);

    // Alice asks Bob for peers.
    ByteWriter req_writer;
    req_writer.u32(0); // request all
    auto ec = alice.send(bob, PeerMessageType::kPeerRecordRequest,
                         std::move(req_writer).take());
    FS_REQUIRE(!ec);

    // Alice receives Carol's record via kPeerRecordResponse and persists it.
    ReceivedMessage msg;
    FS_REQUIRE(alice.receive_next(msg, std::chrono::seconds(5)) == WaitResult::Success);
    FS_REQUIRE(msg.type == PeerMessageType::kPeerRecordResponse);
    FS_REQUIRE(msg.peer_node_id == bob.node_id());

    FS_REQUIRE(alice.runtime().store().get(carol.node_id()).has_value());

    // Alice connects to Carol (Carol authenticates as carol.node_id()).
    FS_REQUIRE(alice.connect_to(carol));
    FS_REQUIRE(alice.wait_for_peer(carol.node_id(), std::chrono::seconds(5)) ==
               WaitResult::Success);

    alice.stop();
    bob.stop();
    carol.stop();
}

FS_TEST(harness_peer_record_announce_discovery) {
    TopologyConfig topology{make_network_id()};

    NodeFixture bob(topology);
    NodeFixture carol(topology);
    bob.start();
    carol.start();

    auto carol_record = make_record_from(carol, topology.network_identifier, 1, 0,
                                         {{TransportType::Tcp, "127.0.0.1", carol.listen_port()}});

    NodeFixture alice(topology);
    alice.start();

    alice.connect_to(bob);
    FS_REQUIRE(alice.wait_for_peer(bob.node_id(), std::chrono::seconds(5)) ==
               WaitResult::Success);

    // Bob announces Carol's record to Alice (unsolicited announce).
    Bytes encoded = encode_peer_record_announce(carol_record);
    auto ec = bob.send(alice, PeerMessageType::kPeerRecordAnnounce, encoded);
    FS_REQUIRE(!ec);

    // Alice validates and persists Carol's record.
    ReceivedMessage msg;
    FS_REQUIRE(alice.receive_next(msg, std::chrono::seconds(5)) == WaitResult::Success);
    FS_REQUIRE(msg.type == PeerMessageType::kPeerRecordAnnounce);
    FS_REQUIRE(alice.runtime().store().get(carol.node_id()).has_value());

    alice.stop();
    bob.stop();
    carol.stop();
}

FS_TEST(harness_invalid_signed_record_rejected) {
    TopologyConfig topology{make_network_id()};

    Ed25519KeyProvider bob_provider = Ed25519KeyProvider::generate();
    Ed25519KeyProvider carol_provider = Ed25519KeyProvider::generate();

    NodeFixture bob(topology);
    NodeFixture alice(topology);
    bob.start();
    alice.start();

    alice.connect_to(bob);
    FS_REQUIRE(alice.wait_for_peer(bob.node_id(), std::chrono::seconds(5)) ==
               WaitResult::Success);

    // Tampered Carol record: flip a signature byte.
    auto carol_record = make_record(carol_provider, topology.network_identifier, 1, 0,
                                    {{TransportType::Tcp, "127.0.0.1", 30333}});
    auto tampered = carol_record;
    tampered.signature[0] ^= 0xff;

    Bytes encoded = encode_peer_record_response({tampered});
    auto ec = bob.send(alice, PeerMessageType::kPeerRecordResponse, encoded);
    FS_REQUIRE(!ec);

    // Give Alice a moment to process, then confirm the bad record was not stored.
    std::this_thread::sleep_for(std::chrono::milliseconds(200));
    FS_REQUIRE(!alice.runtime().store().get(carol_provider.node_id()).has_value());
    FS_REQUIRE(alice.is_running());
    FS_REQUIRE(bob.is_running());

    alice.stop();
    bob.stop();
}

FS_TEST(harness_wrong_network_record_rejected) {
    TopologyConfig topology{make_network_id()};
    Bytes other_network(32, 0x99);

    Ed25519KeyProvider bob_provider = Ed25519KeyProvider::generate();
    Ed25519KeyProvider carol_provider = Ed25519KeyProvider::generate();

    NodeFixture bob(topology);
    NodeFixture alice(topology);
    bob.start();
    alice.start();

    alice.connect_to(bob);
    FS_REQUIRE(alice.wait_for_peer(bob.node_id(), std::chrono::seconds(5)) ==
               WaitResult::Success);

    // Carol's record signed under a different network identifier.
    auto wrong_net = make_record(carol_provider, other_network, 1, 0,
                                 {{TransportType::Tcp, "127.0.0.1", 30333}});
    Bytes encoded = encode_peer_record_response({wrong_net});
    auto ec = bob.send(alice, PeerMessageType::kPeerRecordResponse, encoded);
    FS_REQUIRE(!ec);

    std::this_thread::sleep_for(std::chrono::milliseconds(200));
    FS_REQUIRE(!alice.runtime().store().get(carol_provider.node_id()).has_value());
    FS_REQUIRE(alice.is_running());

    alice.stop();
    bob.stop();
}

FS_TEST(harness_expired_and_stale_records_rejected) {
    TopologyConfig topology{make_network_id()};

    Ed25519KeyProvider carol_provider = Ed25519KeyProvider::generate();

    NodeFixture bob(topology);
    NodeFixture alice(topology);
    bob.start();
    alice.start();

    alice.connect_to(bob);
    FS_REQUIRE(alice.wait_for_peer(bob.node_id(), std::chrono::seconds(5)) ==
               WaitResult::Success);

    const std::uint64_t now = now_ms();
    const std::uint64_t future =
        now + std::chrono::duration_cast<std::chrono::milliseconds>(
                  std::chrono::hours(1)).count();

    // Establish a valid baseline record for carol at sequence 5 so that stale
    // and expired records can be rejected relative to it.
    auto baseline = make_record(carol_provider, topology.network_identifier, 5, future,
                                {{TransportType::Tcp, "127.0.0.1", 30333}});
    Bytes encoded = encode_peer_record_response({baseline});
    auto ec = bob.send(alice, PeerMessageType::kPeerRecordResponse, encoded);
    FS_REQUIRE(!ec);

    // Wait until the baseline record is persisted.
    const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(5);
    while (std::chrono::steady_clock::now() < deadline) {
        if (alice.runtime().store().get(carol_provider.node_id()).has_value()) break;
        std::this_thread::sleep_for(std::chrono::milliseconds(20));
    }
    {
        auto entry = alice.runtime().store().get(carol_provider.node_id());
        FS_REQUIRE(entry.has_value());
        FS_REQUIRE(entry->record.sequence == 5);
    }

    // Expired record (higher sequence, past expiry) must be rejected.
    auto expired = make_record(carol_provider, topology.network_identifier, 6, now - 1000,
                               {{TransportType::Tcp, "127.0.0.1", 30333}});
    encoded = encode_peer_record_response({expired});
    ec = bob.send(alice, PeerMessageType::kPeerRecordResponse, encoded);
    FS_REQUIRE(!ec);

    // Stale record (lower sequence than the baseline) must be rejected.
    auto stale = make_record(carol_provider, topology.network_identifier, 4, future,
                             {{TransportType::Tcp, "127.0.0.1", 30333}});
    encoded = encode_peer_record_response({stale});
    ec = bob.send(alice, PeerMessageType::kPeerRecordResponse, encoded);
    FS_REQUIRE(!ec);

    std::this_thread::sleep_for(std::chrono::milliseconds(200));

    // The baseline remains; expired and stale records did not overwrite it.
    auto entry = alice.runtime().store().get(carol_provider.node_id());
    FS_REQUIRE(entry.has_value());
    FS_REQUIRE(entry->record.sequence == 5);
    FS_REQUIRE(alice.is_running());

    alice.stop();
    bob.stop();
}

FS_TEST(harness_multiple_bootstrap_records_connect_all) {
    TopologyConfig topology{make_network_id()};
    std::uint64_t now = now_ms();
    const std::uint64_t expiry =
        now + std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::hours(1)).count();

    NodeFixture bob(topology);
    NodeFixture dave(topology);
    bob.start();
    dave.start();

    auto bob_record = make_record_from(bob, topology.network_identifier, 1, expiry,
                                       {{TransportType::Tcp, "127.0.0.1", bob.listen_port()}});
    auto dave_record = make_record_from(dave, topology.network_identifier, 1, expiry,
                                        {{TransportType::Tcp, "127.0.0.1", dave.listen_port()}});

    NodeFixture alice(topology);
    alice.runtime().config().bootstrap_seed_records = {bob_record, dave_record};
    alice.start();

    // Alice should auto-connect to both bootstrap targets.
    FS_REQUIRE(alice.wait_for_peer(bob.node_id(), std::chrono::seconds(5)) ==
               WaitResult::Success);
    FS_REQUIRE(alice.wait_for_peer(dave.node_id(), std::chrono::seconds(5)) ==
               WaitResult::Success);
    FS_REQUIRE(alice.runtime().admission().connection_count() == 2);

    alice.stop();
    bob.stop();
    dave.stop();
}

FS_TEST(harness_failed_bootstrap_endpoint_does_not_disable_valid_sources) {
    TopologyConfig topology{make_network_id()};
    std::uint64_t now = now_ms();
    const std::uint64_t expiry =
        now + std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::hours(1)).count();

    NodeFixture bob(topology);
    bob.start();

    auto bob_record = make_record_from(bob, topology.network_identifier, 1, expiry,
                                       {{TransportType::Tcp, "127.0.0.1", bob.listen_port()}});
    // Ghost endpoint: nothing listening, signed by an independent provider.
    Ed25519KeyProvider ghost_provider = Ed25519KeyProvider::generate();
    auto ghost_record = make_record(ghost_provider, topology.network_identifier, 1, expiry,
                                    {{TransportType::Tcp, "127.0.0.1", 1}});

    NodeFixture alice(topology);
    alice.runtime().config().bootstrap_seed_records = {ghost_record, bob_record};
    alice.start();

    FS_REQUIRE(alice.is_running());
    FS_REQUIRE(alice.wait_for_peer(bob.node_id(), std::chrono::seconds(5)) ==
               WaitResult::Success);

    alice.stop();
    bob.stop();
}

//------------------------------------------------------------------------------
// Fault injection and recovery
//------------------------------------------------------------------------------

FS_TEST(harness_connection_refused_does_not_crash) {
    TopologyConfig topology{make_network_id()};
    NodeFixture alice(topology);
    alice.start();

    // Nothing listens on port 1. Use a promise to wait for the async connect
    // to complete (fail fast with connection_refused).
    std::promise<void> connect_done;
    alice.runtime().admission().connect_to_peer(
        NodeId::parse("fn10000000000000000000000000000000000000000000000000000000000000000"),
        "127.0.0.1", 1,
        [&connect_done](const boost::system::error_code& ec, std::shared_ptr<PeerSession>) {
            FS_REQUIRE(ec); // connection must fail
            connect_done.set_value();
        });
    // Wait for the async operation to settle.
    auto future = connect_done.get_future();
    FS_REQUIRE(future.wait_for(std::chrono::seconds(5)) == std::future_status::ready);
    // Yield once so the admission controller's io_context processes the completion.
    std::this_thread::sleep_for(std::chrono::milliseconds(50));
    FS_REQUIRE(alice.is_running());
    FS_REQUIRE(alice.runtime().admission().connection_count() == 0);

    alice.stop();
}

FS_TEST(harness_peer_stops_during_active_session) {
    TopologyConfig topology{make_network_id()};
    NodeFixture alice(topology);
    NodeFixture bob(topology);
    alice.start();
    bob.start();

    alice.connect_to(bob);
    FS_REQUIRE(alice.wait_for_peer(bob.node_id(), std::chrono::seconds(5)) ==
               WaitResult::Success);

    bob.stop();

    FS_REQUIRE(alice.wait_for_no_peer(bob.node_id(), std::chrono::seconds(5)) ==
               WaitResult::Success);
    FS_REQUIRE(alice.is_running());
    FS_REQUIRE(alice.runtime().admission().connection_count() == 0);

    alice.stop();
}

FS_TEST(harness_listener_restart_and_reaccept) {
    TopologyConfig topology{make_network_id()};
    NodeFixture alice(topology);
    NodeFixture bob(topology);
    alice.start();
    bob.start();

    alice.connect_to(bob);
    FS_REQUIRE(alice.wait_for_peer(bob.node_id(), std::chrono::seconds(5)) ==
               WaitResult::Success);

    alice.stop(); // closes listener and sessions

    // Restart Alice with a fresh dynamic port.
    alice.start();
    FS_REQUIRE(alice.listen_port() != 0);

    alice.connect_to(bob);
    FS_REQUIRE(alice.wait_for_peer(bob.node_id(), std::chrono::seconds(5)) ==
               WaitResult::Success);
    FS_REQUIRE(alice.is_running());

    alice.stop();
    bob.stop();
}

FS_TEST(harness_partition_and_heal) {
    TopologyConfig topology{make_network_id()};
    NodeFixture alice(topology);
    NodeFixture bob(topology);
    alice.start();
    bob.start();

    alice.connect_to(bob);
    FS_REQUIRE(alice.wait_for_peer(bob.node_id(), std::chrono::seconds(5)) ==
               WaitResult::Success);

    // Partition: Alice disconnects Bob.
    auto ec = alice.disconnect(bob.node_id());
    FS_REQUIRE(!ec);
    FS_REQUIRE(alice.wait_for_no_peer(bob.node_id(), std::chrono::seconds(5)) ==
               WaitResult::Success);
    FS_REQUIRE(bob.wait_for_no_peer(alice.node_id(), std::chrono::seconds(5)) ==
               WaitResult::Success);

    // Heal: reconnect.
    FS_REQUIRE(alice.connect_to(bob));
    FS_REQUIRE(alice.wait_for_peer(bob.node_id(), std::chrono::seconds(5)) ==
               WaitResult::Success);
    FS_REQUIRE(bob.wait_for_peer(alice.node_id(), std::chrono::seconds(5)) ==
               WaitResult::Success);

    alice.stop();
    bob.stop();
}

FS_TEST(harness_reconnection_after_endpoint_recovery) {
    TopologyConfig topology{make_network_id()};
    NodeFixture alice(topology);
    NodeFixture bob(topology);
    alice.start();
    bob.start();

    alice.connect_to(bob);
    FS_REQUIRE(alice.wait_for_peer(bob.node_id(), std::chrono::seconds(5)) ==
               WaitResult::Success);

    bob.stop();
    FS_REQUIRE(alice.wait_for_no_peer(bob.node_id(), std::chrono::seconds(5)) ==
               WaitResult::Success);

    // Bob comes back with a new dynamic port.
    bob.start();
    FS_REQUIRE(alice.connect_to(bob));
    FS_REQUIRE(alice.wait_for_peer(bob.node_id(), std::chrono::seconds(5)) ==
               WaitResult::Success);

    alice.stop();
    bob.stop();
}

FS_TEST(harness_repeated_connection_failures_no_storm) {
    TopologyConfig topology{make_network_id()};
    NodeFixture alice(topology);
    alice.start();

    // Repeated attempts to a dead endpoint: each fails fast, no storm.
    std::atomic<int> completed{0};
    for (int i = 0; i < 10; ++i) {
        (void)i;
        alice.runtime().admission().connect_to_peer(
            NodeId::parse("fn1" + std::string(64, '0')),
            "127.0.0.1", 1,
            [&completed](const boost::system::error_code& ec, std::shared_ptr<PeerSession>) {
                FS_REQUIRE(ec);
                ++completed;
            });
    }
    // Wait for all connects to complete (10 * 5s max, should be much faster).
    auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(10);
    while (completed < 10 && std::chrono::steady_clock::now() < deadline) {
        std::this_thread::sleep_for(std::chrono::milliseconds(20));
    }
    FS_REQUIRE(completed == 10);
    std::this_thread::sleep_for(std::chrono::milliseconds(50));
    FS_REQUIRE(alice.is_running());
    FS_REQUIRE(alice.runtime().admission().connection_count() == 0);

    alice.stop();
}

FS_TEST(harness_node_restart_persistent_peer_store) {
    TopologyConfig topology{make_network_id()};
    std::uint64_t now = now_ms();
    const std::uint64_t expiry =
        now + std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::hours(1)).count();

    NodeFixture bob(topology);
    bob.start();

    auto bob_record = make_record_from(bob, topology.network_identifier, 1, expiry,
                                       {{TransportType::Tcp, "127.0.0.1", bob.listen_port()}});

    NodeFixture alice(topology);
    alice.runtime().config().bootstrap_seed_records = {bob_record};
    alice.start();

    FS_REQUIRE(alice.wait_for_peer(bob.node_id(), std::chrono::seconds(5)) ==
               WaitResult::Success);

    // Bob's record is persisted in Alice's store.
    FS_REQUIRE(alice.runtime().store().get(bob.node_id()).has_value());

    // Restart Alice: the peer store survives.
    alice.stop();
    alice.start();

    FS_REQUIRE(alice.runtime().store().get(bob.node_id()).has_value());

    // Reconnect to Bob.
    FS_REQUIRE(alice.connect_to(bob));
    FS_REQUIRE(alice.wait_for_peer(bob.node_id(), std::chrono::seconds(5)) ==
               WaitResult::Success);

    alice.stop();
    bob.stop();
}

FS_TEST(harness_malformed_messages_do_not_crash_peers) {
    TopologyConfig topology{make_network_id()};
    NodeFixture alice(topology);
    NodeFixture bob(topology);
    alice.start();
    bob.start();

    alice.connect_to(bob);
    FS_REQUIRE(alice.wait_for_peer(bob.node_id(), std::chrono::seconds(5)) ==
               WaitResult::Success);

    // Malformed peer-record response (truncated garbage).
    Bytes garbage = Bytes{0x00, 0x01, 0x02};
    auto ec = bob.send(alice, PeerMessageType::kPeerRecordResponse, garbage);
    FS_REQUIRE(!ec);

    std::this_thread::sleep_for(std::chrono::milliseconds(200));
    FS_REQUIRE(alice.is_running());
    FS_REQUIRE(bob.is_running());
    FS_REQUIRE(alice.wait_for_peer(bob.node_id(), std::chrono::seconds(2)) ==
               WaitResult::Success); // session still alive

    alice.stop();
    bob.stop();
}

FS_TEST(harness_duplicate_peer_discovery_no_storm) {
    TopologyConfig topology{make_network_id()};

    NodeFixture bob(topology);
    NodeFixture carol(topology);
    bob.start();
    carol.start();

    auto carol_record = make_record_from(carol, topology.network_identifier, 1, 0,
                                         {{TransportType::Tcp, "127.0.0.1", carol.listen_port()}});

    NodeFixture alice(topology);
    alice.start();

    alice.connect_to(bob);
    FS_REQUIRE(alice.wait_for_peer(bob.node_id(), std::chrono::seconds(5)) ==
               WaitResult::Success);

    // Bob stores Carol and shares her twice.
    {
        std::uint64_t now = now_ms();
        auto ura = bob.runtime().store().upsert(carol_record, DiscoverySource::Bootstrap, now);
        FS_REQUIRE(ura == UpsertResult::Inserted);
        bob.runtime().store().mark_verified(carol.node_id(), now);
    }

    Bytes encoded = encode_peer_record_response({carol_record});
    auto ec = bob.send(alice, PeerMessageType::kPeerRecordResponse, encoded);
    FS_REQUIRE(!ec);
    std::this_thread::sleep_for(std::chrono::milliseconds(100));

    ec = bob.send(alice, PeerMessageType::kPeerRecordResponse, encoded);
    FS_REQUIRE(!ec);
    std::this_thread::sleep_for(std::chrono::milliseconds(100));

    FS_REQUIRE(alice.runtime().store().get(carol.node_id()).has_value());
    // Only one connection to Carol (duplicate discovery does not storm).
    FS_REQUIRE(alice.runtime().admission().connection_count() == 1);

    alice.stop();
    bob.stop();
    carol.stop();
}

//------------------------------------------------------------------------------
// Resource and lifecycle
//------------------------------------------------------------------------------

FS_TEST(harness_shutdown_while_sessions_active) {
    TopologyConfig topology{make_network_id()};
    NodeFixture alice(topology);
    NodeFixture bob(topology);
    alice.start();
    bob.start();

    alice.connect_to(bob);
    FS_REQUIRE(alice.wait_for_peer(bob.node_id(), std::chrono::seconds(5)) ==
               WaitResult::Success);

    // Shut down Alice while the session is active; must unblock workers and not
    // hang.
    alice.stop();

    // Bob detects the closed session.
    FS_REQUIRE(bob.wait_for_no_peer(alice.node_id(), std::chrono::seconds(5)) ==
               WaitResult::Success);
    FS_REQUIRE(bob.runtime().admission().connection_count() == 0);

    bob.stop();
}

FS_TEST(harness_repeated_start_stop_cycles) {
    TopologyConfig topology{make_network_id()};
    NodeFixture alice(topology);
    for (int i = 0; i < 3; ++i) {
        alice.start();
        FS_REQUIRE(alice.listen_port() != 0);
        FS_REQUIRE(alice.is_running());
        alice.stop();
    }
}

FS_TEST(harness_failed_initialization_cleanup) {
    TopologyConfig topology{make_network_id()};
    NodeFixture alice(topology);
    alice.start(); // valid start

    // Re-initialize with a missing identity path must fail cleanly, not throw.
    NetworkRuntimeConfig bad = alice.runtime().config();
    bad.identity_path = "/nonexistent/fst/identity";
    auto ec = alice.runtime().initialize(bad);
    FS_REQUIRE(ec);
    FS_REQUIRE(!alice.is_running());

    alice.stop();
}

FS_TEST(harness_malformed_config_rejected) {
    TopologyConfig topology{make_network_id()};
    NodeFixture alice(topology);
    alice.start(); // valid start, runtime initialized and running

    // Re-initialize with a network_identifier that is not 32 bytes: must be
    // rejected with an error_code, never throw, and leave the runtime stopped.
    NetworkRuntimeConfig bad = alice.runtime().config();
    bad.network_identifier = Bytes{0x01, 0x02, 0x03}; // not 32 bytes
    auto ec = alice.runtime().initialize(bad);
    FS_REQUIRE(ec);
    FS_REQUIRE(!alice.is_running());

    alice.stop();
}

FS_TEST(harness_connection_count_returns_after_disconnect) {
    TopologyConfig topology{make_network_id()};
    NodeFixture alice(topology);
    NodeFixture bob(topology);
    alice.start();
    bob.start();

    alice.connect_to(bob);
    FS_REQUIRE(alice.wait_for_peer(bob.node_id(), std::chrono::seconds(5)) ==
               WaitResult::Success);
    FS_REQUIRE(alice.runtime().admission().connection_count() == 1);
    FS_REQUIRE(bob.runtime().admission().connection_count() == 1);

    alice.disconnect(bob.node_id());
    FS_REQUIRE(alice.wait_for_no_peer(bob.node_id(), std::chrono::seconds(5)) ==
               WaitResult::Success);
    FS_REQUIRE(alice.runtime().admission().connection_count() == 0);
    FS_REQUIRE(bob.runtime().admission().connection_count() == 0);

    alice.stop();
    bob.stop();
}
