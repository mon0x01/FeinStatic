#include "test.hpp"

#include "feinstat/common/bytes.hpp"
#include "feinstat/crypto/crypto.hpp"
#include "feinstat/network/identity.hpp"

#include <cstdio>
#include <filesystem>
#include <string>

using namespace feinstat;
using namespace feinstat::crypto;
using namespace feinstat::network;

FS_TEST(identity_derivation_is_deterministic) {
    auto pair1 = generate_ed25519_keypair();
    (void)pair1;

    NodeId id1 = NodeId::from_public_key(pair1.public_key);
    NodeId id1b = NodeId::from_public_key(pair1.public_key);
    FS_REQUIRE(id1.value() == id1b.value());
    FS_REQUIRE(id1.version() == NodeId::kCurrentVersion);
    FS_REQUIRE(id1.value().substr(0, 3) == "fn1");
    FS_REQUIRE(id1.value().size() == 67); // "fn1" + 64 hex chars
}

FS_TEST(identity_roundtrip_parse) {
    auto pair = generate_ed25519_keypair();
    NodeId original = NodeId::from_public_key(pair.public_key);
    NodeId parsed = NodeId::parse(original.value());
    FS_REQUIRE(parsed.value() == original.value());
    FS_REQUIRE(parsed.version() == original.version());
    FS_REQUIRE(parsed.hash() == original.hash());
}

FS_TEST(identity_matches_public_key) {
    auto pair = generate_ed25519_keypair();
    NodeId id = NodeId::from_public_key(pair.public_key);
    FS_REQUIRE(id.matches(pair.public_key));

    auto other = generate_ed25519_keypair();
    FS_REQUIRE(!id.matches(other.public_key));
}

FS_TEST(identity_parse_rejects_invalid) {
    FS_REQUIRE_THROWS(NodeId::parse(""));
    FS_REQUIRE_THROWS(NodeId::parse("fn1"));
    FS_REQUIRE_THROWS(NodeId::parse("fn2" + std::string(64, '0')));
    FS_REQUIRE_THROWS(NodeId::parse("fn1" + std::string(63, '0')));
    FS_REQUIRE_THROWS(NodeId::parse("fn1" + std::string(65, '0')));
    FS_REQUIRE_THROWS(NodeId::parse("xx1" + std::string(64, '0')));
    FS_REQUIRE_THROWS(NodeId::parse("fn1" + std::string(64, 'g')));
}

FS_TEST(key_provider_generate_sign_verify) {
    Ed25519KeyProvider provider = Ed25519KeyProvider::generate();
    PublicKey pub = provider.public_key();
    NodeId id = provider.node_id();

    Bytes message = {'h', 'e', 'l', 'l', 'o'};
    Signature sig = provider.sign(message);
    FS_REQUIRE(verify_ed25519(pub, message, sig));

    // Wrong message should fail
    Bytes tampered = {'h', 'e', 'l', 'l', 'o', '!'};
    FS_REQUIRE(!verify_ed25519(pub, tampered, sig));

    // NodeId should match public key
    FS_REQUIRE(id.matches(pub));
}

FS_TEST(key_provider_domain_separation) {
    Ed25519KeyProvider provider = Ed25519KeyProvider::generate();
    PublicKey pub = provider.public_key();

    Bytes message = {'t', 'e', 's', 't'};
    Signature sig_a = provider.sign_domain("domain-a", message);
    Signature sig_b = provider.sign_domain("domain-b", message);
    Signature sig_plain = provider.sign(message);

    // Different domains produce different signatures
    FS_REQUIRE(sig_a != sig_b);
    FS_REQUIRE(sig_a != sig_plain);

    // But each verifies under its own domain
    ByteWriter wa;
    wa.string("domain-a");
    wa.blob(message);
    Bytes msg_a = std::move(wa).take();
    FS_REQUIRE(verify_ed25519(pub, msg_a, sig_a));

    ByteWriter wb;
    wb.string("domain-b");
    wb.blob(message);
    Bytes msg_b = std::move(wb).take();
    FS_REQUIRE(verify_ed25519(pub, msg_b, sig_b));
}

FS_TEST(key_provider_multiple_independent_identities) {
    Ed25519KeyProvider a = Ed25519KeyProvider::generate();
    Ed25519KeyProvider b = Ed25519KeyProvider::generate();
    FS_REQUIRE(a.node_id() != b.node_id());
    FS_REQUIRE(a.public_key() != b.public_key());

    Bytes msg = {'m', 's', 'g'};
    Signature sig_a = a.sign(msg);
    FS_REQUIRE(verify_ed25519(a.public_key(), msg, sig_a));
    FS_REQUIRE(!verify_ed25519(b.public_key(), msg, sig_a));
}

FS_TEST(file_key_provider_save_load_roundtrip) {
    const std::string path = (std::filesystem::temp_directory_path() /
                              "fst-identity-test-key").string();
    std::error_code ec;
    std::filesystem::remove(path, ec);

    NodeIdentity identity = NodeIdentity::generate();
    PublicKey pub = identity.public_key();
    NodeId id = NodeId::from_public_key(pub);

    identity.save_new(path);
    FS_REQUIRE(std::filesystem::exists(path));

    FileKeyProvider loaded = FileKeyProvider::load(path);
    FS_REQUIRE(loaded.public_key() == pub);
    FS_REQUIRE(loaded.node_id() == id);

    Bytes msg = {'f', 'i', 'l', 'e'};
    Signature sig = loaded.sign(msg);
    FS_REQUIRE(verify_ed25519(pub, msg, sig));

    std::error_code cleanup_ec;
    std::filesystem::remove(path, cleanup_ec);
}

FS_TEST(file_key_provider_vault_save_load_roundtrip) {
    const std::string path = (std::filesystem::temp_directory_path() /
                              "fst-identity-test-vault").string();
    std::error_code ec;
    std::filesystem::remove(path, ec);

    NodeIdentity identity = NodeIdentity::generate();
    PublicKey pub = identity.public_key();
    NodeId id = NodeId::from_public_key(pub);
    const std::string passphrase = "test-passphrase-12345";

    identity.save_vault_new(path, passphrase);
    FS_REQUIRE(std::filesystem::exists(path));
    FS_REQUIRE(NodeIdentity::is_encrypted_vault_file(path));

    FileKeyProvider loaded = FileKeyProvider::load_vault(path, passphrase);
    FS_REQUIRE(loaded.public_key() == pub);
    FS_REQUIRE(loaded.node_id() == id);

    Bytes msg = {'v', 'a', 'u', 'l', 't'};
    Signature sig = loaded.sign(msg);
    FS_REQUIRE(verify_ed25519(pub, msg, sig));

    // Wrong passphrase should fail
    FS_REQUIRE_THROWS(FileKeyProvider::load_vault(path, "wrong-passphrase"));

    std::error_code cleanup_ec;
    std::filesystem::remove(path, cleanup_ec);
}

FS_TEST(rotation_record_fields) {
    Ed25519KeyProvider old_key = Ed25519KeyProvider::generate();
    Ed25519KeyProvider new_key = Ed25519KeyProvider::generate();

    RotationRecord rotation{};
    rotation.old_node_id = old_key.node_id();
    rotation.new_node_id = new_key.node_id();

    FS_REQUIRE(rotation.old_node_id != rotation.new_node_id);
    FS_REQUIRE(rotation.old_node_id.matches(old_key.public_key()));
    FS_REQUIRE(rotation.new_node_id.matches(new_key.public_key()));
}
