#include "feinstat/platform/financial_platform.hpp"

#include "feinstat/crypto/crypto.hpp"

#include <algorithm>
#include <filesystem>
#include <limits>
#include <set>
#include <stdexcept>
#include <utility>

namespace feinstat::platform {
namespace {

class Writer {
public:
    void u8(std::uint8_t value) { bytes_.push_back(value); }
    void u32(std::uint32_t value) {
        for (unsigned shift = 0; shift < 32; shift += 8) u8(static_cast<std::uint8_t>((value >> shift) & 0xFFU));
    }
    void u64(std::uint64_t value) {
        for (unsigned shift = 0; shift < 64; shift += 8) u8(static_cast<std::uint8_t>((value >> shift) & 0xFFU));
    }
    void string(const std::string& value) {
        if (value.size() > 4096) throw std::length_error("platform state string exceeds 4096 bytes");
        u32(static_cast<std::uint32_t>(value.size()));
        bytes_.insert(bytes_.end(), value.begin(), value.end());
    }
    void blob(const Bytes& value, std::size_t maximum = 4096) {
        if (value.size() > maximum || value.size() > UINT32_MAX) {
            throw std::length_error("platform state blob exceeds configured limit");
        }
        u32(static_cast<std::uint32_t>(value.size()));
        bytes_.insert(bytes_.end(), value.begin(), value.end());
    }
    [[nodiscard]] Bytes finish() && { return std::move(bytes_); }
private:
    Bytes bytes_;
};

class Reader {
public:
    explicit Reader(const Bytes& bytes) : bytes_(bytes) {
        if (bytes.size() > 64U * 1024U * 1024U) throw std::runtime_error("platform snapshot exceeds 64 MiB");
    }
    std::uint8_t u8() {
        require(1);
        return bytes_[position_++];
    }
    std::uint32_t u32() {
        std::uint32_t result = 0;
        for (unsigned shift = 0; shift < 32; shift += 8) result |= static_cast<std::uint32_t>(u8()) << shift;
        return result;
    }
    std::uint64_t u64() {
        std::uint64_t result = 0;
        for (unsigned shift = 0; shift < 64; shift += 8) result |= static_cast<std::uint64_t>(u8()) << shift;
        return result;
    }
    std::string string() {
        const std::uint32_t size = u32();
        if (size > 4096) throw std::runtime_error("platform snapshot string exceeds configured limit");
        require(size);
        const auto begin = bytes_.begin() + static_cast<std::ptrdiff_t>(position_);
        const auto end = begin + static_cast<std::ptrdiff_t>(size);
        position_ += size;
        return std::string(begin, end);
    }
    Bytes blob(std::size_t maximum = 4096) {
        const std::uint32_t size = u32();
        if (size > maximum) throw std::runtime_error("platform snapshot blob exceeds configured limit");
        require(size);
        const auto begin = bytes_.begin() + static_cast<std::ptrdiff_t>(position_);
        const auto end = begin + static_cast<std::ptrdiff_t>(size);
        position_ += size;
        return Bytes(begin, end);
    }
    std::size_t count(std::size_t limit, const char* field) {
        const std::uint32_t value = u32();
        if (value > limit) throw std::runtime_error(std::string("platform snapshot ") + field + " count exceeds limit");
        return static_cast<std::size_t>(value);
    }
    void magic(const char* expected, std::size_t size) {
        require(size);
        for (std::size_t i = 0; i < size; ++i) {
            if (bytes_[position_++] != static_cast<Byte>(expected[i])) throw std::runtime_error("invalid platform snapshot magic");
        }
    }
    void finish() const {
        if (position_ != bytes_.size()) throw std::runtime_error("platform snapshot contains trailing data");
    }
private:
    void require(std::size_t size) const {
        if (size > bytes_.size() - position_) throw std::runtime_error("truncated platform snapshot");
    }
    const Bytes& bytes_;
    std::size_t position_{0};
};

void write_settlement(Writer& w, const settlement::Snapshot& s) {
    if (s.accounts.size() > 100'000 || s.balances.size() > 1'000'000 || s.journal.size() > 5'000'000) {
        throw std::length_error("settlement snapshot exceeds serialization limits");
    }
    w.u32(static_cast<std::uint32_t>(s.accounts.size()));
    for (const auto& account : s.accounts) w.string(account);
    w.u32(static_cast<std::uint32_t>(s.balances.size()));
    for (const auto& b : s.balances) {
        w.string(b.account_id); w.string(b.asset_id); w.u64(b.state.total); w.u64(b.state.reserved);
    }
    w.u32(static_cast<std::uint32_t>(s.journal.size()));
    for (const auto& e : s.journal) {
        w.u64(e.sequence); w.string(e.operation); w.string(e.account_id); w.string(e.asset_id);
        w.u64(e.total_after); w.u64(e.reserved_after); w.string(e.reference);
    }
    w.u64(s.next_journal_sequence);
}

settlement::Snapshot read_settlement(Reader& r) {
    settlement::Snapshot s;
    const auto accounts = r.count(100'000, "account");
    s.accounts.reserve(accounts);
    for (std::size_t i = 0; i < accounts; ++i) s.accounts.push_back(r.string());
    const auto balances = r.count(1'000'000, "balance");
    s.balances.reserve(balances);
    for (std::size_t i = 0; i < balances; ++i) {
        settlement::BalanceRecord b;
        b.account_id = r.string(); b.asset_id = r.string();
        b.state.total = r.u64(); b.state.reserved = r.u64();
        s.balances.push_back(std::move(b));
    }
    const auto journal = r.count(5'000'000, "journal");
    s.journal.reserve(journal);
    for (std::size_t i = 0; i < journal; ++i) {
        settlement::JournalEntry e;
        e.sequence = r.u64(); e.operation = r.string(); e.account_id = r.string(); e.asset_id = r.string();
        e.total_after = r.u64(); e.reserved_after = r.u64(); e.reference = r.string();
        s.journal.push_back(std::move(e));
    }
    s.next_journal_sequence = r.u64();
    return s;
}

void write_match(Writer& w, const exchange::Snapshot& s) {
    if (s.registered_accounts.size() > 100'000 || s.orders.size() > 1'000'000 || s.trades.size() > 5'000'000) {
        throw std::length_error("matching snapshot exceeds serialization limits");
    }
    w.u32(static_cast<std::uint32_t>(s.registered_accounts.size()));
    for (const auto& a : s.registered_accounts) w.string(a);
    w.u32(static_cast<std::uint32_t>(s.orders.size()));
    for (const auto& o : s.orders) {
        w.u64(o.id); w.u64(o.sequence); w.string(o.account_id);
        w.u8(static_cast<std::uint8_t>(o.side)); w.u8(static_cast<std::uint8_t>(o.type));
        w.u64(o.limit_price); w.u64(o.quantity); w.u64(o.remaining_quantity);
        w.u64(o.filled_quantity); w.u64(o.reserved_remaining); w.u8(static_cast<std::uint8_t>(o.status));
    }
    w.u32(static_cast<std::uint32_t>(s.trades.size()));
    for (const auto& t : s.trades) {
        w.u64(t.id); w.u64(t.maker_order_id); w.u64(t.taker_order_id);
        w.string(t.buyer); w.string(t.seller); w.u64(t.price); w.u64(t.quantity); w.u64(t.quote_amount);
    }
    w.u64(s.next_order_id); w.u64(s.next_trade_id);
}

exchange::Snapshot read_match(Reader& r) {
    exchange::Snapshot s;
    const auto accounts = r.count(100'000, "Match account");
    s.registered_accounts.reserve(accounts);
    for (std::size_t i = 0; i < accounts; ++i) s.registered_accounts.push_back(r.string());
    const auto orders = r.count(1'000'000, "order");
    s.orders.reserve(orders);
    for (std::size_t i = 0; i < orders; ++i) {
        exchange::Order o;
        o.id = r.u64(); o.sequence = r.u64(); o.account_id = r.string();
        o.side = static_cast<exchange::Side>(r.u8()); o.type = static_cast<exchange::OrderType>(r.u8());
        o.limit_price = r.u64(); o.quantity = r.u64(); o.remaining_quantity = r.u64();
        o.filled_quantity = r.u64(); o.reserved_remaining = r.u64();
        o.status = static_cast<exchange::OrderStatus>(r.u8());
        s.orders.push_back(std::move(o));
    }
    const auto trades = r.count(5'000'000, "trade");
    s.trades.reserve(trades);
    for (std::size_t i = 0; i < trades; ++i) {
        exchange::Trade t;
        t.id = r.u64(); t.maker_order_id = r.u64(); t.taker_order_id = r.u64();
        t.buyer = r.string(); t.seller = r.string(); t.price = r.u64();
        t.quantity = r.u64(); t.quote_amount = r.u64();
        s.trades.push_back(std::move(t));
    }
    s.next_order_id = r.u64(); s.next_trade_id = r.u64();
    return s;
}

void write_flow(Writer& w, const flow::Snapshot& s) {
    if (s.registered_accounts.size() > 100'000 || s.pools.size() > 100'000 ||
        s.lp_positions.size() > 1'000'000 || s.swaps.size() > 5'000'000) {
        throw std::length_error("Flow snapshot exceeds serialization limits");
    }
    w.u32(static_cast<std::uint32_t>(s.registered_accounts.size()));
    for (const auto& a : s.registered_accounts) w.string(a);
    w.u32(static_cast<std::uint32_t>(s.pools.size()));
    for (const auto& p : s.pools) {
        w.string(p.id); w.string(p.asset_x); w.string(p.asset_y); w.u64(p.reserve_x);
        w.u64(p.reserve_y); w.u64(p.total_lp_shares); w.u32(p.fee_bps);
    }
    w.u32(static_cast<std::uint32_t>(s.lp_positions.size()));
    for (const auto& p : s.lp_positions) { w.string(p.pool_id); w.string(p.account_id); w.u64(p.shares); }
    w.u32(static_cast<std::uint32_t>(s.swaps.size()));
    for (const auto& t : s.swaps) {
        w.u64(t.id); w.string(t.account_id); w.string(t.pool_id); w.string(t.asset_in);
        w.string(t.asset_out); w.u64(t.amount_in); w.u64(t.amount_out); w.u64(t.fee_amount);
    }
    w.u64(s.next_pool_id); w.u64(s.next_swap_id);
}

flow::Snapshot read_flow(Reader& r) {
    flow::Snapshot s;
    const auto accounts = r.count(100'000, "Flow account");
    s.registered_accounts.reserve(accounts);
    for (std::size_t i = 0; i < accounts; ++i) s.registered_accounts.push_back(r.string());
    const auto pools = r.count(100'000, "pool");
    s.pools.reserve(pools);
    for (std::size_t i = 0; i < pools; ++i) {
        flow::Pool p;
        p.id = r.string(); p.asset_x = r.string(); p.asset_y = r.string();
        p.reserve_x = r.u64(); p.reserve_y = r.u64(); p.total_lp_shares = r.u64(); p.fee_bps = r.u32();
        s.pools.push_back(std::move(p));
    }
    const auto positions = r.count(1'000'000, "LP position");
    s.lp_positions.reserve(positions);
    for (std::size_t i = 0; i < positions; ++i) {
        flow::LpPosition p;
        p.pool_id = r.string(); p.account_id = r.string(); p.shares = r.u64();
        s.lp_positions.push_back(std::move(p));
    }
    const auto swaps = r.count(5'000'000, "swap");
    s.swaps.reserve(swaps);
    for (std::size_t i = 0; i < swaps; ++i) {
        flow::SwapRecord t;
        t.id = r.u64(); t.account_id = r.string(); t.pool_id = r.string();
        t.asset_in = r.string(); t.asset_out = r.string(); t.amount_in = r.u64();
        t.amount_out = r.u64(); t.fee_amount = r.u64();
        s.swaps.push_back(std::move(t));
    }
    s.next_pool_id = r.u64(); s.next_swap_id = r.u64();
    return s;
}

void write_bridge(Writer& w, const BridgeSnapshot& s) {
    if (s.deposits.size() > 5'000'000 || s.withdrawals.size() > 5'000'000) {
        throw std::length_error("bridge history exceeds serialization limits");
    }
    w.string(s.vault_address);
    w.u32(static_cast<std::uint32_t>(s.deposits.size()));
    for (const auto& deposit : s.deposits) {
        w.string(deposit.transaction_id);
        w.string(deposit.account_id);
        w.u64(deposit.amount);
        w.u64(deposit.block_height);
    }
    w.u32(static_cast<std::uint32_t>(s.withdrawals.size()));
    for (const auto& withdrawal : s.withdrawals) {
        w.string(withdrawal.transaction_id);
        w.string(withdrawal.account_id);
        w.u64(withdrawal.amount);
        w.u64(withdrawal.fee);
        w.blob(withdrawal.signed_transaction);
        w.u8(static_cast<std::uint8_t>(withdrawal.status));
        w.u64(withdrawal.block_height);
    }
}

BridgeSnapshot read_bridge(Reader& r) {
    BridgeSnapshot s;
    s.vault_address = r.string();
    const auto deposits = r.count(5'000'000, "bridge deposit");
    s.deposits.reserve(deposits);
    for (std::size_t i = 0; i < deposits; ++i) {
        BridgeDepositRecord record;
        record.transaction_id = r.string();
        record.account_id = r.string();
        record.amount = r.u64();
        record.block_height = r.u64();
        s.deposits.push_back(std::move(record));
    }
    const auto withdrawals = r.count(5'000'000, "bridge withdrawal");
    s.withdrawals.reserve(withdrawals);
    for (std::size_t i = 0; i < withdrawals; ++i) {
        BridgeWithdrawalRecord record;
        record.transaction_id = r.string();
        record.account_id = r.string();
        record.amount = r.u64();
        record.fee = r.u64();
        record.signed_transaction = r.blob();
        const auto status = r.u8();
        if (status > static_cast<std::uint8_t>(BridgeWithdrawalStatus::Confirmed)) {
            throw std::runtime_error("invalid bridge withdrawal status in snapshot");
        }
        record.status = static_cast<BridgeWithdrawalStatus>(status);
        record.block_height = r.u64();
        s.withdrawals.push_back(std::move(record));
    }
    return s;
}

bool valid_transaction_id(const std::string& id) {
    if (id.size() != 64) return false;
    return std::all_of(id.begin(), id.end(), [](unsigned char c) {
        return (c >= '0' && c <= '9') || (c >= 'a' && c <= 'f');
    });
}

Amount checked_add(Amount left, Amount right, const char* message) {
    if (left > UINT64_MAX - right) throw std::overflow_error(message);
    return left + right;
}

} // namespace

FinancialPlatform::RuntimeState::RuntimeState(std::shared_ptr<settlement::SettlementBook> book)
    : settlement(std::move(book)),
      match(settlement, FinancialPlatform::kBaseAsset, FinancialPlatform::kQuoteAsset,
            FinancialPlatform::kBaseLotSize),
      flow(settlement) {}

FinancialPlatform::RuntimeState FinancialPlatform::RuntimeState::clone() const {
    auto copied_book = std::make_shared<settlement::SettlementBook>(*settlement);
    RuntimeState copy(copied_book);
    copy.match = match.with_settlement(copied_book);
    copy.flow = flow.with_settlement(copied_book);
    copy.bridge = bridge;
    return copy;
}

FinancialPlatform::FinancialPlatform(storage::SqliteStore store,
                                     RuntimeState state,
                                     std::uint64_t revision)
    : store_(std::move(store)), state_(std::move(state)), revision_(revision) {}

FinancialPlatform FinancialPlatform::open(const std::string& ledger_database_path) {
    if (ledger_database_path != ":memory:" && !std::filesystem::is_regular_file(ledger_database_path)) {
        throw std::runtime_error("Ledger database does not exist: " + ledger_database_path);
    }
    storage::SqliteStore store(ledger_database_path);
    store.initialize_schema();
    auto saved = store.load_financial_state();
    if (!saved) {
        RuntimeState empty(std::make_shared<settlement::SettlementBook>());
        return FinancialPlatform(std::move(store), std::move(empty), 0);
    }
    RuntimeState recovered = deserialize_state(saved->payload);
    FinancialPlatform platform(std::move(store), std::move(recovered), saved->revision);
    std::string error;
    if (!platform.validate(&error)) {
        throw std::runtime_error("persisted financial platform validation failed: " + error);
    }
    return platform;
}

AccountBalance FinancialPlatform::balance(const std::string& account_id,
                                          const std::string& asset_id) const {
    const auto value = state_.settlement->state(account_id, asset_id);
    return {value.total, value.reserved};
}

void FinancialPlatform::persist_and_commit(RuntimeState&& candidate) {
    std::string error;
    if (!validate_state(candidate, &error)) {
        throw std::logic_error("financial platform validation failed before commit: " + error);
    }
    const Bytes payload = serialize_state(candidate);
    const std::uint64_t next_revision = store_.save_financial_state(payload, revision_);
    state_ = std::move(candidate);
    revision_ = next_revision;
}

void FinancialPlatform::create_account(const std::string& account_id) {
    RuntimeState candidate = state_.clone();
    candidate.settlement->create_account(account_id);
    candidate.match.register_account(account_id);
    candidate.flow.register_account(account_id);
    persist_and_commit(std::move(candidate));
}

void FinancialPlatform::fund_test_asset(const std::string& account_id, const std::string& asset_id,
                                        Amount amount, const std::string& reason) {
    if (reason.empty()) throw std::invalid_argument("test funding requires an audit reason");
    RuntimeState candidate = state_.clone();
    candidate.settlement->credit(account_id, asset_id, amount, "explicit-test-credit", reason);
    persist_and_commit(std::move(candidate));
}

void FinancialPlatform::transfer_asset(const std::string& from_account, const std::string& to_account,
                                       const std::string& asset_id, Amount amount) {
    RuntimeState candidate = state_.clone();
    candidate.settlement->transfer_available(from_account, to_account, asset_id, amount,
                                             "platform-transfer", "internal account transfer");
    persist_and_commit(std::move(candidate));
}

void FinancialPlatform::configure_bridge_vault(const std::string& vault_address) {
    if (!crypto::is_valid_address(vault_address)) throw std::invalid_argument("invalid bridge vault address");
    if (!state_.bridge.vault_address.empty()) {
        if (state_.bridge.vault_address == vault_address) return;
        throw std::logic_error("bridge vault is immutable once configured");
    }
    if (state_.settlement->has_account(vault_address)) {
        throw std::invalid_argument("bridge vault must not be linked as a settlement customer account");
    }
    RuntimeState candidate = state_.clone();
    candidate.bridge.vault_address = vault_address;
    persist_and_commit(std::move(candidate));
}

bool FinancialPlatform::apply_confirmed_bridge_deposit(const std::string& transaction_id,
                                                        const std::string& account_id,
                                                        Amount amount,
                                                        std::uint64_t block_height) {
    if (state_.bridge.vault_address.empty()) throw std::logic_error("bridge vault is not configured");
    if (!valid_transaction_id(transaction_id) || !crypto::is_valid_address(account_id) ||
        amount == 0 || block_height == 0) {
        throw std::invalid_argument("invalid confirmed bridge deposit metadata");
    }
    for (const auto& existing : state_.bridge.deposits) {
        if (existing.transaction_id == transaction_id) {
            if (existing.account_id != account_id || existing.amount != amount ||
                existing.block_height != block_height) {
                throw std::logic_error("bridge deposit transaction ID conflicts with existing history");
            }
            return false;
        }
    }
    RuntimeState candidate = state_.clone();
    if (!candidate.settlement->has_account(account_id)) {
        candidate.settlement->create_account(account_id);
        candidate.match.register_account(account_id);
        candidate.flow.register_account(account_id);
    }
    candidate.settlement->credit(account_id, kBaseAsset, amount, "bridge-deposit", transaction_id);
    candidate.bridge.deposits.push_back({transaction_id, account_id, amount, block_height});
    persist_and_commit(std::move(candidate));
    return true;
}

void FinancialPlatform::prepare_bridge_withdrawal(const ledger::Transaction& transaction) {
    if (state_.bridge.vault_address.empty()) throw std::logic_error("bridge vault is not configured");
    if (!transaction.verify_signature() || transaction.sender != state_.bridge.vault_address ||
        transaction.asset != kBaseAsset || !crypto::is_valid_address(transaction.recipient) ||
        transaction.amount == 0 || transaction.amount > UINT64_MAX - transaction.fee) {
        throw std::invalid_argument("invalid signed bridge withdrawal transaction");
    }
    const auto id_hash = transaction.id();
    const std::string transaction_id = crypto::hex(id_hash.data(), id_hash.size());
    for (const auto& existing : state_.bridge.withdrawals) {
        if (existing.transaction_id == transaction_id) {
            throw std::logic_error("bridge withdrawal transaction is already recorded");
        }
    }
    if (std::any_of(state_.bridge.withdrawals.begin(), state_.bridge.withdrawals.end(),
                    [](const auto& record) { return record.status == BridgeWithdrawalStatus::Pending; })) {
        throw std::logic_error("resolve the existing pending bridge withdrawal before creating another");
    }
    RuntimeState candidate = state_.clone();
    candidate.settlement->debit_available(transaction.recipient, kBaseAsset, transaction.amount,
                                           "bridge-withdrawal-pending", transaction_id);
    candidate.bridge.withdrawals.push_back({transaction_id, transaction.recipient, transaction.amount,
                                            transaction.fee, transaction.serialize(),
                                            BridgeWithdrawalStatus::Pending, 0});
    persist_and_commit(std::move(candidate));
}

bool FinancialPlatform::confirm_bridge_withdrawal(const std::string& transaction_id,
                                                   std::uint64_t block_height) {
    if (!valid_transaction_id(transaction_id) || block_height == 0) {
        throw std::invalid_argument("invalid confirmed bridge withdrawal metadata");
    }
    const auto found = std::find_if(state_.bridge.withdrawals.begin(), state_.bridge.withdrawals.end(),
        [&](const auto& record) { return record.transaction_id == transaction_id; });
    if (found == state_.bridge.withdrawals.end()) throw std::invalid_argument("unknown bridge withdrawal transaction");
    if (found->status == BridgeWithdrawalStatus::Confirmed) {
        if (found->block_height != block_height) throw std::logic_error("bridge withdrawal confirmation height conflicts");
        return false;
    }
    RuntimeState candidate = state_.clone();
    auto candidate_record = std::find_if(candidate.bridge.withdrawals.begin(), candidate.bridge.withdrawals.end(),
        [&](const auto& record) { return record.transaction_id == transaction_id; });
    candidate_record->status = BridgeWithdrawalStatus::Confirmed;
    candidate_record->block_height = block_height;
    persist_and_commit(std::move(candidate));
    return true;
}

std::vector<std::pair<std::string, std::uint64_t>> FinancialPlatform::bridge_transaction_references() const {
    std::vector<std::pair<std::string, std::uint64_t>> references;
    references.reserve(state_.bridge.deposits.size() + state_.bridge.withdrawals.size());
    for (const auto& deposit : state_.bridge.deposits) {
        references.emplace_back(deposit.transaction_id, deposit.block_height);
    }
    for (const auto& withdrawal : state_.bridge.withdrawals) {
        references.emplace_back(withdrawal.transaction_id, withdrawal.block_height);
    }
    return references;
}

Amount FinancialPlatform::total_fst_liabilities() const {
    Amount total = 0;
    for (const auto& account : state_.settlement->accounts()) {
        total = checked_add(total, state_.settlement->total(account, kBaseAsset),
                            "aggregate FST account liabilities overflow");
    }
    const auto flow_snapshot = state_.flow.snapshot();
    for (const auto& pool : flow_snapshot.pools) {
        if (pool.asset_x == kBaseAsset) {
            total = checked_add(total, pool.reserve_x, "aggregate FST pool liabilities overflow");
        }
        if (pool.asset_y == kBaseAsset) {
            total = checked_add(total, pool.reserve_y, "aggregate FST pool liabilities overflow");
        }
    }
    return total;
}

exchange::OrderId FinancialPlatform::submit_limit(const std::string& account_id,
                                                  exchange::Side side,
                                                  exchange::Price price,
                                                  exchange::Quantity quantity) {
    RuntimeState candidate = state_.clone();
    const auto id = candidate.match.submit_limit(account_id, side, price, quantity);
    persist_and_commit(std::move(candidate));
    return id;
}

exchange::OrderId FinancialPlatform::submit_market_buy(const std::string& account_id, Amount quote_budget) {
    RuntimeState candidate = state_.clone();
    const auto id = candidate.match.submit_market_buy(account_id, quote_budget);
    persist_and_commit(std::move(candidate));
    return id;
}

exchange::OrderId FinancialPlatform::submit_market_sell(const std::string& account_id,
                                                        exchange::Quantity quantity) {
    RuntimeState candidate = state_.clone();
    const auto id = candidate.match.submit_market_sell(account_id, quantity);
    persist_and_commit(std::move(candidate));
    return id;
}

bool FinancialPlatform::cancel_order(const std::string& account_id, exchange::OrderId order_id) {
    RuntimeState candidate = state_.clone();
    const bool cancelled = candidate.match.cancel_order(account_id, order_id);
    if (!cancelled) return false;
    persist_and_commit(std::move(candidate));
    return true;
}

std::string FinancialPlatform::create_pool(const std::string& asset_x,
                                           const std::string& asset_y,
                                           std::uint32_t fee_bps) {
    RuntimeState candidate = state_.clone();
    const auto pool_id = candidate.flow.create_pool(asset_x, asset_y, fee_bps);
    persist_and_commit(std::move(candidate));
    return pool_id;
}

flow::LiquidityResult FinancialPlatform::add_liquidity(const std::string& account_id,
                                                        const std::string& pool_id,
                                                        Amount max_amount_x,
                                                        Amount max_amount_y,
                                                        Amount minimum_shares) {
    RuntimeState candidate = state_.clone();
    const auto result = candidate.flow.add_liquidity(account_id, pool_id, max_amount_x, max_amount_y, minimum_shares);
    persist_and_commit(std::move(candidate));
    return result;
}

flow::LiquidityResult FinancialPlatform::remove_liquidity(const std::string& account_id,
                                                           const std::string& pool_id,
                                                           Amount shares) {
    RuntimeState candidate = state_.clone();
    const auto result = candidate.flow.remove_liquidity(account_id, pool_id, shares);
    persist_and_commit(std::move(candidate));
    return result;
}

flow::SwapQuote FinancialPlatform::quote(const std::string& pool_id,
                                         const std::string& asset_in,
                                         Amount amount_in) const {
    return state_.flow.quote(pool_id, asset_in, amount_in);
}

flow::SwapRecord FinancialPlatform::swap(const std::string& account_id,
                                         const std::string& pool_id,
                                         const std::string& asset_in,
                                         Amount amount_in,
                                         Amount minimum_amount_out) {
    RuntimeState candidate = state_.clone();
    const auto result = candidate.flow.swap(account_id, pool_id, asset_in, amount_in, minimum_amount_out);
    persist_and_commit(std::move(candidate));
    return result;
}

bool FinancialPlatform::validate(std::string* error) const {
    return validate_state(state_, error);
}

bool FinancialPlatform::validate_state(const RuntimeState& state, std::string* error) {
    if (!state.settlement->validate(error)) return false;
    if (!state.match.validate(error)) return false;
    const auto flow_snapshot = state.flow.snapshot();
    for (const auto& pool : flow_snapshot.pools) {
        if (!state.flow.validate_pool(pool.id, error)) return false;
    }
    const auto settlement_accounts = state.settlement->accounts();
    const auto match_accounts = state.match.snapshot().registered_accounts;
    if (settlement_accounts != match_accounts || settlement_accounts != flow_snapshot.registered_accounts) {
        if (error != nullptr) *error = "engine account registrations differ from shared settlement accounts";
        return false;
    }
    if (!state.bridge.vault_address.empty() && !crypto::is_valid_address(state.bridge.vault_address)) {
        if (error != nullptr) *error = "bridge vault address is invalid";
        return false;
    }
    if (!state.bridge.vault_address.empty() && state.settlement->has_account(state.bridge.vault_address)) {
        if (error != nullptr) *error = "bridge vault must not be a settlement customer account";
        return false;
    }
    std::set<std::string> seen_deposits;
    for (const auto& deposit : state.bridge.deposits) {
        if (state.bridge.vault_address.empty() || !valid_transaction_id(deposit.transaction_id) ||
            !crypto::is_valid_address(deposit.account_id) || deposit.account_id == state.bridge.vault_address ||
            !state.settlement->has_account(deposit.account_id) || deposit.amount == 0 || deposit.block_height == 0 ||
            !seen_deposits.insert(deposit.transaction_id).second) {
            if (error != nullptr) *error = "bridge deposit history is inconsistent";
            return false;
        }
    }
    std::set<std::string> seen_withdrawals;
    std::size_t pending_withdrawals = 0;
    for (const auto& withdrawal : state.bridge.withdrawals) {
        if (state.bridge.vault_address.empty() || !valid_transaction_id(withdrawal.transaction_id) ||
            !crypto::is_valid_address(withdrawal.account_id) || withdrawal.account_id == state.bridge.vault_address ||
            !state.settlement->has_account(withdrawal.account_id) ||
            withdrawal.amount == 0 || withdrawal.amount > UINT64_MAX - withdrawal.fee ||
            withdrawal.signed_transaction.empty() || !seen_withdrawals.insert(withdrawal.transaction_id).second) {
            if (error != nullptr) *error = "bridge withdrawal history is inconsistent";
            return false;
        }
        try {
            const auto transaction = ledger::Transaction::deserialize(withdrawal.signed_transaction);
            const auto id_hash = transaction.id();
            const auto expected_id = crypto::hex(id_hash.data(), id_hash.size());
            if (expected_id != withdrawal.transaction_id || !transaction.verify_signature() ||
                transaction.sender != state.bridge.vault_address || transaction.recipient != withdrawal.account_id ||
                transaction.amount != withdrawal.amount || transaction.fee != withdrawal.fee ||
                transaction.asset != kBaseAsset) {
                if (error != nullptr) *error = "bridge withdrawal transaction payload does not match its journal";
                return false;
            }
        } catch (const std::exception& exception) {
            if (error != nullptr) *error = std::string("bridge withdrawal payload is invalid: ") + exception.what();
            return false;
        }
        if (withdrawal.status == BridgeWithdrawalStatus::Pending) ++pending_withdrawals;
        if ((withdrawal.status == BridgeWithdrawalStatus::Pending && withdrawal.block_height != 0) ||
            (withdrawal.status == BridgeWithdrawalStatus::Confirmed && withdrawal.block_height == 0)) {
            if (error != nullptr) *error = "bridge withdrawal confirmation metadata is inconsistent";
            return false;
        }
    }
    if (pending_withdrawals > 1) {
        if (error != nullptr) *error = "bridge snapshot contains multiple pending withdrawals";
        return false;
    }
    return true;
}

Bytes FinancialPlatform::serialize_state(const RuntimeState& state) {
    Writer writer;
    for (const char c : std::string("FSPF")) writer.u8(static_cast<std::uint8_t>(c));
    writer.u32(2);
    write_settlement(writer, state.settlement->snapshot());
    write_match(writer, state.match.snapshot());
    write_flow(writer, state.flow.snapshot());
    write_bridge(writer, state.bridge);
    return std::move(writer).finish();
}

FinancialPlatform::RuntimeState FinancialPlatform::deserialize_state(const Bytes& bytes) {
    Reader reader(bytes);
    reader.magic("FSPF", 4);
    const auto version = reader.u32();
    if (version != 1 && version != 2) throw std::runtime_error("unsupported financial platform snapshot version");
    const auto settlement_snapshot = read_settlement(reader);
    const auto match_snapshot = read_match(reader);
    const auto flow_snapshot = read_flow(reader);

    auto book = std::make_shared<settlement::SettlementBook>(
        settlement::SettlementBook::from_snapshot(settlement_snapshot));
    RuntimeState state(book);
    state.match = exchange::MatchingEngine::restore(book, kBaseAsset, kQuoteAsset, kBaseLotSize, match_snapshot);
    state.flow = flow::AmmEngine::restore(book, flow_snapshot);
    if (version >= 2) state.bridge = read_bridge(reader);
    reader.finish();
    return state;
}

} // namespace feinstat::platform
