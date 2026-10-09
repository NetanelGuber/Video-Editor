#pragma once
#include "project/Project.h"
struct AVCodecContext;
struct AVFrame;
class QImage;
namespace editor::exporting {
// FFmpeg types remain private to the export implementation.
void configureVideoEncoder(AVCodecContext* context, const project::ExportSettings& settings, const QString& encoder);
void configureAudioEncoder(AVCodecContext* context, const project::ExportSettings& settings);
void prepareVideoFrame(AVCodecContext* context, AVFrame* frame);
void fillVideoFrame(AVCodecContext* context, AVFrame* frame, const QImage& image);
int sendEncoderFrame(AVCodecContext* context, AVFrame* frame);
void applyEncoderOptions(AVCodecContext* context, const project::ExportSettings& settings);
void checkAv(int result, const QString& operation);
}
