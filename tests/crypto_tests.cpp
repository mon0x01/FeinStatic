#include "test.hpp"

#include "feinstat/crypto/crypto.hpp"
#include "feinstat/ledger/wallet.hpp"

FS_TEST(ed25519_keypair_signs_and_verifies) {
    const auto wallet = feinstat::ledger::Wallet::generate();
    const feinstat::Bytes message{'f', 'e', 'i', 'n'};
    const auto signature = feinstat::crypto::sign_ed25519(wallet.secret_key(), message);
    FS_REQUIRE(feinstat::crypto::verify_ed25519(wallet.public_key(), message, signature));
    auto altered = message;
    altered[0] ^= 0x01;
    FS_REQUIRE(!feinstat::crypto::verify_ed25519(wallet.public_key(), altered, signature));
    FS_REQUIRE(feinstat::crypto::is_valid_address(wallet.address()));
}

FS_TEST(wallet_secret_is_authenticated_and_encrypted) {
    const auto wallet = feinstat::ledger::Wallet::generate();
    const auto encrypted = feinstat::crypto::encrypt_secret(wallet.secret_key(), "correct horse battery", 100'000);
    const auto decrypted = feinstat::crypto::decrypt_secret(encrypted, "correct horse battery");
    FS_REQUIRE(decrypted == wallet.secret_key());
    FS_REQUIRE_THROWS(feinstat::crypto::decrypt_secret(encrypted, "wrong passphrase"));
}

FS_TEST(hex_encoding_round_trips) {
    const feinstat::Bytes original{0x00, 0x01, 0x7f, 0x80, 0xff};
    FS_REQUIRE(feinstat::crypto::unhex(feinstat::crypto::hex(original.data(), original.size())) == original);
    FS_REQUIRE_THROWS(feinstat::crypto::unhex("abc"));
    FS_REQUIRE_THROWS(feinstat::crypto::unhex("xz"));
}

FS_TEST(wallet_encryption_rejects_unsafe_kdf_parameters_and_oversized_passphrases) {
    const auto wallet = feinstat::ledger::Wallet::generate();
    FS_REQUIRE_THROWS(feinstat::crypto::encrypt_secret(wallet.secret_key(), "valid-passphrase", 99'999));
    FS_REQUIRE_THROWS(feinstat::crypto::encrypt_secret(wallet.secret_key(), "valid-passphrase", 5'000'001));
    FS_REQUIRE_THROWS(feinstat::crypto::encrypt_secret(wallet.secret_key(), std::string(4097, 'x')));

    auto encrypted = feinstat::crypto::encrypt_secret(wallet.secret_key(), "valid-passphrase", 100'000);
    encrypted.iterations = UINT32_MAX;
    FS_REQUIRE_THROWS(feinstat::crypto::decrypt_secret(encrypted, "valid-passphrase"));
}

FS_TEST(new_wallet_encryption_requires_a_minimum_passphrase_length) {
    const auto wallet = feinstat::ledger::Wallet::generate();
    FS_REQUIRE_THROWS(feinstat::crypto::encrypt_secret(wallet.secret_key(), "JAGGU", 100'000));
    const auto encrypted = feinstat::crypto::encrypt_secret(
        wallet.secret_key(), "a-new-development-passphrase", 100'000);
    const auto decrypted = feinstat::crypto::decrypt_secret(encrypted, "a-new-development-passphrase");
    FS_REQUIRE(decrypted == wallet.secret_key());
}

FS_TEST(peer_x25519_session_keys_are_directional_and_aes_gcm_authenticates_frames) {
    using namespace feinstat;
    const auto client = crypto::generate_x25519_keypair();
    const auto server = crypto::generate_x25519_keypair();
    const auto client_shared = crypto::derive_x25519_shared_secret(client.secret_key, server.public_key);
    const auto server_shared = crypto::derive_x25519_shared_secret(server.secret_key, client.public_key);
    FS_REQUIRE(client_shared == server_shared);

    const Bytes transcript{'f', 'e', 'i', 'n', '-', 's', 'e', 's', 's', 'i', 'o', 'n'};
    const auto client_keys = crypto::derive_peer_session_keys(client_shared, transcript);
    const auto server_keys = crypto::derive_peer_session_keys(server_shared, transcript);
    FS_REQUIRE(client_keys.client_to_server == server_keys.client_to_server);
    FS_REQUIRE(client_keys.server_to_client == server_keys.server_to_client);
    FS_REQUIRE(client_keys.client_to_server != client_keys.server_to_client);
    FS_REQUIRE(client_keys.client_nonce_prefix != client_keys.server_nonce_prefix);

    crypto::AeadNonce nonce{};
    std::copy(client_keys.client_nonce_prefix.begin(), client_keys.client_nonce_prefix.end(), nonce.begin());
    nonce[11] = 7;
    const Bytes aad{'t', 'y', 'p', 'e', '-', '1'};
    const Bytes plaintext{'s', 'e', 'c', 'r', 'e', 't', '-', 'p', 'a', 'y', 'l', 'o', 'a', 'd'};
    const Bytes encrypted = crypto::aes256gcm_encrypt(client_keys.client_to_server, nonce, aad, plaintext);
    FS_REQUIRE(encrypted != plaintext);
    FS_REQUIRE(crypto::aes256gcm_decrypt(server_keys.client_to_server, nonce, aad, encrypted) == plaintext);
    auto changed = encrypted;
    changed[0] ^= 0x80;
    FS_REQUIRE_THROWS(crypto::aes256gcm_decrypt(server_keys.client_to_server, nonce, aad, changed));
    FS_REQUIRE_THROWS(crypto::aes256gcm_decrypt(server_keys.client_to_server, nonce, Bytes{'x'}, encrypted));
}
