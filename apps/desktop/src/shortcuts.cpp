#include "shortcuts.hpp"

#include <fmt/format.h>

#include <algorithm>

namespace cloudscope::desktop {

namespace {

bool parsable(const QString& text, QKeySequence* out)
{
    const QKeySequence sequence = QKeySequence::fromString(text, QKeySequence::PortableText);
    if (text.trimmed().isEmpty()) {
        *out = QKeySequence();
        return true;
    }
    if (sequence.isEmpty()) {
        return false;
    }
    for (int i = 0; i < sequence.count(); ++i) {
        if (sequence[static_cast<uint>(i)].key() == Qt::Key_unknown) {
            return false;
        }
    }
    *out = sequence;
    return true;
}

}  // namespace

ShortcutRegistry::ShortcutRegistry(QObject* parent) : QObject(parent) {}

QAction* ShortcutRegistry::add(const ActionSpec& spec, QObject* parent)
{
    auto* action = new QAction(spec.text, parent);
    action->setObjectName(spec.id);
    action->setStatusTip(spec.status_tip);
    action->setToolTip(spec.status_tip.isEmpty() ? spec.text : spec.status_tip);
    const auto override = overrides_.find(spec.id);
    action->setShortcut(override == overrides_.end() ? spec.shortcut : override->second);
    specs_.push_back(spec);
    actions_[spec.id] = action;
    return action;
}

QAction* ShortcutRegistry::action(const QString& id) const
{
    const auto found = actions_.find(id);
    return found == actions_.end() ? nullptr : found->second;
}

QKeySequence ShortcutRegistry::effective(const QString& id) const
{
    if (QAction* found = action(id)) {
        return found->shortcut();
    }
    return {};
}

QKeySequence ShortcutRegistry::default_of(const QString& id) const
{
    const auto found = std::ranges::find_if(specs_, [&](const ActionSpec& spec) { return spec.id == id; });
    return found == specs_.end() ? QKeySequence() : found->shortcut;
}

Expected<std::map<QString, QKeySequence>>
ShortcutRegistry::parse_overrides(const std::map<std::string, std::string>& overrides,
                                  const std::vector<ActionSpec>& known)
{
    std::map<QString, QKeySequence> parsed;
    for (const auto& [id, text] : overrides) {
        const QString qid = QString::fromStdString(id);
        const bool exists = std::ranges::any_of(known, [&](const ActionSpec& spec) { return spec.id == qid; });
        if (!exists) {
            return fail(ErrorCode::InvalidArgument,
                        "desktop.shortcuts." + id + ": unknown action (see Preferences > Shortcuts for the list)");
        }
        QKeySequence sequence;
        if (!parsable(QString::fromStdString(text), &sequence)) {
            return fail(ErrorCode::InvalidArgument,
                        fmt::format(R"(desktop.shortcuts.{}: "{}" is not a key sequence (examples: "F", "Ctrl+1", )"
                                    R"("Ctrl+Shift+S"; empty text removes the shortcut))",
                                    id, text));
        }
        parsed[qid] = sequence;
    }
    return parsed;
}

Expected<void> ShortcutRegistry::apply_overrides(const std::map<std::string, std::string>& overrides)
{
    auto parsed = parse_overrides(overrides, specs_);
    if (!parsed) {
        return fail(parsed.error());
    }
    overrides_ = std::move(*parsed);
    for (auto& [id, action] : actions_) {
        const auto override = overrides_.find(id);
        action->setShortcut(override == overrides_.end() ? default_of(id) : override->second);
    }
    Q_EMIT shortcutsChanged();
    return {};
}

std::vector<ShortcutConflict> ShortcutRegistry::conflicts() const
{
    std::vector<ShortcutConflict> found;
    for (auto first = actions_.begin(); first != actions_.end(); ++first) {
        const QKeySequence shortcut = first->second->shortcut();
        if (shortcut.isEmpty()) {
            continue;
        }
        for (auto second = std::next(first); second != actions_.end(); ++second) {
            if (second->second->shortcut() == shortcut) {
                found.push_back({.first_id = first->first, .second_id = second->first, .shortcut = shortcut});
            }
        }
    }
    return found;
}

QString shortcut_text(const QKeySequence& sequence)
{
    return sequence.toString(QKeySequence::PortableText);
}

}  // namespace cloudscope::desktop
