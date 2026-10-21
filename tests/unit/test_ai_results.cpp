// P069: STRATIA results go into the catalogue per frame and come back as they went in.

#include "hal_contract.hpp"
#include "test_support.hpp"

#include <cloudscope/ai/result_log.hpp>
#include <cloudscope/common/clock.hpp>
#include <cloudscope/session/catalogue.hpp>

#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_floating_point.hpp>

#include <chrono>

using namespace cloudscope;
using Catch::Matchers::WithinAbs;
using cloudscope::test::outcome;
using cloudscope::test::TempWorkspace;

namespace {

ai::Prediction sample(std::uint64_t sequence, UtcTime utc, bool with_base)
{
    ai::Prediction p;
    p.frame.sequence = sequence;
    p.frame.captured.utc = utc;
    p.model_name = "stratia";
    p.model_version = "1.0.0-demo";
    p.provider = "DirectML";
    p.genus_probs.fill(0.1F);
    p.genus_probs[6] = 0.7F;  // Sc
    p.genus_top = 6;
    p.etage_probs = {0.8F, 0.3F, 0.05F};
    p.oktas = 5;
    p.cover_fraction = 0.6;
    p.p_cloud_overhead = with_base ? 0.9 : 0.2;
    if (with_base) {
        p.cbh = ai::CbhEstimate{
            .median_m = 1500.0, .low_80_m = 1100.0, .high_80_m = 2100.0, .low_95_m = 900.0, .high_95_m = 2600.0};
    }
    p.sky_fractions = {0.01F, 0.4F, 0.55F, 0.04F};
    p.ood_score = 0.7F;
    p.uncertain = false;
    p.geometry_valid = true;
    p.run_ms = 42.0;
    return p;
}

}  // namespace

TEST_CASE("a prediction becomes a catalogue row and reads back unchanged", "[ai][catalogue]")
{
    const TempWorkspace workspace;
    auto catalogue = Catalogue::open(workspace.path("catalogue.sqlite"));
    REQUIRE(outcome(catalogue) == "ok");
    const UtcTime t0 = from_unix_ms(1'760'000'000'000);
    const ai::Prediction first = sample(1, t0, true);
    const ai::Prediction second = sample(2, t0 + std::chrono::seconds(30), false);

    const auto id1 = ai::log_prediction(**catalogue, first, "session-a");
    REQUIRE(outcome(id1) == "ok");
    CHECK(*id1 > 0);
    REQUIRE(outcome(ai::log_prediction(**catalogue, second, "session-a")) == "ok");
    REQUIRE(outcome(ai::log_prediction(**catalogue, sample(3, t0 + std::chrono::seconds(60), true))) ==
            "ok");  // outside a session

    CHECK(*(*catalogue)->ai_result_count() == 3);
    CHECK(*(*catalogue)->ai_result_count("session-a") == 2);
    CHECK(*(*catalogue)->ai_result_count(std::string()) == 1);

    const auto rows = (*catalogue)
                          ->ai_results(AiResultQuery{.session_id = std::string("session-a"),
                                                     .from = std::nullopt,
                                                     .to = std::nullopt,
                                                     .limit = 10000,
                                                     .newest_first = false});
    REQUIRE(outcome(rows) == "ok");
    REQUIRE(rows->size() == 2);
    const AiResultEntry& r = rows->front();
    CHECK(r.id == *id1);
    CHECK(r.session_id == "session-a");
    CHECK(r.utc == t0);
    CHECK(r.sequence == 1);
    CHECK(r.model_name == "stratia");
    CHECK(r.model_version == "1.0.0-demo");
    CHECK(r.provider == "DirectML");
    CHECK(r.oktas == 5);
    CHECK_THAT(r.cover_fraction, WithinAbs(0.6, 1e-9));
    CHECK(r.genus_top == 6);
    CHECK_THAT(r.genus_prob, WithinAbs(0.7, 1e-6));
    CHECK_THAT(r.etage_probs[0], WithinAbs(0.8, 1e-6));
    CHECK_THAT(r.p_cloud_overhead, WithinAbs(0.9, 1e-9));
    REQUIRE(r.cbh_median_m.has_value());
    CHECK_THAT(*r.cbh_median_m, WithinAbs(1500.0, 1e-9));
    CHECK_THAT(*r.cbh_low_80_m, WithinAbs(1100.0, 1e-9));
    CHECK_THAT(*r.cbh_high_80_m, WithinAbs(2100.0, 1e-9));
    CHECK_THAT(r.sky_fractions[2], WithinAbs(0.55, 1e-6));
    CHECK_THAT(r.ood_score, WithinAbs(0.7, 1e-6));
    CHECK_FALSE(r.uncertain);
    CHECK(r.geometry_valid);
    CHECK_THAT(r.run_ms, WithinAbs(42.0, 1e-9));
    CHECK_FALSE(rows->back().cbh_median_m.has_value());  // no cloud overhead: no height

    const auto newest = (*catalogue)
                            ->ai_results(AiResultQuery{.session_id = std::nullopt,
                                                       .from = std::nullopt,
                                                       .to = std::nullopt,
                                                       .limit = 1,
                                                       .newest_first = true});
    REQUIRE(outcome(newest) == "ok");
    REQUIRE(newest->size() == 1);
    CHECK(newest->front().sequence == 3);
    const auto window = (*catalogue)
                            ->ai_results(AiResultQuery{.session_id = std::nullopt,
                                                       .from = t0 + std::chrono::seconds(20),
                                                       .to = t0 + std::chrono::seconds(60),
                                                       .limit = 10000,
                                                       .newest_first = false});
    REQUIRE(outcome(window) == "ok");
    REQUIRE(window->size() == 1);
    CHECK(window->front().sequence == 2);

    // an older catalogue (no ai_results table yet) gains the table when reopened
    catalogue->reset();
    auto reopened = Catalogue::open(workspace.path("catalogue.sqlite"));
    REQUIRE(outcome(reopened) == "ok");
    CHECK(*(*reopened)->ai_result_count() == 3);
}
