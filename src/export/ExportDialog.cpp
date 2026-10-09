#include "ExportDialog.h"
#include <QCheckBox>
#include <QComboBox>
#include <QFileDialog>
#include <QFileInfo>
#include <QFormLayout>
#include <QHBoxLayout>
#include <QJsonDocument>
#include <QLabel>
#include <QListView>
#include <QLineEdit>
#include <QMessageBox>
#include <QProgressBar>
#include <QPushButton>
#include <QSignalBlocker>
#include <QSpinBox>
#include <QDoubleSpinBox>
#include <QStandardItemModel>
#include <QTextEdit>
#include <QTimer>
#include <QVBoxLayout>
#include <QDebug>
#include <QScrollArea>
#include <QTableWidget>
#include <QHeaderView>
#include <QScreen>
#include <QGroupBox>
#include <QTabWidget>
#include <numeric>
#include <cmath>
#include <climits>
extern "C" {
#include <libavcodec/avcodec.h>
#include <libavformat/avformat.h>
}
namespace editor::exporting {
namespace {
void select(QComboBox* box, const QString& value) {
    auto index = box->findData(value); if (index < 0) { box->addItem("Unavailable saved choice: " + value, value); index = box->count() - 1; }
    box->setCurrentIndex(index);
}
QString conciseStatus(const QString& text) {
    return text.size() <= 550 ? text : text.left(350) + QStringLiteral(" … ") + text.right(180);
}
project::Rational automaticOutputFrameRate(const playback::RenderDescription& description) {
    if (description.sequence && !description.sequence->primaryVideoMediaId.isEmpty()) {
        for (const auto& media : description.media) if (media.id == description.sequence->primaryVideoMediaId)
            for (const auto& stream : media.streams)
                if (stream.kind == "video" && stream.index == description.sequence->primaryVideoStreamIndex &&
                    stream.frameRate.numerator > 0 && stream.frameRate.denominator > 0) return stream.frameRate;
    }
    return description.frameRate;
}
QByteArray customEncoderProbeKey(const project::ExportSettings& settings) {
    return encoderCatalogCacheKey(settings);
}
QSize simpleOutputSize(const playback::RenderDescription& description, const QString& choice) {
    const int longLimit = choice == "720" ? 1280 : 1920, shortLimit = choice == "720" ? 720 : 1080;
    double scale = 1;
    if (choice != "sequence") scale = std::min({1.0, double(longLimit) / std::max(description.width, description.height), double(shortLimit) / std::min(description.width, description.height)});
    return {std::max(2, int(description.width * scale) / 2 * 2), std::max(2, int(description.height * scale) / 2 * 2)};
}
}
ExportDialog::ExportDialog(playback::RenderDescription d, project::ExportSettings s, QWidget* parent)
    : QDialog(parent), description_(std::move(d)), settings_(std::move(s)), automaticFrameRate_(automaticOutputFrameRate(description_)) {
    setObjectName("exportDialog"); setWindowTitle(tr("Export video"));
    const auto screen = QGuiApplication::primaryScreen(); const auto available = screen ? screen->availableGeometry().size() : QSize(880, 930);
    resize(std::min(800, available.width() - 80), std::min(850, available.height() - 80)); rebuilding_ = true;
    if (settings_ == project::ExportSettings{}) { settings_.width = description_.width; settings_.height = description_.height; }
    if (!settings_.frameRateCustomized) settings_.frameRate = automaticFrameRate_;
    auto* layout = new QVBoxLayout(this);
    auto* summary = new QLabel(this); summary->setObjectName("exportSummary"); summary->setWordWrap(true); summary->setTextFormat(Qt::PlainText); layout->addWidget(summary);
    options_ = new QWidget(this); auto* optionsLayout = new QVBoxLayout(options_); optionsLayout->setContentsMargins(0, 0, 0, 0); layout->addWidget(options_);
    auto* pathRow = new QWidget(options_); auto* pathLayout = new QHBoxLayout(pathRow); pathLayout->setContentsMargins(0, 0, 0, 0);
    pathLayout->addWidget(new QLabel(tr("Output file"), pathRow));
    path_ = new QLineEdit(settings_.outputPath, pathRow); path_->setObjectName("exportPath");
    auto* browse = new QPushButton(tr("Browse…"), pathRow); browse->setObjectName("exportBrowse"); pathLayout->addWidget(path_); pathLayout->addWidget(browse); optionsLayout->addWidget(pathRow);
    tabs_ = new QTabWidget(options_); tabs_->setObjectName("exportTabs"); optionsLayout->addWidget(tabs_);
    auto page = [&](const char* name, const QString& label) {
        auto* scroll = new QScrollArea(tabs_); scroll->setWidgetResizable(true); scroll->setObjectName(name);
        auto* content = new QWidget(scroll); scroll->setWidget(content); tabs_->addTab(scroll, label); return content;
    };
    auto* simple = page("exportSimpleTab", tr("Simple")); auto* simpleLayout = new QVBoxLayout(simple);
    simpleControls_ = new QWidget(simple); auto* simpleForm = new QFormLayout(simpleControls_); simpleLayout->addWidget(simpleControls_);
    simpleTarget_ = new QComboBox(simpleControls_); simpleTarget_->setObjectName("exportSimpleTarget");
    for (const auto& profile : compatibilityProfiles()) { simpleTarget_->addItem(profile.label, profile.id); simpleTarget_->setItemData(simpleTarget_->count()-1, profile.explanation, Qt::ToolTipRole); }
    simpleForm->addRow(tr("Playback target"), simpleTarget_);
    simpleSize_ = new QComboBox(simpleControls_); simpleSize_->setObjectName("exportSimpleSize");
    simpleSize_->addItem(tr("Sequence size"), "sequence"); simpleSize_->addItem(tr("Fit within 1080p"), "1080"); simpleSize_->addItem(tr("Fit within 720p"), "720"); simpleForm->addRow(tr("Video size"), simpleSize_);
    simpleQuality_ = new QComboBox(simpleControls_); simpleQuality_->setObjectName("exportSimpleQuality");
    simpleQuality_->addItem(tr("Standard"), 20); simpleQuality_->addItem(tr("Higher quality — larger file"), 18); simpleQuality_->addItem(tr("Smaller file — less detail"), 24); simpleForm->addRow(tr("Picture quality"), simpleQuality_);
    simpleAudio_ = new QCheckBox(tr("Include audio"), simpleControls_); simpleAudio_->setObjectName("exportSimpleAudio"); simpleForm->addRow(simpleAudio_);
    auto* simpleTiming = new QLabel(tr("Frame rate follows Primary Video, or the sequence when no Primary Video is selected. Use Advanced to set a different rate."), simple); simpleTiming->setWordWrap(true); simpleLayout->addWidget(simpleTiming);
    simpleNotice_ = new QLabel(simple); simpleNotice_->setObjectName("exportSimpleNotice"); simpleNotice_->setWordWrap(true); simpleNotice_->setTextFormat(Qt::PlainText); simpleLayout->addWidget(simpleNotice_); simpleLayout->addStretch();
    auto* advanced = page("exportAdvancedTab", tr("Advanced")); auto* form = new QVBoxLayout(advanced);
    auto group = [&](const char* name, const QString& title) {
        auto* widget = new QGroupBox(title, advanced); widget->setObjectName(name); form->addWidget(widget); return new QFormLayout(widget);
    };
    auto combo = [&](const char* name) { auto* box = new QComboBox(advanced); box->setObjectName(name); return box; };
    auto* videoForm = group("exportVideoGroup", tr("Video encoder and hardware"));
    format_ = combo("exportFormat"); for (const auto& profile : compatibilityProfiles()) { format_->addItem(profile.label, profile.id); format_->setItemData(format_->count()-1, profile.explanation, Qt::ToolTipRole); }
    format_->addItem(tr("Custom encoder / container"), "custom"); select(format_, settings_.compatibilityProfile); videoForm->addRow(tr("Playback target"), format_);
    encoderChoice_ = combo("exportEncoder"); encoderChoice_->addItem(tr("Software (target's x264 / x265 encoder)"), "software"); encoderChoice_->addItem(tr("Automatic (hardware, then software; native defaults)"), "auto");
    for (const auto& name : videoEncoderNames()) encoderChoice_->addItem(name, name);
    select(encoderChoice_, settings_.videoEncoder); videoForm->addRow(tr("Video encoder"), encoderChoice_);
    container_ = combo("exportContainer"); videoForm->addRow(tr("Container"), container_);
    auto* hardwareHint = new QLabel(tr("Automatic tries hardware then software. A named hardware encoder must pass a fresh check on this computer before Export is enabled."), advanced); hardwareHint->setWordWrap(true); videoForm->addRow(hardwareHint);
    auto* sizeForm = group("exportSizeGroup", tr("Size and frame rate"));
    auto spin = [&](const char* name, int low, int high, int value) { auto* box = new QSpinBox(advanced); box->setObjectName(name); box->setRange(low, high); box->setValue(value); return box; };
    width_ = spin("exportWidth", 1, 32768, settings_.width); height_ = spin("exportHeight", 1, 32768, settings_.height); sizeForm->addRow(tr("Width (pixels)"), width_); sizeForm->addRow(tr("Height (pixels)"), height_);
    automaticRate_ = new QCheckBox(tr("Automatic FPS (Primary Video / sequence)"), advanced); automaticRate_->setObjectName("exportAutomaticRate"); automaticRate_->setChecked(!settings_.frameRateCustomized); sizeForm->addRow(automaticRate_);
    rateNumerator_ = spin("exportRateNumerator", 1, INT_MAX, static_cast<int>(settings_.frameRate.numerator)); rateDenominator_ = spin("exportRateDenominator", 1, INT_MAX, static_cast<int>(settings_.frameRate.denominator));
    sizeForm->addRow(tr("Frame rate numerator"), rateNumerator_); sizeForm->addRow(tr("Frame rate denominator"), rateDenominator_);
    auto* timing = new QLabel(tr("30000 / 1001 ≈ 29.97 fps. A different output rate does not retime your timeline."), advanced); timing->setWordWrap(true); sizeForm->addRow(timing);
    auto* qualityForm = group("exportQualityGroup", tr("Quality and encoding speed"));
    speed_ = combo("exportSpeed"); qualityForm->addRow(tr("Encoding speed / preset"), speed_);
    qualityMode_ = combo("exportQualityMode"); qualityMode_->addItem(tr("Native quality"), "native"); qualityMode_->addItem(tr("Encoder default / native overrides"), "default"); qualityMode_->addItem(tr("Target bitrate (kbit/s)"), "bitrate"); select(qualityMode_, settings_.qualityMode); qualityForm->addRow(tr("Quality / rate control"), qualityMode_);
    quality_ = new QDoubleSpinBox(advanced); quality_->setObjectName("exportQuality"); quality_->setDecimals(2); quality_->setRange(0, 1000000); quality_->setValue(settings_.quality); qualityLabel_ = new QLabel(advanced); qualityLabel_->setWordWrap(true); qualityForm->addRow(qualityLabel_, quality_);
    auto* qualityHint = new QLabel(tr("Native quality scales belong to each encoder. Slower presets usually take longer to retain detail in a smaller file. Target bitrate aims at an average size; it does not guarantee quality."), advanced); qualityHint->setWordWrap(true); qualityForm->addRow(qualityHint);
    auto* pixelForm = group("exportPixelGroup", tr("Pixel and color format"));
    pixel_ = combo("exportPixelFormat"); pixelForm->addRow(tr("Pixel format"), pixel_);
    auto* colorHint = new QLabel(tr("Output is BT.709 SDR. RGB uses full range; YUV normally uses limited range. A 10-bit format changes storage, not HDR or the precision of the 8-bit editing image."), advanced); colorHint->setWordWrap(true); colorHint->setObjectName("exportColorHint"); pixelForm->addRow(colorHint);
    auto* audioForm = group("exportAudioGroup", tr("Audio"));
    audio_ = new QCheckBox(tr("Include audio (48 kHz stereo)"), advanced); audio_->setObjectName("exportAudio"); audio_->setChecked(settings_.audioEnabled); audioForm->addRow(audio_);
    audioEncoder_ = combo("exportAudioEncoder"); audioForm->addRow(tr("Audio encoder"), audioEncoder_);
    audioBitrate_ = combo("exportAudioBitrate"); audioForm->addRow(tr("Audio bitrate (kbit/s)"), audioBitrate_);
    auto* audioHint = new QLabel(tr("Higher bitrate usually retains more sound detail and uses more space. FLAC and PCM are lossless and use their encoder defaults instead."), advanced); audioHint->setWordWrap(true); audioForm->addRow(audioHint);
    auto* nativeGroup = new QGroupBox(tr("Profile, tune and encoder options"), advanced); nativeGroup->setObjectName("exportNativeGroup"); nativeGroup->setCheckable(true); nativeGroup->setChecked(false); form->addWidget(nativeGroup);
    auto* nativeLayout = new QVBoxLayout(nativeGroup);
    auto* nativeHint = new QLabel(nativeGroup); nativeHint->setObjectName("exportNativeHint"); nativeHint->setWordWrap(true); nativeLayout->addWidget(nativeHint);
    nativeOptions_ = new QTableWidget(0, 3, nativeGroup); nativeOptions_->setObjectName("exportNativeOptions"); nativeOptions_->setHorizontalHeaderLabels({tr("Encoder option"), tr("Override"), tr("Default / range")});
    nativeOptions_->horizontalHeader()->setSectionResizeMode(QHeaderView::Stretch); nativeOptions_->setMinimumHeight(240); nativeOptions_->setMaximumHeight(360); nativeLayout->addWidget(nativeOptions_);
    nativeHint->hide(); nativeOptions_->hide();
    connect(nativeGroup, &QGroupBox::toggled, this, [nativeHint, this](bool expanded) { nativeHint->setVisible(expanded); nativeOptions_->setVisible(expanded); });
    auto* capabilityButtons = new QHBoxLayout; auto* refresh = new QPushButton(tr("Refresh encoder capabilities"), advanced); refresh->setObjectName("exportRefreshCapabilities"); auto* details = new QPushButton(tr("Export details…"), advanced); details->setObjectName("exportCapabilityDetails"); capabilityButtons->addWidget(refresh); capabilityButtons->addWidget(details); form->addLayout(capabilityButtons); form->addStretch();
    auto* reset = new QPushButton(tr("Use compatible defaults"), options_); reset->setObjectName("exportCompatibleDefaults"); reset->setToolTip(tr("Keep the destination and audio inclusion; reset format, encoder, size, FPS and tuning to Desktop MP4 defaults.")); optionsLayout->addWidget(reset); connect(reset, &QPushButton::clicked, this, &ExportDialog::useCompatibleDefaults);
    capabilityMessage_ = new QLabel(this); capabilityMessage_->setObjectName("exportCapabilities"); capabilityMessage_->setTextFormat(Qt::PlainText); capabilityMessage_->setWordWrap(true); layout->addWidget(capabilityMessage_);
    progress_ = new QProgressBar(this); progress_->setObjectName("exportProgress"); progress_->setRange(0, 100); progress_->setValue(0); layout->addWidget(progress_);
    message_ = new QLabel(this); message_->setObjectName("exportMessage"); message_->setTextFormat(Qt::PlainText); message_->setWordWrap(true); layout->addWidget(message_);
    auto* buttons = new QHBoxLayout; layout->addLayout(buttons); start_ = new QPushButton(tr("Export"), this); start_->setObjectName("exportStart"); start_->setDefault(true); cancel_ = new QPushButton(tr("Cancel export"), this); cancel_->setObjectName("exportCancel"); cancel_->setEnabled(false); close_ = new QPushButton(tr("Close"), this); close_->setObjectName("exportClose"); buttons->addWidget(start_); buttons->addWidget(cancel_); buttons->addWidget(close_);
    checkTimer_ = new QTimer(this); checkTimer_->setSingleShot(true); checkTimer_->setInterval(200); connect(checkTimer_, &QTimer::timeout, this, &ExportDialog::launchCheck);
    rebuilding_ = false; updateControls(false);
    if (!simpleCompatible()) tabs_->setCurrentIndex(1);
    connect(browse, &QPushButton::clicked, this, [this] { const auto ext = containerExtension(container_->currentData().toString()); auto path = QFileDialog::getSaveFileName(this, tr("Export video"), path_->text(), tr("Video (*.%1)").arg(ext), nullptr, QFileDialog::DontConfirmOverwrite); if (!path.isEmpty()) { if (QFileInfo(path).suffix().isEmpty()) path += "." + ext; path_->setText(path); } });
    const auto chooseProfile = [this] {
        if (rebuilding_) return; settings_ = selectedSettings(); const auto p = compatibilityProfile(format_->currentData().toString());
        if (p) { settings_.container = p->container; settings_.videoCodec = p->codec; settings_.audioCodec = "aac"; settings_.audioEncoder = "aac"; settings_.videoEncoder = "software"; settings_.preset = "medium"; settings_.qualityMode = "native"; settings_.quality = 20; settings_.audioBitrate = 192000; settings_.pixelFormat = "auto"; settings_.encoderOptions.clear(); QSignalBlocker block(encoderChoice_); select(encoderChoice_, "software"); }
        else if (settings_.videoEncoder == "auto" || settings_.videoEncoder == "software") { settings_ = adaptToEncoder(settings_, settings_.videoCodec == "hevc" ? "libx265" : "libx264"); QSignalBlocker block(encoderChoice_); select(encoderChoice_, settings_.videoEncoder); }
        updateControls(true); changeExtension(); settingsEdited();
    };
    connect(format_, &QComboBox::currentIndexChanged, this, chooseProfile); connect(format_, &QComboBox::activated, this, chooseProfile);

    const auto chooseEncoder = [this] {
        if (rebuilding_) return; settings_ = selectedSettings(); const auto name = encoderChoice_->currentData().toString();
        if (name != "software" && name != "auto") { settings_ = adaptToEncoder(settings_, name); QSignalBlocker b(format_); select(format_, "custom"); }
        else { if (format_->currentData() == "custom") { QSignalBlocker b(format_); select(format_, "compatible-mp4"); settings_.compatibilityProfile = "compatible-mp4"; settings_.container = "mp4"; settings_.videoCodec = "h264"; settings_.audioCodec = "aac"; settings_.audioEncoder = "aac"; } settings_.encoderOptions.clear(); settings_.preset = name == "auto" ? "default" : "medium"; settings_.qualityMode = name == "auto" ? "default" : "native"; settings_.quality = 20; settings_.pixelFormat = "auto"; }
        updateControls(true); tabs_->setCurrentIndex(1); changeExtension(); settingsEdited();
    };
    connect(encoderChoice_, &QComboBox::currentIndexChanged, this, chooseEncoder);
    connect(encoderChoice_, &QComboBox::activated, this, chooseEncoder);
    connect(container_, &QComboBox::currentIndexChanged, this, [this] { if (rebuilding_) return; settings_ = selectedSettings(); updateControls(false); changeExtension(); settingsEdited(); });
    connect(qualityMode_, &QComboBox::currentIndexChanged, this, [this] { if (rebuilding_) return; settings_ = selectedSettings(); if (settings_.qualityMode == "bitrate") settings_.quality = 5000; else if (settings_.qualityMode == "native") settings_.quality = encoderControls(encoderCandidates(settings_).value(0))["qualityDefault"].toDouble(); updateControls(false); settingsEdited(); });
    for (auto* box : {pixel_, speed_, audioBitrate_}) connect(box, &QComboBox::currentIndexChanged, this, &ExportDialog::settingsEdited);
    for (auto* box : {width_, height_}) connect(box, &QSpinBox::valueChanged, this, &ExportDialog::settingsEdited);
    connect(quality_, &QDoubleSpinBox::valueChanged, this, &ExportDialog::settingsEdited);
    connect(path_, &QLineEdit::textChanged, this, &ExportDialog::settingsEdited);
    connect(audio_, &QCheckBox::toggled, this, [this](bool enabled) { if (enabled) { settings_.sampleRate = 48000; settings_.channels = 2; } settings_ = selectedSettings(); updateControls(false); settingsEdited(); });
    const auto chooseAudioEncoder = [this] {
        if (rebuilding_) return;
        settings_ = selectedSettings();
        const auto* encoder = avcodec_find_encoder_by_name(settings_.audioEncoder.toUtf8().constData());
        if (encoder) settings_.audioCodec = avcodec_get_name(encoder->id);
        updateControls(false); settingsEdited();
    };
    connect(audioEncoder_, &QComboBox::currentIndexChanged, this, chooseAudioEncoder);
    connect(audioEncoder_, &QComboBox::activated, this, chooseAudioEncoder);
    for (auto* box : {rateNumerator_, rateDenominator_}) connect(box, &QSpinBox::valueChanged, this, [this] { if (rebuilding_) return; { QSignalBlocker block(automaticRate_); automaticRate_->setChecked(false); } rateNumerator_->setEnabled(true); rateDenominator_->setEnabled(true); settingsEdited(); });
    connect(automaticRate_, &QCheckBox::toggled, this, [this](bool automatic) {
        if (rebuilding_) return;
        if (automatic) { QSignalBlocker n(rateNumerator_), d(rateDenominator_); rateNumerator_->setValue(static_cast<int>(automaticFrameRate_.numerator)); rateDenominator_->setValue(static_cast<int>(automaticFrameRate_.denominator)); }
        rateNumerator_->setEnabled(!automatic); rateDenominator_->setEnabled(!automatic); settingsEdited();
    });
    connect(simpleTarget_, &QComboBox::currentIndexChanged, this, [this] { if (!rebuilding_) format_->setCurrentIndex(format_->findData(simpleTarget_->currentData())); });
    connect(simpleSize_, &QComboBox::currentIndexChanged, this, [this] {
        if (rebuilding_) return;
        const auto size = simpleOutputSize(description_, simpleSize_->currentData().toString());
        { QSignalBlocker w(width_), h(height_); width_->setValue(size.width()); height_->setValue(size.height()); } settingsEdited();
    });
    connect(simpleQuality_, &QComboBox::currentIndexChanged, this, [this] { if (!rebuilding_) quality_->setValue(simpleQuality_->currentData().toInt()); });
    connect(simpleAudio_, &QCheckBox::toggled, this, [this](bool enabled) { if (!rebuilding_) audio_->setChecked(enabled); });
    connect(refresh, &QPushButton::clicked, this, [this] { ++checkGeneration_; checkedKey_.clear(); customEncoderCatalogKey_.clear(); forceRefreshCapabilities_ = true; refreshDevices_ = true; deviceProbes_ = {}; if (capabilityWorker_) capabilityWorker_->requestInterruption(); settingsEdited(); });
    connect(details, &QPushButton::clicked, this, [this] { QDialog dialog(this); dialog.setWindowTitle(tr("Export capability details")); dialog.resize(780, 580); auto* l = new QVBoxLayout(&dialog); auto* text = new QTextEdit(&dialog); text->setReadOnly(true); auto report = capabilities_; report["deviceProbes"] = deviceProbes_; report["controls"] = encoderControls(encoderCandidates(selectedSettings()).value(0)); text->setPlainText(tr("Custom encoder choices are limited to encoders that passed an isolated encode/mux probe at the current output size and frame rate. Hardware device creation is checked independently.\n\n") + QString::fromUtf8(QJsonDocument(report).toJson())); l->addWidget(text); auto* close = new QPushButton(tr("Close"), &dialog); l->addWidget(close); connect(close, &QPushButton::clicked, &dialog, &QDialog::accept); dialog.exec(); });
    connect(start_, &QPushButton::clicked, this, &ExportDialog::startExport); connect(cancel_, &QPushButton::clicked, this, [this] { if (worker_) { worker_->cancel(); cancel_->setEnabled(false); message_->setText(tr("Cancelling export…")); } }); connect(close_, &QPushButton::clicked, this, &ExportDialog::reject); settingsEdited();
}
ExportDialog::~ExportDialog() { if (capabilityWorker_) { capabilityWorker_->requestInterruption(); capabilityWorker_->wait(); } if (worker_) { worker_->cancel(); worker_->wait(); } }
bool ExportDialog::simpleCompatible() const {
    const auto s = selectedSettings();
    bool sizeMatches = false;
    for (const auto& choice : {"sequence", "1080", "720"}) sizeMatches |= simpleOutputSize(description_, choice) == QSize(s.width, s.height);
    return compatibilityProfile(s.compatibilityProfile).has_value() && s.videoEncoder == "software" &&
        s.preset == "medium" && s.qualityMode == "native" && (s.quality == 18 || s.quality == 20 || s.quality == 24) &&
        s.pixelFormat == "auto" && s.audioEncoder == "aac" && s.audioBitrate == 192000 && s.encoderOptions.isEmpty() &&
        !s.frameRateCustomized && sizeMatches && exportCombinationError(s).isEmpty();
}
void ExportDialog::syncSimple() {
    const bool previous = rebuilding_; rebuilding_ = true;
    const auto s = selectedSettings(); const bool compatible = simpleCompatible();
    simpleControls_->setEnabled(compatible);
    simpleTarget_->setCurrentIndex(simpleTarget_->findData(s.compatibilityProfile));
    simpleSize_->setCurrentIndex(-1);
    for (int i = 0; i < simpleSize_->count(); ++i) if (simpleOutputSize(description_, simpleSize_->itemData(i).toString()) == QSize(s.width, s.height)) { simpleSize_->setCurrentIndex(i); break; }
    simpleQuality_->setCurrentIndex(simpleQuality_->findData(s.quality)); simpleAudio_->setChecked(s.audioEnabled);
    auto* targets = qobject_cast<QStandardItemModel*>(simpleTarget_->model());
    for (int i = 0; i < simpleTarget_->count(); ++i) {
        const auto p = compatibilityProfile(simpleTarget_->itemData(i).toString()); auto candidate = s;
        candidate.compatibilityProfile = p->id; candidate.container = p->container; candidate.videoCodec = p->codec;
        candidate.videoEncoder = "software"; candidate.pixelFormat = "auto"; candidate.encoderOptions.clear(); candidate.audioCodec = "aac"; candidate.audioEncoder = "aac";
        candidate.preset = "medium"; candidate.qualityMode = "native"; candidate.quality = 20; candidate.audioBitrate = 192000;
        const auto error = exportCombinationError(candidate); targets->item(i)->setEnabled(error.isEmpty());
        simpleTarget_->setItemData(i, error.isEmpty() ? p->explanation : error, Qt::ToolTipRole);
    }
    auto* sizes = qobject_cast<QStandardItemModel*>(simpleSize_->model());
    for (int i = 0; i < simpleSize_->count(); ++i) {
        const auto size = simpleOutputSize(description_, simpleSize_->itemData(i).toString()); auto candidate = s; candidate.width = size.width(); candidate.height = size.height();
        const auto error = exportCombinationError(candidate); sizes->item(i)->setEnabled(error.isEmpty());
        simpleSize_->setItemData(i, error.isEmpty() ? tr("%1×%2 pixels; sequence aspect preserved, no upscaling.").arg(size.width()).arg(size.height()) : error, Qt::ToolTipRole);
    }
    const auto profile = compatibilityProfile(s.compatibilityProfile);
    simpleNotice_->setText(compatible ? profile->explanation : tr("Advanced settings are active and are preserved in both tabs. Review or change them in Advanced, or choose Use compatible defaults to enable the Simple choices."));
    rebuilding_ = previous;
}
void ExportDialog::useCompatibleDefaults() {
    const auto current = selectedSettings();
    settings_ = project::ExportSettings{}; settings_.outputPath = current.outputPath; settings_.audioEnabled = current.audioEnabled;
    const auto size = simpleOutputSize(description_, "1080"); settings_.width = size.width(); settings_.height = size.height(); settings_.frameRate = automaticFrameRate_;
    rebuilding_ = true;
    select(format_, settings_.compatibilityProfile); select(encoderChoice_, settings_.videoEncoder);
    width_->setValue(settings_.width); height_->setValue(settings_.height); automaticRate_->setChecked(true);
    rateNumerator_->setValue(static_cast<int>(automaticFrameRate_.numerator)); rateDenominator_->setValue(static_cast<int>(automaticFrameRate_.denominator));
    rebuilding_ = false; updateControls(true); tabs_->setCurrentIndex(0); changeExtension(); settingsEdited();
}
void ExportDialog::changeExtension() {
    if (path_->text().isEmpty()) return; const auto ext = containerExtension(container_->currentData().toString()); const auto suffix = QFileInfo(path_->text()).suffix(); if (!ext.isEmpty()) path_->setText(suffix.isEmpty() ? path_->text() + "." + ext : path_->text().left(path_->text().size() - suffix.size()) + ext);
}
void ExportDialog::updateControls(bool reset) {
    rebuilding_ = true;
    const bool custom = settings_.compatibilityProfile == "custom"; const bool automatic = settings_.videoEncoder == "auto";
    auto inspect = settings_; if (automatic) inspect.videoEncoder = "software"; const auto name = encoderCandidates(inspect).value(0); const auto controls = encoderControls(name);
    auto* hint = findChild<QLabel*>("exportNativeHint");
    hint->setText((reset ? tr("Applied %1 defaults and cleared previous native overrides. ").arg(name) : QString{}) +
        tr("Native options: blank uses the encoder default. Ranges/help belong to this encoder. Select Encoder default for Speed or Quality to override those options directly.") +
        (settings_.container == "image2pipe" ? tr(" Image streams carry no playback timing or audio; players may assume their own frame rate.") : QString{}));
    container_->clear(); for (const auto& v : compatibleContainers(name)) { const auto o = v.toObject(); container_->addItem(o["name"].toString() + " (." + o["extension"].toString() + ")", o["name"].toString()); } select(container_, settings_.container); container_->setEnabled(custom);
    pixel_->clear(); pixel_->addItem(tr("Automatic encoder format"), "auto"); for (const auto& v : controls["pixels"].toArray()) pixel_->addItem(v.toString(), v.toString()); select(pixel_, settings_.pixelFormat);
    speed_->clear(); speed_->addItem(tr("Encoder default"), "default"); for (const auto& v : controls["speeds"].toArray()) speed_->addItem(v.toString(), v.toString()); select(speed_, settings_.preset); speed_->setEnabled(!automatic && !controls["speedKey"].toString().isEmpty()); speed_->setToolTip(controls["speedKey"].toString());
    select(qualityMode_, settings_.qualityMode); qualityMode_->setEnabled(!automatic); if (auto* m = qobject_cast<QStandardItemModel*>(qualityMode_->model())) m->item(qualityMode_->findData("bitrate"))->setEnabled(controls["bitrateSupported"].toBool());
    const bool native = settings_.qualityMode == "native"; quality_->setEnabled(!automatic && (settings_.qualityMode == "bitrate" || (native && !controls["qualityKey"].toString().isEmpty())));
    // Preserve invalid saved values so validation explains them; use native limits as soon as the choice is valid.
    const double low = native ? controls["qualityMinimum"].toDouble() : 1, high = native ? controls["qualityMaximum"].toDouble() : 1000000; quality_->setDecimals(std::floor(settings_.quality) != settings_.quality ? 2 : native ? controls["qualityDecimals"].toInt() : 0);
    if (std::round(settings_.quality * 100) != settings_.quality * 100) quality_->setDecimals(12);
    quality_->setSingleStep(quality_->decimals() ? 0.1 : 1);
    quality_->setRange(std::min(low, settings_.quality), std::max(high, settings_.quality)); quality_->setValue(settings_.quality);
    qualityLabel_->setText(settings_.qualityMode == "bitrate" ? tr("Target bitrate (kbit/s)") : settings_.qualityMode == "default" ? tr("Encoder default") : tr("%1 (%2–%3)").arg(controls["qualityLabel"].toString()).arg(low).arg(high));
    quality_->setToolTip(tr("This scale belongs to %1; equal numbers are not equal quality across encoders.").arg(name));
    audio_->setChecked(settings_.audioEnabled); audioEncoder_->clear();
    const auto* mux = av_guess_format(settings_.container.toUtf8().constData(), nullptr, nullptr);
    for (const auto& a : QStringList{"aac", "libopus", "flac", "libmp3lame", "ac3", "pcm_s16le"}) {
        const auto* c = avcodec_find_encoder_by_name(a.toUtf8().constData());
        if (c && mux && (avformat_query_codec(mux, c->id, FF_COMPLIANCE_NORMAL) > 0 || mux->audio_codec == c->id)) audioEncoder_->addItem(a, a);
    }
    select(audioEncoder_, settings_.audioEncoder); audioEncoder_->setEnabled(custom && settings_.audioEnabled);
    audioBitrate_->clear();
    const QString audioName = settings_.audioEncoder;
    const bool lossless = audioName == "flac" || audioName == "pcm_s16le";
    QList<int> rates{32, 40, 48, 56, 64, 80, 96, 112, 128, 160, 192, 224, 256, 320, 384, 448, 512};
    if (audioName == "libmp3lame") rates = {32, 40, 48, 56, 64, 80, 96, 112, 128, 160, 192, 224, 256, 320};
    for (const int rate : rates) audioBitrate_->addItem(QString::number(rate), rate * 1000);
    if (audioBitrate_->findData(settings_.audioBitrate) < 0) audioBitrate_->addItem(tr("Saved: %1 kbit/s").arg(settings_.audioBitrate / 1000.0), settings_.audioBitrate);
    audioBitrate_->setCurrentIndex(audioBitrate_->findData(settings_.audioBitrate)); audioBitrate_->setEnabled(settings_.audioEnabled && !lossless);
    audioBitrate_->setToolTip(lossless ? tr("Lossless audio uses encoder defaults; this retained bitrate is unused.") : tr("Requested average bitrate; the exact codec/container configuration is checked before export."));
    nativeOptions_->setRowCount(0); auto specs = controls["options"].toArray(); QSet<QString> found;
    for (const auto& v : specs) found.insert(v.toObject()["key"].toString());
    for (auto it = settings_.encoderOptions.begin(); it != settings_.encoderOptions.end(); ++it) if (!found.contains(it.key())) specs.append(QJsonObject{{"key", it.key()}, {"help", "Unavailable saved option; clear this override."}});
    for (const auto& v : specs) {
        const auto spec = v.toObject(); const auto key = spec["key"].toString(); const int row = nativeOptions_->rowCount(); nativeOptions_->insertRow(row);
        auto* label = new QTableWidgetItem(key); label->setFlags(label->flags() & ~Qt::ItemIsEditable); label->setToolTip(spec["help"].toString()); nativeOptions_->setItem(row, 0, label);
        auto* value = new QComboBox(nativeOptions_); value->setEditable(true); value->addItem(""); for (const auto& c : spec["values"].toArray()) value->addItem(c.toObject()["name"].toString()); value->setCurrentText(settings_.encoderOptions.value(key)); value->setToolTip(spec["help"].toString()); nativeOptions_->setCellWidget(row, 1, value);
        auto* range = new QTableWidgetItem(tr("%1; %2…%3").arg(spec["default"].toString()).arg(spec["minimum"].toDouble()).arg(spec["maximum"].toDouble())); range->setFlags(range->flags() & ~Qt::ItemIsEditable); range->setToolTip(spec["help"].toString()); nativeOptions_->setItem(row, 2, range);
        connect(value, &QComboBox::currentTextChanged, this, &ExportDialog::settingsEdited);
    }
    nativeOptions_->setEnabled(!automatic);
    if (!settings_.encoderOptions.isEmpty()) findChild<QGroupBox*>("exportNativeGroup")->setChecked(true);
    rateNumerator_->setEnabled(!automaticRate_->isChecked()); rateDenominator_->setEnabled(!automaticRate_->isChecked());
    rebuilding_ = false; syncSimple();
}
project::ExportSettings ExportDialog::selectedSettings() const {
    auto s = settings_; s.outputPath = path_->text().trimmed().isEmpty() ? QString{} : QFileInfo(path_->text().trimmed()).absoluteFilePath(); s.compatibilityProfile = format_->currentData().toString(); s.videoEncoder = encoderChoice_->currentData().toString(); s.pixelFormat = pixel_->currentData().toString(); s.container = container_->currentData().toString();
    s.width = width_->value(); s.height = height_->value(); s.frameRate = {rateNumerator_->value(), rateDenominator_->value()}; const auto divisor = std::gcd(s.frameRate.numerator, s.frameRate.denominator); s.frameRate.numerator /= divisor; s.frameRate.denominator /= divisor;
    s.frameRateCustomized = !automaticRate_->isChecked();
    s.audioBitrate = audioBitrate_->currentData().toInt();
    s.quality = quality_->value(); s.qualityMode = qualityMode_->currentData().toString(); s.preset = speed_->currentData().toString(); s.audioEnabled = audio_->isChecked(); s.audioEncoder = audioEncoder_->currentData().toString(); const auto* audio = avcodec_find_encoder_by_name(s.audioEncoder.toUtf8().constData()); if (audio && s.audioEncoder != settings_.audioEncoder) s.audioCodec = avcodec_get_name(audio->id);
    s.encoderOptions.clear(); for (int row = 0; row < nativeOptions_->rowCount(); ++row) { const auto* box = qobject_cast<QComboBox*>(nativeOptions_->cellWidget(row, 1)); if (box && !box->currentText().isEmpty()) s.encoderOptions[nativeOptions_->item(row, 0)->text()] = box->currentText(); }
    return s;
}
void ExportDialog::settingsEdited() {
    if (worker_ || rebuilding_) return;
    syncSimple(); showValidation();
    const auto s = selectedSettings(); const auto key = probeSettings(s, "all-supported");
    encoderChoice_->setEnabled(s.compatibilityProfile != "custom" || customEncoderProbeKey(s) == customEncoderCatalogKey_);
    if (key != requestedKey_) { requestedKey_ = key; ++checkGeneration_; }
    if (capabilityWorker_ && key != runningKey_) capabilityWorker_->requestInterruption();
    if (key != checkedKey_) {
        checkTimer_->start();
    }
}
void ExportDialog::showValidation() {
    if (worker_) return;
    const auto s = selectedSettings(); const auto key = probeSettings(s, "all-supported");
    findChild<QLabel*>("exportSummary")->setText(tr("Destination: %1\n%2 · %3×%4 · %5/%6 fps · %7 · Audio: %8")
        .arg(s.outputPath.isEmpty() ? tr("Choose an output file below") : QFileInfo(s.outputPath).fileName(), format_->currentText()).arg(s.width).arg(s.height).arg(s.frameRate.numerator).arg(s.frameRate.denominator).arg(s.container, s.audioEnabled ? s.audioEncoder + tr(" (48 kHz stereo)") : tr("off")));
    if (key == checkedKey_) {
        const auto resolved = resolveEncoder(s, capabilities_["encoderProbes"].toArray());
        capabilities_["encoder"] = resolved.encoder; capabilities_["pixelFormat"] = resolved.pixelFormat;
        capabilities_["error"] = resolved.error; capabilities_["warning"] = resolved.warning;
    }
    const auto error = validateExport(description_, s); start_->setEnabled(false);
    if (!exportCombinationError(s).isEmpty() || !compiledCombinationError(s).isEmpty() || (key == checkedKey_ && !capabilities_["error"].toString().isEmpty()))
        tabs_->setCurrentIndex(1);
    if (!s.encoderOptions.isEmpty() && !nativeSettingsError(s, encoderCandidates(s).value(0)).isEmpty())
        findChild<QGroupBox*>("exportNativeGroup")->setChecked(true);
    if (!error.isEmpty()) message_->setText(error + tr("\nCorrect the choices above, then check again. Your project and existing output are unchanged."));
    else if (key != checkedKey_) message_->setText(tr("Checking these export settings on this computer…"));
    else if (!capabilities_["error"].toString().isEmpty()) message_->setText(tr("%1 / %2 at %3×%4, %5/%6 fps could not be validated on this computer. Choose another encoder, correct the conflict below, turn audio off, or use compatible defaults.\n%7").arg(s.videoEncoder, s.container).arg(s.width).arg(s.height).arg(s.frameRate.numerator).arg(s.frameRate.denominator).arg(capabilities_["error"].toString()));
    else { message_->setText(tr("Ready to export the complete active sequence.")); start_->setEnabled(true); }
    if (key != checkedKey_) capabilityMessage_->setText(tr("Capability check pending. Export will be enabled after the selected configuration succeeds."));
    else if (!capabilities_["error"].toString().isEmpty()) capabilityMessage_->setText(tr("Export blocked for this checked configuration. See Export details… for the probe result."));
    else capabilityMessage_->setText(tr("Validated: %1 / %2. %3").arg(capabilities_["encoder"].toString(), capabilities_["pixelFormat"].toString(), capabilities_["warning"].toString()));
    // Keep controls accessible when a backend supplies long diagnostics; the full text remains available.
    message_->setToolTip(message_->text()); capabilityMessage_->setToolTip(capabilityMessage_->text());
    message_->setText(conciseStatus(message_->text())); capabilityMessage_->setText(conciseStatus(capabilityMessage_->text()));
}
void ExportDialog::updateEncoders() {
    auto* model = qobject_cast<QStandardItemModel*>(encoderChoice_->model()); if (!model) return;
    const auto s = selectedSettings();
    for (int n = 0; n < encoderChoice_->count(); ++n) {
        const auto name = encoderChoice_->itemData(n).toString();
        if (s.compatibilityProfile == "custom" && name != "software" && name != "auto") {
            QJsonObject probe;
            for (const auto& value : capabilities_["encoderCatalogProbes"].toArray())
                if (value.toObject()["encoder"].toString() == name) { probe = value.toObject(); break; }
            const bool usable = probe["usable"].toBool();
            model->item(n)->setEnabled(usable);
            if (auto* view = qobject_cast<QListView*>(encoderChoice_->view())) view->setRowHidden(n, !usable);
            const auto detail = usable ? probe["warning"].toString() : probe["reason"].toString();
            encoderChoice_->setItemData(n, usable ? tr("Validated for this export size and frame rate. %1").arg(detail)
                : tr("Unavailable for this export configuration. %1").arg(detail), Qt::ToolTipRole);
        } else {
            auto candidate = s; candidate.videoEncoder = name;
            const auto resolved = resolveEncoder(candidate, capabilities_["encoderProbes"].toArray());
            model->item(n)->setEnabled(true);
            if (auto* view = qobject_cast<QListView*>(encoderChoice_->view())) view->setRowHidden(n, false);
            encoderChoice_->setItemData(n, resolved.error.isEmpty() ? tr("Validated encoder: %1. %2").arg(resolved.encoder, resolved.warning) : tr("Select to configure and test this encoder. %1").arg(resolved.error), Qt::ToolTipRole);
        }
    }
}
void ExportDialog::launchCheck() {
    if (closing_ || worker_ || capabilityWorker_) return;
    const auto s = selectedSettings(); const auto key = probeSettings(s, "all-supported");
    const bool allCustomEncoders = s.compatibilityProfile == "custom" && customEncoderProbeKey(s) != customEncoderCatalogKey_;
    if ((!exportCombinationError(s).isEmpty() && !allCustomEncoders) || key == checkedKey_) return;
    runningKey_ = key; const bool devices = refreshDevices_; const auto generation = checkGeneration_;
    if (allCustomEncoders) {
        if (exportCombinationError(s).isEmpty() && compiledCombinationError(s).isEmpty())
            message_->setText(tr("Checking which custom encoders work with this output size and frame rate…"));
        capabilityMessage_->setText(tr("Testing custom encoders on this computer. The available list will update when these checks finish."));
    }
    const bool forceRefresh = forceRefreshCapabilities_;
    capabilityWorker_ = new CapabilityWorker(s, devices, this, allCustomEncoders, forceRefresh);
    connect(capabilityWorker_, &QThread::finished, this, [this, key, devices, generation, allCustomEncoders, forceRefresh] {
        const auto result = capabilityWorker_->result(); const bool cancelled = result["cancelled"].toBool();
        capabilityWorker_->deleteLater(); capabilityWorker_ = nullptr;
        if (closing_) return;
        const auto current = selectedSettings();
        if (!cancelled && generation == checkGeneration_ && key == probeSettings(current, "all-supported")) {
            if (result["catalogCacheHit"].toBool()) {
                capabilities_ = result; customEncoderCatalog_ = result["encoderProbes"].toArray();
                customEncoderCatalogKey_ = customEncoderProbeKey(current); checkedKey_.clear();
                if (devices) { deviceProbes_ = {}; refreshDevices_ = false; }
                capabilities_["encoderCatalogProbes"] = customEncoderCatalog_;
                encoderChoice_->setEnabled(true); updateEncoders(); showValidation(); settingsEdited();
                return;
            }
            capabilities_ = result; checkedKey_ = key;
            if (forceRefresh) forceRefreshCapabilities_ = false;
            if (allCustomEncoders) {
                customEncoderCatalog_ = result["encoderProbes"].toArray();
                customEncoderCatalogKey_ = customEncoderProbeKey(current);
            } else if (current.compatibilityProfile == "custom") {
                for (const auto& value : result["encoderProbes"].toArray()) {
                    const auto encoder = value.toObject()["encoder"].toString();
                    bool replaced = false;
                    for (qsizetype n = 0; n < customEncoderCatalog_.size(); ++n) {
                        if (customEncoderCatalog_[n].toObject()["encoder"].toString() != encoder) continue;
                        customEncoderCatalog_[n] = value; replaced = true; break;
                    }
                    if (!replaced) customEncoderCatalog_.append(value);
                }
            }
            if (current.compatibilityProfile == "custom") capabilities_["encoderCatalogProbes"] = customEncoderCatalog_;
            if (devices) { deviceProbes_ = result["deviceProbes"].toArray(); refreshDevices_ = false; }
            encoderChoice_->setEnabled(true);
            updateEncoders(); showValidation();
        } else checkTimer_->start();
    });
    capabilityWorker_->start();
}
void ExportDialog::setBusy(bool busy) { options_->setEnabled(!busy); cancel_->setEnabled(busy); if (busy) start_->setEnabled(false); else showValidation(); }
void ExportDialog::reject() {
    closing_ = true;
    if (capabilityWorker_) capabilityWorker_->requestInterruption(); checkTimer_->stop();
    if (worker_ && worker_->isRunning()) { closing_ = true; worker_->cancel(); cancel_->setEnabled(false); message_->setText(tr("Cancelling export before closing…")); return; }
    QDialog::reject();
}
void ExportDialog::startExport() {
    if (worker_) return;
    const auto s = selectedSettings();
    if (probeSettings(s, "all-supported") != checkedKey_ || !validateExport(description_, s).isEmpty() || !capabilities_["error"].toString().isEmpty()) { settingsEdited(); return; }
    if (QFileInfo::exists(s.outputPath)) {
        QMessageBox box(QMessageBox::Question, tr("Replace export?"), tr("Replace %1 after the export succeeds?").arg(s.outputPath), QMessageBox::Yes | QMessageBox::No, this);
        box.setObjectName("exportOverwriteDialog"); box.setTextFormat(Qt::PlainText); box.setDefaultButton(QMessageBox::No);
        if (box.exec() != QMessageBox::Yes) return;
    }
    settings_ = s; emit settingsChosen(settings_);
    worker_ = new ExportWorker(description_, settings_, this); setBusy(true); progress_->setValue(0); message_->setText(tr("Exporting…"));
    connect(worker_, &ExportWorker::progress, progress_, &QProgressBar::setValue);
    connect(worker_, &QThread::finished, this, [this] {
        const auto result = worker_->result(); worker_->deleteLater(); worker_ = nullptr; setBusy(false);
        if (result["passed"].toBool()) {
            const auto mix = result["audioMix"].toObject(); const auto peak = mix["masterPeak"].toDouble();
            const auto db = peak > 0 ? 20.0 * std::log10(peak) : -60.0; const auto clipped = mix["clippedSamples"].toInteger();
            const auto audioNote = clipped > 0 ? tr(" Audio hard-clipped %1 samples (pre-clip peak %2 dBFS). ").arg(clipped).arg(db, 0, 'f', 1)
                : peak > 0 ? tr(" Audio peak %1 dBFS. ").arg(db, 0, 'f', 1) : QString{};
            message_->setText(tr("Export complete: %1.%2 Encoder: %3. %4").arg(settings_.outputPath, audioNote, result["actualEncoder"].toString(), result["capabilityWarning"].toString())); qInfo() << "Export complete" << result;
        } else { message_->setText(result["cancelled"].toBool() ? tr("Export cancelled. Your project and existing destination file were preserved. Choose Export to retry.") : result["error"].toString() + tr("\nYour project and existing destination file were preserved. Check the destination and settings, then retry Export.")); qWarning() << "Export stopped" << result; }
        if (closing_) QDialog::reject();
    });
    worker_->start();
}
}
