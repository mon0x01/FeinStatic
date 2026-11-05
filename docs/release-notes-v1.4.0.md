# FeinStatic v1.4.0 — encrypted peer sessions and trust authorization

## Main milestone

v1.4.0 upgrades authenticated TCP protocol v5 to protocol v6 and adds application-layer encrypted sessions, role-based trust authorization, and local identity revocation/rotation.

- Adds ephemeral X25519 key agreement to the mutual Ed25519 challenge-response handshake. Both ephemeral public keys are included in the signed transcript and client proof.
- Uses HKDF-SHA-256 to derive independent client-to-server and server-to-client AES-256-GCM keys and nonce prefixes.
- Encrypts all post-handshake application frames. Session binding, message type, and strictly increasing sequence number are authenticated as AES-GCM associated data. Existing Ed25519 frame signatures remain in place over the ciphertext.
- Adds trust roles `peer` and `operator`; only an active operator identity may request Raft voter-set changes. Status queries and ordinary peer replication remain available to active peers.
- Extends the trust-store format with retained `revoked-peer` and `revoked-operator` records. Legacy two-column files remain readable as ordinary peers.
- Adds `peer-trust-revoke` and `peer-trust-rotate` commands. Rotation preserves the old role for the replacement identity and retains a revocation tombstone for the old ID.
- Makes trust-store writes owner-only (mode 0600) and atomic. Duplicate identity rows are rejected.
- Adds X25519 agreement, HKDF direction separation, AES-GCM tag/AAD tamper, role persistence, legacy trust-file compatibility, revocation/rotation, and operator-authorization tests.

## Compatibility and operation

- All authenticated peers must run protocol v6 together; v1.3.0 protocol-v5 peers are rejected.
- No SQLite schema migration is required. The trust-store text format is backward-readable: two-column records load as `peer`; saving writes the new explicit role/status column.
- Existing v1.3.0 identities remain valid. Existing trust entries load as ordinary peers. To preserve membership administration, explicitly promote the dedicated operator identity in each node's local trust store with `peer-trust-add trust.txt <operator-id> <operator-public-key-hex> operator`.
- Revocation is local trust-store state, not a replicated cluster command. Apply it on every node. Existing established sessions are not forcibly disconnected; new handshakes after reload reject the revoked identity.
- Identity rotation is also local. Update every peer trust store and, for a Raft member, the pre-provisioned endpoint roster and joint-consensus membership before removing the old endpoint/identity.

## Security limits

This is application-layer encryption over TCP, not TLS. Network metadata such as addresses, ports, packet sizes and timing remain visible. The key agreement and AEAD construction use standard OpenSSL primitives, but the protocol has not received an independent cryptographic audit. Use controlled private networks only. The implementation does not provide Byzantine fault tolerance, certificate authority, automatic peer discovery, distributed trust-store updates, or automatic termination of already-established connections on revocation. FeinStatic remains an educational crash-fault financial infrastructure prototype, not a production custody or payment system.
