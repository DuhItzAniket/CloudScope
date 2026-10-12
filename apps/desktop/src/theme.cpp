#include "theme.hpp"

#include <QtCore/QString>
#include <QtWidgets/QApplication>
#include <QtWidgets/QStyleFactory>

namespace cloudscope::desktop {

std::string_view to_string(Theme theme)
{
    switch (theme) {
    case Theme::Dark:
        return "dark";
    case Theme::Night:
        return "night";
    }
    return "dark";
}

Expected<Theme> theme_from_string(std::string_view text)
{
    if (text == "dark") {
        return Theme::Dark;
    }
    if (text == "night") {
        return Theme::Night;
    }
    return fail(ErrorCode::InvalidArgument, R"(theme must be "dark" or "night"; got ")" + std::string(text) + "\"");
}

QPalette theme_palette(Theme theme)
{
    QPalette palette;
    if (theme == Theme::Dark) {
        const QColor window(0x2b, 0x2b, 0x2b);
        const QColor base(0x1e, 0x1e, 0x1e);
        const QColor text(0xe6, 0xe6, 0xe6);
        const QColor disabled(0x80, 0x80, 0x80);
        const QColor highlight(0x2d, 0x6c, 0xb5);
        palette.setColor(QPalette::Window, window);
        palette.setColor(QPalette::WindowText, text);
        palette.setColor(QPalette::Base, base);
        palette.setColor(QPalette::AlternateBase, QColor(0x26, 0x26, 0x26));
        palette.setColor(QPalette::ToolTipBase, QColor(0x3c, 0x3c, 0x3c));
        palette.setColor(QPalette::ToolTipText, text);
        palette.setColor(QPalette::Text, text);
        palette.setColor(QPalette::Button, QColor(0x35, 0x35, 0x35));
        palette.setColor(QPalette::ButtonText, text);
        palette.setColor(QPalette::BrightText, QColor(0xff, 0x60, 0x60));
        palette.setColor(QPalette::Link, QColor(0x5c, 0xa0, 0xe0));
        palette.setColor(QPalette::Highlight, highlight);
        palette.setColor(QPalette::HighlightedText, QColor(0xff, 0xff, 0xff));
        palette.setColor(QPalette::PlaceholderText, disabled);
        palette.setColor(QPalette::Disabled, QPalette::Text, disabled);
        palette.setColor(QPalette::Disabled, QPalette::WindowText, disabled);
        palette.setColor(QPalette::Disabled, QPalette::ButtonText, disabled);
        palette.setColor(QPalette::Disabled, QPalette::Highlight, QColor(0x50, 0x50, 0x50));
        return palette;
    }
    // Night: every colour is a shade of red; nothing blue or green reaches the eye.
    const QColor window(0x1a, 0x00, 0x00);
    const QColor base(0x0d, 0x00, 0x00);
    const QColor text(0xff, 0x45, 0x30);
    const QColor disabled(0x80, 0x20, 0x18);
    const QColor highlight(0x80, 0x14, 0x10);
    palette.setColor(QPalette::Window, window);
    palette.setColor(QPalette::WindowText, text);
    palette.setColor(QPalette::Base, base);
    palette.setColor(QPalette::AlternateBase, QColor(0x16, 0x00, 0x00));
    palette.setColor(QPalette::ToolTipBase, QColor(0x2a, 0x00, 0x00));
    palette.setColor(QPalette::ToolTipText, text);
    palette.setColor(QPalette::Text, text);
    palette.setColor(QPalette::Button, QColor(0x2a, 0x05, 0x05));
    palette.setColor(QPalette::ButtonText, text);
    palette.setColor(QPalette::BrightText, QColor(0xff, 0x80, 0x60));
    palette.setColor(QPalette::Link, QColor(0xff, 0x70, 0x50));
    palette.setColor(QPalette::Highlight, highlight);
    palette.setColor(QPalette::HighlightedText, QColor(0xff, 0x90, 0x80));
    palette.setColor(QPalette::PlaceholderText, disabled);
    palette.setColor(QPalette::Disabled, QPalette::Text, disabled);
    palette.setColor(QPalette::Disabled, QPalette::WindowText, disabled);
    palette.setColor(QPalette::Disabled, QPalette::ButtonText, disabled);
    palette.setColor(QPalette::Disabled, QPalette::Highlight, QColor(0x40, 0x0a, 0x08));
    return palette;
}

QString theme_stylesheet(Theme theme)
{
    if (theme == Theme::Dark) {
        return QStringLiteral("QToolTip { color: #e6e6e6; background-color: #3c3c3c; border: 1px solid #5a5a5a; }"
                              "QStatusBar::item { border: none; }");
    }
    return QStringLiteral("QToolTip { color: #ff4530; background-color: #2a0000; border: 1px solid #801410; }"
                          "QStatusBar::item { border: none; }"
                          "QMenu::separator { background: #801410; }");
}

QColor overlay_colour(Theme theme)
{
    return theme == Theme::Dark ? QColor(0xff, 0xc8, 0x00) : QColor(0xff, 0x40, 0x20);
}

QColor accent_colour(Theme theme)
{
    return theme == Theme::Dark ? QColor(0x5c, 0xa0, 0xe0) : QColor(0xff, 0x60, 0x40);
}

QColor warning_colour(Theme theme)
{
    return theme == Theme::Dark ? QColor(0xff, 0x8c, 0x28) : QColor(0xff, 0x30, 0x10);
}

void apply_theme(QApplication& app, Theme theme)
{
    if (QStyle* fusion = QStyleFactory::create(QStringLiteral("Fusion"))) {
        QApplication::setStyle(fusion);
    }
    QApplication::setPalette(theme_palette(theme));
    app.setStyleSheet(theme_stylesheet(theme));
}

}  // namespace cloudscope::desktop
