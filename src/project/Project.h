#pragma once
#include <QJsonObject>
#include <QString>
#include <QStringList>
#include <QVector>
#include <QMap>
#include <optional>

namespace editor::project {
inline constexpr int SchemaVersion = 11;
inline constexpr qint64 MaxDocumentBytes = 16 * 1024 * 1024;

struct Rational {
    qint64 numerator = 30;
    qint64 denominator = 1;
    bool operator==(const Rational&) const = default;
};
enum class Rounding { Floor, Ceil, Nearest }; // Nearest ties round upward, for nonnegative time.
std::optional<qint64> framesToSamples(qint64 frames, Rational fps, int sampleRate, Rounding rounding);
std::optional<qint64> samplesToFrames(qint64 samples, Rational fps, int sampleRate, Rounding rounding);
std::optional<qint64> framesToTicks(qint64 frames, Rational fps, Rational timeBase, Rounding rounding);
std::optional<qint64> ticksToFrames(qint64 ticks, Rational timeBase, Rational fps, Rounding rounding);
// Exact integer ratio, with a 128-bit intermediate. Inputs are nonnegative; denominator is positive.
std::optional<qint64> scaleTime(qint64 value, qint64 numerator, qint64 denominator, Rounding rounding);

struct Stream {
    int index = 0;
    QString kind = QStringLiteral("video"), codec;
    Rational timeBase{1, 90000}, frameRate{30, 1}; // Audio frameRate is 0/1.
    qint64 startTicks = 0, durationTicks = 0;
    int width = 0, height = 0, bitDepth = 0, sampleRate = 0, channels = 0, rotation = 0;
    bool variableFrameRate = false;
    bool operator==(const Stream&) const = default;
};
struct Media {
    QString id, name, path, container;
    qint64 sizeBytes = 0;
    QVector<Stream> streams;
    bool operator==(const Media&) const = default;
};
struct Title {
    QString id, text, fontFamily = QStringLiteral("Segoe UI"), alignment = QStringLiteral("center");
    int fontSize = 48;
    QString color = QStringLiteral("#ffffffff"), background = QStringLiteral("#00000000");
    double x = 0.5, y = 0.5;
    bool shadow = false;
    bool operator==(const Title&) const = default;
};
struct Keyframe {
    qint64 frame = 0; // Sequence frames relative to the original clip content origin.
    double value = 0;
    QString curve = QStringLiteral("linear"); // Outgoing segment: hold, linear, eased (smoothstep).
    bool operator==(const Keyframe&) const = default;
};
struct AudioAutomation {
    qint64 timeOffsetFrames = 0; // Clips retain content-relative keys through trim/split; tracks/buses use zero.
    QVector<Keyframe> volume; // Linear amplitude multiplier, 0–16.
    QVector<Keyframe> pan; // Stereo balance, -1 (left) to +1 (right).
    bool operator==(const AudioAutomation&) const = default;
};
struct Effect {
    QString id, type;
    int version = 1;
    bool enabled = true;
    QJsonObject parameters; // Owned by each versioned effect implementation; retained verbatim.
    qint64 timeOffsetFrames = 0; // Added to clip-local time; trim/split preserve content animation.
    QMap<QString, QVector<Keyframe>> keyframes;
    bool operator==(const Effect&) const = default;
};
struct Clip {
    QString id, name, kind = QStringLiteral("video"), mediaId, titleId;
    int streamIndex = 0;
    qint64 startFrame = 0, durationFrames = 1;
    qint64 sourceInTicks = 0, sourceDurationTicks = 0; // Selected stream timeBase, relative to startTicks.
    double gain = 1.0;
    bool muted = false;
    qint64 fadeInFrames = 0, fadeOutFrames = 0; // Clip-local, linear amplitude ramps in sequence frames.
    QVector<Effect> effects; // Vector order is render order.
    double pan = 0;
    AudioAutomation audioAutomation;
    QString sequenceId; // Optional nested source. Source ticks are child sequence frames, at 1x.
    bool operator==(const Clip&) const = default;
};
struct Track {
    QString id, name, kind = QStringLiteral("video");
    bool enabled = true, locked = false, muted = false, solo = false;
    double gain = 1.0;
    QVector<Clip> clips;
    QString audioBusId; // Empty routes directly to the master bus.
    double pan = 0;
    AudioAutomation audioAutomation; // Key times are absolute sequence frames.
    bool operator==(const Track&) const = default;
};
struct AudioBus {
    QString id, name;
    double gain = 1.0, pan = 0;
    bool muted = false, solo = false;
    bool operator==(const AudioBus&) const = default;
};
struct CameraCut {
    qint64 frame = 0;
    QString trackId;
    bool operator==(const CameraCut&) const = default;
};
struct Multicam {
    QStringList cameraTrackIds; // 2–4 synchronized, full-length video tracks.
    QVector<CameraCut> cuts; // Sorted hold switches; first cut is at zero.
    bool operator==(const Multicam&) const = default;
};
struct Sequence {
    QString id, name;
    Rational frameRate{30, 1};
    qint64 durationFrames = 0;
    int width = 1920, height = 1080, sampleRate = 48000;
    QVector<Track> tracks;
    QVector<AudioBus> audioBuses;
    std::optional<Multicam> multicam;
    QString primaryVideoMediaId;
    int primaryVideoStreamIndex = -1;
    bool operator==(const Sequence&) const = default;
};
struct ExportSettings {
    QString container = QStringLiteral("mp4"), videoCodec = QStringLiteral("h264"), audioCodec = QStringLiteral("aac");
    QString preset = QStringLiteral("medium"), outputPath;
    int width = 1920, height = 1080, sampleRate = 48000, channels = 2;
    int audioBitrate = 192000;
    double quality = 20;
    Rational frameRate{30, 1};
    bool frameRateCustomized = false;
    bool audioEnabled = true;
    QString compatibilityProfile = QStringLiteral("compatible-mp4");
    QString videoEncoder = QStringLiteral("software"), pixelFormat = QStringLiteral("auto");
    QString qualityMode = QStringLiteral("native"), audioEncoder = QStringLiteral("aac");
    QMap<QString, QString> encoderOptions;
    bool operator==(const ExportSettings&) const = default;
};
struct Project {
    QString id, name, activeSequenceId;
    QVector<Media> media;
    QVector<Title> titles;
    QVector<Sequence> sequences;
    ExportSettings exportSettings;
    bool operator==(const Project&) const = default;
};
QString newId();
Project newProject(const QString& name = QStringLiteral("Untitled"));
QByteArray serialize(const Project& project);
struct LoadResult {
    std::optional<Project> project;
    QString error;
    int migratedFrom = 0;
    QStringList offlineMediaIds; // Runtime state, never serialized or silently relinked.
    explicit operator bool() const { return project.has_value(); }
};
LoadResult deserialize(const QByteArray& bytes);
QString validate(const Project& project); // Empty on success, otherwise a field-qualified error.
QString cameraAt(const Sequence& sequence, qint64 frame);
QStringList sequenceMediaIds(const Project& project, const QString& sequenceId);
}
