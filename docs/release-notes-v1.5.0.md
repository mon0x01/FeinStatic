# FeinStatic v1.5.0 — encrypted identity vaults and signed trust-policy replication

v1.5.0 upgrades the authenticated peer protocol from v6 to v7 and adds a safer node-identity storage option plus an explicit way to push signed trust policies to already-provisioned Raft members. The SQLite schema is unchanged.

## Node identity vaults

- `node-keygen-vault <path>` generates an Ed25519 node identity and writes its private seed into a 0600 encrypted vault.
- The vault uses PBKDF2-HMAC-SHA-256 with a random salt and bounded iteration count, then AES-256-GCM with a random nonce and authentication tag.
- Existing commands detect vault files and prompt for a hidden passphrase. Legacy raw 0600 identity files remain readable for compatibility.
- The vault is encrypted at rest only. The decrypted identity exists in process memory while the node is running. There is no TPM/HSM integration, agent, or unattended unlock mechanism.

## Signed trust-policy replication

- Adds protocol-v7 messages to submit a signed full-state trust policy to one existing Raft node over the authenticated encrypted session.
- The policy binds the cluster ID, a positive monotonically increasing epoch, signer ID, canonical sorted identity entries, roles/revocation status, and an Ed25519 signature.
- A receiver requires the signer to match the authenticated peer and to be an active `operator` in the currently installed trust store. It verifies the signature, exact cluster binding, epoch freshness, all identity/key bindings, role/status values, and duplicate rejection.
- Local revocation tombstones are monotonic: they survive omitted rows and cannot be changed back to active by a later signed policy. A policy must leave at least one active operator.
- The receiver checks that all remote endpoints in its pre-provisioned Raft roster remain trusted. A policy cannot silently sever a cluster transport endpoint. The current runtime has no online endpoint-roster removal command, so policy push cannot revoke a node identity still present in that startup roster (even if its voter role was changed to learner).
- The target writes the new trust store atomically before acknowledging. The sender advances its management trust file's epoch only after that acknowledgement.
- An integration test pushes a revocation over real authenticated TCP, restarts the target process, and confirms the revoked identity is rejected on a subsequent handshake.

## Operational use

Generate a new encrypted identity:

```bash
build/release/feinstatic node-keygen-vault runtime/node-a.vault
```

Push a signed full-state trust policy to one target:

```bash
build/release/feinstatic raft-trust-policy-push db host port operator.vault management-trust.txt expected-peer-id cluster-id epoch
```

The management trust file must contain the target public key for server pinning, the signing identity as an active operator, all other desired remote Raft peers, and at least one active operator after the change. Use the next greater positive epoch each time that management file is reused. Push separately to each node; this is not an automatic broadcast or discovery mechanism. If an identity remains in a node's startup endpoint roster, the receiver will reject its revocation; transport-roster replacement is not yet automated.

All authenticated nodes and clients must use protocol v7 together. v1.4.0 protocol-v6 peers are rejected. Existing SQLite databases require no migration. The trust-store text format adds a metadata comment holding the cluster ID and policy epoch; legacy trust rows remain readable.

## Verification

The v1.5.0 suite contains 145 automated tests. It covers encrypted vault file permissions, round-trip, wrong passphrase and tamper rejection; policy signature, cluster binding, epoch replay, tombstone preservation and role validation; real TCP policy push, durable remote persistence across restart, and rejection of a revoked identity's next handshake. The existing three-process Raft failover/partition/snapshot lab and four-process joint-consensus membership lab remain part of `scripts/verify-all.sh`.

These tests are controlled loopback tests, not an external cryptographic audit or proof against arbitrary adversarial schedules. The project remains a crash-fault-tolerant prototype, not Byzantine-fault tolerant or production financial infrastructure.
