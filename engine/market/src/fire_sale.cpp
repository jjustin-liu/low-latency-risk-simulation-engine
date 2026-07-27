#include "risksim/market/fire_sale.hpp"

#include <algorithm>
#include <stdexcept>

#include "risksim/lob/order_book.hpp"

namespace risksim::market {

using lob::OrderBook;

FireSaleModel::FireSaleModel(FireSaleConfig cfg, std::size_t n_banks, std::size_t n_assets,
                             std::span<const std::int64_t> holdings,
                             std::span<const std::int64_t> price0,
                             std::span<const std::int64_t> equity0, const ExposureNetwork* net)
    : cfg_(cfg),
      b_(n_banks),
      m_(n_assets),
      holdings0_(holdings.begin(), holdings.end()),
      price0_(price0.begin(), price0.end()),
      equity0_(equity0.begin(), equity0.end()),
      net_(net) {
    if (holdings.size() != b_ * m_ || price0.size() != m_ || equity0.size() != b_)
        throw std::invalid_argument("FireSaleModel: dimension mismatch");
    if (cfg_.counterparty_coupling && (net_ == nullptr || net_->n != b_))
        throw std::invalid_argument("FireSaleModel: counterparty coupling needs a matching network");
}

FireSaleResult FireSaleModel::run(std::span<const double> shock) const {
    const std::size_t B = b_;
    const std::size_t M = m_;

    std::vector<std::int64_t> holdings = holdings0_;      // B*M lots, mutated
    std::vector<std::int64_t> price(price0_.begin(), price0_.end());  // current mark per asset
    std::vector<std::int64_t> liabilities(B, 0);
    std::vector<double> target_lev(B, 0.0);
    std::vector<char> defaulted(B, 0);

    // Seed one order book per asset with a downward bid ladder = the liquidity.
    std::vector<OrderBook> books;
    books.reserve(M);
    for (std::size_t mm = 0; mm < M; ++mm) {
        const auto top = price0_[mm];
        const std::size_t num_levels = static_cast<std::size_t>(top) + 4;
        const std::size_t max_orders = static_cast<std::size_t>(cfg_.liquidity.depth) + 8;
        books.emplace_back(num_levels, max_orders);
        std::vector<lob::Fill> f;
        for (int d = 0; d < cfg_.liquidity.depth; ++d) {
            const auto tick = top - static_cast<std::int64_t>(d) * cfg_.liquidity.tick_gap;
            if (tick <= 0) break;
            books.back().add_limit(lob::Side::Buy, static_cast<lob::Price>(tick),
                                   cfg_.liquidity.lots_per_level, f);
        }
    }

    // Initial balance sheets: all wealth is in tradable holdings, so
    // liabilities = holdings_value0 - equity0. The initial shock is an exogenous
    // asset write-down, applied as extra liabilities.
    for (std::size_t i = 0; i < B; ++i) {
        std::int64_t hv0 = 0;
        for (std::size_t mm = 0; mm < M; ++mm) hv0 += holdings0_[i * M + mm] * price0_[mm];
        if (equity0_[i] <= 0) throw std::invalid_argument("FireSaleModel: equity0 must be > 0");
        liabilities[i] = hv0 - equity0_[i];
        target_lev[i] = static_cast<double>(hv0) / static_cast<double>(equity0_[i]);
        const double s = i < shock.size() ? shock[i] : 0.0;
        liabilities[i] += static_cast<std::int64_t>(s * static_cast<double>(hv0));
    }

    auto holdings_value = [&](std::size_t i) {
        std::int64_t v = 0;
        for (std::size_t mm = 0; mm < M; ++mm) v += holdings[i * M + mm] * price[mm];
        return v;
    };
    auto remark = [&](std::size_t mm) {
        const auto bb = books[mm].best_bid();
        price[mm] = bb.has_value() ? static_cast<std::int64_t>(*bb) : 0;  // no bids => price collapses
    };
    // Sell `value` worth of bank i's holdings pro-rata across assets; returns proceeds.
    auto sell_value = [&](std::size_t i, std::int64_t value) {
        const std::int64_t hv = holdings_value(i);
        if (hv <= 0 || value <= 0) return std::int64_t{0};
        std::int64_t proceeds = 0;
        std::vector<lob::Fill> f;
        for (std::size_t mm = 0; mm < M; ++mm) {
            const std::int64_t h = holdings[i * M + mm];
            if (h <= 0) continue;
            // lots to sell so the marked value sold ~= value * (this asset's share).
            std::int64_t lots = static_cast<std::int64_t>(
                (static_cast<double>(value) * static_cast<double>(h)) / static_cast<double>(hv));
            lots = std::min(lots, h);
            if (lots <= 0) continue;
            f.clear();
            const std::int64_t unfilled = books[mm].add_market(lob::Side::Sell, lots, f);
            const std::int64_t sold = lots - unfilled;
            for (const auto& fl : f) proceeds += fl.qty * static_cast<std::int64_t>(fl.price);
            holdings[i * M + mm] -= sold;
            remark(mm);
        }
        return proceeds;
    };

    FireSaleResult r;
    r.node_loss.assign(B, 0.0);
    r.defaulted.assign(B, 0);
    r.price_impact.assign(M, 0.0);

    for (int round = 0; round < cfg_.max_rounds; ++round) {
        bool changed = false;
        for (std::size_t i = 0; i < B; ++i) {
            if (defaulted[i]) continue;
            const std::int64_t assets = holdings_value(i);
            const std::int64_t equity = assets - liabilities[i];

            if (equity <= 0) {
                // Insolvent: liquidate everything.
                std::int64_t all_value = 0;
                for (std::size_t mm = 0; mm < M; ++mm) all_value += holdings[i * M + mm] * price[mm];
                const std::int64_t proceeds = sell_value(i, all_value);
                const std::int64_t residual = liabilities[i] - proceeds;  // shortfall to creditors
                liabilities[i] -= proceeds;
                defaulted[i] = 1;
                ++r.num_defaults;
                changed = true;
                if (cfg_.counterparty_coupling && residual > 0 && net_ != nullptr) {
                    const double pbar = net_->total_liabilities(i);
                    if (pbar > 0.0) {
                        for (std::size_t j = 0; j < B; ++j) {
                            if (defaulted[j]) continue;
                            const double exposure = net_->liabilities(i, j);  // i owes j
                            if (exposure <= 0.0) continue;
                            const double loss = cfg_.lgd * static_cast<double>(residual) *
                                                (exposure / pbar);
                            liabilities[j] += static_cast<std::int64_t>(loss);
                        }
                    }
                }
                continue;
            }

            const double lev = static_cast<double>(assets) / static_cast<double>(equity);
            if (lev > target_lev[i] + 1e-9) {
                // Deleverage: raise `value` by selling, repay that much debt.
                const std::int64_t value =
                    assets - static_cast<std::int64_t>(target_lev[i] * static_cast<double>(equity));
                if (value > 0) {
                    const std::int64_t proceeds = sell_value(i, value);
                    if (proceeds > 0) {
                        liabilities[i] -= proceeds;
                        changed = true;
                    }
                }
            }
        }
        r.rounds = round + 1;
        if (!changed) break;
    }

    for (std::size_t mm = 0; mm < M; ++mm)
        r.price_impact[mm] =
            static_cast<double>(price0_[mm] - price[mm]) / static_cast<double>(price0_[mm]);
    for (std::size_t i = 0; i < B; ++i) {
        const std::int64_t final_equity = holdings_value(i) - liabilities[i];
        r.node_loss[i] = static_cast<double>(equity0_[i] - final_equity);
        r.defaulted[i] = defaulted[i];
        r.system_loss += r.node_loss[i];
    }
    return r;
}

}  // namespace risksim::market
