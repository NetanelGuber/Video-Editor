#include "timeline/TimelineWidget.h"
#include "timeline/PropertyDialogs.h"
#include "playback/AudioMixer.h"
#include "playback/PlaybackController.h"
#include "media/Inspection.h"
#include "project/ProjectStore.h"
#include <QApplication>
#include <QCheckBox>
#include <QComboBox>
#include <QDataStream>
#include <QDialog>
#include <QDialogButtonBox>
#include <QDir>
#include <QDoubleSpinBox>
#include <QElapsedTimer>
#include <QEventLoop>
#include <QFile>
#include <QFontComboBox>
#include <QFontDatabase>
#include <QFontInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QLineEdit>
#include <QPlainTextEdit>
#include <QPushButton>
#include <QSpinBox>
#include <QTimer>
#include <QTableWidget>
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <limits>

using namespace editor;
namespace {
bool waitFor(const std::function<bool()>& predicate, int timeout = 15000) {
    QElapsedTimer timer; timer.start();
    while (timer.elapsed() < timeout) {
        if (predicate()) return true;
        QCoreApplication::processEvents(QEventLoop::AllEvents, 5); QThread::msleep(1);
    }
    return predicate();
}
bool wave(const QString& path, bool markers = false) {
    QFile file(path); if (!file.open(QIODevice::WriteOnly)) return false;
    QDataStream stream(&file); stream.setByteOrder(QDataStream::LittleEndian);
    const quint32 bytes = 4 * 48000 * 4;
    file.write("RIFF", 4); stream << quint32(36 + bytes); file.write("WAVEfmt ", 8);
    stream << quint32(16) << quint16(1) << quint16(2) << quint32(48000) << quint32(192000) << quint16(4) << quint16(16);
    file.write("data", 4); stream << bytes;
    for (int n = 0; n < 4 * 48000; ++n) { const qint16 value = markers ? (n % 48000 == 0 ? 8192 : 0) : 4096; stream << value << value; }
    return stream.status() == QDataStream::Ok;
}
QVector<float> collect(playback::AudioMixer& mixer, bool& complete) {
    QVector<float> result; mixer.start();
    complete = waitFor([&] {
        playback::AudioBlock block;
        while (mixer.takeAudio(block)) result += block.samples;
        return mixer.done();
    });
    mixer.cancel(); complete &= mixer.wait(10000); return result;
}
void editDialog(const char* name, const std::function<void(QWidget*)>& edit, bool accept = true) {
    QTimer::singleShot(0, [=] {
        for (auto* widget : QApplication::topLevelWidgets()) if (widget->objectName() == name) {
            edit(widget);
            if (accept) widget->findChild<QDialogButtonBox*>()->button(QDialogButtonBox::Ok)->click();
            else qobject_cast<QDialog*>(widget)->reject();
        }
    });
}
}
int main(int argc, char** argv) {
    QApplication app(argc, argv); app.setQuitOnLastWindowClosed(false);
    if (app.arguments().size() != 3) return 2;
    QDir root(app.arguments()[1]), out(app.arguments()[2]); QDir().mkpath(out.absolutePath());
    QJsonArray failures, mixes; int checks = 0;
    auto check = [&](bool ok, const QString& message) { ++checks; if (!ok) { failures.append(message); std::fprintf(stderr, "FAIL: %s\n", qPrintable(message)); } };
    // Offscreen Qt does not enumerate Windows fonts automatically. Use real installed glyphs for pixel checks.
    check(QFontDatabase::addApplicationFont(QDir(qEnvironmentVariable("WINDIR")).filePath("Fonts/arial.ttf")) >= 0, "Load real Windows Arial glyphs for offscreen title rendering");
    QFontDatabase::addApplicationFont(QDir(qEnvironmentVariable("WINDIR")).filePath("Fonts/segoeui.ttf"));
    check(QFontInfo(QFont("Arial")).family() == "Arial", "Title pixel tests resolve actual Arial font");
    check(wave(out.filePath("constant.wav")) && wave(out.filePath("markers.wav"), true), "Known stereo PCM fixtures written");
    const auto inspected = media::inspect(out.filePath("constant.wav"));
    const auto markers = media::inspect(out.filePath("markers.wav"));
    check(inspected.error.isEmpty() && markers.error.isEmpty(), "Known PCM fixtures inspected by native media layer");
    if (inspected.media.streams.isEmpty()) return 1;
    auto p = project::newProject("Session 7 sample-exact mix"); p.media.append(inspected.media);
    auto& seq = p.sequences[0]; seq.durationFrames = 180;
    const auto sid = seq.id, tid = seq.tracks[1].id;
    project::Clip clip; clip.id = project::newId(); clip.name = "Constant"; clip.kind = "audio"; clip.mediaId = inspected.media.id;
    clip.streamIndex = inspected.media.streams[0].index; clip.startFrame = 30; clip.durationFrames = 90; clip.sourceDurationTicks = 144000;
    seq.tracks[1].clips = {clip};
    project::Track extra; extra.id = project::newId(); extra.name = "Second audio"; extra.kind = "audio";
    auto second = clip; second.id = project::newId(); second.startFrame = 60; second.durationFrames = 90;
    extra.clips = {second}; seq.tracks.append(extra);
    timeline::TimelineEditor editor(p);
    check(editor.execute(timeline::SetClipAudio{sid, tid, clip.id, 0.5, false, 30, 30}).isEmpty(), "Clip gain and frame fades are undoable commands");
    check(editor.execute(timeline::SetTrackAudio{sid, tid, 0.5, false, false}).isEmpty(), "Track gain is undoable");
    check(editor.execute(timeline::SetClipAudio{sid, extra.id, second.id, 0.5}).isEmpty(), "Second audio gain set");
    auto mixedProject = editor.state().project;
    auto graph = playback::compileSequence(mixedProject);
    check(graph.audio.size() == 2 && graph.audio[0].gain == 0.25 && graph.audio[0].fadeInSamples == 48000, "Shared description retains both sources and combines track/clip gain");
    auto automatedProject = mixedProject;
    auto& automatedSequence = automatedProject.sequences[0];
    project::AudioBus bus{project::newId(), "Music bus", 0.8, 0.1, false, false}; automatedSequence.audioBuses.append(bus);
    auto& automatedTrack = automatedSequence.tracks[1]; automatedTrack.audioBusId = bus.id; automatedTrack.pan = 0.2;
    automatedTrack.audioAutomation.volume = {{0, 1, "linear"}, {120, 0.5, "linear"}};
    automatedTrack.audioAutomation.pan = {{0, -0.2, "linear"}, {120, 0.2, "linear"}};
    auto& automatedClip = automatedTrack.clips[0]; automatedClip.fadeInFrames = automatedClip.fadeOutFrames = 0; automatedClip.pan = 0.1;
    automatedClip.audioAutomation.volume = {{0, 0.25, "linear"}, {60, 1, "linear"}};
    automatedClip.audioAutomation.pan = {{0, -0.2, "linear"}, {60, 0.2, "linear"}};
    const auto automatedGraph = playback::compileSequence(automatedProject);
    const auto automatedSource = *std::find_if(automatedGraph.audio.begin(), automatedGraph.audio.end(), [&](const auto& source) { return source.clipId == automatedClip.id; });
    const auto frame60 = project::framesToSamples(60, automatedSequence.frameRate, 48000, project::Rounding::Nearest).value_or(-1);
    check(std::abs(playback::audioGainAtSample(automatedSource, frame60) - 0.09375) < 1e-12 && std::abs(playback::audioPanAtSample(automatedSource, frame60) - 0.4) < 1e-12,
        "Shared render graph evaluates clip and track volume/pan keys with routed bus faders");
    check(project::deserialize(project::serialize(automatedProject)).project == automatedProject,
        "Track/clip automation and routed audio bus survive schema-5 save/reopen");
    timeline::TimelineEditor automationEdits(automatedProject);
    check(automationEdits.execute(timeline::TrimClip{sid, tid, automatedClip.id, 45, 75}).isEmpty(), "Trim applies to an automated clip");
    const auto trimmedGraph = playback::compileSequence(automationEdits.state().project);
    const auto trimmedSource = *std::find_if(trimmedGraph.audio.begin(), trimmedGraph.audio.end(), [&](const auto& source) { return source.clipId == automatedClip.id; });
    const auto frame90 = project::framesToSamples(90, automatedSequence.frameRate, 48000, project::Rounding::Nearest).value_or(-1);
    check(std::abs(playback::audioGainAtSample(trimmedSource, frame90) - playback::audioGainAtSample(automatedSource, frame90)) < 1e-12,
        "Clip automation remains content-aligned after left trim");
    timeline::TimelineEditor splitAutomation(automatedProject);
    check(splitAutomation.execute(timeline::SplitClip{sid, tid, automatedClip.id, 75, project::newId(), {}}).isEmpty(), "Automated audio clip splits transactionally");
    const auto& splitAudio = splitAutomation.state().project.sequences[0].tracks[1].clips;
    const auto rightAudio = splitAudio[1]; const auto splitGraph = playback::compileSequence(splitAutomation.state().project);
    const auto rightSource = *std::find_if(splitGraph.audio.begin(), splitGraph.audio.end(), [&](const auto& source) { return source.clipId == rightAudio.id; });
    check(rightAudio.audioAutomation.timeOffsetFrames == 45 && std::abs(playback::audioGainAtSample(rightSource, frame90) - playback::audioGainAtSample(automatedSource, frame90)) < 1e-12,
        "Split advances content-relative audio automation without changing the right-half value");
    auto legacyV4 = QJsonDocument::fromJson(project::serialize(mixedProject)).object(); legacyV4["schemaVersion"] = 4;
    auto legacySequences = legacyV4["sequences"].toArray();
    for (qsizetype si = 0; si < legacySequences.size(); ++si) {
        auto sequenceObject = legacySequences[si].toObject(); sequenceObject.remove("primaryVideoMediaId"); sequenceObject.remove("primaryVideoStreamIndex"); sequenceObject.remove("audioBuses"); auto tracks = sequenceObject["tracks"].toArray();
        for (qsizetype ti = 0; ti < tracks.size(); ++ti) {
            auto trackObject = tracks[ti].toObject(); trackObject.remove("audioBusId"); trackObject.remove("pan"); trackObject.remove("audioAutomation"); auto clips = trackObject["clips"].toArray();
            for (qsizetype ci = 0; ci < clips.size(); ++ci) { auto clipObject = clips[ci].toObject(); clipObject.remove("pan"); clipObject.remove("audioAutomation"); clips[ci] = clipObject; }
            trackObject["clips"] = clips; tracks[ti] = trackObject;
        }
        sequenceObject["tracks"] = tracks; legacySequences[si] = sequenceObject;
    }
    legacyV4["sequences"] = legacySequences;
    const auto migratedV4 = project::deserialize(QJsonDocument(legacyV4).toJson());
    check(migratedV4 && migratedV4.migratedFrom == 4 && *migratedV4.project == mixedProject,
        "Schema-4 projects migrate with a direct-to-master route and unity pan/automation defaults");
    timeline::TimelineEditor busEdits(mixedProject);
    check(busEdits.executeBatch({timeline::UpsertAudioBus{sid, bus}, timeline::SetTrackAudio{sid, tid, 0.5, false, false, 0.2, bus.id}}, "Create and route bus").isEmpty() &&
        busEdits.state().project.sequences[0].tracks[1].audioBusId == bus.id, "Bus creation and track routing are one undoable edit");
    check(busEdits.execute(timeline::RemoveAudioBus{sid, bus.id}).isEmpty() && busEdits.state().project.sequences[0].tracks[1].audioBusId.isEmpty() &&
        busEdits.undo() && busEdits.state().project.sequences[0].tracks[1].audioBusId == bus.id, "Removing a bus routes its tracks to Master and undo restores routing");
    bool complete = false;
    playback::AudioMixer mixer(graph.audio, 0, graph.durationUs);
    auto pcm = collect(mixer, complete);
    check(complete && pcm.size() == 6 * 48000 * 2, "Mix includes exact sequence duration with leading/trailing gaps");
    double maxError = 0; qint64 firstMismatch = -1, mismatches = 0;
    for (qsizetype n = 0; n < pcm.size() / 2; ++n) {
        double expected = 0;
        if (n >= 48000 && n < 192000) {
            double ramp = std::min(1.0, (n - 48000) / 48000.0) * std::min(1.0, (192000 - n) / 48000.0);
            expected += 0.125 * 0.25 * ramp;
        }
        if (n >= 96000 && n < 240000) expected += 0.125 * 0.5;
        for (int c = 0; c < 2; ++c) {
            const auto error = std::abs(pcm[n * 2 + c] - expected); maxError = std::max(maxError, error);
            if (error > 0.000001) { if (firstMismatch < 0) firstMismatch = n; ++mismatches; }
        }
    }
    check(maxError < 0.000001 && mixer.stats()["error"].toString().isEmpty(), "Every stereo sample agrees with independent constant/ramp/overlap oracle");
    mixes.append(QJsonObject{{"maxSampleError", maxError}, {"firstMismatch", firstMismatch}, {"mismatches", mismatches}, {"stats", mixer.stats()}});
    QFile debugPcm(out.filePath("mix.f32")); if (debugPcm.open(QIODevice::WriteOnly)) debugPcm.write(reinterpret_cast<const char*>(pcm.constData()), pcm.size() * sizeof(float));
    playback::AudioMixer sought(graph.audio, 3500000, graph.durationUs);
    auto tail = collect(sought, complete);
    QFile debugTail(out.filePath("seek.f32")); if (debugTail.open(QIODevice::WriteOnly)) debugTail.write(reinterpret_cast<const char*>(tail.constData()), tail.size() * sizeof(float));
    check(complete && tail == pcm.sliced(3500000LL * 48000 / 1000000 * 2), "Seeking inside overlap and fade equals uninterrupted PCM suffix");
    // Muted/solo/disabled tracks and muted clips never reach the mixer.
    check(editor.execute(timeline::SetTrackAudio{sid, extra.id, 1, false, true}).isEmpty(), "Solo command applies");
    check(playback::compileSequence(editor.state().project).audio.size() == 1, "Solo excludes other audio tracks");
    editor.execute(timeline::SetTrackAudio{sid, tid, 1, false, true});
    check(playback::compileSequence(editor.state().project).audio.size() == 2, "Multiple solos mix together");
    editor.execute(timeline::SetTrackAudio{sid, extra.id, 1, true, true});
    check(playback::compileSequence(editor.state().project).audio.size() == 1, "Mute takes precedence over solo");
    editor.execute(timeline::SetTrackEnabled{sid, extra.id, false});
    editor.execute(timeline::SetTrackAudio{sid, tid, 1, false, false});
    check(playback::compileSequence(editor.state().project).audio.size() == 1, "Disabled solo does not suppress enabled tracks");
    editor.execute(timeline::SetClipAudio{sid, tid, clip.id, 1, true});
    check(playback::compileSequence(editor.state().project).audio.isEmpty(), "Clip mute excludes its audio");
    const auto beforeInvalid = editor.state(); const auto count = editor.undoCount();
    check(!editor.execute(timeline::SetClipAudio{sid, tid, clip.id, 1, false, 91}).isEmpty() && editor.state() == beforeInvalid && editor.undoCount() == count, "Oversized fade rejected without model/history mutation");
    check(!editor.execute(timeline::SetTrackAudio{sid, tid, std::numeric_limits<double>::infinity()}).isEmpty(), "Nonfinite gain rejected");
    editor.execute(timeline::SetTrackLocked{sid, tid, true});
    check(!editor.execute(timeline::SetClipAudio{sid, tid, clip.id}).isEmpty() && !editor.execute(timeline::SetTrackAudio{sid, tid}).isEmpty(), "Locked audio track rejects clip and track mix edits");
    editor.replaceProject(mixedProject);
    editor.execute(timeline::SplitClip{sid, tid, clip.id, 75, project::newId()});
    const auto& halves = editor.state().project.sequences[0].tracks[1].clips;
    check(halves.size() == 2 && halves[0].fadeOutFrames == 0 && halves[1].fadeInFrames == 0 && halves[0].fadeInFrames == 30 && halves[1].fadeOutFrames == 30, "Split keeps fades on outer edges");
    editor.undo(); check(editor.state().project == mixedProject, "Split undo restores exact audio settings");
    editor.execute(timeline::TrimClip{sid, tid, clip.id, 30, 15});
    check(editor.state().project.sequences[0].tracks[1].clips[0].fadeInFrames == 15 && editor.state().project.sequences[0].tracks[1].clips[0].fadeOutFrames == 15, "Short trim clamps fades within clip duration");
    // Markers prove source trim/placement and 30000/1001 scheduling in absolute sample time.
    auto fractional = project::newProject("Fractional timing"); fractional.media = {markers.media};
    auto& fs = fractional.sequences[0]; fs.frameRate = {30000, 1001}; fs.durationFrames = 100;
    auto mc = clip; mc.id = project::newId(); mc.mediaId = markers.media.id; mc.streamIndex = markers.media.streams[0].index;
    mc.startFrame = 1; mc.durationFrames = 60; mc.sourceInTicks = 48000; mc.sourceDurationTicks = 96096; fs.tracks[1].clips = {mc};
    auto fg = playback::compileSequence(fractional);
    check(fg.audio[0].startSample == 1602 && fg.audio[0].endSample == 97698, "NTSC frame boundaries round once to absolute 48 kHz samples");
    playback::AudioMixer fractionalMixer(fg.audio, 0, fg.durationUs);
    auto fp = collect(fractionalMixer, complete);
    bool markerPositions = complete; int markerCount = 0;
    for (qsizetype n = 0; n < fp.size() / 2; ++n) if (std::abs(fp[n * 2]) > 0.01) {
        ++markerCount; markerPositions &= n == 1602 || n == 49602 || n == 97602;
    }
    check(markerPositions && markerCount == 3, "Trimmed impulse timestamps remain aligned at fractional frame rate");
    // Final saturation, whole-source error handling and bounded paused cancellation.
    auto loud = graph.audio; for (auto& source : loud) { source.gain = 16; source.fadeInSamples = source.fadeOutSamples = 0; source.trackId = tid; source.busId = bus.id; }
    playback::AudioMixer saturating(loud, 2500000, 2600000); auto hot = collect(saturating, complete);
    const auto hotStats = saturating.stats();
    check(complete && !hot.isEmpty() && std::all_of(hot.begin(), hot.end(), [](float f) { return f == 1; }) && hotStats["masterPeak"].toDouble() > 1,
        "Final sum clips at unity while retaining the pre-clip master peak");
    check(hotStats["clippedSamples"].toInteger() > 0 && hotStats["trackPeaks"].toObject().contains(tid) && hotStats["busPeaks"].toObject().contains(bus.id),
        "Audio mixer reports track/bus meters and latches the clipped sample count");
    auto missing = graph.audio; missing[0].path += ".missing";
    playback::AudioMixer offline(missing, 2000000, 2200000); auto partial = collect(offline, complete);
    check(complete && !offline.stats()["error"].toString().isEmpty() && !partial.isEmpty() && std::abs(partial[0] - 0.0625) < 0.000001, "Offline clip reports error while other audio remains audible");
    playback::AudioMixer bounded(graph.audio, 0, graph.durationUs); bounded.start();
    check(waitFor([&] { return bounded.stats()["queueHighWater"].toInt() == static_cast<int>(playback::AudioMixer::QueueLimit); }), "Paused mix queue reaches fixed bound");
    bounded.cancel(); check(bounded.wait(10000), "Cancellation wakes full mix and decoder queues");
    // A source overlap also needs a continuous real playback clock through video cuts/gaps.
    auto clockGraph = graph; clockGraph.durationUs = 3000000;
    const auto syncVideo = media::inspect(root.filePath("fixtures/generated/session-5/flash-beep.mp4"));
    const auto sourceVideo = playback::compileSource(syncVideo.media).video;
    if (!sourceVideo.isEmpty()) {
        auto v = sourceVideo.first(); v.endUs = 400000;
        auto v2 = v; v2.clipId += "second"; v2.startUs = 800000; v2.endUs = 1400000;
        clockGraph.video = {v, v2};
        playback::PlaybackController controller; controller.setDescription(clockGraph);
        check(waitFor([&] { return !controller.stats()["buffering"].toBool(); }), "Overlapping-audio controller initializes without UI blocking");
        const auto starts = controller.stats()["pipelineStarts"].toInt(); controller.play();
        check(waitFor([&] { return !controller.isPlaying(); }, 10000) && controller.positionUs() == 3000000, "Multisource playback clock reaches exact sequence end across cuts and gaps");
        check(controller.stats()["pipelineStarts"].toInt() == starts && controller.stats()["audioDecode"].toObject()["decoderHighWater"].toInt() == 2, "Video cuts/gaps preserve one continuous two-source audio pipeline");
        mixes.append(QJsonObject{{"controller", controller.stats()}});
    } else check(false, "Sync video source is available for controller regression");
    // Title and audio properties exercise the actual Qt dialogs through their normal OK path.
    timeline::TimelineWidget ui; ui.setProject(mixedProject); ui.setPlayhead(30); ui.addTitle("Session 7");
    auto state = ui.editor().state(); const auto titleTrack = ui.sequence()->tracks.last(); const auto tc = titleTrack.clips.first();
    const auto originalImage = playback::renderTitles(playback::compileSequence(state.project), {}, 1000000);
    editDialog("titleProperties", [](QWidget* w) {
        w->findChild<QPlainTextEdit*>("titleText")->setPlainText("Edited\nTitle");
        w->findChild<QFontComboBox*>("titleFont")->setCurrentText("Arial"); w->findChild<QSpinBox*>("titleSize")->setValue(72);
        w->findChild<QComboBox*>("titleAlignment")->setCurrentText("left");
        w->findChild<QLineEdit*>("titleColor")->setText("#ffffcc00"); w->findChild<QLineEdit*>("titleBackground")->setText("#80224488");
        w->findChild<QDoubleSpinBox*>("titleX")->setValue(0.4); w->findChild<QDoubleSpinBox*>("titleY")->setValue(0.7);
        w->findChild<QCheckBox*>("titleShadow")->setChecked(true); w->findChild<QLineEdit*>("titleDuration")->setText("120");
    });
    ui.editClipProperties(titleTrack.id, tc.id);
    const auto styled = ui.editor().state(); const auto title = styled.project.titles.last();
    check(title.text == "Edited\nTitle" && title.fontFamily == "Arial" && title.fontSize == 72 && title.alignment == "left" && title.color == "#ffffcc00" && title.background == "#80224488" && title.x == 0.4 && title.y == 0.7 && title.shadow && ui.sequence()->tracks.last().clips.first().durationFrames == 120, "Title dialog commits all styling and duration in one batch");
    auto tg = playback::compileSequence(styled.project);
    const auto styledImage = playback::renderTitles(tg, {}, 1000000);
    check(!styledImage.isNull() && styledImage != originalImage && playback::renderTitles(tg, {}, 999999).isNull() && playback::renderTitles(tg, {}, 5000000).isNull(), "Styled title pixels respect exclusive start/end timing");
    check(styledImage.save(out.filePath("styled-title.png")), "Styled title evidence image saved");
    int firstGlyph = styledImage.width(), lastGlyph = -1;
    for (int y = 0; y < styledImage.height(); ++y) for (int x = 0; x < styledImage.width(); ++x) {
        const auto color = styledImage.pixelColor(x, y);
        if (color.red() > 200 && color.green() > 150 && color.blue() < 80) { firstGlyph = std::min(firstGlyph, x); lastGlyph = std::max(lastGlyph, x); }
    }
    check(firstGlyph >= 512 && firstGlyph < 530 && lastGlyph > firstGlyph && lastGlyph < 900,
        "Left-aligned title glyphs anchor at saved x without fixed-box clipping");
    ui.undo(); check(ui.editor().state() == state, "Title styling and duration undo atomically"); ui.redo(); check(ui.editor().state() == styled, "Title styling redo restores exact state");
    editDialog("titleProperties", [](QWidget* w) { w->findChild<QPlainTextEdit*>("titleText")->setPlainText("Discarded"); }, false);
    ui.editClipProperties(titleTrack.id, tc.id); check(ui.editor().state() == styled, "Cancelled title properties leave model/history unchanged");
    editDialog("titleProperties", [&](QWidget* w) {
        w->findChild<QLineEdit*>("titleColor")->setText("bad color");
        w->findChild<QDialogButtonBox*>()->button(QDialogButtonBox::Ok)->click();
        check(w->isVisible() && ui.editor().state() == styled, "Invalid title color keeps dialog open and model untouched");
    }, false);
    ui.editClipProperties(titleTrack.id, tc.id);
    // Unknown/large values retain exact precision if that control was not edited.
    auto preciseTitle = title; preciseTitle.x = 0.432198765;
    auto preciseClip = clip; preciseClip.gain = 0.123456789;
    editDialog("titleProperties", [](QWidget*) {});
    const auto unchangedTitle = timeline::titleDialog(&ui, preciseTitle, 120);
    check(unchangedTitle && unchangedTitle->title == preciseTitle, "Unchanged title controls preserve serialized position precision");
    editDialog("clipAudioProperties", [](QWidget*) {});
    const auto unchangedAudio = timeline::clipAudioDialog(&ui, sid, tid, preciseClip);
    check(unchangedAudio && unchangedAudio->gain == preciseClip.gain, "Unchanged gain control preserves serialized precision");
    editDialog("clipAudioProperties", [](QWidget* w) {
        w->findChild<QDoubleSpinBox*>("clipGain")->setValue(0.75); w->findChild<QCheckBox*>("clipMuted")->setChecked(true);
        w->findChild<QLineEdit*>("clipFadeIn")->setText("10"); w->findChild<QLineEdit*>("clipFadeOut")->setText("20");
    });
    ui.editClipProperties(tid, clip.id);
    const auto audioEdit = ui.sequence()->tracks[1].clips[0];
    check(audioEdit.gain == 0.75 && audioEdit.muted && audioEdit.fadeInFrames == 10 && audioEdit.fadeOutFrames == 20, "Clip audio dialog applies gain/mute/fades");
    editDialog("trackAudioProperties", [](QWidget* w) {
        w->findChild<QDoubleSpinBox*>("trackGain")->setValue(0.4); w->findChild<QCheckBox*>("trackMuted")->setChecked(true); w->findChild<QCheckBox*>("trackSolo")->setChecked(true);
        w->findChild<QDoubleSpinBox*>("trackPan")->setValue(0.25);
        QTimer::singleShot(0, [] {
            for (auto* widget : QApplication::topLevelWidgets()) if (widget->objectName() == "trackAudioAutomation") {
                auto* table = widget->findChild<QTableWidget*>("automationVolume");
                table->insertRow(0); table->setItem(0, 0, new QTableWidgetItem("0")); table->setItem(0, 1, new QTableWidgetItem("1")); table->setItem(0, 2, new QTableWidgetItem("linear"));
                table->insertRow(1); table->setItem(1, 0, new QTableWidgetItem("120")); table->setItem(1, 1, new QTableWidgetItem("0.5")); table->setItem(1, 2, new QTableWidgetItem("eased"));
                widget->findChild<QDialogButtonBox*>()->button(QDialogButtonBox::Ok)->click();
            }
        });
        w->findChild<QPushButton*>("trackAutomationButton")->click();
    });
    ui.editTrackAudio(tid);
    const auto audioTrack = ui.sequence()->tracks[1];
    check(audioTrack.gain == 0.4 && audioTrack.muted && audioTrack.solo && audioTrack.pan == 0.25 && audioTrack.audioAutomation.volume.size() == 2,
        "Track dialog applies gain/mute/solo/pan and saves automation keys");
    editDialog("audioBusManager", [](QWidget* w) {
        w->findChild<QPushButton*>("addAudioBus")->click(); w->findChild<QTableWidget*>("audioBusTable")->item(0, 0)->setText("Music");
    });
    ui.manageAudioBuses();
    const auto createdBus = ui.sequence()->audioBuses.first();
    editDialog("trackAudioProperties", [createdBus](QWidget* w) {
        auto* buses = w->findChild<QComboBox*>("trackAudioBus"); buses->setCurrentIndex(buses->findData(createdBus.id));
    });
    ui.editTrackAudio(tid);
    check(ui.sequence()->tracks[1].audioBusId == createdBus.id, "Track audio dialog routes its track to a saved bus");
    const auto final = ui.editor().state();
    const auto saved = out.filePath("audio-title.veproject");
    check(project::ProjectStore::save(final.project, saved).isEmpty(), "Audio/title project saves atomically");
    const auto reopened = project::ProjectStore::load(saved);
    check(reopened && *reopened.project == final.project && playback::renderTitles(playback::compileSequence(*reopened.project), {}, 1000000) == styledImage, "Saved project retains all title/audio settings and rendered pixels");
    while (ui.editor().canUndo()) ui.undo(); check(ui.editor().state().project == mixedProject, "Mixed title/audio property history undoes to opened project");
    while (ui.editor().canRedo()) ui.redo(); check(ui.editor().state() == final, "Mixed title/audio property redo restores exact selection and project");
    ui.execute(timeline::SetTrackLocked{sid, titleTrack.id, true});
    auto changed = title; changed.color = "#ff000000";
    check(!ui.execute(timeline::UpsertTitle{changed}).isEmpty(), "Title styling rejects locked use");
    // Migration is explicit; missing current fields and unknown predecessor fields still fail.
    QFile old(root.filePath("fixtures/projects/multitrack-v2.veproject")); check(old.open(QIODevice::ReadOnly), "Read predecessor migration fixture");
    auto migrated = project::deserialize(old.readAll());
    check(migrated && migrated.migratedFrom == 2 && project::deserialize(project::serialize(*migrated.project)).migratedFrom == 0, "Schema 2 migrates to schema 3 with zero fades then writes current version");
    QFile current(root.filePath("fixtures/projects/multitrack-v3.veproject"));
    check(current.open(QIODevice::ReadOnly), "Read schema-3 audio fade fixture");
    const auto currentLoaded = project::deserialize(current.readAll());
    check(currentLoaded && currentLoaded.migratedFrom == 3 && project::deserialize(project::serialize(*currentLoaded.project)).project == currentLoaded.project,
        "Schema-3 fixture round-trips title/audio/fades without migration");
    auto schema = QJsonDocument::fromJson(project::serialize(final.project)).object();
    auto sequences = schema["sequences"].toArray(); auto ss = sequences[0].toObject(); auto tracks = ss["tracks"].toArray();
    auto st = tracks[1].toObject(); auto clips = st["clips"].toArray(); auto sc = clips[0].toObject(); sc.remove("fadeInFrames"); clips[0] = sc;
    st["clips"] = clips; tracks[1] = st; ss["tracks"] = tracks; sequences[0] = ss; schema["sequences"] = sequences;
    const auto missingFade = project::deserialize(QJsonDocument(schema).toJson());
    check(!missingFade && missingFade.error.contains("fadeInFrames"), "Missing schema-3 fade field is rejected with useful error");
    // Human project: full flash/beep video + audio, continuous overlapping soundtrack and a styled title.
    const auto flash = media::inspect(root.filePath("fixtures/generated/session-5/flash-beep.mp4"));
    const auto tone = media::inspect(root.filePath("fixtures/generated/session-7/soundtrack.wav"));
    auto human = project::newProject("Session 7 audio and titles"); human.media = {flash.media, tone.media};
    auto& hs = human.sequences[0]; hs.durationFrames = 360;
    for (const auto& stream : flash.media.streams) {
        project::Clip c; c.id = project::newId(); c.name = stream.kind == "video" ? "Flash video" : "Beep audio"; c.kind = stream.kind; c.mediaId = flash.media.id;
        c.streamIndex = stream.index; c.durationFrames = 360; c.sourceDurationTicks = project::framesToTicks(360, hs.frameRate, stream.timeBase, project::Rounding::Nearest).value_or(0);
        hs.tracks[stream.kind == "video" ? 0 : 1].clips.append(c);
    }
    project::Track sound; sound.id = project::newId(); sound.name = "Soundtrack"; sound.kind = "audio";
    if (!tone.media.streams.isEmpty()) {
        const auto& stream = tone.media.streams[0]; project::Clip c; c.id = project::newId(); c.name = "Continuous soundtrack"; c.kind = "audio"; c.mediaId = tone.media.id;
        c.streamIndex = stream.index; c.startFrame = 90; c.durationFrames = 270; c.fadeInFrames = c.fadeOutFrames = 30; c.gain = 0.4;
        c.sourceDurationTicks = project::framesToTicks(270, hs.frameRate, stream.timeBase, project::Rounding::Nearest).value_or(0); sound.clips.append(c);
    }
    hs.tracks.append(sound);
    auto ht = title; ht.id = project::newId(); ht.text = "Session 7\nAudio + Titles"; ht.alignment = "center"; ht.x = 0.5; human.titles = {ht};
    project::Track titles; titles.id = project::newId(); titles.name = "Titles"; titles.kind = "title";
    auto hc = tc; hc.id = project::newId(); hc.titleId = ht.id; hc.startFrame = 30; hc.durationFrames = 180; titles.clips = {hc}; hs.tracks.append(titles);
    const auto humanPath = root.filePath("fixtures/generated/session-7/audio-titles.veproject");
    check(flash.error.isEmpty() && tone.error.isEmpty() && project::ProjectStore::save(human, humanPath).isEmpty(), "Prepare human soundtrack/title/sync project");
    QFile report(out.filePath("audio-title-result.json"));
    if (!report.open(QIODevice::WriteOnly)) return 2;
    report.write(QJsonDocument(QJsonObject{{"passed", failures.isEmpty()}, {"failures", failures}, {"checkCount", checks}, {"mixes", mixes},
        {"humanFixture", humanPath}, {"evidenceLevel", "Native PCM sample oracle, rational scheduling, queue/cancellation, undo/persistence and offscreen production property dialogs/title pixels; human audibility/display acceptance pending"}}).toJson());
    return failures.isEmpty() ? 0 : 1;
}
