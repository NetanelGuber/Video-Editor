#include "VisualEffects.h"
#include <algorithm>
#include <array>
#include <cmath>
#include <QJsonArray>

namespace editor::playback {
VisualLayer applyEffects(QImage image, const project::Clip& clip, qint64 frame) {
    VisualLayer result{std::move(image)};
    for (const auto& e : clip.effects) {
        if (!e.enabled || !project::supportedEffect(e)) continue;
        auto value = [&](const char* key) { return project::effectValue(e, key, frame); };
        if (e.type == "color" || e.type == "lut" || e.type == "mask" || e.type == "chromaKey") {
            // Work on straight, full-range encoded SDR RGB; retain premultiplied alpha at the boundary.
            auto pixels = result.image.convertToFormat(QImage::Format_ARGB32);
            const auto table = e.parameters.value("table").toArray(); const int n = e.parameters.value("size").toInt();
            const QColor key(e.parameters.value("color").toString("#ff00ff00"));
            const double exposure = std::exp2(value("exposure")), contrast = value("contrast"), saturation = value("saturation");
            const double warm = value("temperature"), tint = value("tint");
            const std::array<double, 3> balance{std::exp2(warm + tint * 0.5), std::exp2(-tint), std::exp2(-warm + tint * 0.5)};
            const double amount = value("amount");
            const double tolerance = value("tolerance"), softness = value("softness"), spill = value("spill");
            const double left = value("left"), right = value("right"), top = value("top"), bottom = value("bottom"), feather = value("feather");
            for (int y = 0; y < pixels.height(); ++y) {
                auto* row = reinterpret_cast<QRgb*>(pixels.scanLine(y));
                for (int x = 0; x < pixels.width(); ++x) {
                    const auto p = row[x]; std::array<double, 3> rgb{qRed(p) / 255.0, qGreen(p) / 255.0, qBlue(p) / 255.0}; double alpha = qAlpha(p) / 255.0;
                    if (e.type == "color") {
                        for (int channel = 0; channel < 3; ++channel) rgb[channel] = (rgb[channel] * exposure * balance[channel] - 0.5) * contrast + 0.5;
                        const auto luma = rgb[0] * 0.2126 + rgb[1] * 0.7152 + rgb[2] * 0.0722;
                        for (auto& channel : rgb) channel = luma + (channel - luma) * saturation;
                    } else if (e.type == "lut" && n >= 2) {
                        const auto original = rgb; std::array<int, 3> low; std::array<double, 3> f;
                        for (int channel = 0; channel < 3; ++channel) { const double pos = rgb[channel] * (n - 1); low[channel] = std::min(n - 2, static_cast<int>(pos)); f[channel] = pos - low[channel]; }
                        rgb = {0, 0, 0};
                        for (int b = 0; b < 2; ++b) for (int g = 0; g < 2; ++g) for (int r = 0; r < 2; ++r) {
                            const auto weight = (r ? f[0] : 1 - f[0]) * (g ? f[1] : 1 - f[1]) * (b ? f[2] : 1 - f[2]);
                            const auto index = ((low[2] + b) * n * n + (low[1] + g) * n + low[0] + r) * 3;
                            for (int channel = 0; channel < 3; ++channel) rgb[channel] += table[index + channel].toDouble() * weight;
                        }
                        for (int channel = 0; channel < 3; ++channel) rgb[channel] = original[channel] + (rgb[channel] - original[channel]) * amount;
                    } else if (e.type == "mask") {
                        const auto px = (x + 0.5) / pixels.width(), py = (y + 0.5) / pixels.height();
                        const auto distance = std::min({px - left, right - px, py - top, bottom - py});
                        double coverage = distance < 0 || right <= left || bottom <= top ? 0 : feather > 0 ? std::clamp(distance / feather, 0.0, 1.0) : 1;
                        if (e.parameters.value("invert").toBool()) coverage = 1 - coverage; alpha *= coverage;
                    } else if (e.type == "chromaKey") {
                        const std::array<double, 3> k{key.redF(), key.greenF(), key.blueF()}; double distance = 0;
                        for (int channel = 0; channel < 3; ++channel) distance += (rgb[channel] - k[channel]) * (rgb[channel] - k[channel]);
                        distance = std::sqrt(distance / 3);
                        const double coverage = softness > 0 ? std::clamp((distance - tolerance) / softness, 0.0, 1.0) : distance <= tolerance ? 0 : 1;
                        alpha *= coverage;
                        const int dominant = static_cast<int>(std::max_element(k.begin(), k.end()) - k.begin());
                        const auto others = std::max(rgb[(dominant + 1) % 3], rgb[(dominant + 2) % 3]);
                        rgb[dominant] -= std::max(0.0, rgb[dominant] - others) * spill * (1 - coverage);
                    }
                    auto byte = [](double v) { return static_cast<int>(std::lround(std::clamp(v, 0.0, 1.0) * 255)); };
                    row[x] = qRgba(byte(rgb[0]), byte(rgb[1]), byte(rgb[2]), byte(alpha));
                }
            }
            result.image = pixels.convertToFormat(QImage::Format_ARGB32_Premultiplied);
        } else if (e.type == "composite") {
            const auto mode = e.parameters["mode"].toString("sourceOver");
            result.mode = mode == "multiply" ? QPainter::CompositionMode_Multiply : mode == "screen" ? QPainter::CompositionMode_Screen : QPainter::CompositionMode_SourceOver;
        } else if (e.type == "transform") {
            QImage transformed(result.image.size(), QImage::Format_ARGB32_Premultiplied); transformed.fill(Qt::transparent);
            QPainter p(&transformed); p.setRenderHint(QPainter::SmoothPixmapTransform);
            const auto w = transformed.width(), h = transformed.height();
            p.translate(w * (0.5 + value("x")), h * (0.5 + value("y")));
            p.rotate(value("rotation")); p.scale(value("scaleX"), value("scaleY")); p.translate(-w * 0.5, -h * 0.5);
            p.drawImage(0, 0, result.image); p.end(); result.image = std::move(transformed);
        } else if (e.type == "crop") {
            const auto w = result.image.width(), h = result.image.height();
            const QRectF keep(w * value("left"), h * value("top"), w * std::max(0.0, 1 - value("left") - value("right")), h * std::max(0.0, 1 - value("top") - value("bottom")));
            QImage cropped(result.image.size(), QImage::Format_ARGB32_Premultiplied); cropped.fill(Qt::transparent);
            QPainter p(&cropped); p.setClipRect(keep); p.drawImage(0, 0, result.image); p.end(); result.image = std::move(cropped);
        } else if (e.type == "opacity") {
            QImage faded(result.image.size(), QImage::Format_ARGB32_Premultiplied); faded.fill(Qt::transparent);
            QPainter p(&faded); p.setOpacity(value("value")); p.drawImage(0, 0, result.image); p.end(); result.image = std::move(faded);
        } else if (e.type == "videoFade") {
            // Apply in then out. Opacity factors multiply; overlapping colors make out the final blend.
            for (const auto& edge : {QString("in"), QString("out")}) {
                const auto duration = e.parameters.value(edge + "Frames").toString("0").toLongLong();
                if (duration <= 0) continue;
                const auto distance = edge == "in" ? frame : clip.durationFrames - 1 - frame;
                const double progress = duration == 1 ? (distance == 0 ? 0 : 1) : static_cast<double>(distance) / static_cast<double>(duration - 1);
                const auto curve = e.parameters.value(edge + "Curve").toString("linear");
                const double weight = edge == "in" ? project::curveValue(curve, progress) : 1 - project::curveValue(curve, 1 - std::clamp(progress, 0.0, 1.0));
                if (e.parameters.value(edge + "Mode").toString("opacity") == "opacity") {
                    QImage faded(result.image.size(), QImage::Format_ARGB32_Premultiplied); faded.fill(Qt::transparent);
                    QPainter p(&faded); p.setOpacity(weight); p.drawImage(0, 0, result.image); p.end(); result.image = std::move(faded);
                } else {
                    QPainter p(&result.image); p.setOpacity(1 - weight); p.fillRect(result.image.rect(), QColor(e.parameters.value(edge + "Color").toString("#ff000000")));
                }
            }
        }
    }
    return result;
}
}
