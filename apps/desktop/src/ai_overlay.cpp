#include "ai_overlay.hpp"

#include <QtCore/QCoreApplication>
#include <QtGui/QFontMetrics>
#include <QtGui/QPainter>

#include <QtCore/QStringList>
#include <QtGui/QPen>

#include <algorithm>
#include <array>
#include <cmath>
#include <vector>

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

QString genus_long_name(int index)
{
    static const std::array<const char*, 12> names{
        QT_TRANSLATE_NOOP("AiOverlay", "Cirrus"),        QT_TRANSLATE_NOOP("AiOverlay", "Cirrocumulus"),
        QT_TRANSLATE_NOOP("AiOverlay", "Cirrostratus"),  QT_TRANSLATE_NOOP("AiOverlay", "Altocumulus"),
        QT_TRANSLATE_NOOP("AiOverlay", "Altostratus"),   QT_TRANSLATE_NOOP("AiOverlay", "Nimbostratus"),
        QT_TRANSLATE_NOOP("AiOverlay", "Stratocumulus"), QT_TRANSLATE_NOOP("AiOverlay", "Stratus"),
        QT_TRANSLATE_NOOP("AiOverlay", "Cumulus"),       QT_TRANSLATE_NOOP("AiOverlay", "Cumulonimbus"),
        QT_TRANSLATE_NOOP("AiOverlay", "Clear sky"),     QT_TRANSLATE_NOOP("AiOverlay", "Contrail")};
    if (index < 0 || index >= static_cast<int>(names.size())) {
        return QStringLiteral("?");
    }
    return QCoreApplication::translate("AiOverlay", names[static_cast<std::size_t>(index)]);
}

QString ai_genus_headline(const ai::Prediction& prediction)
{
    std::vector<int> order;
    for (int i = 0; i < static_cast<int>(prediction.genus_probs.size()); ++i) {
        if (prediction.genus_probs[static_cast<std::size_t>(i)] >= 0.5F) {
            order.push_back(i);
        }
    }
    std::sort(order.begin(), order.end(), [&prediction](int a, int b) {
        return prediction.genus_probs[static_cast<std::size_t>(a)] >
               prediction.genus_probs[static_cast<std::size_t>(b)];
    });
    if (order.empty()) {
        order.push_back(prediction.genus_top);
    }
    if (order.size() > 3) {
        order.resize(3);
    }
    QStringList names;
    for (const int index : order) {
        names.append(genus_long_name(index));
    }
    return names.join(QStringLiteral(" + "));
}

QString ai_layers_text(const ai::Prediction& prediction)
{
    QStringList names;
    int best = 0;
    for (int i = 0; i < static_cast<int>(prediction.etage_probs.size()); ++i) {
        if (prediction.etage_probs[static_cast<std::size_t>(i)] >= 0.5F) {
            names.append(etage_name(i));
        }
        if (prediction.etage_probs[static_cast<std::size_t>(i)] >
            prediction.etage_probs[static_cast<std::size_t>(best)]) {
            best = i;
        }
    }
    if (names.isEmpty()) {
        names.append(etage_name(best));
    }
    return names.join(QStringLiteral(" · "));
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
        for (int x = 0; x < sky.cols; ++x) {
            switch (row[x]) {
            case 0:
                image.setPixel(x, y, invalid);
                break;
            case 2:
                if (layers && layer_row[x] < 3) {
                    image.setPixel(x, y, layer_colours[layer_row[x]]);
                } else if (settings.masks) {
                    image.setPixel(x, y, cloud);
                }
                break;
            case 3:
                if (settings.masks) {
                    image.setPixel(x, y, glare);
                }
                break;
            default:
                break;  // sky stays clear
            }
        }
    }
    // An outline along cloud and glare borders, so the mask reads on white cloud as well as on blue sky. Only on
    // a refined (fine) mask: on the coarse grid it would be a staircase.
    if (settings.masks && !layers && sky.cols >= 256) {
        QColor line = colours.outline;
        line.setAlpha(std::min(255, alpha + 150));
        QColor glare_line = colours.glare;
        glare_line.setAlpha(std::min(255, alpha + 150));
        for (int y = 1; y + 1 < sky.rows; ++y) {
            const auto* up = sky.ptr<std::uint8_t>(y - 1);
            const auto* row = sky.ptr<std::uint8_t>(y);
            const auto* down = sky.ptr<std::uint8_t>(y + 1);
            for (int x = 1; x + 1 < sky.cols; ++x) {
                const std::uint8_t c = row[x];
                if (c != 2 && c != 3) {
                    continue;
                }
                if (row[x - 1] != c || row[x + 1] != c || up[x] != c || down[x] != c) {
                    image.setPixel(x, y, c == 2 ? line.rgba() : glare_line.rgba());
                }
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
                    QColor warning, double top_offset, bool mask_legend)
{
    painter.save();
    painter.setRenderHint(QPainter::Antialiasing, true);
    // The card scales with the view: readable on a projector, modest in a small dock.
    const double base = std::clamp(widget.height() / 46.0, 11.0, 22.0);
    QFont body = painter.font();
    body.setPixelSize(static_cast<int>(std::lround(base)));
    QFont strong = body;
    strong.setBold(true);
    QFont title = strong;
    title.setPixelSize(static_cast<int>(std::lround(base * 1.7)));
    const QFontMetricsF body_metrics(body);
    const QFontMetricsF strong_metrics(strong);
    const QFontMetricsF title_metrics(title);
    const double pad = base * 0.8;
    const double line = body_metrics.height() * 1.25;
    const QColor good(70, 200, 120);

    struct Row {
        QString key;
        QString value;
    };
    QString headline;
    QString chip;
    std::vector<Row> rows;
    if (prediction.uncertain) {
        headline = QCoreApplication::translate("AiOverlay", "Not sure about this sky");
        chip = prediction.night ? QCoreApplication::translate("AiOverlay", "UNCERTAIN · night frame")
                                : QCoreApplication::translate("AiOverlay", "UNCERTAIN · unlike the training skies");
        if (!prediction.night) {
            rows.push_back(
                {QCoreApplication::translate("AiOverlay", "Score"), QString::number(prediction.ood_score, 'f', 2)});
        }
    } else {
        headline = ai_genus_headline(prediction);
        chip = QCoreApplication::translate("AiOverlay", "RELIABLE");
        rows.push_back({QCoreApplication::translate("AiOverlay", "Layers"), ai_layers_text(prediction)});
        rows.push_back({QCoreApplication::translate("AiOverlay", "Cover"),
                        QCoreApplication::translate("AiOverlay", "%1 / 8 oktas").arg(prediction.oktas)});
        if (prediction.cbh.has_value()) {
            rows.push_back({QCoreApplication::translate("AiOverlay", "Cloud base"),
                            QCoreApplication::translate("AiOverlay", "about %1  (%2 to %3)")
                                .arg(height_text(prediction.cbh->median_m), height_text(prediction.cbh->low_80_m),
                                     height_text(prediction.cbh->high_80_m))});
        } else {
            rows.push_back({QCoreApplication::translate("AiOverlay", "Cloud base"),
                            QCoreApplication::translate("AiOverlay", "no cloud overhead")});
        }
    }
    double key_width = 0.0;
    double value_width = 0.0;
    for (const Row& row : rows) {
        key_width = std::max(key_width, body_metrics.horizontalAdvance(row.key));
        value_width = std::max(value_width, strong_metrics.horizontalAdvance(row.value));
    }
    const double dial = prediction.uncertain ? 0.0 : line * 0.8;  // the okta dial sits after the cover row
    const double gap = base * 0.9;
    const double chip_width = strong_metrics.horizontalAdvance(chip) + 2.0 * base * 0.7;
    QFont small = body;
    small.setPixelSize(static_cast<int>(std::lround(base * 0.82)));
    const QFontMetricsF small_metrics(small);
    const QString credit =
        QStringLiteral("%1 %2 · %3 ms")
            .arg(QString::fromStdString(prediction.model_name), QString::fromStdString(prediction.model_version))
            .arg(static_cast<int>(std::lround(prediction.run_ms)));
    const double credit_width = small_metrics.horizontalAdvance(credit);
    const bool legend = mask_legend && !prediction.uncertain;
    const QString legend_cloud = QCoreApplication::translate("AiOverlay", "cloud");
    const QString legend_glare = QCoreApplication::translate("AiOverlay", "Sun glare");
    const double swatch = small_metrics.height() * 0.7;
    const double legend_width = legend ? 2.0 * (swatch + base * 0.35) + small_metrics.horizontalAdvance(legend_cloud) +
                                             small_metrics.horizontalAdvance(legend_glare) + base
                                       : 0.0;
    const double chip_height = strong_metrics.height() * 1.25;
    const double inner = std::max({title_metrics.horizontalAdvance(headline),
                                   key_width + gap + value_width + (dial > 0.0 ? dial + gap * 0.6 : 0.0),
                                   chip_width + gap + credit_width, legend_width});
    const double height = pad + title_metrics.height() + base * 0.3 + static_cast<double>(rows.size()) * line +
                          base * 0.5 + chip_height + (legend ? base * 0.5 + small_metrics.height() : 0.0) + pad;
    const QRectF box(widget.left() + kBadgeMargin, widget.top() + kBadgeMargin + top_offset, inner + 2.0 * pad, height);
    painter.setPen(Qt::NoPen);
    painter.setBrush(QColor(8, 16, 30, 205));
    painter.drawRoundedRect(box, base * 0.5, base * 0.5);
    // accent bar: green when reliable, the warning colour when not
    painter.setBrush(prediction.uncertain ? warning : good);
    painter.drawRoundedRect(QRectF(box.left(), box.top(), base * 0.28, box.height()), base * 0.14, base * 0.14);

    double y = box.top() + pad;
    painter.setFont(title);
    painter.setPen(prediction.uncertain ? warning : QColor(255, 255, 255));
    painter.drawText(QRectF(box.left() + pad, y, inner, title_metrics.height()), Qt::AlignLeft | Qt::AlignVCenter,
                     headline);
    y += title_metrics.height() + base * 0.3;
    for (std::size_t i = 0; i < rows.size(); ++i) {
        painter.setFont(body);
        painter.setPen(QColor(170, 186, 208));
        painter.drawText(QRectF(box.left() + pad, y, key_width, line), Qt::AlignLeft | Qt::AlignVCenter, rows[i].key);
        painter.setFont(strong);
        painter.setPen(text);
        const double value_x = box.left() + pad + key_width + gap;
        painter.drawText(QRectF(value_x, y, value_width, line), Qt::AlignLeft | Qt::AlignVCenter, rows[i].value);
        if (!prediction.uncertain && i == 1) {  // the cover row: a dial with `oktas` of eight sectors filled
            const double used = strong_metrics.horizontalAdvance(rows[i].value);
            const QRectF circle(value_x + used + gap * 0.6, y + (line - dial) / 2.0, dial, dial);
            painter.setPen(Qt::NoPen);
            painter.setBrush(QColor(255, 255, 255, 40));
            painter.drawEllipse(circle);
            painter.setBrush(QColor(170, 220, 255));
            const int oktas = std::clamp(prediction.oktas, 0, 8);
            if (oktas >= 8) {
                painter.drawEllipse(circle);
            } else if (oktas > 0) {
                painter.drawPie(circle, 90 * 16, -oktas * 45 * 16);
            }
            painter.setBrush(Qt::NoBrush);
            painter.setPen(QPen(QColor(255, 255, 255, 200), std::max(1.0, base * 0.08)));
            painter.drawEllipse(circle);
        }
        y += line;
    }
    y += base * 0.5;
    const QRectF chip_box(box.left() + pad, y, chip_width, chip_height);
    painter.setPen(Qt::NoPen);
    QColor chip_fill = prediction.uncertain ? warning : good;
    chip_fill.setAlpha(60);
    painter.setBrush(chip_fill);
    painter.drawRoundedRect(chip_box, chip_height / 2.0, chip_height / 2.0);
    painter.setFont(strong);
    painter.setPen(prediction.uncertain ? warning : good);
    painter.drawText(chip_box, Qt::AlignCenter, chip);
    painter.setFont(small);
    painter.setPen(QColor(150, 166, 190));
    painter.drawText(QRectF(chip_box.right() + gap, y, credit_width + 2.0, chip_height),
                     Qt::AlignLeft | Qt::AlignVCenter, credit);
    if (legend) {
        y += chip_height + base * 0.5;
        double x = box.left() + pad;
        const auto key = [&](const QColor& colour, const QString& label) {
            painter.setPen(Qt::NoPen);
            painter.setBrush(colour);
            painter.drawRoundedRect(QRectF(x, y + (small_metrics.height() - swatch) / 2.0, swatch, swatch), 2.0, 2.0);
            x += swatch + base * 0.35;
            painter.setPen(QColor(200, 212, 230));
            const double width = small_metrics.horizontalAdvance(label);
            painter.drawText(QRectF(x, y, width + 2.0, small_metrics.height()), Qt::AlignLeft | Qt::AlignVCenter,
                             label);
            x += width + base;
        };
        key(AiOverlayColours{}.cloud, legend_cloud);
        key(AiOverlayColours{}.glare, legend_glare);
    }
    painter.restore();
}

}  // namespace cloudscope::desktop
