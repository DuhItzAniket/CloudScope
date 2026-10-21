// AI overlays on the live view (P068, FR-AI-05, FR-AI-06): the sky-parse and layer masks as a translucent
// picture over the frame, and a badge with the headline result. Overlays are drawn only from valid outputs:
// an uncertain result (out-of-domain score, night) shows the badge saying so and hides the masks unless the
// operator asks to see them anyway.

#pragma once

#include "viewport.hpp"

#include <cloudscope/ai/inference_service.hpp>

#include <QtCore/QString>
#include <QtGui/QColor>
#include <QtGui/QImage>

class QPainter;

namespace cloudscope::desktop {

struct AiOverlaySettings {
    bool masks = true;                 // sky parse: cloud, Sun glare, invalid
    bool layers = false;               // cloud layers (low, mid, high) instead of the plain cloud tint
    bool badge = true;                 // the headline text in the corner of the live view
    int opacity_percent = 40;          // of the mask tint
    bool show_when_uncertain = false;  // FR-AI-06: masks of an uncertain result are hidden by default
    friend bool operator==(const AiOverlaySettings&, const AiOverlaySettings&) = default;
};

struct AiOverlayColours {
    QColor cloud{255, 255, 255};
    QColor glare{255, 150, 0};
    QColor invalid{30, 30, 30};
    QColor low{90, 200, 255};
    QColor mid{120, 255, 120};
    QColor high{255, 130, 220};
};

[[nodiscard]] QString genus_name(int index);       // "Cu"; "?" outside the contract's list
[[nodiscard]] QString etage_name(int index);       // "low"
[[nodiscard]] QString height_text(double metres);  // "1.2 km", "850 m"

// The headline: "Uncertain (OOD 2.1)" or "Cu 82 % · 3 okta · base 1.2 km".
[[nodiscard]] QString ai_badge_text(const ai::Prediction& prediction);

// The translucent mask picture (ARGB32, the dense output's size), or a null image when nothing is drawn.
[[nodiscard]] QImage ai_overlay_image(const ai::Prediction& prediction, const AiOverlaySettings& settings,
                                      const AiOverlayColours& colours = {});

// Draws `overlay` stretched over the picture of `image_size` through the viewport.
void paint_ai_overlay(QPainter& painter, const Viewport& viewport, const QImage& overlay, QSize image_size);

// Draws the badge in the top-left corner of the widget.
void paint_ai_badge(QPainter& painter, const QRectF& widget, const ai::Prediction& prediction, QColor text,
                    QColor warning);

}  // namespace cloudscope::desktop
