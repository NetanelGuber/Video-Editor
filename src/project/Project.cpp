#include "Project.h"
#include "Effects.h"
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonParseError>
#include <QRegularExpression>
#include <QSet>
#include <QUuid>
#include <cmath>
#include <algorithm>
#include <climits>
#include <limits>
#include <numeric>
#include <functional>

namespace editor::project {
namespace {
using Object = QJsonObject;
QString integer(qint64 value) { return QString::number(value); }
Object json(const Rational& r) { return {{"numerator", integer(r.numerator)}, {"denominator", integer(r.denominator)}}; }
Object json(const Stream&);
Object json(const Media&);
Object json(const Title&);
Object json(const Effect&);
Object json(const Keyframe&);
Object json(const AudioAutomation&);
Object json(const Clip&);
Object json(const Track&);
Object json(const AudioBus&);
Object json(const Sequence&);
template<class T> QJsonArray array(const QVector<T>& values) {
    QJsonArray result;
    for (const auto& value : values) result.append(json(value));
    return result;
}
Object json(const Stream& s) {
    return {{"index", s.index}, {"kind", s.kind}, {"codec", s.codec}, {"timeBase", json(s.timeBase)},
        {"frameRate", json(s.frameRate)}, {"startTicks", integer(s.startTicks)}, {"durationTicks", integer(s.durationTicks)},
        {"width", s.width}, {"height", s.height}, {"bitDepth", s.bitDepth}, {"sampleRate", s.sampleRate},
        {"channels", s.channels}, {"rotation", s.rotation}, {"variableFrameRate", s.variableFrameRate}};
}
Object json(const Media& m) {
    return {{"id", m.id}, {"name", m.name}, {"path", m.path}, {"container", m.container},
        {"sizeBytes", integer(m.sizeBytes)}, {"streams", array(m.streams)}};
}
Object json(const Title& t) {
    return {{"id", t.id}, {"text", t.text}, {"fontFamily", t.fontFamily}, {"fontSize", t.fontSize},
        {"alignment", t.alignment}, {"color", t.color}, {"background", t.background},
        {"x", t.x}, {"y", t.y}, {"shadow", t.shadow}};
}
Object json(const Effect& e) {
    Object keys;
    for (auto it = e.keyframes.begin(); it != e.keyframes.end(); ++it) {
        QJsonArray a;
        for (const auto& k : *it) a.append(Object{{"frame", integer(k.frame)}, {"value", k.value}, {"curve", k.curve}});
        keys[it.key()] = a;
    }
    return {{"id", e.id}, {"type", e.type}, {"version", e.version}, {"enabled", e.enabled}, {"parameters", e.parameters},
        {"timeOffsetFrames", integer(e.timeOffsetFrames)}, {"keyframes", keys}};
}
Object json(const Keyframe& k) { return {{"frame", integer(k.frame)}, {"value", k.value}, {"curve", k.curve}}; }
Object json(const AudioAutomation& a) {
    return {{"timeOffsetFrames", integer(a.timeOffsetFrames)}, {"volume", array(a.volume)}, {"pan", array(a.pan)}};
}
Object json(const Clip& c) {
    Object result{{"id", c.id}, {"name", c.name}, {"kind", c.kind}, {"mediaId", c.mediaId}, {"titleId", c.titleId},
        {"streamIndex", c.streamIndex}, {"startFrame", integer(c.startFrame)}, {"durationFrames", integer(c.durationFrames)},
        {"sourceInTicks", integer(c.sourceInTicks)}, {"sourceDurationTicks", integer(c.sourceDurationTicks)},
        {"gain", c.gain}, {"muted", c.muted}, {"fadeInFrames", integer(c.fadeInFrames)},
        {"fadeOutFrames", integer(c.fadeOutFrames)}, {"effects", array(c.effects)}, {"pan", c.pan},
        {"audioAutomation", json(c.audioAutomation)}};
    if (!c.sequenceId.isEmpty()) result["sequenceId"] = c.sequenceId;
    return result;
}
Object json(const Track& t) {
    return {{"id", t.id}, {"name", t.name}, {"kind", t.kind}, {"enabled", t.enabled}, {"locked", t.locked},
        {"muted", t.muted}, {"solo", t.solo}, {"gain", t.gain}, {"clips", array(t.clips)}, {"audioBusId", t.audioBusId},
        {"pan", t.pan}, {"audioAutomation", json(t.audioAutomation)}};
}
Object json(const AudioBus& b) {
    return {{"id", b.id}, {"name", b.name}, {"gain", b.gain}, {"pan", b.pan}, {"muted", b.muted}, {"solo", b.solo}};
}
Object json(const Sequence& s) {
    Object result{{"id", s.id}, {"name", s.name}, {"primaryVideoMediaId", s.primaryVideoMediaId}, {"primaryVideoStreamIndex", s.primaryVideoStreamIndex},
        {"frameRate", json(s.frameRate)}, {"durationFrames", integer(s.durationFrames)}, {"automaticEnd", s.automaticEnd},
        {"width", s.width}, {"height", s.height}, {"sampleRate", s.sampleRate}, {"tracks", array(s.tracks)},
        {"audioBuses", array(s.audioBuses)}};
    if (s.multicam) {
        QJsonArray cameras, cuts;
        for (const auto& id : s.multicam->cameraTrackIds) cameras.append(id);
        for (const auto& cut : s.multicam->cuts) cuts.append(Object{{"frame", integer(cut.frame)}, {"trackId", cut.trackId}});
        result["multicam"] = Object{{"cameraTrackIds", cameras}, {"cuts", cuts}};
    }
    return result;
}
Object json(const ExportSettings& e) {
    Object options; for (auto it = e.encoderOptions.begin(); it != e.encoderOptions.end(); ++it) options[it.key()] = it.value();
    return {{"container", e.container}, {"videoCodec", e.videoCodec}, {"audioCodec", e.audioCodec}, {"preset", e.preset},
        {"outputPath", e.outputPath}, {"width", e.width}, {"height", e.height}, {"sampleRate", e.sampleRate},
        {"channels", e.channels}, {"quality", e.quality}, {"frameRate", json(e.frameRate)}, {"frameRateCustomized", e.frameRateCustomized}, {"audioEnabled", e.audioEnabled},
        {"compatibilityProfile", e.compatibilityProfile}, {"videoEncoder", e.videoEncoder}, {"pixelFormat", e.pixelFormat},
        {"qualityMode", e.qualityMode}, {"audioEncoder", e.audioEncoder}, {"audioBitrate", e.audioBitrate}, {"encoderOptions", options}};
}
Object json(const Project& p) {
    return {{"format", "LocalVideoTools.VideoEditor"}, {"schemaVersion", SchemaVersion}, {"id", p.id},
        {"name", p.name}, {"activeSequenceId", p.activeSequenceId}, {"media", array(p.media)},
        {"titles", array(p.titles)}, {"sequences", array(p.sequences)}, {"exportSettings", json(p.exportSettings)}};
}
[[noreturn]] void fail(const QString& path, const QString& reason) { throw path + ": " + reason; }
void require(bool condition, const QString& path, const QString& reason) { if (!condition) fail(path, reason); }
struct Reader {
    Object o;
    QString path;
    Reader(const QJsonValue& value, QString fieldPath, std::initializer_list<const char*> keys) : path(std::move(fieldPath)) {
        require(value.isObject(), path, "expected an object");
        o = value.toObject();
        QSet<QString> allowed;
        for (const auto* key : keys) {
            allowed.insert(QString::fromLatin1(key));
            require(o.contains(key), at(key), "required field is missing");
        }
        for (auto it = o.begin(); it != o.end(); ++it)
            require(allowed.contains(it.key()), path + "." + it.key(), "unknown field; refusing to discard data");
    }
    QString at(const char* key) const { return path + "." + QString::fromLatin1(key); }
    QString str(const char* key, bool nonempty = false) const {
        require(o[key].isString(), at(key), "expected a string");
        const auto s = o[key].toString();
        require(!s.contains(QChar::Null) && s.size() <= 65536 && (!nonempty || !s.trimmed().isEmpty()), at(key), "invalid or empty string");
        return s;
    }
    bool boolean(const char* key) const {
        require(o[key].isBool(), at(key), "expected a boolean");
        return o[key].toBool();
    }
    double number(const char* key, double low, double high) const {
        const auto value = o[key];
        const auto n = value.toDouble(std::numeric_limits<double>::quiet_NaN());
        require(value.isDouble() && std::isfinite(n) && n >= low && n <= high, at(key), "number outside supported range");
        return n;
    }
    int small(const char* key, int low, int high) const {
        const auto n = number(key, low, high);
        require(std::floor(n) == n, at(key), "expected an integer");
        return static_cast<int>(n);
    }
    qint64 big(const char* key, qint64 low = 0) const {
        const auto s = str(key);
        bool ok = false;
        const auto n = s.toLongLong(&ok);
        require(ok && n >= low && QString::number(n) == s, at(key), "expected a canonical decimal int64 string in range");
        return n;
    }
    QString choice(const char* key, const QStringList& choices) const {
        const auto s = str(key);
        require(choices.contains(s), at(key), "unsupported value: " + s);
        return s;
    }
};
Rational rational(const Reader& parent, const char* key, bool zero = false) {
    Reader r(parent.o[key], parent.at(key), {"numerator", "denominator"});
    Rational value{r.big("numerator", zero ? 0 : 1), r.big("denominator", 1)};
    require(value.numerator <= INT_MAX && value.denominator <= INT_MAX &&
        std::gcd(value.numerator, value.denominator) == 1, r.path, "rational must be reduced, with int32-sized positive components (0/1 allowed for audio fps)");
    return value;
}
template<class T, class F> QVector<T> readArray(const Reader& r, const char* key, F parse) {
    require(r.o[key].isArray(), r.at(key), "expected an array");
    const auto a = r.o[key].toArray();
    require(a.size() <= 100000, r.at(key), "array exceeds 100000 records");
    QVector<T> result;
    for (qsizetype i = 0; i < a.size(); ++i) result.append(parse(a[i], r.at(key) + "[" + QString::number(i) + "]"));
    return result;
}
Stream stream(const QJsonValue& value, const QString& path) {
    Reader r(value, path, {"index", "kind", "codec", "timeBase", "frameRate", "startTicks", "durationTicks", "width", "height",
        "bitDepth", "sampleRate", "channels", "rotation", "variableFrameRate"});
    Stream s;
    s.index = r.small("index", 0, INT_MAX); s.kind = r.choice("kind", {"video", "audio"}); s.codec = r.str("codec", true);
    s.timeBase = rational(r, "timeBase"); s.frameRate = rational(r, "frameRate", s.kind == "audio");
    s.startTicks = r.big("startTicks", std::numeric_limits<qint64>::min()); s.durationTicks = r.big("durationTicks");
    s.width = r.small("width", 0, 32768); s.height = r.small("height", 0, 32768); s.bitDepth = r.small("bitDepth", 0, 64);
    s.sampleRate = r.small("sampleRate", 0, 384000); s.channels = r.small("channels", 0, 64); s.rotation = r.small("rotation", -360, 360);
    s.variableFrameRate = r.boolean("variableFrameRate");
    require(s.kind == "video" ? s.width > 0 && s.height > 0 : s.sampleRate >= 8000 && s.channels > 0 && s.frameRate == Rational{0, 1}, path, "incomplete video/audio metadata");
    return s;
}
Media media(const QJsonValue& value, const QString& path) {
    Reader r(value, path, {"id", "name", "path", "container", "sizeBytes", "streams"});
    Media m{r.str("id"), r.str("name", true), r.str("path", true), r.str("container", true), r.big("sizeBytes"), readArray<Stream>(r, "streams", stream)};
    require(!m.streams.isEmpty(), path, "media must have a supported stream");
    QSet<int> indices;
    for (const auto& s : m.streams) {
        require(!indices.contains(s.index), path + ".streams", "duplicate stream index"); indices.insert(s.index);
    }
    return m;
}
Title title(const QJsonValue& value, const QString& path) {
    Reader r(value, path, {"id", "text", "fontFamily", "fontSize", "alignment", "color", "background", "x", "y", "shadow"});
    Title t;
    t.id = r.str("id"); t.text = r.str("text"); t.fontFamily = r.str("fontFamily", true); t.fontSize = r.small("fontSize", 1, 1000);
    t.alignment = r.choice("alignment", {"left", "center", "right"}); t.color = r.str("color"); t.background = r.str("background");
    static const QRegularExpression colorPattern(QStringLiteral("^#[0-9a-fA-F]{8}$"));
    require(colorPattern.match(t.color).hasMatch() && colorPattern.match(t.background).hasMatch(), path, "colors must use #AARRGGBB");
    t.x = r.number("x", 0, 1); t.y = r.number("y", 0, 1); t.shadow = r.boolean("shadow");
    return t;
}
void checkParameters(const QJsonValue& v, const QString& path, int depth = 0) {
    require(depth <= 32, path, "effect parameters exceed nesting limit");
    if (v.isDouble()) require(std::isfinite(v.toDouble()), path, "effect number must be finite");
    if (v.isObject()) {
        const auto o = v.toObject();
        for (auto it = o.begin(); it != o.end(); ++it) checkParameters(it.value(), path + "." + it.key(), depth + 1);
    }
    if (v.isArray()) for (const auto& item : v.toArray()) checkParameters(item, path + "[]", depth + 1);
}
Effect effect(const QJsonValue& value, const QString& path) {
    Reader r(value, path, {"id", "type", "version", "enabled", "parameters", "timeOffsetFrames", "keyframes"});
    require(r.o["parameters"].isObject(), r.at("parameters"), "expected an object");
    checkParameters(r.o["parameters"], r.at("parameters"));
    Effect e{r.str("id"), r.str("type", true), r.small("version", 1, INT_MAX), r.boolean("enabled"), r.o["parameters"].toObject()};
    e.timeOffsetFrames = r.big("timeOffsetFrames", std::numeric_limits<qint64>::min());
    require(r.o["keyframes"].isObject(), r.at("keyframes"), "expected an object");
    const auto keys = r.o["keyframes"].toObject();
    require(keys.size() <= 64, r.at("keyframes"), "limit is 64 animated parameters");
    for (auto it = keys.begin(); it != keys.end(); ++it) {
        require(it.value().isArray() && it.value().toArray().size() <= 10000, r.at("keyframes"), "expected at most 10000 keyframes");
        qint64 previous = -1;
        for (const auto& v : it.value().toArray()) {
            Reader k(v, r.at("keyframes") + "." + it.key(), {"frame", "value", "curve"});
            Keyframe key{k.big("frame"), k.number("value", -1e12, 1e12), k.choice("curve", {"hold", "linear", "eased"})};
            require(key.frame > previous, k.path, "keyframes must be strictly increasing"); previous = key.frame; e.keyframes[it.key()].append(key);
        }
    }
    return e;
}
AudioAutomation audioAutomation(const QJsonValue& value, const QString& path) {
    Reader r(value, path, {"timeOffsetFrames", "volume", "pan"});
    AudioAutomation result;
    result.timeOffsetFrames = r.big("timeOffsetFrames", -10000000);
    auto keys = [&](const char* name, double low, double high) {
        auto parsed = readArray<Keyframe>(r, name, [&](const QJsonValue& entry, const QString& keyPath) {
            Reader k(entry, keyPath, {"frame", "value", "curve"});
            return Keyframe{k.big("frame"), k.number("value", low, high), k.choice("curve", {"hold", "linear", "eased"})};
        });
        require(parsed.size() <= 10000, r.at(name), "limit is 10000 keyframes per audio parameter");
        qint64 previous = -1;
        for (const auto& key : parsed) {
            require(key.frame > previous && key.frame <= 10000000, r.at(name), "keyframe positions must be strictly increasing and at most 10 million frames");
            previous = key.frame;
        }
        return parsed;
    };
    result.volume = keys("volume", 0, 16);
    result.pan = keys("pan", -1, 1);
    return result;
}
Clip clip(const QJsonValue& value, const QString& path) {
    require(value.isObject(), path, "expected an object");
    auto object = value.toObject(); if (!object.contains("sequenceId")) object["sequenceId"] = "";
    Reader r(object, path, {"id", "name", "kind", "mediaId", "titleId", "streamIndex", "startFrame", "durationFrames", "sourceInTicks", "sourceDurationTicks", "gain", "muted", "fadeInFrames", "fadeOutFrames", "effects", "pan", "audioAutomation", "sequenceId"});
    Clip c;
    c.id = r.str("id"); c.name = r.str("name", true); c.kind = r.choice("kind", {"video", "audio", "title"});
    c.mediaId = r.str("mediaId"); c.titleId = r.str("titleId"); c.streamIndex = r.small("streamIndex", 0, INT_MAX);
    c.sequenceId = r.str("sequenceId");
    c.startFrame = r.big("startFrame"); c.durationFrames = r.big("durationFrames", 1);
    c.sourceInTicks = r.big("sourceInTicks"); c.sourceDurationTicks = r.big("sourceDurationTicks");
    c.gain = r.number("gain", 0, 16); c.muted = r.boolean("muted"); c.effects = readArray<Effect>(r, "effects", effect);
    c.pan = r.number("pan", -1, 1); c.audioAutomation = audioAutomation(r.o["audioAutomation"], r.at("audioAutomation"));
    require(c.effects.size() <= 64, path + ".effects", "limit is 64 effects per clip");
    c.fadeInFrames = r.big("fadeInFrames"); c.fadeOutFrames = r.big("fadeOutFrames");
    require(c.fadeInFrames <= c.durationFrames && c.fadeOutFrames <= c.durationFrames,
        path, "fade duration exceeds clip duration");
    require(c.kind == "audio" || (c.fadeInFrames == 0 && c.fadeOutFrames == 0), path, "fades require an audio clip");
    require(c.kind == "audio" || (c.pan == 0 && c.audioAutomation.timeOffsetFrames == 0 && c.audioAutomation.volume.isEmpty() && c.audioAutomation.pan.isEmpty()), path, "audio pan/automation require an audio clip");
    for (const auto& e : c.effects) {
        const auto error = validateEffect(e, c.durationFrames);
        require(error.isEmpty(), path + ".effects." + e.id, error);
    }
    return c;
}
Track track(const QJsonValue& value, const QString& path) {
    Reader r(value, path, {"id", "name", "kind", "enabled", "locked", "muted", "solo", "gain", "clips", "audioBusId", "pan", "audioAutomation"});
    Track result{r.str("id"), r.str("name", true), r.choice("kind", {"video", "audio", "title"}), r.boolean("enabled"),
        r.boolean("locked"), r.boolean("muted"), r.boolean("solo"), r.number("gain", 0, 16), readArray<Clip>(r, "clips", clip)};
    result.audioBusId = r.str("audioBusId"); result.pan = r.number("pan", -1, 1);
    result.audioAutomation = audioAutomation(r.o["audioAutomation"], r.at("audioAutomation"));
    require(result.kind == "audio" || (result.audioBusId.isEmpty() && result.pan == 0 && result.audioAutomation.timeOffsetFrames == 0 && result.audioAutomation.volume.isEmpty() && result.audioAutomation.pan.isEmpty()), path, "audio routing/pan/automation require an audio track");
    require(result.audioAutomation.timeOffsetFrames == 0, r.at("audioAutomation"), "track automation uses absolute sequence frames");
    return result;
}
AudioBus audioBus(const QJsonValue& value, const QString& path) {
    Reader r(value, path, {"id", "name", "gain", "pan", "muted", "solo"});
    return {r.str("id"), r.str("name", true), r.number("gain", 0, 16), r.number("pan", -1, 1), r.boolean("muted"), r.boolean("solo")};
}
Sequence sequence(const QJsonValue& value, const QString& path) {
    require(value.isObject(), path, "expected an object");
    auto object = value.toObject(); if (!object.contains("multicam")) object["multicam"] = QJsonValue::Null;
    Reader r(object, path, {"id", "name", "primaryVideoMediaId", "primaryVideoStreamIndex", "frameRate", "durationFrames", "automaticEnd", "width", "height", "sampleRate", "tracks", "audioBuses", "multicam"});
    Sequence result{r.str("id"), r.str("name", true), rational(r, "frameRate"), r.big("durationFrames"), r.small("width", 1, 32768),
        r.small("height", 1, 32768), r.small("sampleRate", 8000, 384000), readArray<Track>(r, "tracks", track), readArray<AudioBus>(r, "audioBuses", audioBus)};
    result.primaryVideoMediaId = r.str("primaryVideoMediaId");
    result.primaryVideoStreamIndex = r.small("primaryVideoStreamIndex", -1, std::numeric_limits<int>::max());
    result.automaticEnd = r.boolean("automaticEnd");
    if (!r.o["multicam"].isNull()) {
        Reader m(r.o["multicam"], r.at("multicam"), {"cameraTrackIds", "cuts"});
        Multicam group;
        require(m.o["cameraTrackIds"].isArray(), m.at("cameraTrackIds"), "expected an array");
        for (const auto& entry : m.o["cameraTrackIds"].toArray()) {
            require(entry.isString(), m.at("cameraTrackIds"), "expected track ID string"); group.cameraTrackIds.append(entry.toString());
        }
        group.cuts = readArray<CameraCut>(m, "cuts", [](const auto& v, const auto& p) {
            Reader c(v, p, {"frame", "trackId"}); return CameraCut{c.big("frame"), c.str("trackId")};
        });
        result.multicam = group;
    }
    return result;
}
ExportSettings exportSettings(const QJsonValue& value, const QString& path) {
    Reader r(value, path, {"container", "videoCodec", "audioCodec", "preset", "outputPath", "width", "height", "sampleRate", "channels", "quality", "frameRate", "frameRateCustomized", "audioEnabled", "compatibilityProfile", "videoEncoder", "pixelFormat", "qualityMode", "audioEncoder", "audioBitrate", "encoderOptions"});
    ExportSettings e;
    e.container = r.str("container", true); e.videoCodec = r.str("videoCodec", true); e.audioCodec = r.str("audioCodec", true);
    e.preset = r.str("preset", true); e.outputPath = r.str("outputPath"); e.width = r.small("width", 1, 32768); e.height = r.small("height", 1, 32768);
    e.sampleRate = r.small("sampleRate", 8000, 384000); e.channels = r.small("channels", 1, 64); e.quality = r.number("quality", 0, 1000000);
    e.frameRate = rational(r, "frameRate"); e.frameRateCustomized = r.boolean("frameRateCustomized"); e.audioEnabled = r.boolean("audioEnabled");
    e.compatibilityProfile = r.str("compatibilityProfile", true); e.videoEncoder = r.str("videoEncoder", true); e.pixelFormat = r.str("pixelFormat", true);
    e.qualityMode = r.str("qualityMode", true); e.audioEncoder = r.str("audioEncoder", true); e.audioBitrate = r.small("audioBitrate", 1, 1000000);
    require(r.o["encoderOptions"].isObject() && r.o["encoderOptions"].toObject().size() <= 512, r.at("encoderOptions"), "expected at most 512 options");
    const auto options = r.o["encoderOptions"].toObject();
    for (auto it = options.begin(); it != options.end(); ++it) {
        require(it.value().isString() && !it.key().contains(QChar::Null) && it.key().size() <= 256 && !it.value().toString().contains(QChar::Null) && it.value().toString().size() <= 65536,
            r.at("encoderOptions"), "invalid option name/value");
        e.encoderOptions[it.key()] = it.value().toString();
    }
    return e;
}
void references(const Project& p) {
    QSet<QString> ids;
    auto id = [&ids](const QString& value, const QString& path) {
        const auto uuid = QUuid(value);
        require(!uuid.isNull() && uuid.toString(QUuid::WithoutBraces) == value, path, "expected a canonical non-null UUID");
        require(!ids.contains(value), path, "duplicate stable ID"); ids.insert(value);
    };
    id(p.id, "project.id");
    QHash<QString, const Media*> mediaById;
    QSet<QString> titleIds, sequenceIds;
    for (const auto& m : p.media) { id(m.id, "media.id"); mediaById.insert(m.id, &m); }
    for (const auto& t : p.titles) { id(t.id, "titles.id"); titleIds.insert(t.id); }
    require(!p.sequences.isEmpty(), "sequences", "at least one sequence is required");
    require(p.sequences.size() <= 128, "sequences", "limit is 128 sequences");
    QHash<QString, const Sequence*> sequenceById;
    for (const auto& s : p.sequences) sequenceById.insert(s.id, &s);
    for (const auto& s : p.sequences) {
        id(s.id, "sequences.id"); sequenceIds.insert(s.id);
        if (s.primaryVideoMediaId.isEmpty()) require(s.primaryVideoStreamIndex == -1, "sequences.primaryVideoStreamIndex", "must be -1 when no primary video is selected");
        else {
            require(mediaById.contains(s.primaryVideoMediaId), "sequences.primaryVideoMediaId", "missing media reference");
            bool foundPrimaryStream = false;
            for (const auto& st : mediaById[s.primaryVideoMediaId]->streams)
                if (st.index == s.primaryVideoStreamIndex && st.kind == "video") foundPrimaryStream = true;
            require(foundPrimaryStream, "sequences.primaryVideoStreamIndex", "primary video must reference a video stream in the selected media");
        }
        require(s.audioBuses.size() <= 128, "sequences.audioBuses", "limit is 128 audio buses per sequence");
        QSet<QString> busIds;
        for (const auto& bus : s.audioBuses) {
            id(bus.id, "sequences.audioBuses.id");
            require(!bus.name.trimmed().isEmpty() && bus.name.size() <= 256, "sequences.audioBuses.name", "bus name must contain 1–256 characters");
            busIds.insert(bus.id);
        }
        for (const auto& t : s.tracks) {
            id(t.id, "tracks.id");
            require(t.kind == "audio" || t.audioBusId.isEmpty(), "tracks.audioBusId", "only audio tracks can route to an audio bus");
            require(t.audioBusId.isEmpty() || busIds.contains(t.audioBusId), "tracks.audioBusId", "missing audio bus reference");
            for (const auto& c : t.clips) {
                const auto path = "clip[" + c.id + "]";
                id(c.id, path + ".id");
                require(c.kind == t.kind, path, "clip kind must match track kind");
                require(c.audioAutomation.timeOffsetFrames >= -10000000 && c.audioAutomation.timeOffsetFrames <= 10000000, path + ".audioAutomation.timeOffsetFrames", "content offset exceeds 10 million frames");
                if (c.durationFrames - 1 > 0) require(c.audioAutomation.timeOffsetFrames <= std::numeric_limits<qint64>::max() - (c.durationFrames - 1), path + ".audioAutomation", "content time overflows");
                require(c.startFrame <= s.durationFrames && c.durationFrames <= s.durationFrames - c.startFrame, path, "clip extends beyond sequence duration");
                if (!c.sequenceId.isEmpty()) {
                    require(c.kind != "title" && c.mediaId.isEmpty() && c.titleId.isEmpty() && c.streamIndex == 0 && sequenceById.contains(c.sequenceId), path, "invalid nested sequence reference/source fields");
                    const auto& child = *sequenceById[c.sequenceId];
                    require(child.frameRate == s.frameRate, path, "nested sequences require matching frame rates");
                    require(c.sourceDurationTicks == c.durationFrames && c.sourceInTicks <= child.durationFrames && c.durationFrames <= child.durationFrames - c.sourceInTicks, path, "nested source range exceeds child duration or is not 1x");
                    require(!speedEffect(c), path, "speed on a nested sequence is not supported; edit speed inside the child");
                } else if (c.kind == "title") {
                    require(c.mediaId.isEmpty() && titleIds.contains(c.titleId) && c.sourceInTicks == 0 &&
                        c.sourceDurationTicks == 0 && c.streamIndex == 0, path, "invalid title reference/source fields");
                } else {
                    require(c.titleId.isEmpty() && mediaById.contains(c.mediaId), path + ".mediaId", "missing media reference");
                    const Stream* selected = nullptr;
                    for (const auto& st : mediaById[c.mediaId]->streams) if (st.index == c.streamIndex) selected = &st;
                    require(selected && selected->kind == c.kind, path + ".streamIndex", "stream does not exist or has wrong kind");
                    require(c.sourceDurationTicks > 0 && c.sourceInTicks <= selected->durationTicks &&
                        c.sourceDurationTicks <= selected->durationTicks - c.sourceInTicks, path, "source range exceeds stream duration");
                }
                int speeds = 0;
                for (const auto& e : c.effects) {
                    id(e.id, path + ".effects.id");
                    require(c.kind != "audio" || !supportedEffect(e) || e.type == "speed", path + ".effects", "visual effects require video or title clips");
                    require(c.kind != "title" || e.type != "speed", path + ".effects", "speed requires a media clip");
                    require(e.type != "speed" || ++speeds <= 1, path + ".effects", "use one speed effect per clip");
                    if (e.type == "speed") require(c.durationFrames <= 10000000, path + ".durationFrames", "speed limit is 10 million frames");
                }
            }
        }
        if (s.automaticEnd) {
            qint64 lastEnd = 0;
            for (const auto& t : s.tracks) for (const auto& c : t.clips) lastEnd = std::max(lastEnd, c.startFrame + c.durationFrames);
            require(s.durationFrames == lastEnd, "sequences.automaticEnd", "automatic duration must equal the last clip end");
        }
        if (s.multicam) {
            const auto& m = *s.multicam; QSet<QString> cameras;
            require(m.cameraTrackIds.size() >= 2 && m.cameraTrackIds.size() <= 4, "multicam", "requires 2 through 4 cameras");
            for (const auto& camera : m.cameraTrackIds) {
                require(!cameras.contains(camera), "multicam", "duplicate camera track"); cameras.insert(camera);
                const Track* t = nullptr; for (const auto& candidate : s.tracks) if (candidate.id == camera) t = &candidate;
                require(t && t->kind == "video" && t->enabled && t->clips.size() == 1, "multicam", "each camera must be one enabled video track with one clip");
                const auto& c = t->clips[0];
                require(c.sequenceId.isEmpty() && !speedEffect(c) && c.startFrame == 0 && c.durationFrames == s.durationFrames, "multicam", "camera clips must cover the sequence at normal speed; ungroup before trimming cameras");
            }
            require(!m.cuts.isEmpty() && m.cuts[0].frame == 0, "multicam.cuts", "first switch must be at frame zero");
            qint64 previous = -1;
            for (const auto& cut : m.cuts) { require(cut.frame > previous && cut.frame < s.durationFrames && cameras.contains(cut.trackId), "multicam.cuts", "switches must be ordered, unique, in range and reference cameras"); previous = cut.frame; }
        }
    }
    QSet<QString> visiting; QHash<QString, int> depths;
    std::function<int(const QString&)> visit = [&](const QString& sid) {
        if (depths.contains(sid)) return depths[sid];
        require(!visiting.contains(sid), "sequences", "nested sequence cycle detected");
        visiting.insert(sid); int depth = 0;
        for (const auto& t : sequenceById[sid]->tracks) for (const auto& c : t.clips) if (!c.sequenceId.isEmpty()) depth = std::max(depth, 1 + visit(c.sequenceId));
        require(depth <= 8, "sequences", "nesting depth limit is eight");
        visiting.remove(sid); depths[sid] = depth; return depth;
    };
    for (const auto& s : p.sequences) visit(s.id);
    require(sequenceIds.contains(p.activeSequenceId), "activeSequenceId", "missing sequence reference");
}
Object migrateV1(Object o) {
    // v1 used JSON numbers for sequence/clip frame positions. All other fields are unchanged.
    auto convert = [](Object& record, const char* key, const QString& path) {
        const auto v = record[key]; const auto n = v.toDouble(-1);
        require(v.isDouble() && n >= 0 && n <= 9007199254740991.0 && std::floor(n) == n,
            path + "." + key, "v1 time must be an exact nonnegative safe JSON integer");
        record[key] = integer(static_cast<qint64>(n));
    };
    require(o["sequences"].isArray(), "sequences", "expected an array for v1 migration");
    auto sequences = o["sequences"].toArray();
    for (qsizetype i = 0; i < sequences.size(); ++i) {
        require(sequences[i].isObject(), "sequences", "expected object for v1 migration");
        auto s = sequences[i].toObject(); convert(s, "durationFrames", "sequences");
        require(s["tracks"].isArray(), "tracks", "expected array for v1 migration");
        auto tracks = s["tracks"].toArray();
        for (qsizetype j = 0; j < tracks.size(); ++j) {
            require(tracks[j].isObject(), "tracks", "expected object for v1 migration");
            auto t = tracks[j].toObject();
            require(t["clips"].isArray(), "clips", "expected array for v1 migration");
            auto clips = t["clips"].toArray();
            for (qsizetype k = 0; k < clips.size(); ++k) {
                require(clips[k].isObject(), "clips", "expected object for v1 migration");
                auto c = clips[k].toObject(); convert(c, "startFrame", "clips"); convert(c, "durationFrames", "clips"); clips[k] = c;
            }
            t["clips"] = clips; tracks[j] = t;
        }
        s["tracks"] = tracks; sequences[i] = s;
    }
    o["sequences"] = sequences; o["schemaVersion"] = 2;
    return o;
}
Object migrateV2(Object o) {
    require(o["sequences"].isArray(), "sequences", "expected array for v2 migration");
    auto sequences = o["sequences"].toArray();
    for (qsizetype i = 0; i < sequences.size(); ++i) {
        require(sequences[i].isObject(), "sequences", "expected object for v2 migration");
        auto s = sequences[i].toObject();
        require(s["tracks"].isArray(), "tracks", "expected array for v2 migration");
        auto tracks = s["tracks"].toArray();
        for (qsizetype j = 0; j < tracks.size(); ++j) {
            require(tracks[j].isObject(), "tracks", "expected object for v2 migration");
            auto t = tracks[j].toObject();
            require(t["clips"].isArray(), "clips", "expected array for v2 migration");
            auto clips = t["clips"].toArray();
            for (qsizetype k = 0; k < clips.size(); ++k) {
                require(clips[k].isObject(), "clips", "expected object for v2 migration");
                auto c = clips[k].toObject();
                require(!c.contains("fadeInFrames") && !c.contains("fadeOutFrames"), "clips", "unexpected fade fields in old schema");
                c["fadeInFrames"] = "0"; c["fadeOutFrames"] = "0"; clips[k] = c;
            }
            t["clips"] = clips; tracks[j] = t;
        }
        s["tracks"] = tracks; sequences[i] = s;
    }
    o["sequences"] = sequences; o["schemaVersion"] = 3;
    return o;
}
Object migrateV3(Object o) {
    require(o["sequences"].isArray(), "sequences", "expected an array for v3 migration");
    auto sequences = o["sequences"].toArray();
    for (qsizetype i = 0; i < sequences.size(); ++i) {
        require(sequences[i].isObject(), "sequences", "expected an object for v3 migration");
        auto s = sequences[i].toObject(); auto tracks = s["tracks"].toArray();
        require(s["tracks"].isArray(), "tracks", "expected an array for v3 migration");
        for (qsizetype j = 0; j < tracks.size(); ++j) {
            require(tracks[j].isObject(), "tracks", "expected an object for v3 migration");
            auto t = tracks[j].toObject(); auto clips = t["clips"].toArray();
            require(t["clips"].isArray(), "clips", "expected an array for v3 migration");
            for (qsizetype k = 0; k < clips.size(); ++k) {
                require(clips[k].isObject(), "clips", "expected an object for v3 migration");
                auto c = clips[k].toObject(); auto effects = c["effects"].toArray();
                require(c["effects"].isArray(), "effects", "expected an array for v3 migration");
                for (qsizetype l = 0; l < effects.size(); ++l) {
                    require(effects[l].isObject(), "effects", "expected an object for v3 migration");
                    auto e = effects[l].toObject();
                    require(!e.contains("keyframes") && !e.contains("timeOffsetFrames"), "effects", "unexpected animation fields in old schema");
                    e["timeOffsetFrames"] = "0"; e["keyframes"] = Object{}; effects[l] = e;
                }
                c["effects"] = effects; clips[k] = c;
            }
            t["clips"] = clips; tracks[j] = t;
        }
        s["tracks"] = tracks; sequences[i] = s;
    }
    o["sequences"] = sequences; o["schemaVersion"] = 4;
    return o;
}
Object migrateV4(Object o) {
    require(o["sequences"].isArray(), "sequences", "expected an array for v4 migration");
    auto sequences = o["sequences"].toArray();
    const Object emptyAutomation{{"timeOffsetFrames", "0"}, {"volume", QJsonArray{}}, {"pan", QJsonArray{}}};
    for (qsizetype i = 0; i < sequences.size(); ++i) {
        require(sequences[i].isObject(), "sequences", "expected an object for v4 migration");
        auto s = sequences[i].toObject();
        require(!s.contains("audioBuses") && s["tracks"].isArray(), "sequences", "unexpected audio bus data or missing tracks in v4 migration");
        auto tracks = s["tracks"].toArray();
        for (qsizetype j = 0; j < tracks.size(); ++j) {
            require(tracks[j].isObject(), "tracks", "expected an object for v4 migration");
            auto t = tracks[j].toObject();
            require(!t.contains("audioBusId") && !t.contains("pan") && !t.contains("audioAutomation") && t["clips"].isArray(), "tracks", "unexpected audio mix data or missing clips in v4 migration");
            t["audioBusId"] = ""; t["pan"] = 0; t["audioAutomation"] = emptyAutomation;
            auto clips = t["clips"].toArray();
            for (qsizetype k = 0; k < clips.size(); ++k) {
                require(clips[k].isObject(), "clips", "expected an object for v4 migration");
                auto c = clips[k].toObject();
                require(!c.contains("pan") && !c.contains("audioAutomation"), "clips", "unexpected audio automation in v4 migration");
                c["pan"] = 0; c["audioAutomation"] = emptyAutomation; clips[k] = c;
            }
            t["clips"] = clips; tracks[j] = t;
        }
        s["tracks"] = tracks; s["audioBuses"] = QJsonArray{}; sequences[i] = s;
    }
    o["sequences"] = sequences; o["schemaVersion"] = 5;
    return o;
}
}
QString newId() { return QUuid::createUuid().toString(QUuid::WithoutBraces); }
Project newProject(const QString& name, bool automaticEnd) {
    Project p; p.id = newId(); p.name = name;
    Sequence s; s.id = newId(); s.name = QStringLiteral("Sequence 1"); s.automaticEnd = automaticEnd;
    Track v; v.id = newId(); v.name = QStringLiteral("Video 1");
    Track a; a.id = newId(); a.name = QStringLiteral("Audio 1"); a.kind = QStringLiteral("audio");
    s.tracks = {v, a}; p.activeSequenceId = s.id; p.sequences = {s};
    return p;
}
QByteArray serialize(const Project& p) { return QJsonDocument(json(p)).toJson(QJsonDocument::Indented); }
LoadResult deserialize(const QByteArray& bytes) {
    LoadResult result;
    try {
        require(bytes.size() <= MaxDocumentBytes, "project", "document exceeds 16 MiB limit");
        QJsonParseError error;
        const auto document = QJsonDocument::fromJson(bytes, &error);
        require(error.error == QJsonParseError::NoError, "JSON byte " + QString::number(error.offset), error.errorString());
        require(document.isObject(), "project", "root must be an object");
        auto o = document.object();
        require(o["format"] == QJsonValue("LocalVideoTools.VideoEditor"), "format", "not a Video Editor project");
        const auto version = o["schemaVersion"];
        require(version.isDouble() && version.toDouble() >= 1 && version.toDouble() <= SchemaVersion && std::floor(version.toDouble()) == version.toDouble(), "schemaVersion",
            "unsupported schema version; supported versions are 1 through " + QString::number(SchemaVersion) + " (future versions require a newer app)");
        if (version.toInt() == 1) { o = migrateV1(o); result.migratedFrom = 1; }
        if (o["schemaVersion"].toInt() == 2) { o = migrateV2(o); if (!result.migratedFrom) result.migratedFrom = 2; }
        if (o["schemaVersion"].toInt() == 3) { o = migrateV3(o); if (!result.migratedFrom) result.migratedFrom = 3; }
        if (o["schemaVersion"].toInt() == 4) { o = migrateV4(o); if (!result.migratedFrom) result.migratedFrom = 4; }
        if (o["schemaVersion"].toInt() == 5) {
            for (const auto& value : o["sequences"].toArray()) {
                const auto s = value.toObject(); require(!s.contains("multicam"), "sequences", "unexpected multicamera data in schema 5");
                for (const auto& tv : s["tracks"].toArray()) for (const auto& cv : tv.toObject()["clips"].toArray())
                    require(!cv.toObject().contains("sequenceId"), "clips", "unexpected nested sequence data in schema 5");
            }
            o["schemaVersion"] = 6; if (!result.migratedFrom) result.migratedFrom = 5;
        }
        if (o["schemaVersion"].toInt() == 6) {
            auto e = o["exportSettings"].toObject();
            // Synthetic older-schema documents used by regression tests can already contain defaults.
            // Nondefault new settings must never be lost in a mislabeled document.
            for (const auto& key : {QString("compatibilityProfile"), QString("videoEncoder"), QString("pixelFormat")}) {
                const QString defaultValue = key == "compatibilityProfile" ? "compatible-mp4" : key == "videoEncoder" ? "software" : "auto";
                require(!e.contains(key) || e[key] == defaultValue, "exportSettings." + key, "unexpected device export choice in schema 6");
                e[key] = defaultValue;
            }
            o["exportSettings"] = e; o["schemaVersion"] = 7; if (!result.migratedFrom) result.migratedFrom = 6;
        }
        if (o["schemaVersion"].toInt() == 7) {
            auto e = o["exportSettings"].toObject();
            const auto legacyQuality = e["quality"];
            require(legacyQuality.isDouble() && legacyQuality.toDouble() >= 0 && legacyQuality.toDouble() <= 63 && std::floor(legacyQuality.toDouble()) == legacyQuality.toDouble(), "exportSettings.quality", "schema 7 requires an integer quality from 0 to 63");
            require(!e.contains("qualityMode") || e["qualityMode"] == "native", "exportSettings.qualityMode", "unexpected schema 8 choice in schema 7");
            require(!e.contains("audioEncoder") || e["audioEncoder"] == "aac", "exportSettings.audioEncoder", "unexpected schema 8 choice in schema 7");
            require(!e.contains("encoderOptions") || (e["encoderOptions"].isObject() && e["encoderOptions"].toObject().isEmpty()), "exportSettings.encoderOptions", "unexpected schema 8 options in schema 7");
            e["qualityMode"] = "native"; e["audioEncoder"] = "aac"; e["encoderOptions"] = Object{};
            const auto name = e["videoEncoder"].toString(), oldSpeed = e["preset"].toString();
            const QStringList oldSpeeds{"ultrafast", "veryfast", "fast", "medium", "slow"};
            if (oldSpeeds.contains(oldSpeed)) {
                if (name.endsWith("_amf")) e["preset"] = oldSpeed == "slow" ? "quality" : oldSpeed == "medium" ? "balanced" : "speed";
                else if (name.endsWith("_nvenc")) e["preset"] = QStringList{"p1", "p3", "p4", "p5", "p7"}[oldSpeeds.indexOf(oldSpeed)];
                else if (name.endsWith("_qsv") && oldSpeed == "ultrafast") e["preset"] = "veryfast";
            }
            o["exportSettings"] = e; o["schemaVersion"] = 8; if (!result.migratedFrom) result.migratedFrom = 7;
        }
        if (o["schemaVersion"].toInt() == 8) {
            auto e = o["exportSettings"].toObject();
            if (!e.contains("frameRateCustomized")) {
                const auto rate = e["frameRate"].toObject();
                bool followedSequenceRate = e["outputPath"].toString().isEmpty();
                const auto activeSequenceId = o["activeSequenceId"].toString();
                for (const auto& value : o["sequences"].toArray()) {
                    const auto sequence = value.toObject();
                    if (sequence["id"].toString() != activeSequenceId) continue;
                    const auto sequenceRate = sequence["frameRate"].toObject();
                    followedSequenceRate = rate["numerator"].toString() == sequenceRate["numerator"].toString() &&
                        rate["denominator"].toString() == sequenceRate["denominator"].toString();
                    break;
                }
                e["frameRateCustomized"] = !followedSequenceRate;
            }
            o["exportSettings"] = e; o["schemaVersion"] = 9; if (!result.migratedFrom) result.migratedFrom = 8;
        }
        if (o["schemaVersion"].toInt() == 9) {
            auto sequences = o["sequences"].toArray();
            for (qsizetype i = 0; i < sequences.size(); ++i) {
                auto s = sequences[i].toObject();
                require(!s.contains("primaryVideoMediaId") && !s.contains("primaryVideoStreamIndex"), "sequences", "unexpected primary video data in schema 9");
                s["primaryVideoMediaId"] = ""; s["primaryVideoStreamIndex"] = -1;
                sequences[i] = s;
            }
            o["sequences"] = sequences; o["schemaVersion"] = 10; if (!result.migratedFrom) result.migratedFrom = 9;
        }
        if (o["schemaVersion"].toInt() == 10) {
            auto e = o["exportSettings"].toObject();
            require(!e.contains("audioBitrate") || e["audioBitrate"] == 192000, "exportSettings.audioBitrate", "unexpected schema 11 bitrate in older schema");
            e["audioBitrate"] = 192000; o["exportSettings"] = e; o["schemaVersion"] = 11; if (!result.migratedFrom) result.migratedFrom = 10;
        }
        if (o["schemaVersion"].toInt() == 11) {
            auto sequences = o["sequences"].toArray();
            for (qsizetype i = 0; i < sequences.size(); ++i) {
                auto s = sequences[i].toObject();
                require(!s.contains("automaticEnd") || s["automaticEnd"] == false, "sequences.automaticEnd", "unexpected automatic end in older schema");
                s["automaticEnd"] = false; sequences[i] = s;
            }
            o["sequences"] = sequences; o["schemaVersion"] = 12; if (!result.migratedFrom) result.migratedFrom = 11;
        }
        Reader r(o, "project", {"format", "schemaVersion", "id", "name", "activeSequenceId", "media", "titles", "sequences", "exportSettings"});
        Project p{r.str("id"), r.str("name", true), r.str("activeSequenceId"), readArray<Media>(r, "media", media),
            readArray<Title>(r, "titles", title), readArray<Sequence>(r, "sequences", sequence), exportSettings(r.o["exportSettings"], r.at("exportSettings"))};
        references(p); result.project = std::move(p);
    } catch (const QString& error) { result.error = error; }
    return result;
}
QString validate(const Project& p) {
    const auto parsed = deserialize(serialize(p));
    if (!parsed) return parsed.error;
    // Also catch values that JSON would normalize (e.g. nonfinite effect parameters).
    return *parsed.project == p ? QString{} : QStringLiteral("project: a value cannot be represented without loss in this schema");
}
QString cameraAt(const Sequence& sequence, qint64 frame) {
    QString result;
    if (sequence.multicam) for (const auto& cut : sequence.multicam->cuts) {
        if (cut.frame > frame) break;
        result = cut.trackId;
    }
    return result;
}
QStringList sequenceMediaIds(const Project& p, const QString& sid) {
    QSet<QString> seen, media;
    std::function<void(const QString&)> visit = [&](const QString& id) {
        if (seen.contains(id)) return; seen.insert(id);
        for (const auto& s : p.sequences) if (s.id == id) for (const auto& t : s.tracks) for (const auto& c : t.clips) {
            if (!c.mediaId.isEmpty()) media.insert(c.mediaId);
            if (!c.sequenceId.isEmpty()) visit(c.sequenceId);
        }
    };
    visit(sid); auto result = media.values(); std::sort(result.begin(), result.end()); return result;
}
}
