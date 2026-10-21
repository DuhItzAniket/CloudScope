#include "cloudscope/ai/result_log.hpp"

namespace cloudscope::ai {

AiResultEntry ai_result_entry(const Prediction& prediction, std::string session_id)
{
    AiResultEntry r;
    r.session_id = std::move(session_id);
    r.utc = prediction.frame.captured.utc;
    r.sequence = prediction.frame.sequence;
    r.model_name = prediction.model_name;
    r.model_version = prediction.model_version;
    r.provider = prediction.provider;
    r.oktas = prediction.oktas;
    r.cover_fraction = prediction.cover_fraction;
    r.genus_top = prediction.genus_top;
    r.genus_prob = prediction.genus_probs[static_cast<std::size_t>(prediction.genus_top)];
    for (std::size_t i = 0; i < r.etage_probs.size(); ++i) {
        r.etage_probs[i] = prediction.etage_probs[i];
    }
    r.p_cloud_overhead = prediction.p_cloud_overhead;
    if (prediction.cbh) {
        r.cbh_median_m = prediction.cbh->median_m;
        r.cbh_low_80_m = prediction.cbh->low_80_m;
        r.cbh_high_80_m = prediction.cbh->high_80_m;
    }
    for (std::size_t i = 0; i < r.sky_fractions.size(); ++i) {
        r.sky_fractions[i] = prediction.sky_fractions[i];
    }
    r.ood_score = prediction.ood_score;
    r.uncertain = prediction.uncertain;
    r.night = prediction.night;
    r.geometry_valid = prediction.geometry_valid;
    r.run_ms = prediction.run_ms;
    return r;
}

Expected<std::int64_t> log_prediction(Catalogue& catalogue, const Prediction& prediction, std::string session_id)
{
    return catalogue.add_ai_result(ai_result_entry(prediction, std::move(session_id)));
}

}  // namespace cloudscope::ai
