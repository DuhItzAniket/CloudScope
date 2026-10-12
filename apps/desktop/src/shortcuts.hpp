// Keyboard shortcuts (P043, FR-DSP-10): every frequent action is a QAction with an id, a default key and a status
// tip, registered here so that the preferences can list and change them and the configuration can override them
// ([desktop.shortcuts] in config.toml: action id -> key sequence, Qt's portable spelling such as "Ctrl+Shift+F").
//
// Lives on the UI thread.
#pragma once

#include <cloudscope/common/error.hpp>

#include <QtCore/QObject>
#include <QtCore/QString>
#include <QtGui/QAction>
#include <QtGui/QKeySequence>

#include <map>
#include <string>
#include <utility>
#include <vector>

namespace cloudscope::desktop {

struct ActionSpec {
    QString id;  // "view.fit", stable: used in the configuration file
    QString text;
    QKeySequence shortcut;  // the default
    QString status_tip;
};

struct ShortcutConflict {
    QString first_id;
    QString second_id;
    QKeySequence shortcut;
};

class ShortcutRegistry : public QObject {
    Q_OBJECT

public:
    explicit ShortcutRegistry(QObject* parent = nullptr);

    // Creates the action (owned by `parent`) with the spec's default shortcut, or the override set earlier.
    QAction* add(const ActionSpec& spec, QObject* parent);
    [[nodiscard]] QAction* action(const QString& id) const;
    [[nodiscard]] const std::vector<ActionSpec>& specs() const { return specs_; }
    [[nodiscard]] QKeySequence effective(const QString& id) const;
    [[nodiscard]] QKeySequence default_of(const QString& id) const;

    // Parses and checks overrides without applying them: InvalidArgument names an unknown action id or a key
    // sequence Qt cannot parse. `known` lists the action ids that exist.
    [[nodiscard]] static Expected<std::map<QString, QKeySequence>>
    parse_overrides(const std::map<std::string, std::string>& overrides, const std::vector<ActionSpec>& known);
    // Applies overrides to the actions registered so far and to those added later.
    [[nodiscard]] Expected<void> apply_overrides(const std::map<std::string, std::string>& overrides);
    // Actions that share one non-empty shortcut.
    [[nodiscard]] std::vector<ShortcutConflict> conflicts() const;

Q_SIGNALS:
    void shortcutsChanged();

private:
    std::vector<ActionSpec> specs_;
    std::map<QString, QAction*> actions_;
    std::map<QString, QKeySequence> overrides_;
};

// Qt's portable spelling of a key sequence, or empty text for an empty sequence.
[[nodiscard]] QString shortcut_text(const QKeySequence& sequence);

}  // namespace cloudscope::desktop
