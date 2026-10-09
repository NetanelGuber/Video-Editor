#include "InspectorWidget.h"
#include "project/ProjectStore.h"
#include "project/Recovery.h"
#include <QAction>
#include <QCheckBox>
#include <QDoubleSpinBox>
#include <QFormLayout>
#include <QGroupBox>
#include <QLabel>
#include <QListWidget>
#include <QPlainTextEdit>
#include <QPushButton>
#include <QScrollArea>
#include <QSignalBlocker>
#include <QTabWidget>
#include <QVBoxLayout>
#include <cmath>

namespace editor::ui {
namespace {
QLabel* text(const QString& value, QWidget* parent) {
    auto* label = new QLabel(value, parent); label->setTextFormat(Qt::PlainText); label->setWordWrap(true); return label;
}
double decibels(double gain) { return gain > 0 ? 20 * std::log10(gain) : -60; }
void gainValue(QDoubleSpinBox* box, double gain) {
    box->setValue(decibels(gain)); box->setProperty("originalDisplay", box->value());
}
double readGain(QDoubleSpinBox* box, double original) {
    return box->value() == box->property("originalDisplay").toDouble() ? original : std::pow(10.0, box->value() / 20.0);
}
}
InspectorWidget::InspectorWidget(timeline::TimelineWidget& timeline, QWidget* parent) : QWidget(parent), timeline_(timeline) {
    setObjectName("inspector"); setMinimumWidth(245);
    auto* layout = new QVBoxLayout(this); layout->setContentsMargins(6, 6, 6, 6);
    context_ = text({}, this); context_->setObjectName("inspectorContext"); layout->addWidget(context_);
    tabs_ = new QTabWidget(this); tabs_->setObjectName("inspectorTabs"); layout->addWidget(tabs_);
    auto page = [this](const QString& name) {
        auto* scroll = new QScrollArea(tabs_); scroll->setWidgetResizable(true); scroll->setFrameShape(QFrame::NoFrame);
        auto* content = new QWidget(scroll); auto* l = new QVBoxLayout(content); scroll->setWidget(content); tabs_->addTab(scroll, name); return l;
    };
    auto button = [](QVBoxLayout* l, const QString& name, const char* id) {
        auto* b = new QPushButton(name); b->setObjectName(id); l->addWidget(b); return b;
    };
    auto* clipPage = page(tr("Clip"));
    summary_ = text({}, this); summary_->setObjectName("inspectorClipSummary"); clipPage->addWidget(summary_);
    enabled_ = new QCheckBox(tr("Track enabled")); enabled_->setObjectName("inspectorTrackEnabled"); clipPage->addWidget(enabled_);
    locked_ = new QCheckBox(tr("Track locked")); locked_->setObjectName("inspectorTrackLocked"); clipPage->addWidget(locked_);
    title_ = new QPlainTextEdit; title_->setObjectName("inspectorTitleText"); title_->setPlaceholderText(tr("Title text")); title_->setMaximumHeight(150); clipPage->addWidget(title_);
    titleApply_ = button(clipPage, tr("Apply title text"), "inspectorApplyTitle");
    clipDialog_ = button(clipPage, tr("Edit clip details…"), "inspectorClipDetails");
    clipDialog_->setToolTip(tr("Open the focused form for title styling, clip audio, or visual properties."));
    clipPage->addStretch();
    auto* effectsPage = page(tr("Effects"));
    effectsPage->addWidget(text(tr("Effects run from top to bottom. Check an effect to enable it. Use Edit effects for parameters, fades and keyframes."), this));
    effects_ = new QListWidget; effects_->setObjectName("inspectorEffectStack"); effects_->setMinimumHeight(130); effectsPage->addWidget(effects_);
    effectsDialog_ = button(effectsPage, tr("Edit effects…"), "inspectorEditEffects"); effectsPage->addStretch();
    auto* audioPage = page(tr("Audio / Track"));
    audioHint_ = text(tr("Track controls affect the selected audio track. Clip controls affect only the selected audio clip."), this); audioPage->addWidget(audioHint_);
    auto audioGroup = [&](const QString& caption, bool clip) {
        auto* group = new QGroupBox(caption); auto* form = new QFormLayout(group);
        auto spin = [&](const QString& name, double min, double max, const char* id) {
            auto* b = new QDoubleSpinBox; b->setRange(min, max); b->setDecimals(2); b->setObjectName(id); form->addRow(name, b); return b;
        };
        auto* gain = spin(tr("Volume (dB)"), -60, 24, clip ? "inspectorClipGain" : "inspectorTrackGain");
        auto* pan = spin(tr("Balance"), -1, 1, clip ? "inspectorClipPan" : "inspectorTrackPan"); pan->setSingleStep(0.1); pan->setToolTip(tr("−1 left, 0 center, +1 right"));
        auto* mute = new QCheckBox(tr("Mute")); mute->setObjectName(clip ? "inspectorClipMute" : "inspectorTrackMute"); form->addRow(mute);
        auto* apply = new QPushButton(tr("Apply %1 audio").arg(clip ? tr("clip") : tr("track"))); apply->setObjectName(clip ? "inspectorApplyClipAudio" : "inspectorApplyTrackAudio");
        if (clip) { clipGain_ = gain; clipPan_ = pan; clipMute_ = mute; clipApply_ = apply; }
        else { trackGain_ = gain; trackPan_ = pan; trackMute_ = mute; trackApply_ = apply; trackSolo_ = new QCheckBox(tr("Solo")); trackSolo_->setObjectName("inspectorTrackSolo"); form->addRow(trackSolo_); }
        form->addRow(apply); audioPage->addWidget(group); connect(apply, &QPushButton::clicked, this, [this, clip] { applyAudio(clip); });
    };
    audioGroup(tr("Track audio"), false); audioGroup(tr("Clip audio"), true);
    audioDialog_ = button(audioPage, tr("Routing, fades and automation…"), "inspectorAudioDetails");
    auto* buses = button(audioPage, tr("Audio buses…"), "inspectorAudioBuses"); connect(buses, &QPushButton::clicked, &timeline_, &timeline::TimelineWidget::manageAudioBuses);
    audioPage->addStretch();
    connect(clipDialog_, &QPushButton::clicked, this, [this] { timeline_.editClipProperties(); });
    connect(effectsDialog_, &QPushButton::clicked, this, [this] { timeline_.editClipEffects(); });
    connect(audioDialog_, &QPushButton::clicked, this, [this] { timeline_.editTrackAudio(); });
    connect(enabled_, &QCheckBox::clicked, this, [this](bool value) { if (track_) timeline_.execute(timeline::SetTrackEnabled{selection_.sequenceId, track_->id, value}); });
    connect(locked_, &QCheckBox::clicked, this, [this](bool value) { if (track_) timeline_.execute(timeline::SetTrackLocked{selection_.sequenceId, track_->id, value}); });
    connect(titleApply_, &QPushButton::clicked, this, [this] {
        if (!clip_ || !track_ || track_->locked) return;
        for (const auto& title : timeline_.editor().state().project.titles) if (title.id == clip_->titleId) {
            auto copy = title; copy.text = title_->toPlainText(); timeline_.execute(timeline::UpsertTitle{copy}); break;
        }
    });
    connect(effects_, &QListWidget::itemChanged, this, [this](QListWidgetItem* item) {
        if (!clip_ || !track_ || track_->locked) return;
        auto copy = clip_->effects; const auto id = item->data(Qt::UserRole).toString();
        for (auto& effect : copy) if (effect.id == id) effect.enabled = item->checkState() == Qt::Checked;
        timeline_.execute(timeline::SetClipEffects{selection_.sequenceId, track_->id, clip_->id, copy});
    });
    connect(&timeline_, &timeline::TimelineWidget::stateChanged, this, [this] {
        if (timeline_.editor().state().selection != selection_) mediaId_.clear(); refresh();
    });
    refresh();
}
void InspectorWidget::showMedia(const QString& id) { mediaId_ = id; refresh(); }
void InspectorWidget::showTimeline() { mediaId_.clear(); refresh(); }
void InspectorWidget::showSection(const QString& section) {
    showTimeline(); tabs_->setCurrentIndex(section == "effects" ? 1 : section == "audio" ? 2 : 0);
}
void InspectorWidget::refresh() {
    selection_ = timeline_.editor().state().selection; track_.reset(); clip_.reset();
    const auto& project = timeline_.editor().state().project;
    const auto* sequence = timeline_.sequence();
    if (mediaId_.isEmpty() && sequence) for (const auto& track : sequence->tracks) {
        if (selection_.trackIds.size() == 1 && selection_.trackIds.contains(track.id)) track_ = track;
        if (selection_.clipIds.size() == 1) for (const auto& clip : track.clips) if (selection_.clipIds.contains(clip.id)) { track_ = track; clip_ = clip; }
    }
    context_->setText(clip_ ? tr("%1 · %2\n%3").arg(clip_->kind, clip_->name, track_->name) : track_ ? tr("Track · %1").arg(track_->name) : tr("Select a media item, clip or track to inspect it."));
    summary_->setText(tr("Select one clip for its timing and properties."));
    if (clip_ && sequence) {
        const TimeFormat time{sequence->frameRate, timeline_.timeDisplay()};
        summary_->setText(tr("Start: %1\nDuration: %2%3").arg(time.text(clip_->startFrame), time.text(clip_->durationFrames), track_->locked ? tr("\nTrack locked. Unlock it to edit.") : QString{}));
    }
    for (const auto& media : project.media) if (media.id == mediaId_) {
        context_->setText(tr("Project Media · %1").arg(media.name));
        summary_->setText(project::mediaAvailable(media.path) ? tr("Online. Choose Preview source or Add selected media at playhead.") : tr("Offline. Select Locate file… in Project Media to restore the reference.")); break;
    }
    enabled_->setEnabled(track_.has_value()); locked_->setEnabled(track_.has_value());
    enabled_->setChecked(track_ && track_->enabled); locked_->setChecked(track_ && track_->locked);
    const bool editable = clip_ && track_ && !track_->locked;
    clipDialog_->setEnabled(editable); effectsDialog_->setEnabled(editable);
    const QSignalBlocker blocker(effects_); effects_->clear();
    if (clip_) for (const auto& effect : clip_->effects) {
        auto* item = new QListWidgetItem(effect.type, effects_); item->setData(Qt::UserRole, effect.id); item->setFlags(item->flags() | Qt::ItemIsUserCheckable);
        item->setCheckState(effect.enabled ? Qt::Checked : Qt::Unchecked);
        item->setToolTip(tr("%1 parameter(s), %2 animated parameter(s). Edit effects to change them.").arg(effect.parameters.size()).arg(effect.keyframes.size()));
    }
    effects_->setEnabled(editable);
    const bool title = clip_ && clip_->kind == "title";
    title_->setVisible(title); titleApply_->setVisible(title); title_->setEnabled(editable); titleApply_->setEnabled(editable);
    if (title) for (const auto& t : project.titles) if (t.id == clip_->titleId && title_->toPlainText() != t.text) title_->setPlainText(t.text);
    const bool trackAudio = track_ && track_->kind == "audio" && !track_->locked;
    const bool clipAudio = trackAudio && clip_ && clip_->kind == "audio";
    for (auto* widget : {static_cast<QWidget*>(trackGain_), static_cast<QWidget*>(trackPan_), static_cast<QWidget*>(trackMute_), static_cast<QWidget*>(trackSolo_), static_cast<QWidget*>(trackApply_), static_cast<QWidget*>(audioDialog_)}) widget->setEnabled(trackAudio);
    for (auto* widget : {static_cast<QWidget*>(clipGain_), static_cast<QWidget*>(clipPan_), static_cast<QWidget*>(clipMute_), static_cast<QWidget*>(clipApply_)}) widget->setEnabled(clipAudio);
    gainValue(trackGain_, track_ ? track_->gain : 1); trackPan_->setValue(track_ ? track_->pan : 0); trackPan_->setProperty("originalDisplay", trackPan_->value());
    trackMute_->setChecked(track_ && track_->muted); trackSolo_->setChecked(track_ && track_->solo);
    gainValue(clipGain_, clip_ ? clip_->gain : 1); clipPan_->setValue(clip_ ? clip_->pan : 0); clipPan_->setProperty("originalDisplay", clipPan_->value()); clipMute_->setChecked(clip_ && clip_->muted);
}
void InspectorWidget::applyAudio(bool clip) {
    if (!track_ || track_->locked || track_->kind != "audio") return;
    if (clip && clip_) {
        const auto pan = clipPan_->value() == clipPan_->property("originalDisplay").toDouble() ? clip_->pan : clipPan_->value();
        timeline_.execute(timeline::SetClipAudio{selection_.sequenceId, track_->id, clip_->id, readGain(clipGain_, clip_->gain), clipMute_->isChecked(), clip_->fadeInFrames, clip_->fadeOutFrames, pan, clip_->audioAutomation});
    } else if (!clip) {
        const auto pan = trackPan_->value() == trackPan_->property("originalDisplay").toDouble() ? track_->pan : trackPan_->value();
        timeline_.execute(timeline::SetTrackAudio{selection_.sequenceId, track_->id, readGain(trackGain_, track_->gain), trackMute_->isChecked(), trackSolo_->isChecked(), pan, track_->audioBusId, track_->audioAutomation});
    }
}
}
