# Ledger / Blockchain

## Wallet identity

Ed25519 public keys are 32 bytes. The address is `fs1` followed by the first 20 bytes of SHA-256(public-key) in lowercase hexadecimal. A sender must prove ownership by including the public key and a valid signature over canonical transaction bytes.

## Transaction format

Version byte, sender address, recipient address, asset ID, amount atoms, fee atoms, nonce, and public key form the signed message. The 64-byte Ed25519 signature is appended to produce the serialized transaction. Transaction ID is SHA-256 of the signed serialization.

Only `FST` is supported in this initial Ledger. Amounts and fees are unsigned integer atoms; binary floating-point is not used for balance state.

## Account transition

For each transaction in block order:

1. Verify field constraints, signature, and sender/public-key relationship.
2. Require `nonce == current_nonce + 1`.
3. Require `balance >= amount + fee`, after checking addition overflow.
4. Debit amount plus fee, credit recipient amount, and set sender nonce.
5. Accumulate fees with checked addition.

At block end, the miner receives the configured subsidy plus accumulated fees. Total supply increases only by the subsidy; fees are transfers, not newly issued supply.

## Proof of Work

The initial implementation counts leading zero hexadecimal nibbles in the SHA-256 block hash. Difficulty is chain-configured and constrained to 0–8 nibbles for local demonstration. This is not a production consensus or Sybil-resistance design.
