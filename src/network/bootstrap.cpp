#include "feinstat/network/bootstrap.hpp"

#include "feinstat/network/node_record.hpp"

#include <algorithm>
#include <stdexcept>
#include <utility>

namespace feinstat::network {

BootstrapDiscovery::BootstrapDiscovery(Bytes network_identifier)
    : network_identifier_(std::move(network_identifier)) {}

void BootstrapDiscovery::add_entry(BootstrapEntry entry) {
    entries_.push_back(std::move(entry));
}

void BootstrapDiscovery::add_seed_record(SignedNodeRecord record) {
    seed_records_.push_back(std::move(record));
}

std::vector<SignedNodeRecord> BootstrapDiscovery::seed_records() const {
    std::vector<SignedNodeRecord> result = seed_records_;
    for (const auto& entry : entries_) {
        result.insert(result.end(), entry.seed_records.begin(), entry.seed_records.end());
    }
    return result;
}

BootstrapDiscovery::ValidationBatch BootstrapDiscovery::validate_records(
    const std::vector<SignedNodeRecord>& records,
    std::uint64_t now_unix_ms) const {
    ValidationBatch batch;
    RecordValidationContext context{};
    context.network_identifier = network_identifier_;
    context.now_unix_ms = now_unix_ms;

    for (const auto& record : records) {
        RecordValidationResult result = validate_record(record, context);
        if (result.valid) {
            batch.valid.push_back(record);
        } else {
            batch.errors.push_back(result.error);
        }
    }
    return batch;
}

} // namespace feinstat::network
