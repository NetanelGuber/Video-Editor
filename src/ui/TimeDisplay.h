#pragma once
#include "project/Project.h"
#include <QLineEdit>
#include <QRegularExpression>
#include <QRegularExpressionValidator>

namespace editor {
enum class TimeDisplay { Frames, Seconds };
struct TimeFormat {
    project::Rational rate{30, 1};
    TimeDisplay display = TimeDisplay::Frames;
    bool seconds() const { return display == TimeDisplay::Seconds; }
    QString value(qint64 frames, int decimals = 3) const {
        return seconds() ? QString::number(static_cast<double>(frames) * rate.denominator / rate.numerator, 'f', decimals)
                         : QString::number(frames);
    }
    QString inputValue(qint64 frames) const {
        auto text = value(frames, 9);
        if (seconds()) { while (text.endsWith('0')) text.chop(1); if (text.endsWith('.')) text.chop(1); }
        return text;
    }
    QString text(qint64 frames) const { return value(frames) + (seconds() ? " s" : " frames"); }
    QString caption(const QString& name) const { return name + (seconds() ? " (seconds)" : " (sequence frames)"); }
    void initialize(QLineEdit* field, qint64 frames) const {
        const auto text = inputValue(frames);
        field->setText(text);
        field->setProperty("timeOriginalText", text); field->setProperty("timeOriginalFrames", frames);
        field->setValidator(new QRegularExpressionValidator(QRegularExpression(seconds() ? "[0-9]{1,19}(\\.[0-9]{1,9})?" : "[0-9]{1,19}"), field));
        if (seconds()) field->setToolTip("Seconds are rounded to the nearest sequence frame. Unchanged values retain their exact timing.");
    }
    bool parse(const QString& text, qint64& frames, qint64 minimum = 0) const {
        const QRegularExpression pattern(seconds() ? "^[0-9]{1,19}(\\.[0-9]{1,9})?$" : "^[0-9]{1,19}$");
        if (!pattern.match(text).hasMatch()) return false;
        bool ok = false;
        if (!seconds()) { frames = text.toLongLong(&ok); return ok && frames >= minimum; }
        // Parse decimal seconds as an exact rational instead of multiplying a floating point value.
        const auto dot = text.indexOf('.');
        const auto digits = dot < 0 ? 0 : text.size() - dot - 1;
        qint64 scale = 1; for (qsizetype i = 0; i < digits; ++i) scale *= 10;
        auto integer = text; integer.remove('.'); const auto ticks = integer.toLongLong(&ok);
        if (!ok) return false;
        const auto result = project::ticksToFrames(ticks, {1, scale}, rate, project::Rounding::Nearest);
        if (!result || *result < minimum) return false;
        frames = *result; return true;
    }
    bool read(QLineEdit* field, qint64& frames, qint64 minimum = 0) const {
        const auto text = field->text();
        if (text == field->property("timeOriginalText").toString()) {
            frames = field->property("timeOriginalFrames").toLongLong(); return frames >= minimum;
        }
        return field->hasAcceptableInput() && parse(text, frames, minimum);
    }
};
}
