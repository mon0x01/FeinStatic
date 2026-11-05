#include "feinstat/application/amount.hpp"
#include "feinstat/bridge/bridge_service.hpp"
#include "feinstat/application/terminal.hpp"
#include "feinstat/application/verification.hpp"
#include "feinstat/exchange/matching_engine.hpp"
#include "feinstat/flow/amm.hpp"
#include "feinstat/ledger/ledger.hpp"
#include "feinstat/network/node.hpp"
#include "feinstat/platform/financial_platform.hpp"

#include <algorithm>
#include <charconv>
#include <csignal>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <iomanip>
#include <limits>
#include <memory>
#include <optional>
#include <sstream>
#include <stdexcept>
#include <string>
#include <vector>

namespace {

using namespace feinstat;
using namespace feinstat::ledger;

class SecretWiper {
public:
    explicit SecretWiper(std::string& secret) : secret_(secret) {}
    ~SecretWiper() { application::clear_secret(secret_); }
    SecretWiper(const SecretWiper&) = delete;
    SecretWiper& operator=(const SecretWiper&) = delete;
private:
    std::string& secret_;
};

std::string read_line(const std::string& prompt) {
    std::cout << prompt;
    std::string value;
    if (!std::getline(std::cin, value)) throw std::runtime_error("input stream closed");
    return value;
}

std::uint64_t read_uint(const std::string& prompt) {
    const std::string text = read_line(prompt);
    if (text.empty()) throw std::invalid_argument("enter an unsigned integer");
    std::uint64_t value = 0;
    const auto [end, error] = std::from_chars(text.data(), text.data() + text.size(), value);
    if (error != std::errc{} || end != text.data() + text.size()) {
        throw std::invalid_argument("enter an unsigned integer without signs or extra characters");
    }
    return value;
}

void print_dashboard(const Ledger& ledger) {
    const auto& chain = ledger.chain();
    std::cout << "\n  FEINSTATIC  /  PRIVATE LEDGER\n"
              << "  ---------------------------------------------\n"
              << "  Chain height : " << chain.height() << '\n'
              << "  Block count  : " << chain.blocks().size() << '\n'
              << "  Native supply: " << application::format_amount(chain.total_supply()) << " FST\n"
              << "  Pending txs  : " << ledger.mempool().transactions().size() << '\n'
              << "  Wallets      : " << ledger.wallets().size() << '\n'
              << "  ---------------------------------------------\n";
}

void print_wallets(const Ledger& ledger) {
    const auto wallets = ledger.wallets();
    if (wallets.empty()) {
        std::cout << "No wallet records. Create a wallet first.\n";
        return;
    }
    std::cout << "\nStored wallets\n";
    for (const auto& [address, public_key] : wallets) {
        (void)public_key;
        std::cout << "  " << address << "  "
                  << application::format_amount(ledger.chain().balance_of(address)) << " FST\n";
    }
}

void create_wallet(Ledger& ledger) {
    std::string passphrase = application::read_confirmed_secret(
        "New wallet passphrase (input hidden): ", "Confirm wallet passphrase: ");
    SecretWiper wipe_passphrase(passphrase);
    Wallet wallet = ledger.create_wallet(passphrase);
    std::cout << "Wallet created. Store this address safely:\n  " << wallet.address() << '\n'
              << "Balance: 0 FST. The secret key is encrypted in the local SQLite database.\n";
}

void transfer(Ledger& ledger, const platform::FinancialPlatform& financial) {
    print_wallets(ledger);
    const Address sender_address = read_line("Sender address: ");
    if (!financial.bridge().vault_address.empty() && sender_address == financial.bridge().vault_address) {
        throw std::runtime_error("the configured bridge vault is reserved for Bridge withdrawals; use the Bridge menu");
    }
    std::string passphrase = application::read_secret("Wallet passphrase (input hidden): ");
    SecretWiper wipe_passphrase(passphrase);
    Wallet sender = ledger.unlock_wallet(sender_address, passphrase);
    const Address recipient = read_line("Recipient address: ");
    const Amount amount = application::parse_amount(read_line("Amount (FST): "));
    const Amount fee = application::parse_amount(read_line("Fee (FST): "));
    const Transaction transaction = ledger.create_transfer(sender, recipient, amount, fee);
    std::string error;
    if (!ledger.submit(transaction, &error)) throw std::runtime_error("transaction rejected: " + error);
    std::cout << "Accepted into mempool. Transaction ID: ";
    const auto id = transaction.id();
    std::cout << crypto::hex(id.data(), id.size()) << '\n';
}

void mine(Ledger& ledger, platform::FinancialPlatform& financial) {
    print_wallets(ledger);
    const Address miner = read_line("Miner wallet address: ");
    const Block block = ledger.mine(miner);
    std::cout << "Mined block #" << block.height << "\n  hash: "
              << crypto::hex(block.hash.data(), block.hash.size()) << "\n  transactions: "
              << block.transactions.size() << "\n  reward: "
              << application::format_amount(block.reward) << " FST\n";
    bridge::BridgeService bridge_service(ledger, financial);
    const auto reconciled = bridge_service.reconcile_confirmed_withdrawals();
    if (reconciled != 0) {
        std::cout << "Reconciled " << reconciled << " confirmed bridge withdrawal(s).\n";
    }
}

void print_match_depth(const exchange::MatchingEngine& engine) {
    std::cout << "\nMATCH / ORDER BOOK\n"
              << "BIDS (USD per FST / FST lots / orders)\n";
    const auto bids = engine.bid_depth(10);
    if (bids.empty()) std::cout << "  --\n";
    for (const auto& level : bids) {
        std::cout << "  " << application::format_amount(level.price) << " / "
                  << level.quantity << " / " << level.order_count << '\n';
    }
    std::cout << "ASKS (USD per FST / FST lots / orders)\n";
    const auto asks = engine.ask_depth(10);
    if (asks.empty()) std::cout << "  --\n";
    for (const auto& level : asks) {
        std::cout << "  " << application::format_amount(level.price) << " / "
                  << level.quantity << " / " << level.order_count << '\n';
    }
    std::cout << "Best bid: ";
    if (engine.best_bid()) std::cout << application::format_amount(*engine.best_bid()); else std::cout << "--";
    std::cout << "   Best ask: ";
    if (engine.best_ask()) std::cout << application::format_amount(*engine.best_ask()); else std::cout << "--";
    std::cout << "   Last trade: ";
    if (engine.last_price()) std::cout << application::format_amount(*engine.last_price()); else std::cout << "--";
    std::cout << '\n';
}

void run_match_demo() {
    exchange::MatchingEngine engine;
    engine.create_account("alice", 0, 1'000);
    engine.create_account("bob", 10, 0);
    const auto buy = engine.submit_limit("alice", exchange::Side::Buy, 100, 5);
    const auto sell = engine.submit_limit("bob", exchange::Side::Sell, 100, 3);
    std::cout << "\nMATCH DEMO: buy order " << buy << ", sell order " << sell << '\n';
    for (const auto& trade : engine.trades()) {
        std::cout << "Executed " << trade.quantity << " lots @ " << trade.price
                  << "; buyer remaining: " << engine.order(buy)->remaining_quantity << '\n';
    }
}

void run_flow_demo() {
    flow::AmmEngine engine;
    engine.create_account("lp");
    engine.create_account("trader");
    engine.credit("lp", "FST", 1'000ULL * kAtomsPerCoin);
    engine.credit("lp", "USD", 1'000ULL * kAtomsPerCoin);
    engine.credit("trader", "FST", 100ULL * kAtomsPerCoin);
    const auto pool_id = engine.create_pool("FST", "USD");
    (void)engine.add_liquidity("lp", pool_id, 1'000ULL * kAtomsPerCoin, 1'000ULL * kAtomsPerCoin);
    const auto quote = engine.quote(pool_id, "FST", 10ULL * kAtomsPerCoin);
    const auto trade = engine.swap("trader", pool_id, "FST", 10ULL * kAtomsPerCoin, quote.amount_out);
    std::cout << "\nFLOW DEMO: swapped " << application::format_amount(trade.amount_in)
              << " FST for " << application::format_amount(trade.amount_out)
              << " USD; fee " << application::format_amount(trade.fee_amount) << '\n';
}

void print_platform_accounts(const platform::FinancialPlatform& financial) {
    const auto accounts = financial.accounts();
    if (accounts.empty()) {
        std::cout << "No off-chain platform accounts are linked. Link a Ledger wallet first.\n";
        return;
    }
    std::cout << "\nOFF-CHAIN SETTLEMENT ACCOUNTS (asset values are separate from on-chain Ledger balances)\n";
    for (const auto& account : accounts) {
        const auto fst = financial.balance(account, "FST");
        const auto usd = financial.balance(account, "USD");
        std::cout << "  " << account << "\n"
                  << "    FST available=" << application::format_amount(fst.available())
                  << " reserved=" << application::format_amount(fst.reserved)
                  << " total=" << application::format_amount(fst.total) << "\n"
                  << "    USD available=" << application::format_amount(usd.available())
                  << " reserved=" << application::format_amount(usd.reserved)
                  << " total=" << application::format_amount(usd.total) << "\n";
    }
}

void link_ledger_wallet(platform::FinancialPlatform& financial, const Ledger& ledger) {
    const auto wallets = ledger.wallets();
    if (wallets.empty()) throw std::runtime_error("no Ledger wallets exist to link");
    std::cout << "\nLedger wallets\n";
    for (const auto& [address, public_key] : wallets) {
        (void)public_key;
        std::cout << "  " << address << '\n';
    }
    const auto address = read_line("Wallet address to link: ");
    const auto found = std::find_if(wallets.begin(), wallets.end(), [&](const auto& item) {
        return item.first == address;
    });
    if (found == wallets.end()) throw std::invalid_argument("address is not a wallet in this Ledger");
    financial.create_account(address);
    std::cout << "Linked wallet identity to the off-chain settlement platform. No coins were deposited.\n";
}

void run_persistent_match(platform::FinancialPlatform& financial) {
    std::cout << "\nMATCH / PERSISTENT OFF-CHAIN ORDER BOOK\n"
              << "Market: FST/USD. Quantity is whole FST; price is USD per FST.\n"
              << "Order state, trades, shared balances and reservations are persisted in SQLite.\n";
    for (;;) {
        std::cout << "\n  1. Order book\n  2. Place limit order\n  3. Market buy (USD budget)\n"
                  << "  4. Market sell (whole FST)\n  5. Cancel order\n  6. Trade history\n"
                  << "  7. Account balances\n  0. Return\n";
        const auto choice = read_line("match> ");
        try {
            if (choice == "1") print_match_depth(financial.match());
            else if (choice == "2") {
                const auto account = read_line("Account (Ledger wallet address): ");
                const auto side_text = read_line("Side (buy/sell): ");
                const auto side = side_text == "buy" ? exchange::Side::Buy
                    : side_text == "sell" ? exchange::Side::Sell
                    : throw std::invalid_argument("side must be buy or sell");
                const auto price = application::parse_amount(read_line("Limit price (USD per FST): "));
                const auto quantity = read_uint("Quantity (whole FST): ");
                const auto id = financial.submit_limit(account, side, price, quantity);
                std::cout << "Persisted order " << id << " status: "
                          << exchange::to_string(financial.match().order(id)->status) << '\n';
            } else if (choice == "3") {
                const auto account = read_line("Account (Ledger wallet address): ");
                const auto budget = application::parse_amount(read_line("USD budget: "));
                const auto id = financial.submit_market_buy(account, budget);
                std::cout << "Persisted market order " << id << " status: "
                          << exchange::to_string(financial.match().order(id)->status) << '\n';
            } else if (choice == "4") {
                const auto account = read_line("Account (Ledger wallet address): ");
                const auto quantity = read_uint("Quantity (whole FST): ");
                const auto id = financial.submit_market_sell(account, quantity);
                std::cout << "Persisted market order " << id << " status: "
                          << exchange::to_string(financial.match().order(id)->status) << '\n';
            } else if (choice == "5") {
                const auto account = read_line("Account (Ledger wallet address): ");
                const auto id = read_uint("Order ID: ");
                std::cout << (financial.cancel_order(account, id) ? "Cancelled and persisted.\n" : "Order not cancellable.\n");
            } else if (choice == "6") {
                for (const auto& trade : financial.match().trades()) {
                    std::cout << "Trade " << trade.id << ": " << trade.buyer << " bought "
                              << trade.quantity << " FST from " << trade.seller << " @ "
                              << application::format_amount(trade.price) << " USD (notional "
                              << application::format_amount(trade.quote_amount) << " USD)\n";
                }
            } else if (choice == "7") {
                print_platform_accounts(financial);
            } else if (choice == "0") return;
            else std::cout << "Unknown option.\n";
        } catch (const std::exception& exception) {
            std::cout << "Match error: " << exception.what() << '\n';
        }
    }
}

void run_persistent_flow(platform::FinancialPlatform& financial) {
    std::string selected_pool;
    for (;;) {
        const auto pools = financial.flow().snapshot().pools;
        std::cout << "\nFLOW / PERSISTENT CONSTANT-PRODUCT AMM\n";
        if (pools.empty()) std::cout << "No liquidity pools configured.\n";
        for (const auto& pool : pools) {
            std::cout << "  " << pool.id << " " << pool.asset_x << '/' << pool.asset_y
                      << " reserves=" << application::format_amount(pool.reserve_x) << '/'
                      << application::format_amount(pool.reserve_y) << " LP="
                      << application::format_amount(pool.total_lp_shares) << '\n';
        }
        std::cout << "\n  1. Create pool\n  2. Select pool\n  3. Pool status\n"
                  << "  4. Add liquidity\n  5. Remove liquidity\n  6. Quote swap\n"
                  << "  7. Execute swap\n  8. Account balances / LP position\n  0. Return\n";
        const auto choice = read_line("flow> ");
        try {
            if (choice == "1") {
                const auto asset_x = read_line("Asset X (e.g. FST): ");
                const auto asset_y = read_line("Asset Y (e.g. USD): ");
                const auto fee = read_uint("Fee (basis points, max 1000): ");
                if (fee > 1'000) throw std::invalid_argument("fee cannot exceed 1000 bps");
                selected_pool = financial.create_pool(asset_x, asset_y, static_cast<std::uint32_t>(fee));
                std::cout << "Pool created and persisted: " << selected_pool << '\n';
            } else if (choice == "2") {
                selected_pool = read_line("Pool ID: ");
                if (!financial.flow().pool(selected_pool)) throw std::invalid_argument("unknown pool");
                std::cout << "Selected " << selected_pool << '\n';
            } else if (choice == "3") {
                if (selected_pool.empty()) throw std::runtime_error("select or create a pool first");
                const auto pool = financial.flow().pool(selected_pool).value();
                std::cout << pool.id << " " << pool.asset_x << '/' << pool.asset_y << " fee="
                          << pool.fee_bps << " bps\nReserve X: " << application::format_amount(pool.reserve_x)
                          << " " << pool.asset_x << "\nReserve Y: " << application::format_amount(pool.reserve_y)
                          << " " << pool.asset_y << "\nLP shares: " << application::format_amount(pool.total_lp_shares) << '\n';
            } else if (choice == "4") {
                if (selected_pool.empty()) throw std::runtime_error("select or create a pool first");
                const auto account = read_line("Account (Ledger wallet address): ");
                const auto pool = financial.flow().pool(selected_pool).value();
                const auto x = application::parse_amount(read_line("Maximum " + pool.asset_x + " amount: "));
                const auto y = application::parse_amount(read_line("Maximum " + pool.asset_y + " amount: "));
                const auto result = financial.add_liquidity(account, selected_pool, x, y);
                std::cout << "Liquidity added; minted " << application::format_amount(result.shares) << " LP shares.\n";
            } else if (choice == "5") {
                if (selected_pool.empty()) throw std::runtime_error("select or create a pool first");
                const auto account = read_line("Account (Ledger wallet address): ");
                const auto shares = application::parse_amount(read_line("LP shares to remove: "));
                const auto result = financial.remove_liquidity(account, selected_pool, shares);
                std::cout << "Removed liquidity: " << application::format_amount(result.amount_x_used)
                          << " / " << application::format_amount(result.amount_y_used) << '\n';
            } else if (choice == "6" || choice == "7") {
                if (selected_pool.empty()) throw std::runtime_error("select or create a pool first");
                const auto pool = financial.flow().pool(selected_pool).value();
                const auto asset_in = read_line("Input asset (" + pool.asset_x + "/" + pool.asset_y + "): ");
                const auto amount_in = application::parse_amount(read_line("Input amount: "));
                const auto quote = financial.quote(selected_pool, asset_in, amount_in);
                std::cout << "Expected output: " << application::format_amount(quote.amount_out)
                          << " " << quote.asset_out << "  fee: " << application::format_amount(quote.fee_amount)
                          << "  price impact: " << quote.price_impact_bps << " bps\n";
                if (choice == "7") {
                    const auto account = read_line("Account (Ledger wallet address): ");
                    const auto minimum = application::parse_amount(read_line("Minimum output: "));
                    const auto trade = financial.swap(account, selected_pool, asset_in, amount_in, minimum);
                    std::cout << "Swap " << trade.id << " executed and persisted: "
                              << application::format_amount(trade.amount_out) << " " << trade.asset_out << '\n';
                }
            } else if (choice == "8") {
                const auto account = read_line("Account (Ledger wallet address): ");
                const auto pool = selected_pool.empty() ? std::optional<flow::Pool>{} : financial.flow().pool(selected_pool);
                std::cout << "FST available: " << application::format_amount(financial.balance(account, "FST").available())
                          << "  USD available: " << application::format_amount(financial.balance(account, "USD").available());
                if (pool) std::cout << "  LP shares: " << application::format_amount(financial.flow().lp_shares_of(account, selected_pool));
                std::cout << '\n';
            } else if (choice == "0") return;
            else std::cout << "Unknown option.\n";
        } catch (const std::exception& exception) {
            std::cout << "Flow error: " << exception.what() << '\n';
        }
    }
}

void print_bridge_status(const bridge::Status& status) {
    std::cout << "\nNATIVE FST BRIDGE STATUS\n";
    if (!status.configured) {
        std::cout << "  Not configured. Create a dedicated Ledger wallet first.\n";
        return;
    }
    std::cout << "  Vault address        : " << status.vault_address << '\n'
              << "  On-chain FST         : " << application::format_amount(status.vault_on_chain_balance) << '\n'
              << "  Off-chain liabilities: " << application::format_amount(status.off_chain_fst_liabilities) << '\n'
              << "  Pending withdrawal  : " << application::format_amount(status.pending_withdrawal_reserve) << '\n'
              << "  Other vault mempool : " << application::format_amount(status.untracked_vault_mempool_reserve) << '\n'
              << "  Required collateral  : " << application::format_amount(status.required_collateral) << '\n'
              << "  Collateral surplus   : " << application::format_amount(status.collateral_surplus) << '\n'
              << "  Collateral shortfall : " << application::format_amount(status.collateral_shortfall) << '\n'
              << "  Collateralized       : " << (status.fully_collateralized ? "YES" : "NO") << '\n'
              << "  Confirmed deposits   : " << status.deposits << '\n'
              << "  Pending withdrawals  : " << status.pending_withdrawals << '\n'
              << "  Await reconciliation : " << status.pending_awaiting_reconciliation << '\n'
              << "  Other vault mempool  : " << status.untracked_vault_mempool_transactions << " tx(s)\n"
              << "  Confirmed withdrawals: " << status.confirmed_withdrawals << '\n'
              << "  Note: local single-node chain; this is not production custody or distributed finality.\n";
}

void run_bridge(platform::FinancialPlatform& financial, Ledger& ledger) {
    bridge::BridgeService service(ledger, financial);
    const auto reconciled_on_entry = service.reconcile_confirmed_withdrawals();
    if (reconciled_on_entry != 0) {
        std::cout << "Recovered " << reconciled_on_entry << " confirmed bridge withdrawal(s).\n";
    }

    for (;;) {
        std::cout << "\n  FEINSTATIC / LEDGER-SETTLEMENT BRIDGE\n"
                  << "  1. Configure dedicated vault wallet\n"
                  << "  2. Bridge status / collateralization\n"
                  << "  3. Claim confirmed FST deposit by transaction ID\n"
                  << "  4. Request FST withdrawal\n"
                  << "  5. Retry pending withdrawals\n"
                  << "  6. Bridge history\n"
                  << "  0. Return\n";
        const auto choice = read_line("bridge> ");
        try {
            if (choice == "1") {
                const auto wallets = ledger.wallets();
                if (wallets.empty()) throw std::runtime_error("create a dedicated Ledger wallet first");
                std::cout << "Ledger wallet candidates (do not use a customer wallet as the vault):\n";
                for (const auto& [address, public_key] : wallets) {
                    (void)public_key;
                    std::cout << "  " << address << "  on-chain="
                              << application::format_amount(ledger.chain().balance_of(address)) << " FST\n";
                }
                const auto address = read_line("Dedicated bridge vault address: ");
                service.configure_vault(address);
                std::cout << "Bridge vault configured. Fund it with an on-chain FST transfer before accepting withdrawals.\n";
            } else if (choice == "2") {
                print_bridge_status(service.status());
            } else if (choice == "3") {
                const auto id = read_line("Confirmed Ledger transaction ID (64 hex chars): ");
                const auto result = service.claim_deposit(id);
                std::cout << (result.newly_credited ? "Deposit credited" : "Deposit already credited")
                          << ": " << application::format_amount(result.amount) << " FST\n"
                          << "  Sender account: " << result.account_id << '\n'
                          << "  Confirmed block: " << result.block_height << '\n'
                          << "  Transaction ID: " << result.transaction_id << '\n';
            } else if (choice == "4") {
                const auto account = read_line("Recipient Ledger wallet / settlement account: ");
                const auto amount = application::parse_amount(read_line("Withdrawal amount (FST): "));
                const auto fee = application::parse_amount(read_line("Ledger fee (FST): "));
                const auto vault = financial.bridge().vault_address;
                if (vault.empty()) throw std::runtime_error("configure a bridge vault first");
                std::string passphrase = application::read_secret("Bridge vault passphrase (input hidden): ");
                SecretWiper wipe_passphrase(passphrase);
                const Wallet vault_wallet = ledger.unlock_wallet(vault, passphrase);
                const auto result = service.request_withdrawal(account, amount, fee, vault_wallet);
                std::cout << "Withdrawal persisted: " << application::format_amount(result.amount) << " FST\n"
                          << "  Transaction ID: " << result.transaction_id << '\n';
                if (result.submitted_to_mempool) {
                    std::cout << "  Status: submitted to Ledger mempool; mine a block to confirm.\n";
                } else {
                    std::cout << "  Status: durably pending; mempool submission needs retry.\n"
                              << "  Reason: " << result.submission_error << '\n';
                }
            } else if (choice == "5") {
                const auto result = service.retry_pending_withdrawals();
                std::cout << "Retry results: submitted=" << result.submitted
                          << " already_pending=" << result.already_pending
                          << " already_confirmed=" << result.already_confirmed << '\n';
                for (const auto& error : result.errors) std::cout << "  ERROR: " << error << '\n';
            } else if (choice == "6") {
                const auto& state = financial.bridge();
                std::cout << "\nBRIDGE DEPOSITS\n";
                if (state.deposits.empty()) std::cout << "  --\n";
                for (const auto& record : state.deposits) {
                    std::cout << "  " << record.transaction_id << "  "
                              << application::format_amount(record.amount) << " FST  account="
                              << record.account_id << "  block=" << record.block_height << '\n';
                }
                std::cout << "BRIDGE WITHDRAWALS\n";
                if (state.withdrawals.empty()) std::cout << "  --\n";
                for (const auto& record : state.withdrawals) {
                    std::cout << "  " << record.transaction_id << "  "
                              << application::format_amount(record.amount) << " FST  account="
                              << record.account_id << "  status="
                              << (record.status == platform::BridgeWithdrawalStatus::Pending ? "PENDING" : "CONFIRMED")
                              << "  block=" << record.block_height << '\n';
                }
            } else if (choice == "0") {
                return;
            } else {
                std::cout << "Unknown option.\n";
            }
        } catch (const std::exception& exception) {
            std::cout << "Bridge error: " << exception.what() << '\n';
        }
    }
}

void run_financial_platform(platform::FinancialPlatform& financial, Ledger& ledger) {
    for (;;) {
        std::cout << "\n  FEINSTATIC / SHARED OFF-CHAIN SETTLEMENT\n"
                  << "  Revision: " << financial.revision() << "\n"
                  << "  Accounts: " << financial.accounts().size() << "\n"
                  << "  1. List settlement accounts and balances\n"
                  << "  2. Link an existing Ledger wallet\n"
                  << "  3. Add synthetic TEST asset credit\n"
                  << "  4. Transfer asset between platform accounts\n"
                  << "  5. Match (persistent order book)\n"
                  << "  6. Flow (persistent AMM pools)\n"
                  << "  7. Validate shared platform state\n"
                  << "  8. Ledger / Settlement bridge (native FST)\n"
                  << "  0. Return\n";
        const auto choice = read_line("platform> ");
        try {
            if (choice == "1") print_platform_accounts(financial);
            else if (choice == "2") link_ledger_wallet(financial, ledger);
            else if (choice == "3") {
                const auto account = read_line("Platform account ID: ");
                const auto asset = read_line("Asset ID (FST/USD): ");
                const auto amount = application::parse_amount(read_line("Synthetic test credit amount: "));
                const auto reason = read_line("Audit reason: ");
                financial.fund_test_asset(account, asset, amount, reason);
                std::cout << "Synthetic TEST credit persisted. This is not an on-chain deposit or issuance.\n";
            } else if (choice == "4") {
                const auto from = read_line("From account: ");
                const auto to = read_line("To account: ");
                const auto asset = read_line("Asset ID: ");
                const auto amount = application::parse_amount(read_line("Amount: "));
                financial.transfer_asset(from, to, asset, amount);
                std::cout << "Internal transfer committed.\n";
            } else if (choice == "5") run_persistent_match(financial);
            else if (choice == "6") run_persistent_flow(financial);
            else if (choice == "7") {
                std::string error;
                if (financial.validate(&error)) std::cout << "Settlement, Match and Flow state valid.\n";
                else std::cout << "Platform state invalid: " << error << '\n';
            } else if (choice == "8") run_bridge(financial, ledger);
            else if (choice == "0") return;
            else std::cout << "Unknown option.\n";
        } catch (const std::exception& exception) {
            std::cout << "Platform error: " << exception.what() << '\n';
        }
    }
}

void run_menu(Ledger& ledger, platform::FinancialPlatform& financial) {
    for (;;) {
        print_dashboard(ledger);
        std::cout << "  1. List wallets and balances\n"
                  << "  2. Create wallet\n"
                  << "  3. Submit transfer\n"
                  << "  4. Mine pending transactions\n"
                  << "  5. Validate chain\n"
                  << "  6. Exit\n"
                  << "  7. Shared financial platform (persistent Match + Flow)\n";
        const std::string choice = read_line("\nfeinstatic> ");
        try {
            if (choice == "1") print_wallets(ledger);
            else if (choice == "2") create_wallet(ledger);
            else if (choice == "3") transfer(ledger, financial);
            else if (choice == "4") mine(ledger, financial);
            else if (choice == "5") {
                std::string error;
                if (ledger.chain().validate(&error)) std::cout << "Chain valid.\n";
                else std::cout << "Chain invalid: " << error << '\n';
            }
            else if (choice == "6") return;
            else if (choice == "7") run_financial_platform(financial, ledger);
            else std::cout << "Unknown option.\n";
        } catch (const std::exception& exception) {
            std::cout << "Error: " << exception.what() << '\n';
        }
    }
}

int run_demo() {
    Wallet alice = Wallet::generate();
    Wallet bob = Wallet::generate();
    auto ledger = Ledger::initialize(":memory:",
        {{alice.address(), 1'000ULL * kAtomsPerCoin}}, 2, 2ULL * kAtomsPerCoin);
    ledger.register_wallet(alice, "demo-alice-passphrase");
    ledger.register_wallet(bob, "demo-bob-passphrase");

    const auto transfer_tx = ledger.create_transfer(alice, bob.address(),
        25ULL * kAtomsPerCoin, 1'000'000ULL);
    std::string error;
    if (!ledger.submit(transfer_tx, &error)) throw std::runtime_error("demo transfer rejected: " + error);
    const Block mined = ledger.mine(bob.address());

    std::cout << "\nFEINSTATIC / LEDGER DEMONSTRATION\n"
              << "---------------------------------------------\n"
              << "Block height : " << ledger.chain().height() << '\n'
              << "Block hash   : " << crypto::hex(mined.hash.data(), mined.hash.size()) << '\n'
              << "Alice        : " << alice.address() << '\n'
              << "Alice balance: " << application::format_amount(ledger.chain().balance_of(alice.address())) << " FST\n"
              << "Bob          : " << bob.address() << '\n'
              << "Bob balance  : " << application::format_amount(ledger.chain().balance_of(bob.address())) << " FST\n"
              << "Supply       : " << application::format_amount(ledger.chain().total_supply()) << " FST\n"
              << "Chain check  : " << (ledger.chain().validate() ? "PASS" : "FAIL") << '\n'
              << "Note: in-memory demonstration with synthetic test wallets and assets.\n";
    run_match_demo();
    run_flow_demo();
    return 0;
}

int initialize_database(const std::string& path) {
    std::string passphrase = application::read_confirmed_secret(
        "Initial wallet passphrase (input hidden): ", "Confirm initial wallet passphrase: ");
    SecretWiper wipe_passphrase(passphrase);
    Wallet initial_wallet = Wallet::generate();
    auto ledger = Ledger::initialize(path,
        {{initial_wallet.address(), 10'000ULL * kAtomsPerCoin}}, 2, kDefaultBlockReward);
    ledger.register_wallet(initial_wallet, passphrase);
    auto financial = platform::FinancialPlatform::open(path);
    std::cout << "Private test ledger initialized.\n"
              << "Initial wallet: " << initial_wallet.address() << '\n'
              << "Genesis allocation: 10000 FST\n"
              << "Database: " << path << '\n';
    run_menu(ledger, financial);
    return 0;
}

int verify_database_command(const std::string& path) {
    const auto report = application::verify_database(path);
    std::cout << "\nFEINSTATIC / AUTOMATED DATABASE VERIFICATION\n"
              << "Database: " << path << "\n\n";

    if (report.ledger_opened && report.ledger_valid) {
        std::cout << "[PASS] Ledger chain: height=" << report.chain_height
                  << " blocks=" << report.block_count
                  << " supply=" << application::format_amount(report.native_supply)
                  << " FST wallets=" << report.wallet_count
                  << " pending=" << report.pending_transactions << '\n';
    } else {
        std::cout << "[FAIL] Ledger: "
                  << (report.ledger_error.empty() ? "validation failed" : report.ledger_error) << '\n';
    }

    if (report.platform_opened && report.platform_valid) {
        std::cout << "[PASS] Shared platform: revision=" << report.platform_revision
                  << " accounts=" << report.platform_account_count
                  << " journal_entries=" << report.settlement_journal_entries << '\n'
                  << "[INFO] Match: orders=" << report.match_order_count
                  << " trades=" << report.match_trade_count << '\n'
                  << "[INFO] Flow: pools=" << report.flow_pool_count
                  << " LP_positions=" << report.flow_lp_position_count
                  << " swaps=" << report.flow_swap_count << '\n';
        if (report.bridge_configured) {
            std::cout << "[INFO] Bridge: vault=" << report.bridge_vault_address
                      << " deposits=" << report.bridge_deposit_count
                      << " withdrawals_pending=" << report.bridge_pending_withdrawal_count
                      << " withdrawals_confirmed=" << report.bridge_confirmed_withdrawal_count
                      << " awaiting_reconciliation=" << report.bridge_pending_awaiting_reconciliation_count
                      << " on_chain=" << application::format_amount(report.bridge_vault_balance)
                      << " liabilities=" << application::format_amount(report.bridge_fst_liabilities)
                      << " pending_reserve=" << application::format_amount(report.bridge_pending_withdrawal_reserve)
                      << " required=" << application::format_amount(report.bridge_required_collateral)
                      << " collateralized=" << (report.bridge_fully_collateralized ? "YES" : "NO") << '\n';
            if (!report.bridge_fully_collateralized) {
                std::cout << "[WARN] Bridge vault is below required FST collateral, including pending withdrawals. Withdrawals are blocked.\n";
            }
        } else {
            std::cout << "[INFO] Bridge: not configured\n";
        }
    } else {
        std::cout << "[FAIL] Shared platform: "
                  << (report.platform_error.empty() ? "validation not run" : report.platform_error) << '\n';
    }

    if (!report.success()) {
        std::cout << "\nRESULT: FAIL (database integrity)\n";
        return 1;
    }
    if (report.bridge_configured && !report.bridge_fully_collateralized) {
        std::cout << "\nRESULT: PASS (database integrity); BRIDGE COLLATERAL: SHORTFALL\n";
        return 0;
    }
    std::cout << "\nRESULT: PASS (database integrity and configured bridge collateral)\n";
    return 0;
}

std::uint16_t parse_port(const std::string& text) {
    unsigned int value = 0;
    const auto [end, error] = std::from_chars(text.data(), text.data() + text.size(), value);
    if (error != std::errc{} || end != text.data() + text.size() || value == 0 || value > 65535) {
        throw std::invalid_argument("port must be an integer between 1 and 65535");
    }
    return static_cast<std::uint16_t>(value);
}

volatile std::sig_atomic_t g_node_stop_requested = 0;

void request_node_stop(int) {
    g_node_stop_requested = 1;
}

std::string read_identity_passphrase(const std::string& prompt) {
    // Reuse the terminal reader that restores echo on SIGINT/SIGTERM and fails
    // closed for non-interactive stdin; do not maintain a second weaker path.
    return application::read_secret(prompt);
}

network::NodeIdentity load_node_identity(const std::string& identity_path) {
    if (!network::NodeIdentity::is_encrypted_vault_file(identity_path)) {
        return network::NodeIdentity::load(identity_path);
    }
    std::string passphrase = read_identity_passphrase("Node identity vault passphrase (input hidden): ");
    SecretWiper wipe(passphrase);
    return network::NodeIdentity::load_vault(identity_path, passphrase);
}

network::PeerAuthConfig load_peer_authentication(const std::string& identity_path,
                                                   const std::string& trust_path,
                                                   const std::string& expected_peer_id = {}) {
    network::PeerAuthConfig authentication;
    authentication.identity = std::make_shared<network::NodeIdentity>(
        load_node_identity(identity_path));
    authentication.trusted_peers = network::PeerTrustStore::load(trust_path);
    authentication.expected_peer_id = expected_peer_id;
    if (authentication.trusted_peers.active_size() == 0) {
        throw std::runtime_error("peer trust store has no active identities; explicitly trust the expected node identity first");
    }
    return authentication;
}

std::unique_ptr<network::PeerClient> make_peer_client(
    const std::string& host, std::uint16_t port,
    std::optional<network::PeerAuthConfig> authentication) {
    if (authentication) {
        return std::make_unique<network::PeerClient>(host, port, std::move(*authentication));
    }
    return std::make_unique<network::PeerClient>(host, port);
}

int node_keygen_command(const std::string& identity_path) {
    auto identity = network::NodeIdentity::generate();
    identity.save_new(identity_path);
    std::cout << "NODE IDENTITY CREATED\n"
              << "  Private key file : " << identity_path << " (mode 0600)\n"
              << "  Node ID          : " << identity.peer_id() << '\n'
              << "  Public key       : " << crypto::hex(identity.public_key().data(), identity.public_key().size()) << '\n'
              << "Keep the private identity file backed up and never share it. Share only the node ID and public key.\n";
    return 0;
}

int node_keygen_vault_command(const std::string& identity_path) {
    std::string passphrase = application::read_confirmed_secret(
        "New node identity vault passphrase (minimum 12 bytes, input hidden): ",
        "Confirm node identity vault passphrase: ");
    SecretWiper wipe(passphrase);
    auto identity = network::NodeIdentity::generate();
    identity.save_vault_new(identity_path, passphrase);
    std::cout << "ENCRYPTED NODE IDENTITY VAULT CREATED\n"
              << "  Vault file       : " << identity_path << " (mode 0600; AES-256-GCM/PBKDF2-HMAC-SHA256)\n"
              << "  Node ID          : " << identity.peer_id() << '\n'
              << "  Public key       : " << crypto::hex(identity.public_key().data(), identity.public_key().size()) << '\n'
              << "The vault passphrase is required when starting this node. Back up the vault and passphrase separately.\n";
    return 0;
}

int peer_trust_add_command(const std::string& trust_path, const std::string& peer_id,
                           const std::string& public_key_hex, network::PeerRole role) {
    const Bytes decoded = crypto::unhex(public_key_hex);
    if (decoded.size() != 32) throw std::invalid_argument("peer public key must be exactly 32 bytes (64 hex characters)");
    PublicKey public_key{};
    std::copy(decoded.begin(), decoded.end(), public_key.begin());
    if (network::peer_id_from_public_key(public_key) != peer_id) {
        throw std::invalid_argument("supplied peer ID does not match the public key");
    }
    network::PeerTrustStore store;
    if (std::filesystem::exists(trust_path)) store = network::PeerTrustStore::load(trust_path);
    store.trust(public_key, role);
    store.save_atomic(trust_path);
    std::cout << "Trusted peer stored: " << peer_id << "\nRole: "
              << (role == network::PeerRole::Operator ? "operator" : "peer")
              << "\nTrust store: " << trust_path << '\n';
    return 0;
}

int peer_trust_revoke_command(const std::string& trust_path, const std::string& peer_id) {
    auto store = network::PeerTrustStore::load(trust_path);
    store.revoke(peer_id);
    store.save_atomic(trust_path);
    std::cout << "Revoked peer identity: " << peer_id << "\nExisting sessions are not retroactively terminated; new handshakes will be rejected.\n";
    return 0;
}

int peer_trust_rotate_command(const std::string& trust_path, const std::string& old_peer_id,
                              const std::string& replacement_public_hex) {
    const Bytes decoded = crypto::unhex(replacement_public_hex);
    if (decoded.size() != 32) throw std::invalid_argument("replacement public key must be exactly 32 bytes (64 hex characters)");
    PublicKey replacement{};
    std::copy(decoded.begin(), decoded.end(), replacement.begin());
    auto store = network::PeerTrustStore::load(trust_path);
    store.rotate(old_peer_id, replacement);
    store.save_atomic(trust_path);
    std::cout << "Identity rotation recorded. Old identity revoked: " << old_peer_id
              << "\nReplacement identity: " << network::peer_id_from_public_key(replacement)
              << "\nUpdate every cluster member's trust store before restarting the rotated node.\n";
    return 0;
}

int listen_node_command(const std::string& database, const std::string& address,
                        std::uint16_t port,
                        std::optional<network::PeerAuthConfig> authentication = std::nullopt) {
    auto ledger = Ledger::open(database);
    std::unique_ptr<network::NodeServer> server;
    if (authentication) {
        server = std::make_unique<network::NodeServer>(ledger, address, port, std::move(*authentication));
    } else {
        server = std::make_unique<network::NodeServer>(ledger, address, port);
    }
    g_node_stop_requested = 0;
    const auto previous_int = std::signal(SIGINT, request_node_stop);
    const auto previous_term = std::signal(SIGTERM, request_node_stop);
    std::cout << "FeinStatic node listening on " << address << ':' << server->port() << '\n'
              << "Chain height: " << ledger.chain().height() << '\n'
              << "Protocol: " << network::kProtocolVersion << '\n'
              << "Peer authentication: " << (authentication ? "Ed25519 mutual challenge-response" : "disabled (loopback only)") << '\n'
              << "Press Ctrl+C to stop. The node processes one peer request at a time.\n";
    try {
        server->run(&g_node_stop_requested);
    } catch (...) {
        std::signal(SIGINT, previous_int);
        std::signal(SIGTERM, previous_term);
        throw;
    }
    std::signal(SIGINT, previous_int);
    std::signal(SIGTERM, previous_term);
    std::cout << "Node stopped cleanly.\n";
    return 0;
}

struct ParsedRaftMembers {
    std::vector<std::string> members;
    std::vector<std::string> learners;
    std::vector<network::RaftPeerEndpoint> remote_endpoints;
};

ParsedRaftMembers load_raft_members(const std::string& path,
                                    const std::string& local_peer_id,
                                    std::uint16_t local_port) {
    std::ifstream input(path);
    if (!input) throw std::runtime_error("cannot open Raft members file: " + path);
    ParsedRaftMembers result;
    bool local_found = false;
    std::string line;
    std::size_t line_number = 0;
    while (std::getline(input, line)) {
        ++line_number;
        const auto first = line.find_first_not_of(" \t\r");
        if (first == std::string::npos || line[first] == '#') continue;
        std::istringstream parser(line);
        std::string peer_id;
        std::string host;
        std::string port_text;
        std::string role;
        std::string extra;
        if (!(parser >> peer_id >> host >> port_text)) {
            throw std::runtime_error("invalid Raft members file row " + std::to_string(line_number) +
                                     "; expected: <peer-id> <host> <port> [voter|learner]");
        }
        if (!(parser >> role)) role = "voter";
        if (parser >> extra) {
            throw std::runtime_error("invalid Raft members file row " + std::to_string(line_number) +
                                     "; expected at most one role field");
        }
        if (role != "voter" && role != "learner") {
            throw std::runtime_error("invalid Raft member role at row " + std::to_string(line_number) +
                                     "; use voter or learner");
        }
        const auto peer_port = parse_port(port_text);
        if (role == "voter") result.members.push_back(peer_id);
        else result.learners.push_back(peer_id);
        if (peer_id == local_peer_id) {
            if (local_found) throw std::runtime_error("local node appears more than once in the Raft members file");
            if (peer_port != local_port) {
                throw std::runtime_error("local Raft members-file port does not match the listener port");
            }
            local_found = true;
        } else {
            result.remote_endpoints.push_back({std::move(peer_id), std::move(host), peer_port});
        }
    }
    if (!input.eof()) throw std::runtime_error("failed while reading Raft members file: " + path);
    if (!local_found) throw std::runtime_error("local node identity is absent from the Raft members file");
    if (result.members.size() < 3 || result.members.size() > consensus::kRaftMaximumMembers ||
        result.members.size() % 2U == 0U) {
        throw std::runtime_error("Raft voter set must contain 3, 5, 7, or 9 odd-count voters");
    }
    if (result.members.size() + result.learners.size() > consensus::kRaftMaximumMembers) {
        throw std::runtime_error("Raft pre-provisioned voter plus learner roster cannot exceed nine nodes");
    }
    std::sort(result.members.begin(), result.members.end());
    std::sort(result.learners.begin(), result.learners.end());
    std::vector<std::string> all = result.members;
    all.insert(all.end(), result.learners.begin(), result.learners.end());
    std::sort(all.begin(), all.end());
    if (std::adjacent_find(all.begin(), all.end()) != all.end()) {
        throw std::runtime_error("Raft members file contains duplicate peer IDs or voter/learner overlap");
    }
    return result;
}

int raft_node_command(const std::string& database, const std::string& address,
                      std::uint16_t port, const std::string& identity_path,
                      const std::string& trust_path, const std::string& cluster_id,
                      const std::string& members_path) {
    auto authentication = load_peer_authentication(identity_path, trust_path);
    const std::string local_peer_id = authentication.identity->peer_id();
    const ParsedRaftMembers parsed = load_raft_members(members_path, local_peer_id, port);

    consensus::RaftConfig consensus_config;
    consensus_config.node_id = local_peer_id;
    consensus_config.cluster_id = cluster_id;
    consensus_config.members = parsed.members;
    consensus_config.learners = parsed.learners;
    consensus_config.election_timeout_min = std::chrono::milliseconds(900);
    consensus_config.election_timeout_max = std::chrono::milliseconds(1600);
    consensus_config.heartbeat_interval = std::chrono::milliseconds(200);

    network::RaftNetworkConfig raft_configuration;
    raft_configuration.database_path = database;
    raft_configuration.trust_store_path = trust_path;
    raft_configuration.endpoint_registry_path = database + ".endpoints";
    raft_configuration.outbound_worker_count = 4;
    raft_configuration.maximum_queued_messages_per_peer = 128;
    raft_configuration.consensus = std::move(consensus_config);
    raft_configuration.peers = parsed.remote_endpoints;
    raft_configuration.outbound_timeout = std::chrono::milliseconds(400);

    const std::string endpoint_registry_display = raft_configuration.endpoint_registry_path;
    const std::size_t outbound_worker_count = raft_configuration.outbound_worker_count;
    auto ledger = Ledger::open(database);
    network::NodeServer server(ledger, address, port, std::move(authentication),
                               std::move(raft_configuration), std::chrono::milliseconds(150));
    g_node_stop_requested = 0;
    const auto previous_int = std::signal(SIGINT, request_node_stop);
    const auto previous_term = std::signal(SIGTERM, request_node_stop);
    std::cout << "FEINSTATIC RAFT NODE\n"
              << "  Node ID       : " << local_peer_id << '\n'
              << "  Cluster ID    : " << cluster_id << '\n'
              << "  Endpoint      : " << address << ':' << server.port() << '\n'
              << "  Voters        : " << parsed.members.size() << '\n'
              << "  Learners      : " << parsed.learners.size() << '\n'
              << "  Endpoints     : " << endpoint_registry_display << '\n'
              << "  Outbound pool : " << outbound_worker_count << " workers\n"
              << "  Ledger height : " << ledger.chain().height() << '\n'
              << "  Checkpoint    : " << crypto::hex(ledger.chain().blocks().back().hash.data(),
                                                        ledger.chain().blocks().back().hash.size()) << '\n'
              << "  Peer security : Ed25519 authenticated; X25519/AES-256-GCM encrypted sessions\n"
              << "  Consensus     : joint-consensus crash-fault Raft; not Byzantine tolerant\n"
              << "Press Ctrl+C to stop. Direct block submission and peer chain-sync are disabled in this mode.\n";
    try {
        server.run(&g_node_stop_requested);
    } catch (...) {
        std::signal(SIGINT, previous_int);
        std::signal(SIGTERM, previous_term);
        throw;
    }
    std::signal(SIGINT, previous_int);
    std::signal(SIGTERM, previous_term);
    std::cout << "Raft node stopped cleanly.\n";
    return 0;
}

int raft_change_membership_command(const std::string& database, const std::string& host,
                                   std::uint16_t port, network::PeerAuthConfig authentication,
                                   const std::string& members_csv) {
    std::vector<std::string> members;
    std::istringstream parser(members_csv);
    std::string member;
    while (std::getline(parser, member, ',')) {
        if (member.empty()) throw std::invalid_argument("membership list contains an empty peer ID");
        members.push_back(member);
    }
    if (members.size() < 3 || members.size() > consensus::kRaftMaximumMembers ||
        members.size() % 2U == 0U) {
        throw std::invalid_argument("new voter set must contain 3, 5, 7, or 9 peer IDs");
    }
    auto ledger = Ledger::open(database);
    network::PeerClient peer(host, port, std::move(authentication));
    const auto result = peer.change_raft_membership(ledger, members);
    std::cout << "RAFT JOINT-CONSENSUS CHANGE PROPOSED\n"
              << "  Term                 : " << result.term << '\n'
              << "  Configuration index : " << result.log_index << '\n'
              << "  Commit index at reply: " << result.commit_index << '\n'
              << "  Voters               : ";
    for (std::size_t i = 0; i < members.size(); ++i) {
        if (i != 0) std::cout << ", ";
        std::cout << members[i];
    }
    std::cout << "\nThe new configuration is not committed merely because this request was accepted. "
                 "Check raft-status-auth on the cluster members until commit/applied indices advance.\n";
    return 0;
}

int raft_status_command(const std::string& database, const std::string& host,
                        std::uint16_t port, network::PeerAuthConfig authentication) {
    auto ledger = Ledger::open(database);
    network::PeerClient peer(host, port, std::move(authentication));
    const auto status = peer.raft_status(ledger);
    const char* role = status.role == consensus::RaftRole::Leader ? "leader" :
        status.role == consensus::RaftRole::Candidate ? "candidate" : "follower";
    std::cout << "RAFT NODE STATUS\n"
              << "  Node ID       : " << status.node_id << '\n'
              << "  Role          : " << role << '\n'
              << "  Leader        : " << (status.leader_id.empty() ? "unknown" : status.leader_id) << '\n'
              << "  Term          : " << status.current_term << '\n'
              << "  Commit index  : " << status.commit_index << '\n'
              << "  Applied index : " << status.last_applied << '\n'
              << "  Log index     : " << status.last_log_index << '\n'
              << "  Ledger height : " << status.ledger_height << '\n'
              << "  Ledger tip    : " << crypto::hex(status.ledger_tip_hash.data(), status.ledger_tip_hash.size()) << '\n'
              << "  Membership    : " << (status.membership_phase == consensus::MembershipPhase::Stable ? "stable" :
                    status.membership_phase == consensus::MembershipPhase::JointPending ? "joint-pending" :
                    status.membership_phase == consensus::MembershipPhase::JointCommitted ? "joint-committed" : "final-pending") << '\n'
              << "  Voters        : ";
    for (std::size_t i = 0; i < status.voters.size(); ++i) std::cout << (i ? ", " : "") << status.voters[i];
    std::cout << "\n  Learners      : ";
    for (std::size_t i = 0; i < status.learners.size(); ++i) std::cout << (i ? ", " : "") << status.learners[i];
    if (!status.joint_voters.empty()) {
        std::cout << "\n  Joint voters  : ";
        for (std::size_t i = 0; i < status.joint_voters.size(); ++i) std::cout << (i ? ", " : "") << status.joint_voters[i];
    }
    std::cout << '\n';
    return 0;
}

int raft_propose_block_command(const std::string& database, const std::string& host,
                               std::uint16_t port, network::PeerAuthConfig authentication,
                               const Address& miner) {
    auto ledger = Ledger::open(database);
    network::PeerClient peer(host, port, std::move(authentication));
    const auto proposal = peer.propose_raft_block(ledger, miner);
    std::cout << "RAFT LEDGER BLOCK PROPOSED\n"
              << "  Log index : " << proposal.index << '\n'
              << "  Term      : " << proposal.term << '\n'
              << "  Block hash: " << crypto::hex(proposal.block_hash.data(), proposal.block_hash.size()) << '\n'
              << "Proposal accepted by the addressed node; wait for majority commitment and application.\n";
    return 0;
}

int peer_status_command(const std::string& database, const std::string& host,
                        std::uint16_t port,
                        std::optional<network::PeerAuthConfig> authentication = std::nullopt) {
    auto ledger = Ledger::open(database);
    auto peer = make_peer_client(host, port, std::move(authentication));
    const auto status = peer->status(ledger);
    std::cout << "PEER STATUS\n"
              << "  Endpoint : " << host << ':' << port << '\n'
              << "  Height   : " << status.height << '\n'
              << "  Tip hash : " << crypto::hex(status.tip_hash.data(), status.tip_hash.size()) << '\n'
              << "  Mempool  : " << status.pending_transactions << " transaction(s)\n";
    return 0;
}

int peer_sync_command(const std::string& database, const std::string& host,
                      std::uint16_t port,
                      std::optional<network::PeerAuthConfig> authentication = std::nullopt) {
    auto ledger = Ledger::open(database);
    auto financial = platform::FinancialPlatform::open(database);
    const auto protected_transactions = financial.bridge_transaction_references();
    auto peer = make_peer_client(host, port, std::move(authentication));
    const auto result = peer->synchronize(ledger, 4096, protected_transactions);
    std::cout << "PEER SYNCHRONIZATION\n"
              << "  Candidate blocks: " << result.blocks_received << '\n'
              << "  Final height    : " << result.final_height << '\n'
              << "  Peer height     : " << result.peer_height << '\n'
              << "  Common ancestor : " << result.common_ancestor_height << '\n'
              << "  Chain adopted   : " << (result.chain_adopted ? "YES" : "NO") << '\n'
              << "  Reorganized     : " << (result.chain_reorganized ? "YES" : "NO") << '\n'
              << "  Requeued txs    : " << result.requeued_transactions << '\n'
              << "  Dropped txs     : " << result.dropped_detached_transactions << '\n'
              << "  State           : " << (result.already_current ? "already current / peer is a prefix" :
                    (result.local_chain_preferred ? "local chain preferred" : "peer chain evaluated")) << '\n';
    const auto verification = application::verify_database(database);
    if (!verification.success()) throw std::runtime_error("database verification failed after peer synchronization");
    std::cout << "  Local database  : PASS\n";
    return 0;
}

int peer_submit_transaction_command(const std::string& database, const std::string& host,
                                    std::uint16_t port, const Address& sender_address,
                                    const Address& recipient, Amount amount, Amount fee,
                                    std::optional<network::PeerAuthConfig> authentication = std::nullopt) {
    auto ledger = Ledger::open(database);
    std::string passphrase = application::read_secret("Sender wallet passphrase (input hidden): ");
    SecretWiper wipe_passphrase(passphrase);
    Wallet sender = ledger.unlock_wallet(sender_address, passphrase);
    const auto transaction = ledger.create_transfer(sender, recipient, amount, fee);
    std::string error;
    if (!ledger.submit(transaction, &error)) throw std::runtime_error("local transaction rejected: " + error);
    const auto id = transaction.id();
    const std::string transaction_id = crypto::hex(id.data(), id.size());
    std::cout << "Accepted locally. Transaction ID: " << transaction_id << '\n';
    try {
        auto peer = make_peer_client(host, port, std::move(authentication));
        if (!peer->submit_transaction(ledger, transaction, &error)) {
            throw std::runtime_error("peer rejected transaction: " + error);
        }
    } catch (const std::exception&) {
        std::cerr << "Peer relay failed; transaction remains in the local mempool: " << transaction_id << '\n';
        throw;
    }
    std::cout << "Peer accepted transaction into its mempool.\n";
    return 0;
}

int peer_relay_tip_command(const std::string& database, const std::string& host,
                           std::uint16_t port,
                           std::optional<network::PeerAuthConfig> authentication = std::nullopt) {
    auto ledger = Ledger::open(database);
    if (ledger.chain().height() == 0) throw std::runtime_error("genesis cannot be relayed as a new block");
    auto peer = make_peer_client(host, port, std::move(authentication));
    peer->submit_block(ledger, ledger.chain().blocks().back());
    std::cout << "Local tip block accepted by peer: height=" << ledger.chain().height() << '\n';
    return 0;
}

std::uint64_t parse_u64(const std::string& text, const char* field_name) {
    std::uint64_t value = 0;
    const auto [end, error] = std::from_chars(text.data(), text.data() + text.size(), value);
    if (error != std::errc{} || end != text.data() + text.size() || value == 0) {
        throw std::invalid_argument(std::string(field_name) + " must be a positive unsigned integer");
    }
    return value;
}

int raft_trust_policy_push_command(const std::string& database, const std::string& host,
                                   std::uint16_t port, const std::string& identity_path,
                                   const std::string& trust_path, const std::string& expected_peer_id,
                                   const std::string& cluster_id, std::uint64_t epoch) {
    auto authentication = load_peer_authentication(identity_path, trust_path, expected_peer_id);
    auto policy_store = network::PeerTrustStore::load(trust_path);
    const std::string signer_id = authentication.identity->peer_id();
    if (!policy_store.is_operator(signer_id)) {
        throw std::runtime_error("local identity must be an active operator in the source trust store");
    }
    const Bytes bundle = policy_store.create_signed_policy(cluster_id, epoch, *authentication.identity);
    auto ledger = Ledger::open(database);
    network::PeerClient peer(host, port, authentication);
    const auto result = peer.push_trust_policy(ledger, bundle);
    // Advance the source trust-store epoch only after the remote node confirms
    // durable acceptance. This keeps future policy epochs monotonic locally.
    policy_store.apply_signed_policy(bundle, cluster_id, signer_id);
    policy_store.save_atomic(trust_path);
    std::cout << "SIGNED TRUST POLICY INSTALLED ON REMOTE RAFT NODE\n"
              << "  Cluster ID    : " << cluster_id << '\n'
              << "  Policy epoch  : " << result.policy_epoch << '\n'
              << "  Active peers  : " << result.active_peers << '\n'
              << "  Total entries : " << result.total_entries << '\n'
              << "  Source trust store epoch advanced and persisted.\n"
              << "Policy updates do not terminate already-established peer sessions.\n";
    return 0;
}

int raft_peer_health_command(const std::string& database, const std::string& host,
                             std::uint16_t port, network::PeerAuthConfig authentication) {
    auto ledger = Ledger::open(database);
    network::PeerClient peer(host, port, std::move(authentication));
    const auto report = peer.raft_peer_health(ledger);
    std::cout << "RAFT PEER HEALTH\n"
              << "  Endpoint registry generation: " << report.endpoint_registry_generation << '\n'
              << "  Outbound workers             : " << report.worker_count << '\n'
              << "  Total dropped messages       : " << report.outbound_dropped_total << '\n';
    for (const auto& item : report.peers) {
        std::cout << "\n  Peer       : " << item.peer_id << '\n'
                  << "  Endpoint   : " << item.host << ':' << item.port << '\n'
                  << "  Enabled    : " << (item.enabled ? "yes" : "no") << '\n'
                  << "  In flight  : " << (item.in_flight ? "yes" : "no") << '\n'
                  << "  Queue      : " << item.queued_messages << '\n'
                  << "  Attempts   : " << item.attempts << '\n'
                  << "  Successes  : " << item.successes << '\n'
                  << "  Failures   : " << item.failures << '\n'
                  << "  Dropped    : " << item.dropped << '\n'
                  << "  Consecutive failures: " << item.consecutive_failures << '\n'
                  << "  Retry delay: " << item.retry_delay_ms << " ms\n";
        if (!item.last_error.empty()) std::cout << "  Last error : " << item.last_error << '\n';
    }
    return 0;
}

int raft_peer_endpoint_set_command(const std::string& database, const std::string& host,
                                   std::uint16_t port, network::PeerAuthConfig authentication,
                                   const std::string& target_peer_id, const std::string& endpoint_host,
                                   std::uint16_t endpoint_port, bool enabled) {
    auto ledger = Ledger::open(database);
    network::PeerClient peer(host, port, std::move(authentication));
    const auto result = peer.update_raft_peer_endpoint(ledger, target_peer_id,
                                                        endpoint_host, endpoint_port, enabled);
    std::cout << "RAFT PEER ENDPOINT UPDATED\n"
              << "  Peer ID       : " << result.peer_id << '\n'
              << "  New endpoint  : " << result.host << ':' << result.port << '\n'
              << "  Enabled       : " << (result.enabled ? "yes" : "no") << '\n'
              << "  Registry epoch: " << result.endpoint_registry_generation << '\n'
              << "Endpoint update is durable. This does not change Raft membership or trust policy.\n";
    return 0;
}

void print_usage(const char* executable) {
    std::cout << "FeinStatic 1.7.0 — bounded inbound admission and transaction gossip\n\n"
              << "Usage:\n"
              << "  " << executable << " demo                  Run synthetic Ledger/Match/Flow demonstrations\n"
              << "  " << executable << " init [database.db]    Create a new private ledger\n"
              << "  " << executable << " open [database.db]    Open an existing private ledger\n"
              << "  " << executable << " verify database.db    Validate Ledger and platform without menus\n"
              << "  " << executable << " node-keygen identity.key  Generate a raw Ed25519 node identity (legacy)\n"
              << "  " << executable << " node-keygen-vault identity.vault  Generate a passphrase-encrypted node identity\n"
              << "  " << executable << " peer-trust-add trust.txt peer-id public-key-hex [peer|operator]\n"
              << "  " << executable << " peer-trust-revoke trust.txt peer-id\n"
              << "  " << executable << " peer-trust-rotate trust.txt old-peer-id replacement-public-key-hex\n"
              << "  " << executable << " node-listen db 127.0.0.1 port  Unauthenticated loopback-only listener\n"
              << "  " << executable << " node-listen-auth db address port identity.key trust.txt\n"
              << "  " << executable << " raft-node db address port identity.key trust.txt cluster-id members.txt (roles: voter/learner)\n"
              << "  " << executable << " peer-status-auth db host port identity.key trust.txt expected-peer-id\n"
              << "  " << executable << " peer-sync-auth db host port identity.key trust.txt expected-peer-id\n"
              << "  " << executable << " peer-submit-tx-auth db host port identity.key trust.txt expected-peer-id sender recipient amount fee\n"
              << "  " << executable << " peer-relay-tip-auth db host port identity.key trust.txt expected-peer-id\n"
              << "  " << executable << " raft-status-auth db host port identity.key trust.txt expected-peer-id\n"
              << "  " << executable << " raft-propose-block-auth db host port identity.key trust.txt expected-peer-id miner-address\n"
              << "  " << executable << " raft-change-members-auth db host port identity.key trust.txt expected-peer-id voter-id-1,voter-id-2,voter-id-3\n"
              << "  " << executable << " raft-trust-policy-push db host port identity.key trust.txt expected-peer-id cluster-id epoch\n"
              << "  " << executable << " raft-peer-health-auth db host port identity.key trust.txt expected-peer-id\n"
              << "  " << executable << " raft-peer-endpoint-set-auth db host port identity.key trust.txt expected-peer-id target-peer-id endpoint-host endpoint-port enabled|disabled\n"
              << "  " << executable << " --help                Show this help\n\n"
              << "Raft peers require the same Ledger checkpoint and cluster ID. Pre-provision voters and learners with the same member roster and mutual trust configuration; roles use the optional fourth field in members.txt.\n"
              << "Raft is crash-fault tolerant, not Byzantine fault tolerant. Protocol v8 encrypts application frames but is not TLS.\n"
              << "Protocol v8 adds bounded inbound admission and automatic Raft transaction gossip to X25519/AES-256-GCM sessions.\n"
              << "v1.7.0 uses separate bounded Raft/gossip queues, consensus-priority fair dispatch, retry requeue, and durable-mempool gossip recovery.\n"
              << "Endpoint disable pauses transport to a configured member; it does not remove the member from Raft quorum or membership.\n"
              << "Node identity vaults use PBKDF2-HMAC-SHA256 and AES-256-GCM; legacy raw 0600 identities remain readable.\n";
}

} // namespace

int main(int argc, char** argv) {
    try {
        const std::string command = argc > 1 ? argv[1] : "--help";
        if (command == "demo") return run_demo();
        if (command == "match-demo") { run_match_demo(); return 0; }
        if (command == "flow-demo") { run_flow_demo(); return 0; }
        if (command == "init") {
            const std::string path = argc > 2 ? argv[2] : "feinstatic.db";
            if (std::filesystem::exists(path) && std::filesystem::file_size(path) > 0) {
                throw std::runtime_error("database path already exists; choose another path");
            }
            return initialize_database(path);
        }
        if (command == "verify") {
            if (argc != 3) throw std::invalid_argument("verify requires exactly one existing database path");
            return verify_database_command(argv[2]);
        }
        if (command == "node-keygen") {
            if (argc != 3) throw std::invalid_argument("node-keygen requires a private identity file path");
            return node_keygen_command(argv[2]);
        }
        if (command == "node-keygen-vault") {
            if (argc != 3) throw std::invalid_argument("node-keygen-vault requires a vault file path");
            return node_keygen_vault_command(argv[2]);
        }
        if (command == "peer-trust-add") {
            if (argc != 5 && argc != 6) throw std::invalid_argument("peer-trust-add requires trust file, peer ID, public key hex, and optional role");
            network::PeerRole role = network::PeerRole::Peer;
            if (argc == 6) {
                const std::string role_text = argv[5];
                if (role_text == "operator") role = network::PeerRole::Operator;
                else if (role_text != "peer") throw std::invalid_argument("peer role must be peer or operator");
            }
            return peer_trust_add_command(argv[2], argv[3], argv[4], role);
        }
        if (command == "peer-trust-revoke") {
            if (argc != 4) throw std::invalid_argument("peer-trust-revoke requires trust file and peer ID");
            return peer_trust_revoke_command(argv[2], argv[3]);
        }
        if (command == "peer-trust-rotate") {
            if (argc != 5) throw std::invalid_argument("peer-trust-rotate requires trust file, old peer ID, and replacement public key hex");
            return peer_trust_rotate_command(argv[2], argv[3], argv[4]);
        }
        if (command == "node-listen-auth") {
            if (argc != 7) throw std::invalid_argument("node-listen-auth requires database, bind address, port, identity file, and trust file");
            auto auth = load_peer_authentication(argv[5], argv[6]);
            return listen_node_command(argv[2], argv[3], parse_port(argv[4]), std::move(auth));
        }
        if (command == "raft-node") {
            if (argc != 9) throw std::invalid_argument("raft-node requires database, bind address, port, identity file, trust file, cluster ID, and members file");
            return raft_node_command(argv[2], argv[3], parse_port(argv[4]), argv[5], argv[6], argv[7], argv[8]);
        }
        if (command == "raft-status-auth") {
            if (argc != 8) throw std::invalid_argument("raft-status-auth requires database, host, port, identity file, trust file, and expected peer ID");
            return raft_status_command(argv[2], argv[3], parse_port(argv[4]),
                                       load_peer_authentication(argv[5], argv[6], argv[7]));
        }
        if (command == "raft-propose-block-auth") {
            if (argc != 9) throw std::invalid_argument("raft-propose-block-auth requires database, host, port, identity file, trust file, expected peer ID, and miner address");
            return raft_propose_block_command(argv[2], argv[3], parse_port(argv[4]),
                load_peer_authentication(argv[5], argv[6], argv[7]), argv[8]);
        }
        if (command == "raft-change-members-auth") {
            if (argc != 9) throw std::invalid_argument("raft-change-members-auth requires database, host, port, identity file, trust file, expected peer ID, and comma-separated voter IDs");
            return raft_change_membership_command(argv[2], argv[3], parse_port(argv[4]),
                load_peer_authentication(argv[5], argv[6], argv[7]), argv[8]);
        }
        if (command == "raft-trust-policy-push") {
            if (argc != 10) throw std::invalid_argument("raft-trust-policy-push requires database, host, port, identity file, trust file, expected peer ID, cluster ID, and policy epoch");
            return raft_trust_policy_push_command(argv[2], argv[3], parse_port(argv[4]),
                argv[5], argv[6], argv[7], argv[8], parse_u64(argv[9], "policy epoch"));
        }
        if (command == "raft-peer-health-auth") {
            if (argc != 8) throw std::invalid_argument("raft-peer-health-auth requires database, host, port, identity file, trust file, and expected peer ID");
            return raft_peer_health_command(argv[2], argv[3], parse_port(argv[4]),
                load_peer_authentication(argv[5], argv[6], argv[7]));
        }
        if (command == "raft-peer-endpoint-set-auth") {
            if (argc != 12) throw std::invalid_argument("raft-peer-endpoint-set-auth requires database, host, port, identity file, trust file, expected peer ID, target peer ID, endpoint host, endpoint port, and enabled|disabled");
            const std::string enabled_text = argv[11];
            if (enabled_text != "enabled" && enabled_text != "disabled") {
                throw std::invalid_argument("endpoint state must be enabled or disabled");
            }
            return raft_peer_endpoint_set_command(argv[2], argv[3], parse_port(argv[4]),
                load_peer_authentication(argv[5], argv[6], argv[7]), argv[8], argv[9],
                parse_port(argv[10]), enabled_text == "enabled");
        }
        if (command == "peer-status-auth" || command == "peer-sync-auth" || command == "peer-relay-tip-auth") {
            if (argc != 8) throw std::invalid_argument(command + " requires database, host, port, identity file, trust file, and expected peer ID");
            auto auth = load_peer_authentication(argv[5], argv[6], argv[7]);
            if (command == "peer-status-auth") return peer_status_command(argv[2], argv[3], parse_port(argv[4]), std::move(auth));
            if (command == "peer-sync-auth") return peer_sync_command(argv[2], argv[3], parse_port(argv[4]), std::move(auth));
            return peer_relay_tip_command(argv[2], argv[3], parse_port(argv[4]), std::move(auth));
        }
        if (command == "peer-submit-tx-auth") {
            if (argc != 12) throw std::invalid_argument("peer-submit-tx-auth requires database, host, port, identity file, trust file, expected peer ID, sender, recipient, amount, and fee");
            auto auth = load_peer_authentication(argv[5], argv[6], argv[7]);
            return peer_submit_transaction_command(argv[2], argv[3], parse_port(argv[4]), argv[8], argv[9],
                application::parse_amount(argv[10]), application::parse_amount(argv[11]), std::move(auth));
        }
        if (command == "node-listen") {
            if (argc != 5) throw std::invalid_argument("node-listen requires database, bind address, and port");
            return listen_node_command(argv[2], argv[3], parse_port(argv[4]));
        }
        if (command == "peer-status") {
            if (argc != 5) throw std::invalid_argument("peer-status requires database, host, and port");
            return peer_status_command(argv[2], argv[3], parse_port(argv[4]));
        }
        if (command == "peer-sync") {
            if (argc != 5) throw std::invalid_argument("peer-sync requires database, host, and port");
            return peer_sync_command(argv[2], argv[3], parse_port(argv[4]));
        }
        if (command == "peer-submit-tx") {
            if (argc != 9) throw std::invalid_argument("peer-submit-tx requires database, host, port, sender, recipient, amount, and fee");
            return peer_submit_transaction_command(argv[2], argv[3], parse_port(argv[4]), argv[5], argv[6],
                application::parse_amount(argv[7]), application::parse_amount(argv[8]));
        }
        if (command == "peer-relay-tip") {
            if (argc != 5) throw std::invalid_argument("peer-relay-tip requires database, host, and port");
            return peer_relay_tip_command(argv[2], argv[3], parse_port(argv[4]));
        }
        if (command == "open") {
            const std::string path = argc > 2 ? argv[2] : "feinstatic.db";
            auto ledger = Ledger::open(path);
            auto financial = platform::FinancialPlatform::open(path);
            run_menu(ledger, financial);
            return 0;
        }
        if (command == "--help" || command == "-h") {
            print_usage(argv[0]);
            return 0;
        }
        print_usage(argv[0]);
        return 2;
    } catch (const std::exception& exception) {
        std::cerr << "FeinStatic error: " << exception.what() << '\n';
        return 1;
    }
}
