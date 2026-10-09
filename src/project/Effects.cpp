#include "Effects.h"
#include <algorithm>
#include <cmath>
#include <limits>
#include <QRegularExpression>
#include <QFile>
#include <QJsonArray>

namespace editor::project {
QStringList effectTypes() { return {"transform", "crop", "opacity", "composite", "videoFade", "color", "lut", "speed", "mask", "chromaKey"}; }
QVector<EffectParameter> effectParameters(const QString& type) {
    if (type == "transform") return {{"x", "Horizontal offset (canvas fraction)", 0, -2, 2}, {"y", "Vertical offset (canvas fraction)", 0, -2, 2},
        {"scaleX", "Horizontal scale", 1, 0.01, 8}, {"scaleY", "Vertical scale", 1, 0.01, 8}, {"rotation", "Rotation (degrees)", 0, -3600, 3600}};
    if (type == "crop") return {{"left", "Crop left (0–1)", 0, 0, 1}, {"right", "Crop right (0–1)", 0, 0, 1},
        {"top", "Crop top (0–1)", 0, 0, 1}, {"bottom", "Crop bottom (0–1)", 0, 0, 1}};
    if (type == "opacity") return {{"value", "Opacity (0–1)", 1, 0, 1}};
    if (type == "color") return {{"exposure", "Exposure (stops)", 0, -5, 5}, {"contrast", "Contrast", 1, 0, 4},
        {"temperature", "Temperature (cool to warm)", 0, -1, 1}, {"tint", "Tint (green to magenta)", 0, -1, 1}, {"saturation", "Saturation", 1, 0, 4}};
    if (type == "lut") return {{"amount", "LUT amount", 1, 0, 1}};
    if (type == "speed") return {{"rate", "Speed (0.125–8 times; audio preserves pitch)", 1, 0.125, 8}};
    if (type == "mask") return {{"left", "Left (canvas fraction)", 0, 0, 1}, {"top", "Top (canvas fraction)", 0, 0, 1},
        {"right", "Right (canvas fraction)", 1, 0, 1}, {"bottom", "Bottom (canvas fraction)", 1, 0, 1}, {"feather", "Inside feather (canvas fraction)", 0, 0, 0.5}};
    if (type == "chromaKey") return {{"tolerance", "Key tolerance (RGB distance)", 0.1, 0, 1}, {"softness", "Key edge softness", 0.1, 0, 1}, {"spill", "Spill suppression", 0, 0, 1}};
    return {};
}
bool supportedEffect(const Effect& e) { return e.version == 1 && effectTypes().contains(e.type); }
Effect defaultEffect(const QString& type, const QString& id) {
    Effect e; e.id = id.isEmpty() ? newId() : id; e.type = type;
    for (const auto& p : effectParameters(type)) e.parameters[p.name] = p.initial;
    if (type == "composite") e.parameters["mode"] = "sourceOver";
    if (type == "lut") { e.parameters["path"] = ""; e.parameters["size"] = 0; e.parameters["table"] = QJsonArray{}; }
    if (type == "mask") e.parameters["invert"] = false;
    if (type == "chromaKey") e.parameters["color"] = "#ff00ff00";
    if (type == "videoFade") e.parameters = {{"inFrames", "0"}, {"outFrames", "0"}, {"inMode", "opacity"}, {"outMode", "opacity"},
        {"inColor", "#ff000000"}, {"outColor", "#ff000000"}, {"inCurve", "linear"}, {"outCurve", "linear"}};
    return e;
}
double curveValue(const QString& curve, double t) {
    t = std::clamp(t, 0.0, 1.0);
    if (curve == "hold") return t >= 1 ? 1 : 0;
    if (curve == "eased") return t * t * (3 - 2 * t);
    return t;
}
QString validateEffect(const Effect& e, qint64 duration) {
    // Unknown effect/version payloads stay editable and lossless; rendering reports bypass/rejection.
    if (!supportedEffect(e)) return {};
    for (const auto& p : effectParameters(e.type)) if (e.parameters.contains(p.name)) {
        const auto v = e.parameters[p.name]; const double n = v.toDouble();
        if (!v.isDouble() || !std::isfinite(n) || n < p.low || n > p.high) return p.name + ": value outside supported range";
    }
    for (auto it = e.keyframes.begin(); it != e.keyframes.end(); ++it) {
        const auto specs = effectParameters(e.type);
        const auto spec = std::find_if(specs.begin(), specs.end(), [&](const auto& p) { return p.name == it.key(); });
        if (spec == specs.end()) return "keyframes: unknown or non-animatable parameter " + it.key();
        if (it->size() > 10000) return "keyframes: limit is 10000 per parameter";
        if (e.type == "speed" && it->size() > 256) return "speed: limit is 256 rate keys";
        qint64 previous = -1;
        for (const auto& k : *it) {
            if (k.frame <= previous || k.frame < 0) return "keyframes: frames must be nonnegative and strictly increasing";
            if (!std::isfinite(k.value) || k.value < spec->low || k.value > spec->high) return "keyframes: value outside supported range";
            if (!QStringList{"hold", "linear", "eased"}.contains(k.curve)) return "keyframes: unknown interpolation curve";
            previous = k.frame;
        }
    }
    if (e.type == "speed" && (e.timeOffsetFrames < -10000000 || e.timeOffsetFrames > 10000000)) return "speed: content offset exceeds 10 million frames";
    if (e.type == "composite" && e.parameters.contains("mode") && (!e.parameters["mode"].isString() || !QStringList{"sourceOver", "multiply", "screen"}.contains(e.parameters["mode"].toString()))) return "mode: unsupported blend mode";
    if (e.type == "mask" && e.parameters.contains("invert") && !e.parameters["invert"].isBool()) return "invert: expected a boolean";
    if (e.type == "chromaKey" && !QRegularExpression("^#[fF]{2}[0-9a-fA-F]{6}$").match(e.parameters.value("color").toString()).hasMatch()) return "color: use opaque #ffRRGGBB";
    if (e.type == "lut") {
        const auto n = e.parameters.value("size").toInt(-1); const auto table = e.parameters.value("table").toArray();
        if (!e.parameters.value("path").isString() || !e.parameters.value("size").isDouble() || e.parameters.value("size").toDouble() != n || !e.parameters.value("table").isArray()) return "LUT: invalid embedded table";
        if ((n != 0 && (n < 2 || n > 33)) || table.size() != n * n * n * 3) return "LUT: supports 2–33 point 3D tables";
        for (const auto& v : table) if (!v.isDouble() || !std::isfinite(v.toDouble()) || v.toDouble() < 0 || v.toDouble() > 1) return "LUT: values must be finite normalized SDR RGB";
    }
    if (e.type == "videoFade") for (const auto& edge : {QString("in"), QString("out")}) {
        bool ok = false; const auto text = e.parameters.value(edge + "Frames").toString("0"); const auto n = text.toLongLong(&ok);
        if ((e.parameters.contains(edge + "Frames") && !e.parameters[edge + "Frames"].isString()) || !ok || QString::number(n) != text || n < 0 || n > duration) return edge + "Frames: fade must fit within clip duration";
        for (const auto& field : {QString("Mode"), QString("Curve"), QString("Color")}) if (e.parameters.contains(edge + field) && !e.parameters[edge + field].isString()) return edge + field + ": expected a string";
        if (!QStringList{"opacity", "color"}.contains(e.parameters.value(edge + "Mode").toString("opacity"))) return edge + "Mode: expected opacity or color";
        if (!QStringList{"hold", "linear", "eased"}.contains(e.parameters.value(edge + "Curve").toString("linear"))) return edge + "Curve: unknown curve";
        if (!QRegularExpression("^#[fF]{2}[0-9a-fA-F]{6}$").match(e.parameters.value(edge + "Color").toString("#ff000000")).hasMatch()) return edge + "Color: use an opaque #ffRRGGBB solid color";
    }
    if (e.timeOffsetFrames > 0 && duration - 1 > std::numeric_limits<qint64>::max() - e.timeOffsetFrames) return "timeOffsetFrames: animation time overflows";
    return {};
}
double effectValue(const Effect& e, const QString& parameter, qint64 frame) {
    double fallback = 0;
    for (const auto& p : effectParameters(e.type)) if (p.name == parameter) fallback = p.initial;
    const auto& keys = e.keyframes.value(parameter);
    if (keys.isEmpty()) return e.parameters.value(parameter).toDouble(fallback);
    const auto time = frame + e.timeOffsetFrames; // Validated for every visible frame.
    if (time <= keys.first().frame) return keys.first().value;
    if (time >= keys.last().frame) return keys.last().value;
    auto right = std::upper_bound(keys.begin(), keys.end(), time, [](qint64 f, const auto& k) { return f < k.frame; });
    const auto& left = *(right - 1);
    const double t = static_cast<double>(time - left.frame) / static_cast<double>(right->frame - left.frame);
    return left.value + (right->value - left.value) * curveValue(left.curve, t);
}
QString trimEffects(Clip& c, qint64 offset, qint64 duration) {
    for (auto& e : c.effects) {
        if ((offset > 0 && e.timeOffsetFrames > std::numeric_limits<qint64>::max() - offset) ||
            (offset < 0 && e.timeOffsetFrames < std::numeric_limits<qint64>::min() - offset)) return "Effect content origin exceeds supported time range.";
        // Legacy effects with no animation need no origin field update.
        if (!e.keyframes.isEmpty() || e.type == "speed") e.timeOffsetFrames += offset;
        if (supportedEffect(e) && e.type == "videoFade") for (const auto& edge : {QString("inFrames"), QString("outFrames")})
            e.parameters[edge] = QString::number(std::min(duration, e.parameters.value(edge).toString("0").toLongLong()));
    }
    return {};
}
QString splitEffects(Clip& left, Clip& right, qint64 offset) {
    if (const auto error = trimEffects(right, offset, right.durationFrames); !error.isEmpty()) return error;
    if (const auto error = trimEffects(left, 0, left.durationFrames); !error.isEmpty()) return error;
    for (auto& e : left.effects) if (supportedEffect(e) && e.type == "videoFade") e.parameters["outFrames"] = "0";
    for (auto& e : right.effects) if (supportedEffect(e) && e.type == "videoFade") e.parameters["inFrames"] = "0";
    return {};
}
const Effect* speedEffect(const Clip& c) {
    for (const auto& e : c.effects) if (e.enabled && supportedEffect(e) && e.type == "speed") return &e;
    return nullptr;
}
namespace {
long double primitive(const QString& curve, long double t) {
    if (curve == "hold") return 0;
    if (curve == "eased") return t * t * t - t * t * t * t / 2;
    return t * t / 2;
}
long double integrate(const Effect& e, long double from, long double to) {
    if (from > to) return -integrate(e, to, from);
    const auto keys = e.keyframes.value("rate");
    if (keys.isEmpty()) return (to - from) * e.parameters.value("rate").toDouble(1);
    long double sum = 0;
    if (from < keys.first().frame) { const auto stop = std::min(to, static_cast<long double>(keys.first().frame)); sum += (stop - from) * keys.first().value; from = stop; }
    for (qsizetype i = 1; i < keys.size() && from < to; ++i) {
        const auto& a = keys[i - 1]; const auto& b = keys[i];
        const auto begin = std::max(from, static_cast<long double>(a.frame)), stop = std::min(to, static_cast<long double>(b.frame));
        if (stop <= begin) continue;
        const long double span = b.frame - a.frame;
        sum += a.value * (stop - begin) + (b.value - a.value) * span * (primitive(a.curve, (stop - a.frame) / span) - primitive(a.curve, (begin - a.frame) / span));
        from = stop;
    }
    if (from < to) sum += (to - from) * keys.last().value;
    return sum;
}
}
long double speedIntegral(const Clip& c, long double frame) {
    const auto* e = speedEffect(c); return e ? integrate(*e, e->timeOffsetFrames, e->timeOffsetFrames + frame) : frame;
}
long double sourceFraction(const Clip& c, long double frame) { return speedIntegral(c, frame) / speedIntegral(c, c.durationFrames); }
long double outputFrameAtFraction(const Clip& c, long double fraction) {
    long double lo = 0, hi = c.durationFrames;
    const auto target = std::clamp(fraction, 0.0L, 1.0L) * speedIntegral(c, c.durationFrames);
    for (int i = 0; i < 56; ++i) { const auto mid = (lo + hi) / 2; if (speedIntegral(c, mid) < target) lo = mid; else hi = mid; }
    return (lo + hi) / 2;
}
std::optional<qint64> speedDuration(const Clip& c, Rational fps, Rational base) {
    const long double natural = static_cast<long double>(c.sourceDurationTicks) * base.numerator * fps.numerator / base.denominator / fps.denominator;
    if (!(natural > 0) || natural > 1250000) return {}; // At most 10 million output frames at minimum speed.
    qint64 lo = 1, hi = static_cast<qint64>(std::ceil(natural * 8)) + 1;
    while (lo < hi) { const auto mid = lo + (hi - lo) / 2; if (speedIntegral(c, mid) + 1e-9L < natural) lo = mid + 1; else hi = mid; }
    return lo;
}
QString importCube(Effect& e, const QString& path) {
    QFile file(path); if (!file.open(QIODevice::ReadOnly)) return "Cannot open LUT: " + file.errorString();
    if (file.size() > 4 * 1024 * 1024) return "LUT exceeds 4 MiB import limit";
    int size = 0; QJsonArray values;
    for (const auto& raw : QString::fromUtf8(file.readAll()).split('\n')) {
        const auto line = raw.section('#', 0, 0).trimmed(); if (line.isEmpty()) continue;
        const auto words = line.split(QRegularExpression("\\s+"), Qt::SkipEmptyParts);
        if (words[0] == "TITLE") continue;
        if (words[0] == "LUT_3D_SIZE") { bool ok = false; if (size || words.size() != 2) return "Invalid LUT size header"; size = words[1].toInt(&ok); if (!ok || size < 2 || size > 33) return "LUT size must be 2–33"; continue; }
        if (words[0] == "DOMAIN_MIN" || words[0] == "DOMAIN_MAX") {
            const auto expected = words[0] == "DOMAIN_MIN" ? 0.0 : 1.0;
            if (words.size() != 4) return "Invalid LUT domain";
            for (int i = 1; i < 4; ++i) { bool ok = false; if (words[i].toDouble(&ok) != expected || !ok) return "Only normalized 0–1 LUT domains are supported"; }
            continue;
        }
        if (!size || words.size() != 3 || values.size() >= size * size * size * 3) return "Unsupported LUT directive or table length";
        for (const auto& word : words) { bool ok = false; const auto n = word.toDouble(&ok); if (!ok || !std::isfinite(n) || n < 0 || n > 1) return "LUT values must be normalized finite RGB"; values.append(n); }
    }
    if (!size || values.size() != size * size * size * 3) return "Incomplete LUT table";
    e.parameters["path"] = path; e.parameters["size"] = size; e.parameters["table"] = values; return {};
}
}
