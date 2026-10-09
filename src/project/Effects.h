#pragma once
#include "Project.h"

namespace editor::project {
struct EffectParameter { QString name, label; double initial = 0, low = 0, high = 1; };
QStringList effectTypes();
QVector<EffectParameter> effectParameters(const QString& type);
bool supportedEffect(const Effect& effect);
Effect defaultEffect(const QString& type, const QString& id = {});
QString validateEffect(const Effect& effect, qint64 durationFrames);
double effectValue(const Effect& effect, const QString& parameter, qint64 localFrame);
double curveValue(const QString& curve, double progress);
// Checked content-origin shifts. Fades use local edges instead of the content origin.
QString trimEffects(Clip& clip, qint64 offset, qint64 duration);
QString splitEffects(Clip& left, Clip& right, qint64 offset);
const Effect* speedEffect(const Clip& clip);
// Integral of positive speed over output frames. Source endpoints remain exact integers.
long double speedIntegral(const Clip& clip, long double frame);
long double sourceFraction(const Clip& clip, long double frame);
long double outputFrameAtFraction(const Clip& clip, long double fraction);
std::optional<qint64> speedDuration(const Clip& clip, Rational fps, Rational sourceBase);
QString importCube(Effect& effect, const QString& path);
}
