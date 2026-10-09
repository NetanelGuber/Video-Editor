#pragma once
#include <QWidget>
#include <QPainter>
#include <QVector>

class WaveformWidget final : public QWidget {
    Q_OBJECT
public:
    explicit WaveformWidget(QWidget* parent = nullptr) : QWidget(parent) {
        setObjectName(QStringLiteral("audioWaveform")); setMinimumHeight(64);
        setMaximumHeight(90); setAccessibleName(tr("Audio peak waveform"));
    }
    void setPeaks(QVector<float> values) { peaks_ = std::move(values); update(); }
    const QVector<float>& peaks() const { return peaks_; }
protected:
    void paintEvent(QPaintEvent*) override {
        QPainter painter(this); painter.fillRect(rect(), QColor("#14171c"));
        painter.setPen(QColor("#505863")); painter.drawLine(0, height()/2, width(), height()/2);
        painter.setPen(QColor("#65bed8"));
        for (qsizetype i = 0; i < peaks_.size(); ++i) {
            const int x = static_cast<int>(i * width() / peaks_.size());
            const int amplitude = static_cast<int>(peaks_[i] * (height()/2 - 3));
            painter.drawLine(x, height()/2 - amplitude, x, height()/2 + amplitude);
        }
    }
private:
    QVector<float> peaks_;
};
