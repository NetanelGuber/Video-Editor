#include "project/ProjectStore.h"
#include "project/Effects.h"
#include "timeline/TimelineWidget.h"
#include "timeline/TimelineCanvas.h"
#include "playback/PlaybackController.h"
#include "playback/AudioMixer.h"
#include "playback/VisualEffects.h"
#include "export/ExportWorker.h"
#include "media/Inspection.h"
#include <QApplication>
#include <QAction>
#include <QComboBox>
#include <QDialogButtonBox>
#include <QLineEdit>
#include <QPushButton>
#include <QDir>
#include <QFile>
#include <QElapsedTimer>
#include <QJsonArray>
#include <QJsonDocument>
#include <QProcess>
#include <QPainter>
#include <QTimer>
#include <Windows.h>
#include <psapi.h>
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstring>
using namespace editor;
namespace {
QByteArray read(const QString& path) { QFile f(path); return f.open(QIODevice::ReadOnly) ? f.readAll() : QByteArray{}; }
bool write(const QString& path, const QByteArray& data) { QFile f(path); return f.open(QIODevice::WriteOnly) && f.write(data) == data.size(); }
bool waitFor(const std::function<bool()>& predicate, int timeout = 60000) {
    QElapsedTimer timer; timer.start(); while (timer.elapsed() < timeout) { if (predicate()) return true; QApplication::processEvents(QEventLoop::AllEvents, 5); QThread::msleep(1); } return predicate();
}
qint64 memory() { PROCESS_MEMORY_COUNTERS_EX m{}; m.cb = sizeof(m); GetProcessMemoryInfo(GetCurrentProcess(), reinterpret_cast<PROCESS_MEMORY_COUNTERS*>(&m), sizeof(m)); return m.WorkingSetSize; }
project::Clip clip(const project::Media& m, const QString& kind, qint64 frames, qint64 in = 0, project::Rational fps = {30, 1}) {
    project::Clip c; c.id = project::newId(); c.name = m.name; c.mediaId = m.id; c.kind = kind; c.durationFrames = frames;
    for (const auto& s : m.streams) if (s.kind == kind) { c.streamIndex = s.index; c.sourceInTicks = project::framesToTicks(in, fps, s.timeBase, project::Rounding::Nearest).value_or(0); c.sourceDurationTicks = project::framesToTicks(frames, fps, s.timeBase, project::Rounding::Nearest).value_or(0); break; } return c;
}
QByteArray process(const QString& exe, const QStringList& args, bool& ok) { QProcess p; p.start(exe, args); ok = p.waitForStarted() && p.waitForFinished(60000) && p.exitCode() == 0; if (!ok) std::fprintf(stderr, "%s\n", p.readAllStandardError().constData()); return p.readAllStandardOutput(); }
QImage image(const QByteArray& bytes, int frame) { constexpr int stride = 320 * 180 * 4; if (frame < 0 || bytes.size() < (frame + 1) * stride) return {}; return QImage(reinterpret_cast<const uchar*>(bytes.constData() + frame * stride), 320, 180, 1280, QImage::Format_RGB32).copy(); }
double difference(const QImage& a, const QImage& b) { if (a.isNull() || a.size() != b.size()) return 1000; double sum = 0; for (int y = 0; y < a.height(); ++y) for (int x = 0; x < a.width(); ++x) { const auto p = a.pixelColor(x,y), q = b.pixelColor(x,y); sum += std::abs(p.red()-q.red()) + std::abs(p.green()-q.green()) + std::abs(p.blue()-q.blue()); } return sum / (a.width()*a.height()*3); }
}
int main(int argc, char** argv) {
    QApplication app(argc, argv); app.setQuitOnLastWindowClosed(false); const auto args = app.arguments(); if (args.size() < 3) return 2;
    QDir root(args[1]), out(args[2]); root.setPath(root.absolutePath()); out.setPath(out.absolutePath()); QDir().mkpath(out.absolutePath()); QJsonArray failures, benchmarks; int checks = 0;
    auto check = [&](bool ok, const QString& label) { ++checks; if (!ok) { failures.append(label); std::fprintf(stderr, "FAIL: %s\n", qPrintable(label)); } };
    if (args.contains("--measure")) {
        auto p = project::newProject("Four synchronized real cameras"); auto& s = p.sequences[0]; s.durationFrames = 180; s.tracks.clear();
        const auto inventory = QJsonDocument::fromJson(read(root.filePath("evidence/session-0/test-set/inventory.json"))).object()["files"].toArray();
        for (int i = 0; i < 4 && i < inventory.size(); ++i) { const auto inspected = media::inspect(inventory[i].toObject()["path"].toString()); check(inspected.error.isEmpty(), "Inspect real camera"); p.media.append(inspected.media); project::Track t; t.id = project::newId(); t.name = QString("Camera %1").arg(i+1); t.clips = {clip(inspected.media, "video", 180)}; s.tracks.append(t); }
        project::Multicam measuredGroup; for (const auto& t : s.tracks) measuredGroup.cameraTrackIds.append(t.id); measuredGroup.cuts = {{0,s.tracks[0].id}}; s.multicam=measuredGroup;
        check(s.tracks.size() == 4 && project::validate(p).isEmpty(), "Four-source synchronized graph");
        if (failures.isEmpty()) for (auto mode : {playback::DecodeMode::Cpu, playback::DecodeMode::D3D11}) {
            playback::PlaybackController controller; controller.setPreviewProfile({1280,720}); controller.setDecodeMode(mode); controller.setDescription(playback::compileSequence(p));
            controller.setCameraOverview(true);
            auto ready = [&] { const auto st = controller.stats(); return !st["buffering"].toBool() && st["presented"].toInt() > 0 && st["videoLayers"].toArray().size() == 4; };
            QElapsedTimer timer; timer.start(); check(waitFor(ready), "Four-source startup"); const auto startup = timer.elapsed();
            QJsonArray seeks; for (auto us : {2500000LL, 0LL}) { const auto before = controller.stats()["presented"].toInt(); timer.restart(); controller.seek(us); check(waitFor([&] { return ready() && controller.stats()["presented"].toInt() > before; }), "Four-source seek"); seeks.append(timer.elapsed()); check(timer.elapsed() < 1000, "Four-source seek under one second"); }
            const auto before = controller.stats()["presented"].toInt(); qint64 peak = memory(), maxSkew = 0; int synchronizedSamples = 0; QJsonArray lastLayers; controller.play(); timer.restart();
            check(waitFor([&] { peak = std::max(peak, memory()); const auto snapshot = controller.stats(); const auto layers = snapshot["videoLayers"].toArray(); if (layers.size() == 4) lastLayers = layers;
                const auto pts = snapshot["videoPtsUs"].toObject(); if (!snapshot["buffering"].toBool() && pts.size() == 4 && timer.elapsed() > 200) { qint64 lo = std::numeric_limits<qint64>::max(), hi = 0; for (const auto& value : pts) { lo=std::min(lo,value.toInteger()); hi=std::max(hi,value.toInteger()); } maxSkew=std::max(maxSkew,hi-lo); ++synchronizedSamples; }
                return !controller.isPlaying(); }, 15000), "Four-source paced playback completes");
            const auto st = controller.stats(); const auto frames = st["presented"].toInt() - before;
            check(frames >= 162 && timer.elapsed() < 7500, "Four-source 90 percent of 30 fps cadence"); check(peak < 1024LL*1024*1024, "Four-source working set below 1 GiB");
            check(lastLayers.size() == 4, "Four decoder reports retained at sequence end");
            check(synchronizedSamples > 100 && maxSkew <= 50000, "All camera presentation timestamps stay within 50 ms");
            for (const auto& layer : lastLayers) { const auto v = layer.toObject(); check(v["error"].toString().isEmpty() && v["decoded"].toInteger() >= 162, "Every camera decodes synchronized footage"); if (mode == playback::DecodeMode::D3D11) check(v["path"].toString().contains("D3D11VA"), "Every camera uses actual target GPU"); }
            benchmarks.append(QJsonObject{{"decode",mode == playback::DecodeMode::Cpu ? "CPU" : "D3D11VA"},{"startupMs",startup},{"seekMs",seeks},{"wallMs",timer.elapsed()},{"presented",frames},{"maxCameraSkewUs",maxSkew},{"synchronizedSamples",synchronizedSamples},{"peakWorkingSetBytes",peak},{"lastVideoLayers",lastLayers},{"controller",st}});
        }
        write(out.filePath("multicam-benchmark.json"), QJsonDocument(QJsonObject{{"passed",failures.isEmpty()},{"checks",checks},{"failures",failures},{"measurements",benchmarks},{"evidenceLevel","Target-PC four distinct 1080p real sources, concurrent native decoders and production 720p/30fps controller; offscreen raster presentation"}}).toJson());
        return failures.isEmpty() ? 0 : 1;
    }
    const auto gate = QJsonDocument::fromJson(read(root.filePath("evidence/session-14/multicam-benchmark.json"))).object();
    check(gate["passed"].toBool() && gate["measurements"].toArray().size() == 2, "Target-PC synchronized playback gate recorded before multicamera acceptance");
    const auto lock = QJsonDocument::fromJson(read(root.filePath("dependencies.lock.json"))).object();
    const auto bin = root.filePath(lock["packages"].toArray()[1].toObject()["prefix"].toString() + "/bin/");
    QDir generated(root.filePath("fixtures/generated/session-14")); QDir().mkpath(generated.absolutePath()); bool ok;
    QVector<project::Media> media; QVector<QByteArray> raw;
    for (int i = 0; i < 4; ++i) {
        const auto filter = i == 0 ? QString("testsrc2=s=320x180:r=30:d=4") : QString("color=c=%1:s=320x180:r=30:d=4").arg(i == 1 ? "blue" : i == 2 ? "lime" : "red");
        const auto path = generated.filePath(QString("camera-%1.mov").arg(i+1));
        process(bin + "ffmpeg.exe", {"-v","error","-nostdin","-y","-f","lavfi","-i",filter,"-f","lavfi","-i",QString("sine=frequency=%1:sample_rate=48000:duration=4").arg(440+i*110),"-c:v","libx264","-crf","0","-pix_fmt","yuv420p","-c:a","pcm_s16le",path}, ok); check(ok, "Generate independent video/audio camera");
        const auto inspected = media::inspect(path); check(inspected.error.isEmpty(), "Inspect synthetic camera"); media.append(inspected.media);
        raw.append(process(bin + "ffmpeg.exe", {"-v","error","-i",path,"-an","-f","rawvideo","-pix_fmt","bgra","pipe:1"}, ok)); check(ok && raw.last().size() == 120*320*180*4, "Independently decode every camera frame");
    }
    if (media.size() != 4 || std::any_of(raw.begin(),raw.end(),[](const auto& bytes) { return bytes.size() != 120*320*180*4; })) return 1;
    auto p = project::newProject("Nested reference"); p.media = media; auto parent = p.sequences[0]; parent.name = "Parent"; parent.width = 320; parent.height = 180; parent.durationFrames = 90;
    auto child = project::newProject().sequences[0]; child.name = "Child"; child.width = 320; child.height = 180; child.durationFrames = 120;
    child.tracks[0].clips = {clip(media[0], "video", 120)}; child.tracks[1].clips = {clip(media[0], "audio", 120)};
    child.tracks[1].gain = 0.5; child.tracks[1].clips[0].audioAutomation.volume = {{0,0.4,"linear"},{119,1,"linear"}};
    project::Clip nested; nested.id = project::newId(); nested.name = child.name; nested.sequenceId = child.id; nested.startFrame = 15; nested.durationFrames = nested.sourceDurationTicks = 60; nested.sourceInTicks = 30;
    parent.tracks[0].clips = {nested}; auto nestedAudio = nested; nestedAudio.id = project::newId(); nestedAudio.kind = "audio"; nestedAudio.gain = 0.6; nestedAudio.fadeInFrames = 6; parent.tracks[1].gain = 0.8; parent.tracks[1].clips = {nestedAudio};
    p.sequences = {parent,child}; p.exportSettings.width = 320; p.exportSettings.height = 180; p.exportSettings.quality = 8; p.exportSettings.preset = "veryfast";
    const auto base = p; check(project::validate(p).isEmpty(), "Parent with nested video and audio validates");
    check(project::deserialize(project::serialize(p)).project == std::optional<project::Project>(p), "Nested optional fields round-trip losslessly");
    auto legacy = project::newProject("Legacy schema 5"); auto old = QJsonDocument::fromJson(project::serialize(legacy)).object(); old["schemaVersion"] = 5;
    auto oldSequences = old["sequences"].toArray();
    for (qsizetype i = 0; i < oldSequences.size(); ++i) { auto sequence = oldSequences[i].toObject(); sequence.remove("primaryVideoMediaId"); sequence.remove("primaryVideoStreamIndex"); oldSequences[i] = sequence; }
    old["sequences"] = oldSequences;
    write(root.filePath("fixtures/projects/empty-v5.veproject"), QJsonDocument(old).toJson()); const auto migrated = project::deserialize(QJsonDocument(old).toJson());
    check(migrated && migrated.migratedFrom == 5 && *migrated.project == legacy, "Schema 5 migrates without optional timeline features");
    auto mislabeled=QJsonDocument::fromJson(project::serialize(p)).object(); mislabeled["schemaVersion"]=5; auto mislabeledSequences=mislabeled["sequences"].toArray();
    for(qsizetype i=0;i<mislabeledSequences.size();++i){auto sequence=mislabeledSequences[i].toObject();sequence.remove("primaryVideoMediaId");sequence.remove("primaryVideoStreamIndex");mislabeledSequences[i]=sequence;}
    mislabeled["sequences"]=mislabeledSequences; check(!project::deserialize(QJsonDocument(mislabeled).toJson()),"Nested fields cannot masquerade as schema 5");
    auto rejected = [&](project::Project bad, const QString& label) { check(!project::validate(bad).isEmpty(), label); };
    auto bad = p; bad.sequences[0].tracks[0].clips[0].sequenceId = parent.id; rejected(bad,"Direct cycle rejected");
    bad = p; auto cycle = nested; cycle.id = project::newId(); cycle.sequenceId = parent.id; cycle.sourceInTicks = 0; bad.sequences[1].tracks[0].clips = {cycle}; rejected(bad,"Indirect cycle rejected"); bad.sequences[1].tracks[0].enabled = false; rejected(bad,"Disabled-track cycle rejected");
    bad = p; bad.sequences[0].tracks[0].clips[0].sequenceId = project::newId(); rejected(bad,"Missing child rejected");
    bad = p; bad.sequences[1].frameRate = {24,1}; rejected(bad,"Mixed-rate nesting reports explicit unsupported mapping");
    bad = p; bad.sequences[0].tracks[0].clips[0].sourceInTicks = 90; rejected(bad,"Child endpoint bounds enforced");
    bad = p; bad.sequences[0].tracks[0].clips[0].effects = {project::defaultEffect("speed")}; rejected(bad,"Nested speed requires editing child");
    bad = p; for (int i = 0; i < 9; ++i) { auto s = project::newProject().sequences[0]; s.durationFrames = 120; auto c = nested; c.id = project::newId(); c.sequenceId = bad.sequences.last().id; c.startFrame = c.sourceInTicks = 0; s.tracks[0].clips = {c}; bad.sequences.append(s); } rejected(bad,"Nine nesting levels rejected");
    timeline::TimelineEditor editor(p); const auto initial = editor.state();
    check(editor.execute(timeline::TrimClip{parent.id,parent.tracks[0].id,nested.id,20,50}).isEmpty() && editor.state().project.sequences[0].tracks[0].clips[0].sourceInTicks == 35, "Nested trim advances exact child frame");
    check(editor.undo() && editor.state() == initial, "Nested trim undo exact");
    const auto right = project::newId(); check(editor.execute(timeline::SplitClip{parent.id,parent.tracks[0].id,nested.id,45,right,{}}).isEmpty(), "Nested split executes");
    check(editor.state().project.sequences[0].tracks[0].clips[1].sourceInTicks == 60 && editor.state().project.sequences[0].tracks[0].clips[1].sourceDurationTicks == 30, "Nested split retains child origin and duration");
    check(editor.undo() && editor.redo() && editor.undo() && editor.state() == initial,"Nested split redo and undo retain stable identities");
    check(!editor.execute(timeline::RemoveSequence{child.id}).isEmpty() && editor.state() == initial,"Referenced child deletion fails transactionally");
    check(!editor.execute(timeline::ResizeSequence{child.id,80}).isEmpty() && editor.state() == initial,"Child shortening cannot invalidate parent instances");
    check(editor.execute(timeline::ActivateSequence{child.id}).isEmpty() && editor.undo() && editor.state() == initial,"Sequence navigation undo preserves editing history");
    auto graph = playback::compileSequence(p); check(graph.video.size() == 1 && graph.audio.size() == 1 && playback::visualGraphError(graph).isEmpty(),"Nested graph compiles video/audio leaves");
    check(playback::sourceMediaUs(graph.video[0],500000) == 1000000 && playback::sourceTimelineUs(graph.video[0],1000000) == 500000,"Nested media/time inverse mapping at child in point");
    check(playback::activeVideoSources(graph,499999).isEmpty() && playback::activeVideoSources(graph,500000).size() == 1 && playback::activeVideoSources(graph,2500000).isEmpty(),"Nested half-open boundaries");
    const auto src = graph.audio[0]; const auto childSample = 72000LL;
    check(std::abs(playback::audioGainAtSample(src,48000) - playback::audioGainAtSample(*src.nestedSource,childSample)*0.48) < 1e-10,"Child automation and parent gains multiply at mapped sample");
    auto mix = [&](const playback::RenderDescription& d) { playback::AudioMixer mixer(d.audio,0,d.durationUs); mixer.start(); QVector<float> pcm; check(waitFor([&] { playback::AudioBlock block; while (mixer.takeAudio(block)) pcm += block.samples; return mixer.done(); }),"Shared nested PCM mix finishes"); mixer.wait(); check(mixer.stats()["error"].toString().isEmpty(),"Nested PCM decode has no error"); return pcm; };
    const auto pcm = mix(graph); auto childProject = p; childProject.activeSequenceId = child.id; const auto childPcm = mix(playback::compileSequence(childProject));
    double rms = 0; for (int i = 48000*2; i < 96000*2; ++i) { const auto e = pcm[i] - childPcm[i+24000*2]*0.48; rms += e*e; } rms = std::sqrt(rms/(48000*2)); check(rms < 0.00001,"Nested PCM equals independently sliced child mix");
    QJsonArray exportReports; double worstPixels = 0, worstAudio = 0;
    auto exportCheck = [&](project::Project reference, const QString& name, const std::function<QImage(int)>& expected) {
        reference.exportSettings.outputPath = out.filePath(name + ".mp4");
        check(project::ProjectStore::save(reference,out.filePath(name+".veproject")).isEmpty(),"Save reference project");
        const auto reopened = project::ProjectStore::load(out.filePath(name+".veproject")); check(reopened && project::serialize(*reopened.project) == project::serialize(reference),"Export source reopens with same settings");
        const auto d = playback::compileSequence(reopened ? *reopened.project : reference);
        exporting::ExportWorker worker(d,reference.exportSettings); worker.start(); check(waitFor([&] { return worker.isFinished(); }),"Background nested/camera export completes"); worker.wait(); const auto report = worker.result(); exportReports.append(report); check(report["passed"].toBool(),"Nested/camera export succeeds: " + report["error"].toString());
        const auto bytes = process(bin+"ffmpeg.exe",{"-v","error","-i",reference.exportSettings.outputPath,"-an","-f","rawvideo","-pix_fmt","bgra","pipe:1"},ok); check(ok,"Independent MP4 decode");
        const auto count = reference.sequences[0].durationFrames; check(bytes.size() == count*320*180*4,"Export includes exact first through last frame");
        for (int frame = 0; frame < count; ++frame) { const auto e = difference(image(bytes,frame),expected(frame)); worstPixels = std::max(worstPixels,e); check(e < 6,"Independently decoded frame " + name + " " + QString::number(frame)); }
        const auto audioBytes = process(bin+"ffmpeg.exe",{"-v","error","-i",reference.exportSettings.outputPath,"-vn","-f","f32le","-ac","2","-ar","48000","pipe:1"},ok); check(ok,"Independent AAC decode"); const auto expectedAudio = mix(d);
        double e = 0; const auto samples = std::min<qsizetype>(expectedAudio.size(),audioBytes.size()/4); for (qsizetype i=0;i<samples;++i) { float v; std::memcpy(&v,audioBytes.constData()+i*4,4); const auto delta=v-expectedAudio[i]; e+=delta*delta; } const auto audioRms = samples ? std::sqrt(e/samples) : 1000; worstAudio=std::max(worstAudio,audioRms); check(samples >= expectedAudio.size() && audioRms < 0.005,"AAC matches mapped shared PCM timing");
    };
    QImage black(320,180,QImage::Format_RGB32); black.fill(Qt::black);
    exportCheck(p,"nested",[&](int frame){ return frame < 15 || frame >= 75 ? black : image(raw[0],frame+15); });
    auto grand = project::newProject().sequences[0]; grand.name="Grandparent"; grand.width=320; grand.height=180; grand.durationFrames=90;
    for(auto& t:grand.tracks){ auto c=nested; c.id=project::newId(); c.sequenceId=parent.id; c.kind=t.kind; c.sourceInTicks=c.startFrame=0; c.durationFrames=c.sourceDurationTicks=90; t.clips={c}; }
    p=base; p.sequences.prepend(grand); p.activeSequenceId=grand.id;
    exportCheck(p,"two-level-nesting",[&](int frame){ return frame<15||frame>=75?black:image(raw[0],frame+15); });
    p=base; p.sequences[0].durationFrames=110; auto repeat=nested; repeat.id=project::newId(); repeat.startFrame=80; repeat.sourceInTicks=0; repeat.durationFrames=repeat.sourceDurationTicks=30; p.sequences[0].tracks[0].clips.append(repeat);
    exportCheck(p,"repeated-child",[&](int frame){ if(frame>=80)return image(raw[0],frame-80); return frame<15||frame>=75?black:image(raw[0],frame+15); });
    p=base; for(auto& sequence:p.sequences)sequence.frameRate={30000,1001}; p.exportSettings.frameRate={30000,1001};
    exportCheck(p,"fractional-nesting",[&](int frame){ return frame<15||frame>=75?black:image(raw[0],frame+15); });
    // Child effects/titles remain a composition group, with outer effects applied after compositing.
    p = base; project::Title title; title.id = project::newId(); title.text = "Nested title"; title.fontSize=24; p.titles={title}; project::Track titles; titles.id=project::newId(); titles.name="Titles"; titles.kind="title";
    project::Clip tc; tc.id=project::newId(); tc.kind="title"; tc.name=title.text; tc.titleId=title.id; tc.startFrame=40; tc.durationFrames=30; titles.clips={tc}; p.sequences[1].tracks.append(titles);
    auto color=project::defaultEffect("color"); color.parameters["exposure"]=0.25; p.sequences[1].tracks[0].clips[0].effects={color}; auto opacity=project::defaultEffect("opacity"); opacity.parameters["value"]=0.7; p.sequences[0].tracks[0].clips[0].effects={opacity};
    const auto effectsGraph=playback::compileSequence(p); const auto childGraph=*effectsGraph.children[nested.id];
    exportCheck(p,"nested-effects-titles",[&](int frame){ if(frame<15||frame>=75)return black; const auto childFrame=frame+15; auto layer=playback::renderLayers(childGraph,{{child.tracks[0].clips[0].id,image(raw[0],childFrame)}},project::framesToTicks(childFrame,{30,1},{1,1000000},project::Rounding::Nearest).value_or(0),{320,180}); auto outer=p.sequences[0].tracks[0].clips[0]; auto visual=playback::applyEffects(layer,outer,frame-15); QImage result=black.copy(); QPainter painter(&result); painter.drawImage(0,0,visual.image); return result; });
    // Retimed child audio is cropped after pitch preservation, without retiming it a second time.
    p=base; p.sequences[1].tracks[1].clips[0].effects={project::defaultEffect("speed")}; p.sequences[1].tracks[1].clips[0].effects[0].parameters["rate"]=2.0; p.sequences[1].tracks[1].clips[0].durationFrames=60;
    p.sequences[0].tracks[1].clips[0].sourceInTicks=12; p.sequences[0].tracks[1].clips[0].sourceDurationTicks=p.sequences[0].tracks[1].clips[0].durationFrames=30;
    const auto retimed=mix(playback::compileSequence(p)); childProject=p; childProject.activeSequenceId=child.id; const auto retimedChild=mix(playback::compileSequence(childProject)); rms=0;
    for(int i=48000*2;i<64000*2;++i){ const auto e=retimed[i]-retimedChild[i-4800*2]*0.48; rms+=e*e; } check(std::sqrt(rms/(16000*2))<0.00001,"Nested speed audio preserves child PCM and pitch policy");
    exportCheck(p,"nested-retimed-audio",[&](int frame){return frame<15||frame>=75?black:image(raw[0],frame+15);});
    auto cameras=project::newProject("Four camera cuts"); cameras.media=media; auto& cs=cameras.sequences[0]; cs.width=320; cs.height=180; cs.durationFrames=90; cs.tracks.clear(); project::Multicam group;
    for(int i=0;i<4;++i){ project::Track t; t.id=project::newId(); t.name=QString("Camera %1").arg(i+1); t.clips={clip(media[i],"video",90,30)}; group.cameraTrackIds.append(t.id); cs.tracks.append(t); }
    group.cuts={{0,group.cameraTrackIds[0]},{20,group.cameraTrackIds[1]},{40,group.cameraTrackIds[2]},{60,group.cameraTrackIds[3]},{80,group.cameraTrackIds[0]}}; cs.multicam=group;
    project::Track audio; audio.id=project::newId(); audio.kind="audio"; audio.name="Fixed camera 1 audio"; audio.clips={clip(media[0],"audio",90,30)}; cs.tracks.append(audio); cameras.exportSettings=base.exportSettings;
    check(project::validate(cameras).isEmpty() && project::deserialize(project::serialize(cameras)).project==std::optional<project::Project>(cameras),"Multicamera cuts and fixed audio round-trip");
    exportCheck(cameras,"multicam",[&](int frame){ const int camera=frame<20?0:frame<40?1:frame<60?2:frame<80?3:0; return image(raw[camera],frame+30); });
    timeline::TimelineEditor cameraEditor(cameras); const auto cameraBefore=cameraEditor.state();
    check(cameraEditor.execute(timeline::SwitchCamera{cs.id,group.cameraTrackIds[2],20}).isEmpty() && project::cameraAt(cameraEditor.state().project.sequences[0],20)==group.cameraTrackIds[2],"Switch replaces existing cut at exact frame");
    check(cameraEditor.undo() && cameraEditor.state()==cameraBefore,"Camera switch undo restores cuts");
    check(!cameraEditor.execute(timeline::SwitchCamera{cs.id,project::newId(),10}).isEmpty() && cameraEditor.state()==cameraBefore,"Missing camera switch rolls back");
    check(cameraEditor.execute(timeline::SetMulticam{cs.id,{}}).isEmpty() && cameraEditor.undo() && cameraEditor.state()==cameraBefore,"Ungroup and undo preserve all camera media");
    bad=cameras; bad.sequences[0].multicam->cuts[0].frame=1; rejected(bad,"Missing frame-zero cut rejected"); bad=cameras; bad.sequences[0].multicam->cuts[1].frame=0; rejected(bad,"Duplicate cut positions rejected"); bad=cameras; bad.sequences[0].multicam->cameraTrackIds.append(group.cameraTrackIds[0]); rejected(bad,"More than four cameras rejected");
    auto offline=cameras; offline.media[3].path=out.filePath("missing-camera.mkv"); auto settings=offline.exportSettings; settings.outputPath=out.filePath("offline.mp4"); check(exporting::validateExport(playback::compileSequence(offline),settings).contains("missing"),"Missing unselected camera media reported at export preflight");
    timeline::TimelineWidget ui; ui.setProject(base);
    ui.execute(timeline::SetSelection{{parent.id,{parent.tracks[0].id},{nested.id}}}); ui.findChild<QAction*>("openChildAction")->trigger(); check(ui.sequence()->id==child.id,"Native Open child uses nested source"); ui.undo(); check(ui.sequence()->id==parent.id,"Native child navigation retains undo history");
    ui.canvas()->setOfflineMediaIds({media[0].id}); check(ui.canvas()->clipCaption(nested).startsWith("OFFLINE"),"Nested missing source shows offline caption");
    ui.newSequence("Empty child"); check(ui.sequence()->name=="Empty child" && ui.editor().canUndo(),"Native new sequence is undoable"); ui.undo(); check(ui.sequence()->id==parent.id,"Undo sequence creation returns to parent");
    ui.setProject(cameras); ui.setPlayhead(25); ui.findChild<QAction*>("camera3Action")->trigger(); check(project::cameraAt(*ui.sequence(),25)==group.cameraTrackIds[2],"Native camera button inserts playhead cut"); ui.undo(); check(*ui.sequence()==cs,"Native camera switch undo restores sequence");
    QTimer::singleShot(0,[&]{ auto* dialog=QApplication::activeModalWidget(); check(dialog&&dialog->objectName()=="multicamDialog","Native multicamera creation dialog opens"); if(!dialog)return; for(int i=0;i<4;++i){ dialog->findChild<QComboBox*>(QString("multicamSource%1").arg(i+1))->setCurrentIndex(i+1); dialog->findChild<QLineEdit*>(QString("multicamOffset%1").arg(i+1))->setText("15"); } dialog->findChild<QDialogButtonBox*>()->button(QDialogButtonBox::Ok)->click(); });
    ui.createMulticam(); check(ui.sequence()->multicam && ui.sequence()->multicam->cameraTrackIds.size()==4 && ui.sequence()->durationFrames==105 && ui.sequence()->tracks[0].clips[0].sourceInTicks>0,"Native multicamera dialog aligns source offsets and shortest remaining duration"); ui.undo(); check(*ui.sequence()==cs,"Multicamera creation undo restores original document");
    const auto cg=playback::compileSequence(cameras); playback::PlaybackController controller; controller.setDescription(cg); check(waitFor([&]{return !controller.stats()["buffering"].toBool()&&controller.stats()["presented"].toInt()>0;}),"Four-camera production controller ready");
    const auto starts=controller.stats()["pipelineStarts"].toInt(); controller.play(); auto switched=cameras; switched.sequences[0].multicam->cuts.insert(1,{10,group.cameraTrackIds[2]}); check(controller.updateCameraCuts(playback::compileSequence(switched))&&controller.isPlaying()&&controller.stats()["pipelineStarts"].toInt()==starts,"Live camera edits keep warm decoders and audio clock running"); controller.setCameraOverview(true); check(controller.description().cameraOverview,"Overview is native preview state"); controller.pause();
    auto overview=cg; overview.cameraOverview=true; QMap<QString,QImage> frames; for(int i=0;i<4;++i)frames[cs.tracks[i].clips[0].id]=image(raw[i],30); const auto grid=playback::renderLayers(overview,frames,0,{320,180}); check(grid.pixelColor(240,45).blue()>200 && grid.pixelColor(80,135).green()>200 && grid.pixelColor(240,135).red()>200,"Camera overview contains distinct synchronized thumbnails");
    check(!playback::compileSequence(cameras).cameraOverview,"Export compilation excludes camera overview");
    // The relay must preserve original 4K detail, even though multicamera preview is capped.
    const auto high=media::inspect(root.filePath("fixtures/generated/pinned/synthetic-4k-h264-aac.mp4")); check(high.error.isEmpty(),"Inspect original 4K regression fixture");
    if(high.error.isEmpty()) {
        auto highProject=project::newProject("Nested original 4K"); highProject.media={high.media}; auto hs=project::newProject().sequences[0]; hs.width=3840; hs.height=2160; hs.durationFrames=2; hs.tracks[0].clips={clip(high.media,"video",2)};
        auto& hp=highProject.sequences[0]; hp.width=3840; hp.height=2160; hp.durationFrames=2; auto hc=nested; hc.id=project::newId(); hc.sequenceId=hs.id; hc.startFrame=hc.sourceInTicks=0; hc.durationFrames=hc.sourceDurationTicks=2; hp.tracks[0].clips={hc}; highProject.sequences.append(hs);
        auto settings4k=highProject.exportSettings; settings4k.width=3840; settings4k.height=2160; settings4k.audioEnabled=false; settings4k.quality=8; settings4k.preset="veryfast"; settings4k.outputPath=out.filePath("nested-4k.mp4");
        exporting::ExportWorker worker(playback::compileSequence(highProject),settings4k); worker.start(); check(waitFor([&]{return worker.isFinished();}),"Nested full-resolution export finishes"); worker.wait(); exportReports.append(worker.result()); check(worker.result()["passed"].toBool(),"Nested full-resolution export succeeds");
        const auto metadata=QJsonDocument::fromJson(process(bin+"ffprobe.exe",{"-v","error","-show_streams","-of","json",settings4k.outputPath},ok)).object()["streams"].toArray(); check(ok&&!metadata.isEmpty()&&metadata[0].toObject()["width"]==3840&&metadata[0].toObject()["height"]==2160,"Nested export retains original 3840x2160 resolution");
        auto fullFrame=[&](const QString& path){ const auto bytes=process(bin+"ffmpeg.exe",{"-v","error","-i",path,"-frames:v","1","-an","-f","rawvideo","-pix_fmt","bgra","pipe:1"},ok); check(ok&&bytes.size()==3840*2160*4,"Independently decode full 4K frame"); return bytes.size()==3840*2160*4?QImage(reinterpret_cast<const uchar*>(bytes.constData()),3840,2160,3840*4,QImage::Format_RGB32).copy():QImage{}; };
        const auto original=fullFrame(high.media.path), actual=fullFrame(settings4k.outputPath); const auto pixelError=difference(original,actual); worstPixels=std::max(worstPixels,pixelError); check(pixelError<6,"Nested 4K export preserves source detail without preview scaling");
    }
    check(project::ProjectStore::save(base,generated.filePath("nested.veproject")).isEmpty() && project::ProjectStore::save(cameras,generated.filePath("multicam.veproject")).isEmpty(),"Ready-to-open nested and camera fixtures saved");
    write(out.filePath("sequences-result.json"),QJsonDocument(QJsonObject{{"passed",failures.isEmpty()},{"checks",checks},{"failures",failures},{"exports",exportReports},{"worstPixelMeanError",worstPixels},{"worstAudioRmsError",worstAudio},{"nestedPcmRmsError",rms},{"evidenceLevel","Model, native offscreen controls, shared PCM, independently decoded MP4 video/audio; no human visual acceptance"}}).toJson());
    return failures.isEmpty()?0:1;
}
