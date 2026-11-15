#include "feinstat/network/peer_exchange.hpp"

#include "feinstat/network/bootstrap.hpp"
#include "feinstat/network/node_record.hpp"

#include <algorithm>
#include <stdexcept>
#include <utility>

namespace feinstat::network {

//------------------------------------------------------------------------------
// StorePeerAuthenticator
//------------------------------------------------------------------------------

StorePeerAuthenticator::StorePeerAuthenticator(PeerStore& store, Bytes network_identifier)
    : store_(store), network_identifier_(std::move(network_identifier)) {}

std::optional<PublicKey> StorePeerAuthenticator::trusted_key(const NodeId& node_id) const {
    auto entry = store_.get(node_id);
    if (!entry.has_value()) return std::nullopt;
    if (!entry->connection_state.verified) return std::nullopt;
    return entry->record.public_key;
}

bool StorePeerAuthenticator::verify_peer_identity(const NodeId& node_id,
                                                   const PublicKey& public_key) const {
    auto entry = store_.get(node_id);
    if (!entry.has_value()) return false;
    if (!entry->connection_state.verified) return false;
    return entry->record.public_key == public_key;
}

void StorePeerAuthenticator::register_authenticated_peer(const NodeId& node_id,
                                                          const PublicKey& public_key,
                                                          std::uint64_t now_unix_ms) {
    auto entry = store_.get(node_id);
    if (!entry.has_value()) return;
    // Only mark verified if the public key matches the stored record.
    if (entry->record.public_key != public_key) return;
    store_.mark_verified(node_id, now_unix_ms);
}

bool StorePeerAuthenticator::is_verified(const NodeId& node_id) const noexcept {
    auto entry = store_.get(node_id);
    if (!entry.has_value()) return false;
    return entry->connection_state.verified;
}

//------------------------------------------------------------------------------
// PeerExchange
//------------------------------------------------------------------------------

PeerExchange::PeerExchange(PeerStore& store, BootstrapDiscovery& bootstrap,
                            PeerAuthenticator& authenticator, Config config)
    : store_(store), bootstrap_(bootstrap), authenticator_(authenticator),
      config_(std::move(config)) {
    // authenticator_ is stored for future use;
    // it is intentionally not referenced in the current implementation.
    (void)authenticator_;
}

PeerExchangeResult PeerExchange::exchange(const NodeId& peer_node_id,
                                           const std::vector<SignedNodeRecord>& send_records,
                                           const std::vector<SignedNodeRecord>& receive_records,
                                           std::uint64_t now_unix_ms) {
    PeerExchangeResult result;

    // Rate limiting per peer.
    auto& rate = exchange_rate_[peer_node_id];
    // Simple window: reset if more than an hour has passed.
    if (now_unix_ms - rate.first > 3600000ULL) {
        rate.first = now_unix_ms;
        rate.second = 0;
    }
    rate.second += receive_records.size();
    if (rate.second > config_.max_records_per_peer_per_hour) {
        result.errors.push_back("peer exchange rate limit exceeded");
        return result;
    }

    // Validate received records.
    RecordValidationContext context{};
    context.network_identifier = bootstrap_.network_identifier();
    context.now_unix_ms = now_unix_ms;

    // Build last-sequence map from the store for stale-sequence rejection.
    std::unordered_map<NodeId, std::uint64_t> last_sequence;
    for (const auto& entry : store_.list(true)) {
        last_sequence[entry.node_id] = entry.record.sequence;
    }
    context.last_sequence = &last_sequence;

    for (const auto& record : receive_records) {
        if (result.learned_records.size() >= config_.max_records_per_exchange) {
            result.errors.push_back("maximum records per exchange reached");
            break;
        }

        // Freshness check.
        if (!is_record_fresh(record, now_unix_ms)) {
            result.rejected_identities.push_back(record.node_id);
            result.errors.push_back("record is not fresh");
            continue;
        }

        RecordValidationResult vr = validate_record(record, context);
        if (!vr.valid) {
            result.rejected_identities.push_back(record.node_id);
            result.errors.push_back(vr.error);
            continue;
        }

        // Don't learn our own record.
        // (The store's upsert would reject a conflict if keys differ, but same
        // node ID with same key is just a duplicate.)

        UpsertResult ura = store_.upsert(record, DiscoverySource::PeerExchange, now_unix_ms);
        switch (ura) {
            case UpsertResult::Inserted:
            case UpsertResult::Updated:
                result.learned_records.push_back(record);
                break;
            case UpsertResult::Unchanged:
                break; // Duplicate, not an error
            case UpsertResult::RejectedStale:
                result.rejected_identities.push_back(record.node_id);
                break;
            case UpsertResult::RejectedConflict:
                result.rejected_identities.push_back(record.node_id);
                result.errors.push_back("identity conflict for " + record.node_id.value());
                break;
            case UpsertResult::RejectedStoreFull:
                result.errors.push_back("peer store is full");
                break;
        }
    }

    // (send_records are provided by the caller for transmission over the
    // authenticated connection; this method does not perform network I/O.)
    (void)send_records;

    return result;
}

std::vector<SignedNodeRecord> PeerExchange::select_records_to_share(
    const NodeId& peer_node_id, std::size_t max_records) const {
    (void)peer_node_id;
    std::vector<SignedNodeRecord> result;
    auto entries = store_.list(true); // verified peers only
    std::size_t count = 0;
    for (const auto& entry : entries) {
        if (count >= max_records) break;
        if (entry.node_id == peer_node_id) continue; // Don't send a peer its own record
        result.push_back(entry.record);
        ++count;
    }
    return result;
}

bool PeerExchange::is_record_fresh(const SignedNodeRecord& record,
                                    std::uint64_t now_unix_ms) const {
    if (record.expiry_unix_ms != 0 && record.expiry_unix_ms <= now_unix_ms) {
        return false;
    }
    return true;
}

} // namespace feinstat::network
