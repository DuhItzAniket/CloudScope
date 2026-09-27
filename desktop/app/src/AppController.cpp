#include "AppController.h"
#include "CameraSource.h"

AppController::AppController(QObject* parent) : QObject(parent)
{
}

void AppController::setStatus(const QString& s)
{
    if (s == status_)
        return;
    status_ = s;
    emit statusChanged();
}

QStringList AppController::probeCameras(int maxIndex)
{
    QStringList out;
    for (const auto& d : CameraSource::enumerateLocal(maxIndex)) {
        out << QString::fromStdString(d.name) +
                   (d.opened ? QStringLiteral(" [ok %1x%2]").arg(d.width).arg(d.height)
                             : QStringLiteral(" [n/a]"));
    }
    setStatus(QStringLiteral("probed"));
    return out;
}
