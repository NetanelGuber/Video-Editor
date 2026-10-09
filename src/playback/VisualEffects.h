#pragma once
#include "project/Effects.h"
#include <QImage>
#include <QPainter>

namespace editor::playback {
struct VisualLayer { QImage image; QPainter::CompositionMode mode = QPainter::CompositionMode_SourceOver; };
VisualLayer applyEffects(QImage image, const project::Clip& clip, qint64 localFrame);
}
