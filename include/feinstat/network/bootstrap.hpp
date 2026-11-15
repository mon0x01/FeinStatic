#pragma once

#include "feinstat/common/bytes.hpp"
#include "feinstat/common/types.hpp"
#include "feinstat/network/identity.hpp"
#include "feinstat/network/node_record.hpp"

#include <cstdint>
#include <memory>
#include <optional>
#include <string>
#include <vector>

namespace feinstat::network {

// A single bootstrap source. Multiple independent entries are supported so no
// single source becomes a central authority (P1, P2). Records supplied by a
// bootstrap source are validated by signature; the source itself is not trusted.
struct BootstrapEntry {
    std::string id;              // Human-readable source identifier
    std::string address;         // Network address (host:port or URL)
    std::vector<SignedNodeRecord> seed_records; // Pre-configured seed records
};

// BootstrapDiscovery loads and validates node records from bootstrap sources.
// It does not perform network I/O itself; callers provide record sources.
// Bootstrap services provide connectivity assistance only — they do not
// authorize membership, determine consensus participation, or act as trust roots.
class BootstrapDiscovery {
public:
    explicit BootstrapDiscovery(Bytes network_identifier);

    // Add a bootstrap entry. Multiple entries may be configured.
    void add_entry(BootstrapEntry entry);

    // Add a single seed record directly (for testing or configuration).
    void add_seed_record(SignedNodeRecord record);

    // Return all configured bootstrap entries.
    [[nodiscard]] const std::vector<BootstrapEntry>& entries() const noexcept { return entries_; }

    // Return all seed records from all entries.
    [[nodiscard]] std::vector<SignedNodeRecord> seed_records() const;

    // Validate a batch of records from a bootstrap source. Returns the valid
    // records and a description of any validation failures.
    struct ValidationBatch {
        std::vector<SignedNodeRecord> valid;
        std::vector<std::string> errors;
    };
    [[nodiscard]] ValidationBatch validate_records(
        const std::vector<SignedNodeRecord>& records,
        std::uint64_t now_unix_ms) const;

    [[nodiscard]] const Bytes& network_identifier() const noexcept { return network_identifier_; }

private:
    Bytes network_identifier_;
    std::vector<BootstrapEntry> entries_;
    std::vector<SignedNodeRecord> seed_records_;
};

// Bootstrap source interface for implementations that fetch records over the
// network (e.g. HTTP, DNS, custom protocol). The discovery core remains
// transport-independent.
class BootstrapSource {
public:
    virtual ~BootstrapSource() = default;
    BootstrapSource(const BootstrapSource&) = delete;
    BootstrapSource& operator=(const BootstrapSource&) = delete;
    // Fetch signed node records from this source. Returns empty on failure.
    virtual std::vector<SignedNodeRecord> fetch_records() = 0;
};

} // namespace feinstat::network
