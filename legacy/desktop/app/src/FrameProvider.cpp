#include "FrameProvider.h"
#include <opencv2/imgproc.hpp>

QImage matToQImage(const cv::Mat& bgr)
{
    if (bgr.empty())
        return QImage();
    cv::Mat rgb;
    if (bgr.channels() == 3)
        cv::cvtColor(bgr, rgb, cv::COLOR_BGR2RGB);
    else if (bgr.channels() == 1)
        cv::cvtColor(bgr, rgb, cv::COLOR_GRAY2RGB);
    else if (bgr.channels() == 4)
        cv::cvtColor(bgr, rgb, cv::COLOR_BGRA2RGB);
    else
        return QImage();
    return QImage(rgb.data, rgb.cols, rgb.rows,
                  static_cast<int>(rgb.step), QImage::Format_RGB888)
        .copy();
}

FrameProvider::FrameProvider()
    : QQuickImageProvider(QQmlImageProviderBase::Image)
{
}

QImage FrameProvider::requestImage(const QString&, QSize* size,
                                   const QSize& requestedSize)
{
    QMutexLocker lock(&mutex_);
    QImage out = latest_;
    if (size)
        *size = out.size();
    if (!out.isNull() && requestedSize.isValid())
        out = out.scaled(requestedSize, Qt::KeepAspectRatio, Qt::SmoothTransformation);
    return out;
}

void FrameProvider::publish(const cv::Mat& bgr)
{
    QImage img = matToQImage(bgr);
    if (img.isNull())
        return;
    QMutexLocker lock(&mutex_);
    latest_ = img;
}
