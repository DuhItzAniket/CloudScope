#pragma once
#include <QObject>
#include <QString>
#include <QStringList>

// Owns capture + inference threads and exposes state to QML.
// Ph25: version + camera-matrix helpers only; live pipeline lands in Ph28.
class AppController : public QObject {
    Q_OBJECT
    Q_PROPERTY(QString version READ version CONSTANT)
    Q_PROPERTY(QString status READ status NOTIFY statusChanged)
public:
    explicit AppController(QObject* parent = nullptr);

    QString version() const { return QStringLiteral("0.1.0-ph25"); }
    QString status() const { return status_; }

    // "0","1",... or "Camera 2" labels for the QML source picker.
    Q_INVOKABLE QStringList probeCameras(int maxIndex = 5);

signals:
    void statusChanged();

private:
    void setStatus(const QString& s);
    QString status_ = QStringLiteral("idle");
};
