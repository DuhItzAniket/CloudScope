// Per-frame logging of STRATIA results into the frame catalogue (P069, FR-AI-07).

#pragma once

#include <cloudscope/ai/inference_service.hpp>
#include <cloudscope/common/error.hpp>
#include <cloudscope/session/catalogue.hpp>

#include <string>

namespace cloudscope::ai {

// The catalogue row of a prediction. `session_id` is empty for frames outside a recorded session.
[[nodiscard]] AiResultEntry ai_result_entry(const Prediction& prediction, std::string session_id = {});

// Writes the prediction's row; the id of the new row.
[[nodiscard]] Expected<std::int64_t> log_prediction(Catalogue& catalogue, const Prediction& prediction,
                                                    std::string session_id = {});

}  // namespace cloudscope::ai
