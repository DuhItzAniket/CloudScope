#pragma once
#include <QMutex>
#include <QObject>
#include <QString>
#include <QStringList>
#include <QThread>
#include <QImage>
#include <atomic>
#include <opencv2/core.hpp>

#include "OnnxInfer.h"
#include "CloudVision.h"

class FrameProvider;

// Capture+inference worker: read -> classify+segment -> render -> publish.
class PipelineWorker : public QObject {
    Q_OBJECT
public:
    PipelineWorker(OnnxInfer* infer, FrameProvider* frames);
    void configure(const std::string& source, OverlayMode mode, double minConf);

signals:
    void frameStats(const QString& label, double conf, int objects, bool maskOk,
                    double fps);
    void logLine(const QString& line);
    void finished();

public slots:
    void run();
    void requestStop();

private:
    OnnxInfer* infer_;
    FrameProvider* frames_;
    std::string source_;
    OverlayMode mode_ = OverlayMode::Rect;
    double minConf_ = 0.30;
    std::atomic<bool> stop_{false};
    cv::Mat lastAnnotated_;
    QMutex lastMutex_;
    friend class AppController;
};

// Owns the worker thread and exposes live state to QML.
class AppController : public QObject {
    Q_OBJECT
    Q_PROPERTY(QString version READ version CONSTANT)
    Q_PROPERTY(QString status READ status NOTIFY statusChanged)
    Q_PROPERTY(QString label READ label NOTIFY frameStats)
    Q_PROPERTY(double confidence READ confidence NOTIFY frameStats)
    Q_PROPERTY(double fps READ fps NOTIFY frameStats)
    Q_PROPERTY(QString device READ device NOTIFY frameStats)
    Q_PROPERTY(int objectCount READ objectCount NOTIFY frameStats)
    Q_PROPERTY(bool maskOk READ maskOk NOTIFY frameStats)
    Q_PROPERTY(int overlayMode READ overlayMode WRITE setOverlayMode NOTIFY overlayChanged)
    Q_PROPERTY(double minConf READ minConf WRITE setMinConf NOTIFY overlayChanged)
public:
    explicit AppController(OnnxInfer* infer, FrameProvider* frames,
                           QObject* parent = nullptr);
    ~AppController() override;

    QString version() const { return QStringLiteral("0.4.0-ph28"); }
    QString status() const { return status_; }
    QString label() const { return label_; }
    double confidence() const { return confidence_; }
    double fps() const { return fps_; }
    QString device() const { return device_; }
    int objectCount() const { return objectCount_; }
    bool maskOk() const { return maskOk_; }
    int overlayMode() const { return overlayMode_; }
    double minConf() const { return minConf_; }

    Q_INVOKABLE QStringList probeCameras(int maxIndex = 5);
    Q_INVOKABLE void startSource(const QString& spec);
    Q_INVOKABLE void stop();
    Q_INVOKABLE void setOverlayMode(int m);
    Q_INVOKABLE void setMinConf(double v);
    Q_INVOKABLE bool snapshot(const QString& path);
    Q_INVOKABLE QStringList logMessages() const { return logs_; }

signals:
    void statusChanged();
    void frameStats();
    void overlayChanged();
    void frameReady();
    void logAdded(const QString& line);

private slots:
    void onFrameStats(const QString& label, double conf, int objects, bool maskOk,
                      double fps);
    void onLogLine(const QString& line);

private:
    void setStatus(const QString& s);
    void pushLog(const QString& s);

    OnnxInfer* infer_;
    FrameProvider* frames_;
    QThread* thread_ = nullptr;
    PipelineWorker* worker_ = nullptr;
    QString status_ = QStringLiteral("idle");
    QString label_ = QStringLiteral("-");
    double confidence_ = 0.0;
    double fps_ = 0.0;
    QString device_ = QStringLiteral("?");
    int objectCount_ = 0;
    bool maskOk_ = true;
    int overlayMode_ = 0;
    double minConf_ = 0.30;
    QStringList logs_;
    mutable QMutex logMutex_;
};
