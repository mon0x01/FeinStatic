# Dynamic Peer Admission — FST Milestone 2D

## Overview
Peers are discovered dynamically through bootstrap sources and peer exchange.
No operator pre-provisions every peer in a trust store.

## Components
- **PeerStore**: SQLite-backed, monotonic sequence-tracked store of validated
  signed node records. Primary key: NodeId.
- **BootstrapDiscovery**: Loads and validates records from multiple independent
  bootstrap sources. No single source is privileged.
- **PeerAuthenticator**: Decides whether a session may be established with a
  given identity. Backed by PeerStore (verified peers).
- **PeerExchange**: Exchanges signed peer records over authenticated sessions.
- **AdmissionController**: Manages inbound/outbound connections, connection
  limits, duplicate resolution, reconnection with bounded backoff.
- **PeerLifecycle**: Per-peer circuit breaker with exponential backoff.

## Admission Flow
1. Bootstrap records are validated by signature (network ID, expiry, sequence).
2. Valid records are upserted into PeerStore (monotonic sequence enforced).
3. Outbound connection: dial peer endpoint, TLS 1.3 mutual auth, exchange
   NodeId + signed record + binding signature.
4. Inbound connection: accept TLS 1.3, perform same exchange, verify peer.
5. On success: register authenticated peer, mark verified in store.
6. On failure: close connection, record failure, schedule reconnect with
   bounded backoff (PeerLifecycle).

## Connection Limits
- Maximum total connections (default 64).
- Maximum inbound connections (default 32).
- Maximum outbound connections (default 32).
- Maximum connections per peer (default 2, for duplicate resolution).

## Security
- Invalid credentials: TLS handshake fails, connection rejected.
- Wrong network: node record network_identifier mismatch → rejected.
- Identity mismatch: NodeId doesn't match public key → rejected.
- Stale/forged records: signature or sequence validation fails → rejected.
- Duplicate connections: second connection to same peer is closed.
- Connection flooding: bounded admission with connection limits.

## Bootstrap Authority
A bootstrap source must not possess authority to approve financial transactions,
validators or network membership. It provides connectivity assistance only.
