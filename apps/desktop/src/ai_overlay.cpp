#include "ai_overlay.hpp"

#include <QtCore/QCoreApplication>
#include <QtGui/QFontMetrics>
#include <QtGui/QPainter>

#include <algorithm>
#include <cmath>

namespace cloudscope::desktop {

namespace {

constexpr int kBadgeMargin = 8;
constexpr int kBadgePadding = 6;

}  // namespace

QString genus_name(int index)
{
    if (index < 0 || index >= static_cast<int>(ai::kGenusClasses.size())) {
        return QStringLiteral("?");
    }
    return QString::fromUtf8(ai::kGenusClasses[static_cast<std::size_t>(index)].data(),
                             static_cast<qsizetype>(ai::kGenusClasses[static_cast<std::size_t>(index)].size()));
}

QString etage_name(int index)
{
    if (index < 0 || index >= static_cast<int>(ai::kEtageClasses.size())) {
        return QStringLiteral("?");
    }
    return QString::fromUtf8(ai::kEtageClasses[static_cast<std::size_t>(index)].data(),
                             static_cast<qsizetype>(ai::kEtageClasses[static_cast<std::size_t>(index)].size()));
}

QString height_text(double metres)
{
    if (metres >= 1000.0) {
        return QCoreApplication::translate("AiOverlay", "%1 km").arg(QString::number(metres / 1000.0, 'f', 1));
    }
    return QCoreApplication::translate("AiOverlay", "%1 m")
        .arg(QString::number(std::round(metres / 10.0) * 10.0, 'f', 0));
}

QString ai_badge_text(const ai::Prediction& prediction)
{
    if (prediction.uncertain) {
        if (prediction.night) {
            return QCoreApplication::translate("AiOverlay", "AI: uncertain (night frame)");
        }
        return QCoreApplication::translate("AiOverlay", "AI: uncertain (out of domain, score %1)")
            .arg(QString::number(prediction.ood_score, 'f', 2));
    }
    const int percent =
        static_cast<int>(std::lround(prediction.genus_probs[static_cast<std::size_t>(prediction.genus_top)] * 100.0F));
    QString text = QCoreApplication::translate("AiOverlay", "%1 %2 % · %3 okta")
                       .arg(genus_name(prediction.genus_top))
                       .arg(percent)
                       .arg(prediction.oktas);
    if (prediction.cbh.has_value()) {
        text += QCoreApplication::translate("AiOverlay", " · base %1").arg(height_text(prediction.cbh->median_m));
    } else {
        text += QCoreApplication::translate("AiOverlay", " · no cloud overhead");
    }
    return text;
}

QImage ai_overlay_image(const ai::Prediction& prediction, const AiOverlaySettings& settings,
                        const AiOverlayColours& colours)
{
    if (!settings.masks && !settings.layers) {
        return {};
    }
    if (prediction.uncertain && !settings.show_when_uncertain) {
        return {};
    }
    const cv::Mat& sky = prediction.sky_parse;
    if (sky.empty() || sky.type() != CV_8UC1) {
        return {};
    }
    const int alpha = std::clamp(settings.opacity_percent, 0, 100) * 255 / 100;
    const auto with_alpha = [alpha](QColor c) {
        c.setAlpha(alpha);
        return c.rgba();
    };
    const QRgb cloud = with_alpha(colours.cloud);
    const QRgb glare = with_alpha(colours.glare);
    const QRgb invalid = with_alpha(colours.invalid);
    const std::array<QRgb, 3> layer_colours{with_alpha(colours.low), with_alpha(colours.mid), with_alpha(colours.high)};
    QImage image(sky.cols, sky.rows, QImage::Format_ARGB32);
    image.fill(Qt::transparent);
    const bool layers = settings.layers && !prediction.layer.empty();
    for (int y = 0; y < sky.rows; ++y) {
        const auto* row = sky.ptr<std::uint8_t>(y);
        const std::uint8_t* layer_row = layers ? prediction.layer.ptr<std::uint8_t>(y) : nullptr;
        auto* out = reinterpret_cast<QRgb*>(image.scanLine(y));
        for (int x = 0; x < sky.cols; ++x) {
            switch (row[x]) {
            case 0:
                out[x] = invalid;
                break;
            case 2:
                if (layers && layer_row[x] < 3) {
                    out[x] = layer_colours[layer_row[x]];
                } else if (settings.masks) {
                    out[x] = cloud;
                }
                break;
            case 3:
                if (settings.masks) {
                    out[x] = glare;
                }
                break;
            default:
                break;  // sky stays clear
            }
        }
    }
    return image;
}

void paint_ai_overlay(QPainter& painter, const Viewport& viewport, const QImage& overlay, QSize image_size)
{
    if (overlay.isNull() || image_size.isEmpty()) {
        return;
    }
    painter.save();
    painter.setTransform(viewport.transform());
    painter.setRenderHint(QPainter::SmoothPixmapTransform, true);
    painter.drawImage(QRectF(QPointF(0.0, 0.0), QSizeF(image_size)), overlay);
    painter.restore();
}

void paint_ai_badge(QPainter& painter, const QRectF& widget, const ai::Prediction& prediction, QColor text,
                    QColor warning)
{
    const QString label = ai_badge_text(prediction);
    painter.save();
    QFont font = painter.font();
    font.setBold(true);
    painter.setFont(font);
    const QFontMetrics metrics(font);
    const QRectF box(widget.left() + kBadgeMargin, widget.top() + kBadgeMargin,
                     metrics.horizontalAdvance(label) + 2 * kBadgePadding, metrics.height() + 2 * kBadgePadding);
    painter.setPen(Qt::NoPen);
    painter.setBrush(QColor(0, 0, 0, 160));
    painter.drawRoundedRect(box, 4.0, 4.0);
    painter.setPen(prediction.uncertain ? warning : text);
    painter.drawText(box, Qt::AlignCenter, label);
    painter.restore();
}

}  // namespace cloudscope::desktop
