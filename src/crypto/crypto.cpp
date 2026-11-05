#include "feinstat/crypto/crypto.hpp"

#include <openssl/crypto.h>
#include <openssl/evp.h>
#include <openssl/kdf.h>
#include <openssl/rand.h>
#include <openssl/sha.h>

#include <algorithm>
#include <climits>
#include <cctype>
#include <memory>
#include <stdexcept>
#include <vector>

namespace feinstat::crypto {
namespace {

using PKeyPtr = std::unique_ptr<EVP_PKEY, decltype(&EVP_PKEY_free)>;
using PKeyCtxPtr = std::unique_ptr<EVP_PKEY_CTX, decltype(&EVP_PKEY_CTX_free)>;
using MdCtxPtr = std::unique_ptr<EVP_MD_CTX, decltype(&EVP_MD_CTX_free)>;
using CipherCtxPtr = std::unique_ptr<EVP_CIPHER_CTX, decltype(&EVP_CIPHER_CTX_free)>;

void require_openssl(int result, const char* operation) {
    if (result != 1) {
        throw std::runtime_error(std::string("OpenSSL operation failed: ") + operation);
    }
}

void random_bytes(Byte* output, std::size_t size) {
    if (size > static_cast<std::size_t>(INT_MAX)) {
        throw std::length_error("random request too large");
    }
    require_openssl(RAND_bytes(output, static_cast<int>(size)), "RAND_bytes");
}

std::array<Byte, 32> derive_encryption_key(const std::string& passphrase,
                                           const std::array<Byte, 16>& salt,
                                           std::uint32_t iterations) {
    if (passphrase.empty()) throw std::invalid_argument("wallet passphrase cannot be empty");
    if (passphrase.size() > 4096) throw std::length_error("wallet passphrase exceeds 4096 bytes");
    if (iterations < 100'000 || iterations > 5'000'000) {
        throw std::invalid_argument("PBKDF2 iteration count outside supported range");
    }
    std::array<Byte, 32> key{};
    require_openssl(PKCS5_PBKDF2_HMAC(passphrase.data(),
                                      static_cast<int>(passphrase.size()),
                                      salt.data(), static_cast<int>(salt.size()),
                                      static_cast<int>(iterations), EVP_sha256(),
                                      static_cast<int>(key.size()), key.data()),
                    "PBKDF2-HMAC-SHA256");
    return key;
}

} // namespace

Hash sha256(const Bytes& data) {
    Hash result{};
    unsigned int output_size = 0;
    const Byte* input = data.empty() ? nullptr : data.data();
    require_openssl(EVP_Digest(input, data.size(), result.data(), &output_size,
                               EVP_sha256(), nullptr), "SHA-256");
    if (output_size != result.size()) throw std::runtime_error("unexpected SHA-256 output size");
    return result;
}

std::string hex(const Byte* data, std::size_t size) {
    static constexpr char alphabet[] = "0123456789abcdef";
    std::string result;
    result.reserve(size * 2);
    for (std::size_t i = 0; i < size; ++i) {
        result.push_back(alphabet[data[i] >> 4U]);
        result.push_back(alphabet[data[i] & 0x0fU]);
    }
    return result;
}

Bytes unhex(const std::string& text) {
    if (text.size() % 2 != 0) throw std::invalid_argument("hex string has odd length");
    const auto nibble = [](char value) -> Byte {
        if (value >= '0' && value <= '9') return static_cast<Byte>(value - '0');
        if (value >= 'a' && value <= 'f') return static_cast<Byte>(value - 'a' + 10);
        if (value >= 'A' && value <= 'F') return static_cast<Byte>(value - 'A' + 10);
        throw std::invalid_argument("invalid hexadecimal character");
    };
    Bytes result;
    result.reserve(text.size() / 2);
    for (std::size_t i = 0; i < text.size(); i += 2) {
        result.push_back(static_cast<Byte>((nibble(text[i]) << 4U) | nibble(text[i + 1])));
    }
    return result;
}

Address address_from_public_key(const PublicKey& public_key) {
    Bytes input(public_key.begin(), public_key.end());
    const auto digest = sha256(input);
    return "fs1" + hex(digest.data(), 20);
}

bool is_valid_address(const Address& address) noexcept {
    if (address.size() != 43 || address.compare(0, 3, "fs1") != 0) return false;
    for (std::size_t i = 3; i < address.size(); ++i) {
        const char c = address[i];
        if (!((c >= '0' && c <= '9') || (c >= 'a' && c <= 'f'))) return false;
    }
    return true;
}

X25519KeyPair generate_x25519_keypair() {
    PKeyCtxPtr context(EVP_PKEY_CTX_new_id(EVP_PKEY_X25519, nullptr), EVP_PKEY_CTX_free);
    if (!context) throw std::runtime_error("unable to create X25519 key context");
    require_openssl(EVP_PKEY_keygen_init(context.get()), "X25519 keygen init");
    EVP_PKEY* raw_key = nullptr;
    require_openssl(EVP_PKEY_keygen(context.get(), &raw_key), "X25519 key generation");
    PKeyPtr key(raw_key, EVP_PKEY_free);
    X25519KeyPair result{};
    std::size_t public_size = result.public_key.size();
    std::size_t private_size = result.secret_key.size();
    require_openssl(EVP_PKEY_get_raw_public_key(key.get(), result.public_key.data(), &public_size),
                    "extract X25519 public key");
    require_openssl(EVP_PKEY_get_raw_private_key(key.get(), result.secret_key.data(), &private_size),
                    "extract X25519 private key");
    if (public_size != result.public_key.size() || private_size != result.secret_key.size()) {
        throw std::runtime_error("unexpected X25519 key size");
    }
    return result;
}

SecretKey derive_x25519_shared_secret(const SecretKey& private_key,
                                      const PublicKey& peer_public_key) {
    PKeyPtr local(EVP_PKEY_new_raw_private_key(EVP_PKEY_X25519, nullptr,
                                               private_key.data(), private_key.size()), EVP_PKEY_free);
    PKeyPtr remote(EVP_PKEY_new_raw_public_key(EVP_PKEY_X25519, nullptr,
                                               peer_public_key.data(), peer_public_key.size()), EVP_PKEY_free);
    if (!local || !remote) throw std::runtime_error("unable to import X25519 key material");
    PKeyCtxPtr context(EVP_PKEY_CTX_new(local.get(), nullptr), EVP_PKEY_CTX_free);
    if (!context) throw std::runtime_error("unable to create X25519 derive context");
    require_openssl(EVP_PKEY_derive_init(context.get()), "X25519 derive init");
    require_openssl(EVP_PKEY_derive_set_peer(context.get(), remote.get()), "X25519 set peer");
    SecretKey shared{};
    std::size_t size = shared.size();
    require_openssl(EVP_PKEY_derive(context.get(), shared.data(), &size), "X25519 shared secret derivation");
    if (size != shared.size()) throw std::runtime_error("unexpected X25519 shared secret size");
    Byte accumulator = 0;
    for (const Byte byte : shared) accumulator = static_cast<Byte>(accumulator | byte);
    if (accumulator == 0) {
        OPENSSL_cleanse(shared.data(), shared.size());
        throw std::runtime_error("X25519 produced an all-zero shared secret");
    }
    return shared;
}

PeerSessionKeys derive_peer_session_keys(const SecretKey& shared_secret,
                                         const Bytes& transcript) {
    const Hash salt = sha256(transcript);
    const auto expand = [&](const std::string& label, Byte* output, std::size_t size) {
        PKeyCtxPtr context(EVP_PKEY_CTX_new_id(EVP_PKEY_HKDF, nullptr), EVP_PKEY_CTX_free);
        if (!context) throw std::runtime_error("unable to create HKDF context");
        require_openssl(EVP_PKEY_derive_init(context.get()), "HKDF derive init");
        require_openssl(EVP_PKEY_CTX_set_hkdf_md(context.get(), EVP_sha256()), "HKDF digest");
        require_openssl(EVP_PKEY_CTX_set1_hkdf_salt(context.get(), salt.data(),
                                                    static_cast<int>(salt.size())), "HKDF salt");
        require_openssl(EVP_PKEY_CTX_set1_hkdf_key(context.get(), shared_secret.data(),
                                                   static_cast<int>(shared_secret.size())), "HKDF input key");
        require_openssl(EVP_PKEY_CTX_add1_hkdf_info(context.get(),
            reinterpret_cast<const unsigned char*>(label.data()), static_cast<int>(label.size())), "HKDF info");
        std::size_t output_size = size;
        require_openssl(EVP_PKEY_derive(context.get(), output, &output_size), "HKDF expansion");
        if (output_size != size) throw std::runtime_error("unexpected HKDF output size");
    };
    PeerSessionKeys result{};
    expand("FeinStatic peer channel v1 client-to-server key", result.client_to_server.data(), result.client_to_server.size());
    expand("FeinStatic peer channel v1 server-to-client key", result.server_to_client.data(), result.server_to_client.size());
    expand("FeinStatic peer channel v1 client nonce prefix", result.client_nonce_prefix.data(), result.client_nonce_prefix.size());
    expand("FeinStatic peer channel v1 server nonce prefix", result.server_nonce_prefix.data(), result.server_nonce_prefix.size());
    return result;
}

Bytes aes256gcm_encrypt(const AeadKey& key, const AeadNonce& nonce,
                        const Bytes& associated_data, const Bytes& plaintext) {
    if (plaintext.size() > static_cast<std::size_t>(INT_MAX) ||
        associated_data.size() > static_cast<std::size_t>(INT_MAX)) {
        throw std::length_error("AES-GCM input exceeds supported size");
    }
    CipherCtxPtr context(EVP_CIPHER_CTX_new(), EVP_CIPHER_CTX_free);
    if (!context) throw std::runtime_error("unable to create peer AES-GCM context");
    require_openssl(EVP_EncryptInit_ex(context.get(), EVP_aes_256_gcm(), nullptr, nullptr, nullptr), "peer AES-GCM init");
    require_openssl(EVP_CIPHER_CTX_ctrl(context.get(), EVP_CTRL_GCM_SET_IVLEN,
                                        static_cast<int>(nonce.size()), nullptr), "peer AES-GCM IV length");
    require_openssl(EVP_EncryptInit_ex(context.get(), nullptr, nullptr, key.data(), nonce.data()), "peer AES-GCM key setup");
    int written = 0;
    if (!associated_data.empty()) {
        require_openssl(EVP_EncryptUpdate(context.get(), nullptr, &written, associated_data.data(),
                                          static_cast<int>(associated_data.size())), "peer AES-GCM AAD");
    }
    Bytes output(plaintext.size() + 16U);
    written = 0;
    if (!plaintext.empty()) {
        require_openssl(EVP_EncryptUpdate(context.get(), output.data(), &written, plaintext.data(),
                                          static_cast<int>(plaintext.size())), "peer AES-GCM encrypt");
    }
    int final_written = 0;
    require_openssl(EVP_EncryptFinal_ex(context.get(), output.data() + written, &final_written), "peer AES-GCM final");
    const std::size_t ciphertext_size = static_cast<std::size_t>(written + final_written);
    output.resize(ciphertext_size + 16U);
    require_openssl(EVP_CIPHER_CTX_ctrl(context.get(), EVP_CTRL_GCM_GET_TAG, 16, output.data() + ciphertext_size),
                    "peer AES-GCM tag");
    return output;
}

Bytes aes256gcm_decrypt(const AeadKey& key, const AeadNonce& nonce,
                        const Bytes& associated_data, const Bytes& ciphertext_and_tag) {
    constexpr std::size_t tag_size = 16;
    if (ciphertext_and_tag.size() < tag_size ||
        ciphertext_and_tag.size() > static_cast<std::size_t>(INT_MAX) ||
        associated_data.size() > static_cast<std::size_t>(INT_MAX)) {
        throw std::length_error("AES-GCM ciphertext or AAD has an invalid size");
    }
    const std::size_t ciphertext_size = ciphertext_and_tag.size() - tag_size;
    CipherCtxPtr context(EVP_CIPHER_CTX_new(), EVP_CIPHER_CTX_free);
    if (!context) throw std::runtime_error("unable to create peer AES-GCM context");
    require_openssl(EVP_DecryptInit_ex(context.get(), EVP_aes_256_gcm(), nullptr, nullptr, nullptr), "peer AES-GCM init");
    require_openssl(EVP_CIPHER_CTX_ctrl(context.get(), EVP_CTRL_GCM_SET_IVLEN,
                                        static_cast<int>(nonce.size()), nullptr), "peer AES-GCM IV length");
    require_openssl(EVP_DecryptInit_ex(context.get(), nullptr, nullptr, key.data(), nonce.data()), "peer AES-GCM key setup");
    int written = 0;
    if (!associated_data.empty()) {
        require_openssl(EVP_DecryptUpdate(context.get(), nullptr, &written, associated_data.data(),
                                          static_cast<int>(associated_data.size())), "peer AES-GCM AAD");
    }
    // Keep a non-null output buffer even for an authenticated empty frame.
    Bytes plaintext(ciphertext_size + 16U);
    written = 0;
    if (ciphertext_size != 0) {
        require_openssl(EVP_DecryptUpdate(context.get(), plaintext.data(), &written,
                                          ciphertext_and_tag.data(), static_cast<int>(ciphertext_size)), "peer AES-GCM decrypt");
    }
    require_openssl(EVP_CIPHER_CTX_ctrl(context.get(), EVP_CTRL_GCM_SET_TAG, tag_size,
                                        const_cast<Byte*>(ciphertext_and_tag.data() + ciphertext_size)), "peer AES-GCM set tag");
    int final_written = 0;
    if (EVP_DecryptFinal_ex(context.get(), plaintext.data() + written, &final_written) != 1) {
        OPENSSL_cleanse(plaintext.data(), plaintext.size());
        throw std::runtime_error("peer frame AES-GCM authentication failed");
    }
    plaintext.resize(static_cast<std::size_t>(written + final_written));
    return plaintext;
}

KeyPair generate_ed25519_keypair() {
    PKeyCtxPtr context(EVP_PKEY_CTX_new_id(EVP_PKEY_ED25519, nullptr), EVP_PKEY_CTX_free);
    if (!context) throw std::runtime_error("unable to create Ed25519 key context");
    require_openssl(EVP_PKEY_keygen_init(context.get()), "Ed25519 keygen init");
    EVP_PKEY* raw_key = nullptr;
    require_openssl(EVP_PKEY_keygen(context.get(), &raw_key), "Ed25519 key generation");
    PKeyPtr key(raw_key, EVP_PKEY_free);

    KeyPair result{};
    std::size_t public_size = result.public_key.size();
    std::size_t secret_size = result.secret_key.size();
    require_openssl(EVP_PKEY_get_raw_public_key(key.get(), result.public_key.data(), &public_size),
                    "extract Ed25519 public key");
    require_openssl(EVP_PKEY_get_raw_private_key(key.get(), result.secret_key.data(), &secret_size),
                    "extract Ed25519 private key");
    if (public_size != result.public_key.size() || secret_size != result.secret_key.size()) {
        throw std::runtime_error("unexpected Ed25519 key size");
    }
    return result;
}

PublicKey derive_ed25519_public_key(const SecretKey& secret_key) {
    PKeyPtr key(EVP_PKEY_new_raw_private_key(EVP_PKEY_ED25519, nullptr,
                                             secret_key.data(), secret_key.size()), EVP_PKEY_free);
    if (!key) throw std::runtime_error("unable to import Ed25519 private key");
    PublicKey result{};
    std::size_t size = result.size();
    require_openssl(EVP_PKEY_get_raw_public_key(key.get(), result.data(), &size),
                    "derive Ed25519 public key");
    if (size != result.size()) throw std::runtime_error("unexpected Ed25519 public key size");
    return result;
}

Signature sign_ed25519(const SecretKey& secret_key, const Bytes& message) {
    PKeyPtr key(EVP_PKEY_new_raw_private_key(EVP_PKEY_ED25519, nullptr,
                                             secret_key.data(), secret_key.size()), EVP_PKEY_free);
    if (!key) throw std::runtime_error("unable to import Ed25519 private key");
    MdCtxPtr context(EVP_MD_CTX_new(), EVP_MD_CTX_free);
    if (!context) throw std::runtime_error("unable to create Ed25519 signing context");
    require_openssl(EVP_DigestSignInit(context.get(), nullptr, nullptr, nullptr, key.get()),
                    "Ed25519 signing init");
    Signature signature{};
    std::size_t size = signature.size();
    const Byte* input = message.empty() ? nullptr : message.data();
    require_openssl(EVP_DigestSign(context.get(), signature.data(), &size, input, message.size()),
                    "Ed25519 signing");
    if (size != signature.size()) throw std::runtime_error("unexpected Ed25519 signature size");
    return signature;
}

bool verify_ed25519(const PublicKey& public_key,
                    const Bytes& message,
                    const Signature& signature) {
    PKeyPtr key(EVP_PKEY_new_raw_public_key(EVP_PKEY_ED25519, nullptr,
                                            public_key.data(), public_key.size()), EVP_PKEY_free);
    if (!key) return false;
    MdCtxPtr context(EVP_MD_CTX_new(), EVP_MD_CTX_free);
    if (!context) throw std::runtime_error("unable to create Ed25519 verification context");
    require_openssl(EVP_DigestVerifyInit(context.get(), nullptr, nullptr, nullptr, key.get()),
                    "Ed25519 verification init");
    const Byte* input = message.empty() ? nullptr : message.data();
    const int result = EVP_DigestVerify(context.get(), signature.data(), signature.size(),
                                        input, message.size());
    return result == 1;
}

EncryptedSecret encrypt_secret(const SecretKey& secret,
                               const std::string& passphrase,
                               std::uint32_t iterations) {
    if (passphrase.size() < 12) {
        throw std::invalid_argument("new wallet passphrases must contain at least 12 bytes");
    }
    EncryptedSecret result{};
    result.iterations = iterations;
    random_bytes(result.salt.data(), result.salt.size());
    random_bytes(result.nonce.data(), result.nonce.size());
    auto key = derive_encryption_key(passphrase, result.salt, iterations);
    struct KeyWiper {
        std::array<Byte, 32>& value;
        ~KeyWiper() { OPENSSL_cleanse(value.data(), value.size()); }
    } key_wiper{key};

    CipherCtxPtr context(EVP_CIPHER_CTX_new(), EVP_CIPHER_CTX_free);
    if (!context) throw std::runtime_error("unable to create AES-GCM context");
    require_openssl(EVP_EncryptInit_ex(context.get(), EVP_aes_256_gcm(), nullptr, nullptr, nullptr),
                    "AES-256-GCM init");
    require_openssl(EVP_CIPHER_CTX_ctrl(context.get(), EVP_CTRL_GCM_SET_IVLEN,
                                        static_cast<int>(result.nonce.size()), nullptr),
                    "AES-GCM IV length");
    require_openssl(EVP_EncryptInit_ex(context.get(), nullptr, nullptr, key.data(), result.nonce.data()),
                    "AES-GCM key/nonce setup");

    result.ciphertext.resize(secret.size() + 16);
    int written = 0;
    require_openssl(EVP_EncryptUpdate(context.get(), result.ciphertext.data(), &written,
                                      secret.data(), static_cast<int>(secret.size())),
                    "AES-GCM encryption");
    int final_written = 0;
    require_openssl(EVP_EncryptFinal_ex(context.get(), result.ciphertext.data() + written,
                                        &final_written), "AES-GCM finalization");
    result.ciphertext.resize(static_cast<std::size_t>(written + final_written));
    require_openssl(EVP_CIPHER_CTX_ctrl(context.get(), EVP_CTRL_GCM_GET_TAG,
                                        static_cast<int>(result.tag.size()), result.tag.data()),
                    "AES-GCM tag extraction");
    return result;
}

SecretKey decrypt_secret(const EncryptedSecret& encrypted, const std::string& passphrase) {
    auto key = derive_encryption_key(passphrase, encrypted.salt, encrypted.iterations);
    struct KeyWiper {
        std::array<Byte, 32>& value;
        ~KeyWiper() { OPENSSL_cleanse(value.data(), value.size()); }
    } key_wiper{key};
    CipherCtxPtr context(EVP_CIPHER_CTX_new(), EVP_CIPHER_CTX_free);
    if (!context) throw std::runtime_error("unable to create AES-GCM context");
    require_openssl(EVP_DecryptInit_ex(context.get(), EVP_aes_256_gcm(), nullptr, nullptr, nullptr),
                    "AES-256-GCM init");
    require_openssl(EVP_CIPHER_CTX_ctrl(context.get(), EVP_CTRL_GCM_SET_IVLEN,
                                        static_cast<int>(encrypted.nonce.size()), nullptr),
                    "AES-GCM IV length");
    require_openssl(EVP_DecryptInit_ex(context.get(), nullptr, nullptr, key.data(), encrypted.nonce.data()),
                    "AES-GCM key/nonce setup");

    SecretKey secret{};
    try {
        if (encrypted.ciphertext.size() != secret.size()) {
            throw std::runtime_error("encrypted wallet secret has invalid size");
        }
        int written = 0;
        require_openssl(EVP_DecryptUpdate(context.get(), secret.data(), &written,
                                          encrypted.ciphertext.data(),
                                          static_cast<int>(encrypted.ciphertext.size())),
                        "AES-GCM decryption");
        require_openssl(EVP_CIPHER_CTX_ctrl(context.get(), EVP_CTRL_GCM_SET_TAG,
                                            static_cast<int>(encrypted.tag.size()),
                                            const_cast<Byte*>(encrypted.tag.data())),
                        "AES-GCM tag setup");
        int final_written = 0;
        const int final_result = EVP_DecryptFinal_ex(context.get(), secret.data() + written, &final_written);
        if (final_result != 1) {
            throw std::runtime_error("wallet unlock failed: wrong passphrase or damaged data");
        }
        if (static_cast<std::size_t>(written + final_written) != secret.size()) {
            throw std::runtime_error("decrypted wallet secret has invalid size");
        }
        return secret;
    } catch (...) {
        OPENSSL_cleanse(secret.data(), secret.size());
        throw;
    }
}

} // namespace feinstat::crypto
