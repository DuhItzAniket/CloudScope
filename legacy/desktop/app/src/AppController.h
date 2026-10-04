#pragma once
#include <QMutex>
#include <QObject>
#include <QString>
#include <QStringList>
#include <QThread>
#include <QImage>
#include <QVariantList>
#include <atomic>
#include <opencv2/core.hpp>

#include "OnnxInfer.h"
#include "CloudVision.h"

class FrameProvider;

// Pending camera knobs (applied by the worker on its own thread).
struct CameraSettings {
    bool dirty = false;
    bool hasExposure = false;
    double exposure = 0.0;
    bool hasGain = false;
    double gain = 0.0;
    bool hasAutoExposure = false;
    double autoExposure = 1.0;
    bool hasResolution = false;
    int width = 0;
    int height = 0;
};

// Capture+inference worker: read -> classify+segment -> render -> publish.
class PipelineWorker : public QObject {
    Q_OBJECT
public:
    PipelineWorker(OnnxInfer* infer, FrameProvider* frames);
    void configure(const std::string& source, OverlayMode mode, double minConf);
    void requestCameraSettings(const CameraSettings& s);

signals:
    void frameStats(const QString& label, double conf, int objects, bool maskOk,
                    double fps, int frameW, int frameH, long long frameNo,
                    const QVariantList& hist);
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
    QMutex settingsMutex_;
    CameraSettings pending_;
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
    Q_PROPERTY(int frameW READ frameW NOTIFY frameStats)
    Q_PROPERTY(int frameH READ frameH NOTIFY frameStats)
    Q_PROPERTY(long long frameNo READ frameNo NOTIFY frameStats)
    Q_PROPERTY(QVariantList histogram READ histogram NOTIFY histogramReady)
    Q_PROPERTY(int overlayMode READ overlayMode WRITE setOverlayMode NOTIFY overlayChanged)
    Q_PROPERTY(double minConf READ minConf WRITE setMinConf NOTIFY overlayChanged)
public:
    explicit AppController(OnnxInfer* infer, FrameProvider* frames,
                           QObject* parent = nullptr);
    ~AppController() override;

    QString version() const { return QStringLiteral("1.1.0"); }
    QString status() const { return status_; }
    QString label() const { return label_; }
    double confidence() const { return confidence_; }
    double fps() const { return fps_; }
    QString device() const { return device_; }
    int objectCount() const { return objectCount_; }
    bool maskOk() const { return maskOk_; }
    int frameW() const { return frameW_; }
    int frameH() const { return frameH_; }
    long long frameNo() const { return frameNo_; }
    QVariantList histogram() const { return histogram_; }
    int overlayMode() const { return overlayMode_; }
    double minConf() const { return minConf_; }

    Q_INVOKABLE QStringList probeCameras(int maxIndex = 5);
    Q_INVOKABLE void startSource(const QString& spec);
    Q_INVOKABLE void stop();
    Q_INVOKABLE void setOverlayMode(int m);
    Q_INVOKABLE void setMinConf(double v);
    Q_INVOKABLE bool snapshot(const QString& path);
    Q_INVOKABLE QStringList logMessages() const { return logs_; }
    // Camera knobs (applied live by the worker; ranges are camera-dependent).
    Q_INVOKABLE void setExposure(double v);
    Q_INVOKABLE void setGain(double v);
    Q_INVOKABLE void setAutoExposure(bool on);
    Q_INVOKABLE void setResolution(int w, int h);

signals:
    void statusChanged();
    void frameStats();
    void overlayChanged();
    void frameReady();
    void logAdded(const QString& line);
    void histogramReady();

private slots:
    void onFrameStats(const QString& label, double conf, int objects, bool maskOk,
                      double fps, int fw, int fh, long long frameNo,
                      const QVariantList& hist);
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
    int frameW_ = 0;
    int frameH_ = 0;
    long long frameNo_ = 0;
    QVariantList histogram_;
    int overlayMode_ = 0;
    double minConf_ = 0.30;
    CameraSettings staged_;
    QMutex stagedMutex_;
    QStringList logs_;
    mutable QMutex logMutex_;
};
