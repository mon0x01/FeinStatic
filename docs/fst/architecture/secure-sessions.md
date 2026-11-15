# Secure Session Design — FST Milestone 2D

## Transport Selection: TLS 1.3

FST uses **OpenSSL 3.x via Boost.Asio SSL** for TLS 1.3 mutual authentication.
TLS 1.2 and earlier are explicitly disabled at the SSL context level.

### Why TLS 1.3
- Forward secrecy by default (ephemeral key exchange).
- Authenticated encryption (AES-256-GCM or ChaCha20-Poly1305).
- Secure cipher suite negotiation (server/client propose, strongest mutual).
- 0-RTT not used (replay concerns).
- Mature, maintained, widely audited implementation.

### Why not Noise
The Noise protocol pattern remains **OPEN RESEARCH** per the decision register.
A custom cryptographic handshake is explicitly prohibited. TLS 1.3 via OpenSSL
provides equivalent or better security properties with a maintained library.

## Node Identity Binding

The long-term Ed25519 node identity key is NOT used as the TLS certificate key.
Instead:

1. Each node generates an ephemeral Ed25519 key pair for TLS.
2. Creates a self-signed X.509 certificate (no central CA).
3. Signs `SHA-256(TLS_cert_DER)` with the long-term identity key using domain
   separation (`"FST TLS Binding v1"`).
4. During the application-level handshake (after TLS 1.3 completes), exchanges:
   - NodeId
   - SignedNodeRecord (proves network identity, peer-record identity)
   - TLS certificate (DER)
   - TLS certificate hash
   - Binding signature (Ed25519 over cert hash)

The peer verifies:
1. Control of the TLS session credential (cert hash matches presented hash).
2. Binding to FST NodeId (binding signature verifies under record's public key).
3. Correct network identity (record.network_identifier matches expected).
4. Correct peer-record identity (record signature verifies under record's public key).
5. Protocol-version compatibility.

## Security Properties
- Mutual authentication: both sides present and verify credentials.
- No central certificate authority: self-signed certs with cryptographic binding.
- Forward secrecy: TLS 1.3 ephemeral key exchange.
- Replay protection: TLS 1.3 sequence numbers + application-level sequence.
- Certificate verification is mandatory: no arbitrary certificate acceptance.

## Limitations
- No certificate revocation (CRL/OCSP) in this milestone.
- TLS certificates are self-signed; trust is established through the signed
  node record binding, not a CA chain.
- Certificate pinning is not implemented; cert hash is validated per-session.
