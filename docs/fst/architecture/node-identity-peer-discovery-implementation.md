# FST Node Identity and Peer Discovery Implementation

**Milestone:** FST Milestone 2C — Node Identity, Signed Records and Operational Peer Discovery
**Status:** IMPLEMENTED
**Classification:** PROVISIONAL (preferred architectural direction requiring validation;
not an approved production design)

## Overview

This document describes the implementation of the node identity, signed node record,
persistent peer store, bootstrap discovery, and peer exchange subsystems for FST Milestone 2C.

The implementation provides:
- A reusable node identity subsystem with key-provider abstraction
- Cryptographically verifiable signed node records
- Persistent peer management with SQLite storage
- Bootstrap discovery from multiple independent sources
- Authenticated peer record exchange
- Integration with the existing FeinStatic authenticated transport through a clean
  `PeerAuthenticator` interface boundary

No consensus algorithm, validator admission mechanism, or Noise handshake pattern is
implemented or approved by this milestone.

## Components

### 1. Node Identity (`include/feinstat/network/identity.hpp`, `src/network/identity.cpp`)

#### `NodeId` — Versioned, self-certifying node identifier

The node identifier is derived from the canonical public key material:

- **v1 format:** `"fn1" + hex(sha256(public_key))` — backward compatible with the
  existing `peer_id_from_public_key()` in `src/network/peer_identity.cpp:320`
- **Versioned:** The `NodeId` class parses the leading prefix (`"fn1"`) to determine
  the format version. Future versions (e.g. `"fn2"`) can use different hash or key
  algorithms without breaking the identity abstraction.
- **Self-certifying:** `NodeId::matches(public_key)` verifies that a public key
  corresponds to a node identifier by recomputing `sha256(public_key)`.

#### `KeyProvider` — Key abstraction interface

An abstract interface that isolates key management from consumers:

- `public_key()` — returns the Ed25519 public key
- `node_id()` — derives the NodeId from the public key
- `sign(message)` — Ed25519 signature
- `sign_domain(domain, message)` — domain-separated signing: signs
  `canonical(domain) || message`, preventing cross-context signature reuse

#### `Ed25519KeyProvider` — In-memory implementation

Holds an Ed25519 key pair in memory. Private key is cleansed with `OPENSSL_cleanse`
on destruction. Suitable for tests and short-lived processes. No plaintext private
key is persisted by this class.

#### `FileKeyProvider` — File-backed implementation using existing NodeIdentity

Wraps the existing `NodeIdentity` (from `src/network/peer_identity.cpp`) which
provides:
- Mode-0600 plaintext seed file storage (`save_new`/`load`)
- AES-256-GCM + PBKDF2-HMAC-SHA256 encrypted vault (`save_vault_new`/`load_vault`)
- Secret memory cleansing on destruction and move

**Limitation:** Production-grade durable key protection (HSM/TPM/enclave) is not
available in this milestone. The `FileKeyProvider` abstraction provides a clean
boundary for adding such backends in the future.

#### `RotationRecord` — Identity rotation support

Binds an old `NodeId` to a new `NodeId`, signed by the old key. Supports key
rotation and identity continuity while preserving monotonic sequence boundaries.

**Domain separation:** All node-specific signing uses domain-separated messages.
A node-record signature cannot be reused for a session handshake or any other
context.

### 2. Signed Node Record (`include/feinstat/network/node_record.hpp`, `src/network/node_record.cpp`)

#### `SignedNodeRecord` — Canonical, signed, versioned node record

Fields:
| Field | Type | Description |
|-------|------|-------------|
| `format_version` | `uint8` | Record format version (1 = current) |
| `network_identifier` | `Bytes[32]` | Network/genesis hash for fork protection (D-036) |
| `node_id` | `NodeId` | Self-certifying node identifier |
| `key_algorithm` | `uint8` | Public-key algorithm (1 = Ed25519) |
| `public_key` | `PublicKey[32]` | Ed25519 public key |
| `sequence` | `uint64` | Monotonic sequence number per node ID |
| `protocol_versions` | `vector<uint16>` | Supported protocol versions |
| `capabilities` | `vector<string>` | Advertised capabilities |
| `endpoints` | `vector<NodeEndpoint>` | Network transport addresses |
| `expiry_unix_ms` | `uint64` | Expiration timestamp (0 = no expiry) |
| `signature` | `Signature[64]` | Ed25519 signature over domain-separated canonical body |

#### Canonical serialization

The canonical body is serialized using `ByteWriter`/`ByteReader` in a fixed,
deterministic order. Two records with identical fields always produce identical
bytes. The signature covers `canonical("FST Signed Node Record v1") || canonical_body`.

#### Validation rules (`validate_record`)

1. **Unsupported critical-version rejection:** `format_version` must be 1
2. **Unsupported key algorithm rejection:** `key_algorithm` must be 1 (Ed25519)
3. **Wrong-network rejection:** `network_identifier` must match the expected network
   (fork protection, D-036)
4. **Node ID / public key correspondence:** `node_id.matches(public_key)` must hold
5. **Expired-record rejection:** `expiry_unix_ms` must be in the future (0 = no expiry)
6. **Stale-sequence rejection:** `sequence` must be strictly greater than the last
   accepted sequence for the node ID
7. **Invalid-signature rejection:** Ed25519 signature must verify over the
   domain-separated canonical body

#### Strict bounds

| Limit | Value |
|-------|-------|
| Maximum record size | 64 KiB |
| Maximum endpoints per record | 16 |
| Maximum capabilities per record | 32 |
| Maximum protocol versions per record | 16 |
| Maximum capability string length | 64 bytes |
| Maximum host string length | 253 bytes |

**Security invariant:** A signed node record never grants validator status, account
authority, or permission to transfer funds. It is solely a claim about node identity
and reachability.

### 3. Persistent Peer Store (`include/feinstat/network/peer_store.hpp`, `src/network/peer_store.cpp`)

#### `PeerStore` — SQLite-backed peer store

- **Primary key:** Node ID (`NodeId`)
- **Storage:** SQLite with WAL mode, `synchronous = FULL`, busy timeout, mode-0600
  file permissions (reusing patterns from `src/storage/sqlite_store.cpp`)
- **Validated signed record storage:** Only cryptographically validated records are
  stored
- **Monotonic sequence tracking:** `upsert()` rejects records with sequence <= the
  stored sequence (rollback prevention)
- **Duplicate detection:** Identical records return `Unchanged` without modification
- **Conflict handling:** Same node ID with different public key returns
  `RejectedConflict` — unvalidated records never overwrite verified records
- **Discovery source tracking:** Each entry records how it was learned
  (`Bootstrap`, `PeerExchange`, `Cached`, `Configured`, `Unknown`)
- **Last-seen and freshness:** `first_seen_unix_ms`, `last_seen_unix_ms`,
  `last_verified_unix_ms`
- **Peer capability information:** Stored in the signed record
- **Connection and verification state:** `connected`, `verified`, attempt/success/
  failure counts, last success/failure timestamps
- **Expiry and stale-record cleanup:** `cleanup_expired()` removes records past their
  expiry or stale TTL
- **Bounded storage:** `max_peers` config; `upsert()` returns `RejectedStoreFull`
  when the limit is reached
- **Safe persistence and recovery:** Each `upsert()` persists to SQLite atomically;
  `open()` reloads from disk on restart

**Peer updates preserve monotonic record sequence rules and prevent rollback.**
**Unvalidated records never silently overwrite verified records.**

### 4. Bootstrap Discovery (`include/feinstat/network/bootstrap.hpp`, `src/network/bootstrap.cpp`)

#### `BootstrapDiscovery` — Bootstrap source management and record validation

- **Multiple configurable bootstrap entries:** Each entry has an ID, address, and
  optional seed records
- **Bootstrap record loading and validation:** Records are validated by signature
  against the embedded public key; the bootstrap source is not a trust root
- **Wrong-network rejection:** Records from a different network are rejected
- **Multiple independent sources:** No single source becomes a central authority
  (P1, P2). Bootstrap services provide connectivity assistance only — they do not
  authorize membership or determine consensus participation

#### `BootstrapSource` — Network fetch interface

Abstract interface for implementations that fetch records over the network (HTTP,
DNS, custom protocol). Keeps the discovery core transport-independent.

### 5. Peer Exchange and Authentication (`include/feinstat/network/peer_exchange.hpp`, `src/network/peer_exchange.cpp`)

#### `PeerAuthenticator` — Session authentication isolation

Interface that isolates session authentication from peer discovery:

- `trusted_key(node_id)` — returns the trusted public key for a verified node ID
- `verify_peer_identity(node_id, public_key)` — verifies a public key matches a
  known, verified peer
- `register_authenticated_peer(node_id, public_key, now_ms)` — marks a peer as
  verified after a successful session handshake
- `is_verified(node_id)` — checks whether a node ID has been verified

#### `StorePeerAuthenticator` — Store-backed implementation

Wraps a `PeerStore`. A peer is trusted when it has a validated signed record; it
becomes verified after a successful session handshake.

#### `PeerExchange` — Peer record exchange

Manages exchange of signed peer records through authenticated connections:

- **Validation:** All received records are validated (signature, sequence, expiry,
  network)
- **Freshness checks:** Expired records are rejected
- **Rate limiting:** Per-peer record rate limits prevent address flooding
- **Duplicate detection:** Identical records are not re-learned
- **Conflict handling:** Identity conflicts are detected and reported
- **Bounded exchange:** Maximum records per exchange
- **Record selection:** `select_records_to_share()` picks records to send to a peer
  (excluding the peer's own record)

**Peer records are claims about node identity and reachability, not proof of
maliciousness or correctness.**

### 6. Transport Independence

The identity, record, and peer store layers are transport-independent:
- Node ID contains no address, port, or transport hint
- Node records carry transport-typed endpoints (TCP, QUIC, mesh, radio, satellite,
  DTN)
- No TCP, QUIC, or other transport adapter is implemented in this milestone
- Extension interfaces are preserved for future transport adapters

## Integration with Existing Network

The new components integrate with the existing FeinStatic networking through:

1. **NodeIdentity reuse:** The existing `NodeIdentity` (Ed25519 key generation,
   mode-0600 storage, encrypted vault, `fn1` peer ID derivation) is reused via
   `FileKeyProvider`
2. **PeerAuthenticator boundary:** Isolates session authentication (existing
   `NodeServer`/`PeerClient` with `PeerAuthConfig`) from peer discovery
3. **No bespoke handshake:** No new cryptographic handshake is implemented
4. **No Noise pattern selection:** The Noise handshake decision remains OPEN RESEARCH

The legacy roster-based transport (`RaftNetworkConfig::peers`, pre-provisioned peer
roster) is not modified. The new discovery subsystem operates alongside it through
the `PeerAuthenticator` interface. Roster-dependent connectivity is not described
as permissionless.

## Security Properties

- **Domain-separated signatures:** Node-record signatures cannot be reused in other
  contexts
- **Fork protection:** Network identifier bound into records (D-036)
- **Replay protection:** Monotonic sequence numbers prevent rollback
- **Expired-record rejection:** Records past expiry are not accepted
- **Wrong-network rejection:** Records from other networks are rejected
- **Identity conflict detection:** Same node ID with different key is rejected
- **Bounded storage:** Record size, endpoint count, and peer count limits prevent
  resource exhaustion
- **No plaintext private key persistence:** Keys are stored in mode-0600 files or
  AES-256-GCM encrypted vaults
- **Secret cleansing:** Private keys are cleansed from memory on destruction

## Known Limitations

1. **No production-grade key storage:** HSM/TPM/enclave support is not available.
   The `KeyProvider` abstraction provides a clean boundary for future backends.
2. **No Noise handshake:** Session authentication pattern remains OPEN RESEARCH.
   The `PeerAuthenticator` interface isolates this decision.
3. **No DHT:** Kademlia-style distributed discovery is not implemented (OPEN
   RESEARCH).
4. **No NAT traversal:** Relay, hole punching, and AutoNAT are not implemented.
5. **Bootstrap sources are not authenticated:** Records are self-certifying, but
   source discovery (static list vs dynamic) is unresolved.

## Test Commands

```bash
# Build with debug
cmake --preset debug && cmake --build build/debug -j2

# Run all tests
./build/debug/feinstatic_tests

# Run only discovery tests
./build/debug/feinstatic_tests identity
./build/debug/feinstatic_tests node_record
./build/debug/feinstatic_tests peer_store
./build/debug/feinstatic_tests bootstrap
./build/debug/feinstatic_tests peer_exchange
./build/debug/feinstatic_tests demo

# Build with AddressSanitizer and UBSan
cmake --preset asan && cmake --build build/asan -j2
./build/asan/feinstatic_tests
```

## Test Coverage

| Area | Tests |
|------|-------|
| Identity derivation determinism | identity_derivation_is_deterministic, identity_roundtrip_parse |
| Public/private key correspondence | identity_matches_public_key, key_provider_generate_sign_verify |
| Signature creation and verification | key_provider_generate_sign_verify, key_provider_domain_separation |
| Tampered record rejection | node_record_tampered_signature_rejected, peer_exchange_rejects_tampered_record |
| Wrong-network rejection | node_record_wrong_network_rejected, bootstrap_validate_records_wrong_network, peer_exchange_rejects_wrong_network |
| Expiry handling | node_record_expired_rejected, peer_exchange_rejects_expired_record |
| Sequence rollback rejection | node_record_sequence_rollback_rejected, node_record_sequence_equal_is_stale, peer_exchange_stale |
| Invalid public-key/node-ID combinations | node_record_node_id_mismatch_rejected |
| Duplicate peer identity handling | peer_store_duplicate_is_unchanged, peer_exchange_duplicate_not_learned_twice |
| Conflicting record handling | peer_store_rejects_conflicting_identity, demo_duplicate_identity_conflict_handling |
| Peer-store restart/recovery | peer_store_persistence_and_restart, demo_peer_store_persistence_and_restart |
| Malicious peer-record exchange | peer_exchange_rejects_tampered_record, peer_exchange_rejects_expired_record |
| Resource-exhaustion boundaries | peer_store_bounded_storage, node_record_strict_bounds_max_endpoints |
| Connection and lifecycle failure handling | peer_store_connection_state_tracking |
