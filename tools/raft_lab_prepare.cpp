#include "feinstat/crypto/crypto.hpp"
#include "feinstat/ledger/ledger.hpp"
#include "feinstat/network/peer_identity.hpp"
#include "feinstat/platform/financial_platform.hpp"

#include <filesystem>
#include <iostream>
#include <memory>
#include <stdexcept>
#include <string>
#include <vector>

using namespace feinstat;

int main(int argc, char** argv) {
    try {
        if (argc != 2) throw std::invalid_argument("usage: feinstatic_raft_lab_prepare <workspace>");
        const std::filesystem::path root = std::filesystem::absolute(argv[1]);
        std::filesystem::create_directories(root);
        const auto base = root / "base.db";
        if (std::filesystem::exists(base)) throw std::runtime_error("workspace already contains base.db; use a fresh workspace");

        const auto wallet = ledger::Wallet::generate();
        {
            auto database = ledger::Ledger::initialize(
                base.string(), {{wallet.address(), 10'000ULL * kAtomsPerCoin}}, 0, kAtomsPerCoin);
            auto platform = platform::FinancialPlatform::open(base.string());
        }
        std::cout << "MINER_ADDRESS=" << wallet.address() << '\n';

        std::vector<std::shared_ptr<network::NodeIdentity>> members;
        for (int i = 0; i < 4; ++i) {
            auto identity = std::make_shared<network::NodeIdentity>(network::NodeIdentity::generate());
            identity->save_new((root / ("node-" + std::to_string(i) + ".identity")).string());
            members.push_back(identity);
            std::cout << "NODE_" << i << "_ID=" << identity->peer_id() << '\n';
            std::cout << "NODE_" << i << "_PUBLIC=" << crypto::hex(identity->public_key().data(), identity->public_key().size()) << '\n';
        }
        auto observer = std::make_shared<network::NodeIdentity>(network::NodeIdentity::generate());
        observer->save_new((root / "observer.identity").string());
        std::cout << "OBSERVER_ID=" << observer->peer_id() << '\n';

        network::PeerTrustStore observer_trust;
        for (const auto& member : members) observer_trust.trust(member->public_key());
        observer_trust.save_atomic((root / "observer.trust").string());

        for (int i = 0; i < 4; ++i) {
            network::PeerTrustStore trust;
            for (int j = 0; j < 4; ++j) if (i != j) trust.trust(members[static_cast<std::size_t>(j)]->public_key());
            trust.trust(observer->public_key(), network::PeerRole::Operator);
            trust.save_atomic((root / ("node-" + std::to_string(i) + ".trust")).string());
            std::filesystem::copy_file(base, root / ("node-" + std::to_string(i) + ".db"));
        }
        std::filesystem::copy_file(base, root / "observer.db");
        std::cout << "PREPARED=" << root.string() << '\n';
        return 0;
    } catch (const std::exception& error) {
        std::cerr << "RAFT LAB PREPARATION FAILED: " << error.what() << '\n';
        return 1;
    }
}
