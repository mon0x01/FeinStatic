# Message Dispatch Protocol — FST Milestone 2D

## Frame Format
All messages over authenticated peer sessions use a length-prefixed frame:

| Field | Size | Description |
|-------|------|-------------|
| Length | 4 bytes | Big-endian payload length |
| Type | 2 bytes | Message type (PeerMessageType) |
| Checksum | 32 bytes | SHA-256 of payload |
| Payload | Length bytes | Message payload |

Maximum payload: 1 MiB per message.

## Message Types
| Type | Value | Description |
|------|-------|-------------|
| kReserved | 0 | Session handshake (internal) |
| kProtocolNegotiate | 1 | Protocol version negotiation |
| kProtocolNegotiateAck | 2 | Protocol negotiation ack |
| kNodeIdentity | 3 | Node identity exchange |
| kNodeIdentityAck | 4 | Node identity ack |
| kPing | 5 | Peer health check |
| kPong | 6 | Ping response |
| kPeerRecordAnnounce | 7 | Announce a peer record |
| kPeerRecordRequest | 8 | Request peer records |
| kPeerRecordResponse | 9 | Peer records response |
| kCapabilityAdvertise | 10 | Capability advertisement |
| kCapabilityAck | 11 | Capability ack |
| kDisconnect | 12 | Graceful disconnect |
| kError | 255 | Error / rejection |

## Validation
- All messages have size limits per type.
- Malformed or unsupported message types are rejected.
- Checksum mismatch → connection terminated.
- Oversized payloads → connection terminated.
- Unauthenticated sessions cannot send or receive messages.

## Future Work
Transaction propagation will be added as new message types in a subsequent
milestone. The dispatch architecture is prepared for this extension.
