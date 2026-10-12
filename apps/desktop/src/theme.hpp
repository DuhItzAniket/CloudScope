// Themes of the desktop application (P033, FR-DSP-08): a dark theme for the day and a red night-vision theme that
// keeps the operator's dark adaptation. A theme is a Qt palette on the Fusion style plus a short stylesheet; the
// live view renders red under the night theme (display_stretch.hpp night_vision()).
#pragma once

#include <cloudscope/common/error.hpp>

#include <QtGui/QColor>
#include <QtGui/QPalette>

#include <cstdint>
#include <string_view>

class QApplication;
class QString;

namespace cloudscope::desktop {

enum class Theme : std::uint8_t { Dark, Night };

[[nodiscard]] std::string_view to_string(Theme theme);  // "dark", "night"
[[nodiscard]] Expected<Theme> theme_from_string(std::string_view text);

[[nodiscard]] QPalette theme_palette(Theme theme);
[[nodiscard]] QString theme_stylesheet(Theme theme);
// Colours for things drawn by hand: overlays, histogram, indicators.
[[nodiscard]] QColor overlay_colour(Theme theme);
[[nodiscard]] QColor accent_colour(Theme theme);
[[nodiscard]] QColor warning_colour(Theme theme);

// Fusion style, palette and stylesheet for the whole application.
void apply_theme(QApplication& app, Theme theme);

}  // namespace cloudscope::desktop
