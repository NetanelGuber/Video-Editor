#include <QtCore/QCoreApplication>
#include <QtWidgets/QWidget>
#include <iostream>
extern "C" {
#include <libavcodec/avcodec.h>
#include <libavformat/avformat.h>
#include <libavutil/avutil.h>
}
int main(int argc, char** argv) {
    QCoreApplication app(argc, argv);
    // Reference Widgets' linked metaobject without starting a UI or a window.
    std::cout << "Qt " << qVersion() << "; widget " << QWidget::staticMetaObject.className()
              << "; FFmpeg " << av_version_info() << '\n';
    std::cout << "libavcodec " << avcodec_version() << "; libavformat " << avformat_version() << '\n';
    return avcodec_find_decoder(AV_CODEC_ID_H264) && avcodec_find_decoder(AV_CODEC_ID_HEVC)
               && avcodec_find_decoder(AV_CODEC_ID_AAC) && avcodec_find_decoder(AV_CODEC_ID_FLAC)
               && avcodec_find_decoder(AV_CODEC_ID_ALAC) ? 0 : 1;
}
