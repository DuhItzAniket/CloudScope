#include <QtCore/QCoreApplication>
#include <QtCore/QString>
#include <opencv2/core.hpp>
#include <onnxruntime_cxx_api.h>

#include <iostream>

int main(int argc, char** argv)
{
    QCoreApplication app(argc, argv);
    std::cout << "Qt " << QT_VERSION_STR << "\n";
    std::cout << "OpenCV " << CV_VERSION << "\n";
    std::cout << "ORT " << Ort::GetVersionString() << "\n";
    cv::Mat m = cv::Mat::zeros(4, 4, CV_8UC1);
    std::cout << "mat ok " << m.rows << "x" << m.cols << "\n";
    return 0;
}
