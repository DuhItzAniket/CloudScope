#include "AppController.h"

#include <QDateTime>
#include <QElapsedTimer>

#include <opencv2/imgcodecs.hpp>
#include <opencv2/imgproc.hpp>

#include "CameraSource.h"
#include "FrameProvider.h"

namespace {
// OpenCV prop ids (videoio) — applied live by the worker thread.
constexpr int kPropExposure = cv::CAP_PROP_EXPOSURE;
constexpr int kPropGain = cv::CAP_PROP_GAIN;
constexpr int kPropAutoExposure = cv::CAP_PROP_AUTO_EXPOSURE;

QVariantList lumaHist64(const cv::Mat& bgr)
{
    QVariantList out;
    out.reserve(64);
    if (bgr.empty()) {
        for (int i = 0; i < 64; ++i)
            out << 0.0;
        return out;
    }
    cv::Mat gray, small;
    cv::cvtColor(bgr, gray, cv::COLOR_BGR2GRAY);
    cv::resize(gray, small, cv::Size(160, 120), 0, 0, cv::INTER_AREA);
    int histSize = 64;
    float range[] = {0, 256};
    const float* ranges[] = {range};
    cv::Mat hist;
    cv::calcHist(&small, 1, nullptr, cv::Mat(), hist, 1, &histSize, ranges);
    double total = small.total();
    for (int i = 0; i < 64; ++i)
        out << (hist.at<float>(i) / total);
    return out;
}
}  // namespace

PipelineWorker::PipelineWorker(OnnxInfer* infer, FrameProvider* frames)
    : infer_(infer), frames_(frames)
{
}

void PipelineWorker::configure(const std::string& source, OverlayMode mode,
                               double minConf)
{
    source_ = source;
    mode_ = mode;
    minConf_ = minConf;
}

void PipelineWorker::requestCameraSettings(const CameraSettings& s)
{
    QMutexLocker lock(&settingsMutex_);
    pending_ = s;
    pending_.dirty = true;
}

void PipelineWorker::requestStop() { stop_.store(true); }

void PipelineWorker::run()
{
    CameraSource src;
    if (!src.open(source_)) {
        emit logLine(QString::fromStdString("cannot open source: " + source_));
        emit finished();
        return;
    }
    emit logLine(QString::fromStdString("source open: " + source_));
    QElapsedTimer fpsT;
    fpsT.start();
    int frames = 0;
    long long frameNo = 0;
    double fps = 0.0;
    cv::Mat frame;
    while (!stop_.load()) {
        {
            QMutexLocker lock(&settingsMutex_);
            if (pending_.dirty) {
                if (pending_.hasAutoExposure)
                    src.setProp(kPropAutoExposure, pending_.autoExposure);
                if (pending_.hasExposure)
                    src.setProp(kPropExposure, pending_.exposure);
                if (pending_.hasGain)
                    src.setProp(kPropGain, pending_.gain);
                if (pending_.hasResolution) {
                    src.setProp(cv::CAP_PROP_FRAME_WIDTH, pending_.width);
                    src.setProp(cv::CAP_PROP_FRAME_HEIGHT, pending_.height);
                }
                pending_.dirty = false;
            }
        }
        if (!src.read(frame)) {
            emit logLine(QStringLiteral("frame grab failed; stopping"));
            break;
        }
        ++frameNo;
        std::string label = "cloud";
        double conf = 0.0;
        std::vector<CloudObject> objs;
        bool maskOk = true;
        if (infer_ && infer_->hasClassifier()) {
            auto r = infer_->classify(frame);
            if (r.id >= 0) {
                label = r.label;
                conf = r.confidence;
            }
        }
        if (infer_ && infer_->hasSegmenter() && conf >= minConf_) {
            cv::Mat mask = infer_->segment(frame);
            if (!mask.empty()) {
                objs = CloudVision::extract(mask == 1);
                maskOk = CloudVision::sane(objs, frame.cols, frame.rows);
            }
        }
        cv::Mat ann = frame.clone();
        if (!objs.empty() && maskOk) {
            CloudVision::render(ann, objs, label, mode_);
        } else {
            std::string badge = label + (maskOk ? "" : " (mask uncertain)");
            cv::putText(ann, badge, cv::Point(20, 40), cv::FONT_HERSHEY_SIMPLEX,
                        1.2, cv::Scalar(255, 255, 255), 2);
        }
        {
            QMutexLocker lock(&lastMutex_);
            lastAnnotated_ = ann.clone();
        }
        frames_->publish(ann);
        ++frames;
        double el = fpsT.elapsed() / 1000.0;
        if (el >= 1.0) {
            fps = frames / el;
            frames = 0;
            fpsT.restart();
        }
        QVariantList hist;
        if (frameNo % 5 == 0)
            hist = lumaHist64(frame);
        emit frameStats(QString::fromStdString(label), conf,
                        static_cast<int>(objs.size()), maskOk, fps, frame.cols,
                        frame.rows, frameNo, hist);
    }
    emit finished();
}

AppController::AppController(OnnxInfer* infer, FrameProvider* frames,
                             QObject* parent)
    : QObject(parent), infer_(infer), frames_(frames)
{
    histogram_.reserve(64);
    for (int i = 0; i < 64; ++i)
        histogram_ << 0.0;
}

AppController::~AppController() { stop(); }

void AppController::setStatus(const QString& s)
{
    if (s == status_)
        return;
    status_ = s;
    emit statusChanged();
}

void AppController::pushLog(const QString& s)
{
    QString line = QDateTime::currentDateTime().toString("hh:mm:ss ") + s;
    {
        QMutexLocker lock(&logMutex_);
        logs_ << line;
        while (logs_.size() > 100)
            logs_.removeFirst();
    }
    emit logAdded(line);
}

QStringList AppController::probeCameras(int maxIndex)
{
    QStringList out;
    for (const auto& d : CameraSource::enumerateLocal(maxIndex)) {
        out << QString::fromStdString(d.name) +
                   (d.opened ? QStringLiteral(" [ok %1x%2]").arg(d.width).arg(d.height)
                             : QStringLiteral(" [n/a]"));
    }
    pushLog(QStringLiteral("camera probe done"));
    return out;
}

void AppController::startSource(const QString& spec)
{
    stop();
    if (!infer_ || (!infer_->hasClassifier() && !infer_->hasSegmenter())) {
        pushLog(QStringLiteral("no models loaded — cannot start"));
        setStatus(QStringLiteral("error: no models"));
        return;
    }
    thread_ = new QThread(this);
    worker_ = new PipelineWorker(infer_, frames_);
    worker_->configure(spec.toStdString(),
                       static_cast<OverlayMode>(overlayMode_), minConf_);
    {
        QMutexLocker lock(&stagedMutex_);
        if (staged_.dirty) {
            worker_->requestCameraSettings(staged_);
            staged_.dirty = false;
        }
    }
    worker_->moveToThread(thread_);
    connect(thread_, &QThread::started, worker_, &PipelineWorker::run);
    connect(worker_, &PipelineWorker::finished, thread_, &QThread::quit);
    connect(worker_, &PipelineWorker::finished, worker_, &QObject::deleteLater);
    connect(thread_, &QThread::finished, thread_, &QObject::deleteLater);
    connect(worker_, &PipelineWorker::frameStats, this,
            &AppController::onFrameStats, Qt::QueuedConnection);
    connect(worker_, &PipelineWorker::logLine, this, &AppController::onLogLine,
            Qt::QueuedConnection);
    thread_->start();
    setStatus(QStringLiteral("running: ") + spec);
    pushLog(QStringLiteral("started ") + spec);
}

void AppController::stop()
{
    if (worker_)
        worker_->requestStop();
    if (thread_) {
        thread_->quit();
        thread_->wait(5000);
        thread_ = nullptr;
        worker_ = nullptr;
    }
    setStatus(QStringLiteral("idle"));
}

void AppController::setOverlayMode(int m)
{
    if (m < 0 || m > 2 || m == overlayMode_)
        return;
    overlayMode_ = m;
    emit overlayChanged();
    pushLog(QStringLiteral("overlay mode=%1").arg(m));
}

void AppController::setMinConf(double v)
{
    v = qBound(0.0, v, 1.0);
    if (qFuzzyCompare(v + 1.0, minConf_ + 1.0))
        return;
    minConf_ = v;
    emit overlayChanged();
}

void AppController::setExposure(double v)
{
    QMutexLocker lock(&stagedMutex_);
    staged_.hasExposure = true;
    staged_.exposure = v;
    staged_.dirty = true;
    lock.unlock();
    if (worker_)
        worker_->requestCameraSettings(staged_);
    pushLog(QStringLiteral("exposure=%1").arg(v));
}

void AppController::setGain(double v)
{
    QMutexLocker lock(&stagedMutex_);
    staged_.hasGain = true;
    staged_.gain = v;
    staged_.dirty = true;
    lock.unlock();
    if (worker_)
        worker_->requestCameraSettings(staged_);
    pushLog(QStringLiteral("gain=%1").arg(v));
}

void AppController::setAutoExposure(bool on)
{
    QMutexLocker lock(&stagedMutex_);
    staged_.hasAutoExposure = true;
    staged_.autoExposure = on ? 1.0 : 0.0;
    staged_.dirty = true;
    lock.unlock();
    if (worker_)
        worker_->requestCameraSettings(staged_);
    pushLog(QStringLiteral("auto-exposure %1").arg(on ? "on" : "off"));
}

void AppController::setResolution(int w, int h)
{
    {
        QMutexLocker lock(&stagedMutex_);
        staged_.hasResolution = true;
        staged_.width = w;
        staged_.height = h;
        staged_.dirty = true;
    }
    if (worker_)
        worker_->requestCameraSettings(staged_);
    pushLog(QStringLiteral("resolution=%1x%2").arg(w).arg(h));
}

bool AppController::snapshot(const QString& path)
{
    if (!worker_)
        return false;
    cv::Mat img;
    {
        QMutexLocker lock(&worker_->lastMutex_);
        if (worker_->lastAnnotated_.empty())
            return false;
        img = worker_->lastAnnotated_.clone();
    }
    bool ok = cv::imwrite(path.toStdString(), img);
    pushLog(ok ? QStringLiteral("snapshot ") + path : QStringLiteral("snapshot FAILED"));
    return ok;
}

void AppController::onFrameStats(const QString& label, double conf, int objects,
                                 bool maskOk, double fps, int fw, int fh,
                                 long long frameNo, const QVariantList& hist)
{
    label_ = label;
    confidence_ = conf;
    objectCount_ = objects;
    maskOk_ = maskOk;
    fps_ = fps;
    frameW_ = fw;
    frameH_ = fh;
    frameNo_ = frameNo;
    if (!hist.isEmpty()) {
        histogram_ = hist;
        emit histogramReady();
    }
    if (infer_)
        device_ = QString::fromStdString(infer_->backend());
    emit frameStats();
    emit frameReady();
}

void AppController::onLogLine(const QString& line) { pushLog(line); }
