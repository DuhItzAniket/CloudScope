#pragma once
#include <QMutex>
#include <QImage>
#include <QQuickImageProvider>
#include <QQuickImageProvider>
#include <opencv2/core.hpp>

// Thread-safe latest-frame store + QML image provider ("image://frames/live").
class FrameProvider : public QQuickImageProvider {
public:
    FrameProvider();
    QImage requestImage(const QString& id, QSize* size,
                        const QSize& requestedSize) override;
    // Called from any thread. Converts BGR cv::Mat -> QImage (deep copy).
    void publish(const cv::Mat& bgr);

private:
    QMutex mutex_;
    QImage latest_;
};

QImage matToQImage(const cv::Mat& bgr);
