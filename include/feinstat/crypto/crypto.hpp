#pragma once

#include "feinstat/common/types.hpp"

#include <cstdint>
#include <string>

namespace feinstat::crypto {

struct KeyPair {
    PublicKey public_key{};
    SecretKey secret_key{};
};

struct X25519KeyPair {
    PublicKey public_key{};
    SecretKey secret_key{};
};

using AeadKey = std::array<Byte, 32>;
using AeadNonce = std::array<Byte, 12>;
using AeadNoncePrefix = std::array<Byte, 4>;

struct PeerSessionKeys {
    AeadKey client_to_server{};
    AeadKey server_to_client{};
    AeadNoncePrefix client_nonce_prefix{};
    AeadNoncePrefix server_nonce_prefix{};
};

struct EncryptedSecret {
    std::array<Byte, 16> salt{};
    std::array<Byte, 12> nonce{};
    Bytes ciphertext;
    std::array<Byte, 16> tag{};
    std::uint32_t iterations{310'000};
};

[[nodiscard]] Hash sha256(const Bytes& data);
[[nodiscard]] std::string hex(const Byte* data, std::size_t size);
[[nodiscard]] Bytes unhex(const std::string& text);
[[nodiscard]] Address address_from_public_key(const PublicKey& public_key);
[[nodiscard]] bool is_valid_address(const Address& address) noexcept;

[[nodiscard]] KeyPair generate_ed25519_keypair();
[[nodiscard]] X25519KeyPair generate_x25519_keypair();
[[nodiscard]] SecretKey derive_x25519_shared_secret(const SecretKey& private_key,
                                                     const PublicKey& peer_public_key);
[[nodiscard]] PeerSessionKeys derive_peer_session_keys(const SecretKey& shared_secret,
                                                         const Bytes& transcript);
[[nodiscard]] Bytes aes256gcm_encrypt(const AeadKey& key, const AeadNonce& nonce,
                                      const Bytes& associated_data, const Bytes& plaintext);
[[nodiscard]] Bytes aes256gcm_decrypt(const AeadKey& key, const AeadNonce& nonce,
                                      const Bytes& associated_data, const Bytes& ciphertext_and_tag);
[[nodiscard]] PublicKey derive_ed25519_public_key(const SecretKey& secret_key);
[[nodiscard]] Signature sign_ed25519(const SecretKey& secret_key, const Bytes& message);
[[nodiscard]] bool verify_ed25519(const PublicKey& public_key,
                                  const Bytes& message,
                                  const Signature& signature);

[[nodiscard]] EncryptedSecret encrypt_secret(const SecretKey& secret,
                                              const std::string& passphrase,
                                              std::uint32_t iterations = 310'000);
[[nodiscard]] SecretKey decrypt_secret(const EncryptedSecret& encrypted,
                                       const std::string& passphrase);

} // namespace feinstat::crypto
