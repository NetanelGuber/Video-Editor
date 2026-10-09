#include "PropertyDialogs.h"
#include <QCheckBox>
#include <QColorDialog>
#include <QComboBox>
#include <QDialog>
#include <QDialogButtonBox>
#include <QDoubleSpinBox>
#include <QFontComboBox>
#include <QFormLayout>
#include <QGroupBox>
#include <QHBoxLayout>
#include <QLabel>
#include <QLineEdit>
#include <QPlainTextEdit>
#include <QPushButton>
#include <QRegularExpressionValidator>
#include <QSpinBox>
#include <QTableWidget>
#include <QHeaderView>
#include <QVBoxLayout>
#include <QInputDialog>
#include <QHash>
#include <QSet>
#include <cmath>
#include <limits>

namespace editor::timeline {
namespace {
struct Form {
    QDialog dialog;
    QVBoxLayout layout;
    QFormLayout* fields;
    QLabel* error;
    QDialogButtonBox* buttons;
    Form(QWidget* parent, const char* name, const QString& caption) : dialog(parent), layout(&dialog) {
        dialog.setObjectName(name); dialog.setWindowTitle(caption); dialog.resize(480, 300);
        fields = new QFormLayout; layout.addLayout(fields);
        error = new QLabel(&dialog); error->setWordWrap(true); error->setObjectName("propertyError"); layout.addWidget(error);
        buttons = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel, &dialog); layout.addWidget(buttons);
        QObject::connect(buttons, &QDialogButtonBox::rejected, &dialog, &QDialog::reject);
    }
    QLineEdit* timeField(const char* name, qint64 value, const QString& caption, TimeFormat time,
                         QFormLayout* target = nullptr, QWidget* parent = nullptr) {
        auto* edit = new QLineEdit(parent ? parent : &dialog); edit->setObjectName(name);
        time.initialize(edit, value); (target ? target : fields)->addRow(time.caption(caption), edit); return edit;
    }
    QDoubleSpinBox* number(const char* name, double value, double low, double high, const QString& caption,
                           QFormLayout* target = nullptr, QWidget* parent = nullptr) {
        auto* box = new QDoubleSpinBox(parent ? parent : &dialog); box->setObjectName(name); box->setRange(low, high); box->setDecimals(4); box->setSingleStep(0.1); box->setValue(value);
        box->setProperty("initialValue", box->value()); box->setProperty("exactValue", value);
        (target ? target : fields)->addRow(caption, box); return box;
    }
    QCheckBox* flag(const char* name, bool value, const QString& caption,
                    QFormLayout* target = nullptr, QWidget* parent = nullptr) {
        auto* box = new QCheckBox(caption, parent ? parent : &dialog); box->setObjectName(name); box->setChecked(value); (target ? target : fields)->addRow(box); return box;
    }
    QLineEdit* color(const char* name, const QString& value, const QString& caption) {
        auto* row = new QWidget(&dialog); auto* line = new QHBoxLayout(row); line->setContentsMargins(0, 0, 0, 0);
        auto* edit = new QLineEdit(value, row); edit->setObjectName(name);
        auto* pick = new QPushButton("Choose…", row); line->addWidget(edit); line->addWidget(pick);
        QObject::connect(pick, &QPushButton::clicked, &dialog, [this, edit] {
            const auto color = QColorDialog::getColor(QColor(edit->text()), &dialog, "Choose color", QColorDialog::ShowAlphaChannel);
            if (color.isValid()) edit->setText(color.name(QColor::HexArgb));
        });
        fields->addRow(caption + " (#AARRGGBB)", row); return edit;
    }
};
double numberValue(QDoubleSpinBox* field) {
    return field->value() == field->property("initialValue").toDouble() ? field->property("exactValue").toDouble() : field->value();
}
constexpr int AutomationOriginalTextRole = Qt::UserRole + 11;
constexpr int AutomationOriginalFrameRole = Qt::UserRole + 12;
void setAutomationFrame(QTableWidgetItem* item, qint64 frame, const TimeFormat& time) {
    const auto text = time.inputValue(frame);
    item->setText(text);
    item->setData(AutomationOriginalTextRole, text);
    item->setData(AutomationOriginalFrameRole, QVariant::fromValue<qlonglong>(frame));
}
bool automationFrame(const QTableWidgetItem* item, const TimeFormat& time, qint64& frame) {
    if (!item) return false;
    const auto text = item->text();
    if (item->data(AutomationOriginalFrameRole).isValid() && text == item->data(AutomationOriginalTextRole).toString()) {
        bool ok = false;
        frame = item->data(AutomationOriginalFrameRole).toLongLong(&ok);
        return ok && frame >= 0;
    }
    return time.parse(text, frame);
}
QTableWidget* automationTable(QWidget* parent, const char* name, const QVector<Keyframe>& keys, double initial, const TimeFormat& time) {
    auto* table = new QTableWidget(parent); table->setObjectName(name); table->setColumnCount(3); table->setRowCount(keys.size());
    table->setHorizontalHeaderLabels({time.seconds() ? "Time (seconds)" : "Frame", "Value", "Curve"}); table->horizontalHeader()->setSectionResizeMode(QHeaderView::Stretch);
    table->verticalHeader()->setVisible(false); table->setMaximumHeight(125);
    for (qsizetype row = 0; row < keys.size(); ++row) {
        auto* position = new QTableWidgetItem;
        setAutomationFrame(position, keys[row].frame, time);
        table->setItem(static_cast<int>(row), 0, position);
        table->setItem(static_cast<int>(row), 1, new QTableWidgetItem(QString::number(keys[row].value, 'g', 17)));
        table->setItem(static_cast<int>(row), 2, new QTableWidgetItem(keys[row].curve));
    }
    table->setProperty("initialValue", initial);
    return table;
}
void addAutomationKey(QTableWidget* table, const TimeFormat& time) {
    const int row = table->rowCount(); table->insertRow(row);
    qint64 next = 0, previous = 0;
    if (row && automationFrame(table->item(row - 1, 0), time, previous))
        next = previous < std::numeric_limits<qint64>::max() ? previous + 1 : previous;
    const auto value = row ? table->item(row - 1, 1)->text() : QString::number(table->property("initialValue").toDouble(), 'g', 17);
    auto* position = new QTableWidgetItem;
    setAutomationFrame(position, next, time);
    table->setItem(row, 0, position);
    table->setItem(row, 1, new QTableWidgetItem(value));
    table->setItem(row, 2, new QTableWidgetItem("linear"));
    table->setCurrentCell(row, 0);
}
std::optional<AudioAutomation> automationDialog(QWidget* parent, const AudioAutomation& original, bool contentRelative, TimeFormat time, const char* objectName) {
    QDialog dialog(parent); dialog.setObjectName(objectName); dialog.setWindowTitle(contentRelative ? "Clip volume and pan automation" : "Track volume and pan automation"); dialog.resize(500, 440);
    auto* layout = new QVBoxLayout(&dialog);
    const auto clipPosition = time.seconds() ? "seconds from original clip-content start" : "original clip-content frames";
    const auto trackPosition = time.seconds() ? "seconds from sequence start" : "absolute sequence frames";
    auto* help = new QLabel(contentRelative ? QString("Clip keys use %1 and stay aligned after trim or split. Values multiply the clip fader or offset its pan.").arg(clipPosition)
                                            : QString("Track keys use %1. Values multiply the track fader or offset its pan.").arg(trackPosition), &dialog);
    help->setWordWrap(true); layout->addWidget(help);
    const double initialVolume = 1.0, initialPan = 0.0;
    auto makeGroup = [&](const QString& title, const char* tableName, const QVector<Keyframe>& keys, double initial) {
        auto* group = new QGroupBox(title, &dialog); auto* box = new QVBoxLayout(group);
        auto* table = automationTable(group, tableName, keys, initial, time); box->addWidget(table);
        auto* controls = new QHBoxLayout; auto* add = new QPushButton("Add key", group); auto* remove = new QPushButton("Remove selected", group);
        controls->addWidget(add); controls->addWidget(remove); controls->addStretch(); box->addLayout(controls);
        QObject::connect(add, &QPushButton::clicked, table, [table, time] { addAutomationKey(table, time); });
        QObject::connect(remove, &QPushButton::clicked, table, [table] { if (table->currentRow() >= 0) table->removeRow(table->currentRow()); });
        layout->addWidget(group); return table;
    };
    auto* volume = makeGroup("Volume multiplier (0–16)", "automationVolume", original.volume, initialVolume);
    auto* pan = makeGroup("Pan offset (-1 left to +1 right)", "automationPan", original.pan, initialPan);
    auto* error = new QLabel(&dialog); error->setObjectName("automationError"); error->setWordWrap(true); layout->addWidget(error);
    auto* buttons = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel, &dialog); layout->addWidget(buttons);
    QObject::connect(buttons, &QDialogButtonBox::rejected, &dialog, &QDialog::reject);
    AudioAutomation edited = original;
    QObject::connect(buttons, &QDialogButtonBox::accepted, &dialog, [&] {
        auto parse = [&](QTableWidget* table, double low, double high, QVector<Keyframe>& out) {
            out.clear(); qint64 previous = -1;
            for (int row = 0; row < table->rowCount(); ++row) {
                bool frameOk = false, valueOk = false;
                qint64 frame = -1;
                frameOk = automationFrame(table->item(row, 0), time, frame);
                const auto value = table->item(row, 1) ? table->item(row, 1)->text().toDouble(&valueOk) : std::numeric_limits<double>::quiet_NaN();
                const auto curve = table->item(row, 2) ? table->item(row, 2)->text().trimmed() : QString{};
                if (!frameOk || frame < 0 || frame > 10000000 || frame <= previous || !valueOk || !std::isfinite(value) || value < low || value > high || !QStringList{"hold", "linear", "eased"}.contains(curve)) return false;
                out.append({frame, value, curve}); previous = frame;
            }
            return true;
        };
        if (!parse(volume, 0, 16, edited.volume) || !parse(pan, -1, 1, edited.pan)) {
            error->setText(time.seconds() ? "Use strictly increasing seconds that round to distinct sequence frames, supported values, and hold, linear, or eased curves."
                                          : "Use strictly increasing frame positions, supported values, and hold, linear, or eased curves."); return;
        }
        dialog.accept();
    });
    if (dialog.exec() == QDialog::Accepted) return edited;
    return {};
}
QDoubleSpinBox* busNumber(QWidget* parent, const char* name, double low, double high, double value) {
    auto* field = new QDoubleSpinBox(parent); field->setObjectName(name); field->setRange(low, high); field->setDecimals(4); field->setSingleStep(0.1); field->setValue(value);
    field->setProperty("initialValue", field->value()); field->setProperty("exactValue", value); return field;
}
double busNumberValue(QDoubleSpinBox* field) {
    return field->value() == field->property("initialValue").toDouble() ? field->property("exactValue").toDouble() : field->value();
}
}
std::optional<TitleEdit> titleDialog(QWidget* parent, const Title& original, qint64 duration, TimeFormat time) {
    Form form(parent, "titleProperties", "Title properties");
    auto* text = new QPlainTextEdit(original.text, &form.dialog); text->setObjectName("titleText"); text->setMaximumHeight(100); form.fields->addRow("Text", text);
    auto* font = new QFontComboBox(&form.dialog); font->setObjectName("titleFont"); font->setEditable(true); font->setCurrentText(original.fontFamily); form.fields->addRow("Font", font);
    auto* size = new QSpinBox(&form.dialog); size->setObjectName("titleSize"); size->setRange(1, 1000); size->setValue(original.fontSize); form.fields->addRow("Size (sequence pixels)", size);
    auto* align = new QComboBox(&form.dialog); align->setObjectName("titleAlignment"); align->addItems({"left", "center", "right"}); align->setCurrentText(original.alignment); form.fields->addRow("Alignment", align);
    auto* color = form.color("titleColor", original.color, "Text color");
    auto* background = form.color("titleBackground", original.background, "Background");
    auto* x = form.number("titleX", original.x, 0, 1, "Horizontal position (0–1)");
    auto* y = form.number("titleY", original.y, 0, 1, "Vertical position (0–1)");
    auto* shadow = form.flag("titleShadow", original.shadow, "Shadow");
    auto* length = form.timeField("titleDuration", duration, "Duration", time);
    form.fields->addRow(new QLabel("Split halves share title styling. Duration changes only this clip.", &form.dialog));
    TitleEdit edited{original, duration};
    QObject::connect(form.buttons, &QDialogButtonBox::accepted, &form.dialog, [&] {
        const QRegularExpression pattern("^#[0-9a-fA-F]{8}$");
        if (!time.read(length, edited.durationFrames, 1) || !pattern.match(color->text()).hasMatch() || !pattern.match(background->text()).hasMatch() || font->currentText().trimmed().isEmpty()) {
            form.error->setText("Use a positive duration, a font name, and colors in #AARRGGBB form (AA is opacity)."); return;
        }
        edited.title.text = text->toPlainText(); edited.title.fontFamily = font->currentText(); edited.title.fontSize = size->value();
        edited.title.alignment = align->currentText(); edited.title.color = color->text(); edited.title.background = background->text();
        edited.title.x = numberValue(x); edited.title.y = numberValue(y); edited.title.shadow = shadow->isChecked(); form.dialog.accept();
    });
    if (form.dialog.exec() == QDialog::Accepted) return edited;
    return {};
}
std::optional<AudioEdit> audioDialog(QWidget* parent, const QString& sid, const Track& track,
                                     const Clip* clip, TimeFormat time, const char* objectName, const QVector<AudioBus>& buses) {
    QString caption = "Audio settings — " + track.name;
    if (clip) caption += " / " + clip->name;
    Form form(parent, objectName, caption);
    form.dialog.resize(580, 620);
    auto* trackGroup = new QGroupBox(QString("Whole track: %1 (all clips)").arg(track.name), &form.dialog);
    auto* trackFields = new QFormLayout(trackGroup);
    auto* trackScope = new QLabel("These controls affect every clip on this track.", trackGroup); trackScope->setWordWrap(true);
    trackFields->addRow(trackScope);
    auto* trackGain = form.number("trackGain", track.gain, 0, 16, "Track volume (1 = original, 0 = silent)", trackFields, trackGroup);
    auto* trackPan = form.number("trackPan", track.pan, -1, 1, "Track pan (-1 left, 0 center, +1 right)", trackFields, trackGroup);
    auto* trackBus = new QComboBox(trackGroup); trackBus->setObjectName("trackAudioBus");
    trackBus->addItem("Master (direct)", QString{});
    for (const auto& bus : buses) trackBus->addItem(bus.name, bus.id);
    trackBus->setCurrentIndex(std::max(0, trackBus->findData(track.audioBusId)));
    trackFields->addRow("Route to bus", trackBus);
    auto* trackMute = form.flag("trackMuted", track.muted, "Mute the entire track", trackFields, trackGroup);
    auto* trackSolo = form.flag("trackSolo", track.solo, "Solo this track", trackFields, trackGroup);
    auto* trackAutomationButton = new QPushButton("Edit track automation…", trackGroup); trackAutomationButton->setObjectName("trackAutomationButton");
    trackFields->addRow(trackAutomationButton);
    auto* soloHelp = new QLabel("Track and bus solos can play together. Track or bus mute overrides solo.", trackGroup);
    soloHelp->setWordWrap(true); trackFields->addRow(soloHelp);
    form.fields->addRow(trackGroup);

    const QString clipHeading = clip ? QString("This clip only: %1").arg(clip->name) : "Clip controls (select a clip)";
    auto* clipGroup = new QGroupBox(clipHeading, &form.dialog);
    auto* clipFields = new QFormLayout(clipGroup);
    auto* clipScope = new QLabel(clip ? "These controls affect only this clip." : "Select one audio clip on this track to enable its volume, mute, and fades.", clipGroup);
    clipScope->setWordWrap(true); clipFields->addRow(clipScope);
    auto* clipGain = form.number("clipGain", clip ? clip->gain : 1.0, 0, 16, "Clip volume (1 = original, 0 = silent)", clipFields, clipGroup);
    auto* clipPan = form.number("clipPan", clip ? clip->pan : 0, -1, 1, "Clip pan (-1 left, 0 center, +1 right)", clipFields, clipGroup);
    auto* clipMute = form.flag("clipMuted", clip ? clip->muted : false, "Mute only this clip", clipFields, clipGroup);
    auto* fadeIn = form.timeField("clipFadeIn", clip ? clip->fadeInFrames : 0, "Fade this clip in", time, clipFields, clipGroup);
    auto* fadeOut = form.timeField("clipFadeOut", clip ? clip->fadeOutFrames : 0, "Fade this clip out", time, clipFields, clipGroup);
    auto* clipAutomationButton = new QPushButton("Edit clip automation…", clipGroup); clipAutomationButton->setObjectName("clipAutomationButton");
    clipFields->addRow(clipAutomationButton);
    if (clip) {
        auto* fadeHelp = new QLabel(QString("A fade changes this clip from or to silence. Zero disables it; the duration can be up to %1.").arg(time.text(clip->durationFrames)), clipGroup);
        fadeHelp->setWordWrap(true); clipFields->addRow(fadeHelp);
    } else {
        clipGain->setEnabled(false); clipPan->setEnabled(false); clipMute->setEnabled(false); fadeIn->setEnabled(false); fadeOut->setEnabled(false); clipAutomationButton->setEnabled(false);
    }
    form.fields->addRow(clipGroup);

    AudioEdit edited; edited.track = SetTrackAudio{sid, track.id};
    if (clip) edited.clip = SetClipAudio{sid, track.id, clip->id};
    edited.track.pan = track.pan; edited.track.audioBusId = track.audioBusId; edited.track.automation = track.audioAutomation;
    if (clip) { edited.clip->pan = clip->pan; edited.clip->automation = clip->audioAutomation; }
    QObject::connect(trackAutomationButton, &QPushButton::clicked, &form.dialog, [&] {
        if (const auto automation = automationDialog(&form.dialog, edited.track.automation, false, time, "trackAudioAutomation")) edited.track.automation = *automation;
    });
    QObject::connect(clipAutomationButton, &QPushButton::clicked, &form.dialog, [&] {
        if (clip && edited.clip) if (const auto automation = automationDialog(&form.dialog, edited.clip->automation, true, time, "clipAudioAutomation")) edited.clip->automation = *automation;
    });
    QObject::connect(form.buttons, &QDialogButtonBox::accepted, &form.dialog, [&] {
        if (clip && (!time.read(fadeIn, edited.clip->fadeInFrames) || !time.read(fadeOut, edited.clip->fadeOutFrames)
                     || edited.clip->fadeInFrames > clip->durationFrames || edited.clip->fadeOutFrames > clip->durationFrames)) {
            form.error->setText(time.seconds() ? "Fade durations must be seconds from zero through the clip duration." : "Fade durations must be whole frames from zero through the clip duration."); return;
        }
        edited.track.gain = numberValue(trackGain); edited.track.muted = trackMute->isChecked(); edited.track.solo = trackSolo->isChecked();
        edited.track.pan = numberValue(trackPan); edited.track.audioBusId = trackBus->currentData().toString();
        if (clip) { edited.clip->gain = numberValue(clipGain); edited.clip->muted = clipMute->isChecked(); edited.clip->pan = numberValue(clipPan); }
        form.dialog.accept();
    });
    if (form.dialog.exec() == QDialog::Accepted) return edited;
    return {};
}
std::optional<SetClipAudio> clipAudioDialog(QWidget* parent, const QString& sid, const QString& tid, const Clip& clip, TimeFormat time) {
    Track track; track.id = tid; track.kind = "audio"; track.name = "Audio track";
    if (const auto edited = audioDialog(parent, sid, track, &clip, time, "clipAudioProperties")) return edited->clip;
    return {};
}
std::optional<SetTrackAudio> trackAudioDialog(QWidget* parent, const QString& sid, const Track& track, const QVector<AudioBus>& buses, TimeFormat time) {
    if (const auto edited = audioDialog(parent, sid, track, nullptr, time, "trackAudioProperties", buses)) return edited->track;
    return {};
}
std::optional<QVector<AudioBus>> audioBusManagerDialog(QWidget* parent, const QVector<AudioBus>& original) {
    QDialog dialog(parent); dialog.setObjectName("audioBusManager"); dialog.setWindowTitle("Audio buses"); dialog.resize(640, 360);
    auto* layout = new QVBoxLayout(&dialog); auto* help = new QLabel("Tracks routed to a bus share its fader, pan, mute, and solo. Removing a bus sends its tracks directly to Master.", &dialog);
    help->setWordWrap(true); layout->addWidget(help);
    auto* table = new QTableWidget(&dialog); table->setObjectName("audioBusTable"); table->setColumnCount(5); table->setHorizontalHeaderLabels({"Name", "Volume", "Pan", "Mute", "Solo"});
    table->horizontalHeader()->setSectionResizeMode(QHeaderView::Stretch); table->verticalHeader()->setVisible(false); layout->addWidget(table);
    auto add = [&](const AudioBus& bus) {
        const int row = table->rowCount(); table->insertRow(row);
        auto* name = new QTableWidgetItem(bus.name); name->setData(Qt::UserRole, bus.id); table->setItem(row, 0, name);
        table->setCellWidget(row, 1, busNumber(table, "busGain", 0, 16, bus.gain));
        table->setCellWidget(row, 2, busNumber(table, "busPan", -1, 1, bus.pan));
        auto* muted = new QCheckBox(table); muted->setObjectName("busMuted"); muted->setChecked(bus.muted); table->setCellWidget(row, 3, muted);
        auto* solo = new QCheckBox(table); solo->setObjectName("busSolo"); solo->setChecked(bus.solo); table->setCellWidget(row, 4, solo);
    };
    for (const auto& bus : original) add(bus);
    auto* controls = new QHBoxLayout; auto* addButton = new QPushButton("Add bus", &dialog); addButton->setObjectName("addAudioBus");
    auto* removeButton = new QPushButton("Remove selected", &dialog); removeButton->setObjectName("removeAudioBus"); controls->addWidget(addButton); controls->addWidget(removeButton); controls->addStretch(); layout->addLayout(controls);
    QObject::connect(addButton, &QPushButton::clicked, &dialog, [&] { add({newId(), QString("Bus %1").arg(table->rowCount() + 1)}); table->setCurrentCell(table->rowCount() - 1, 0); });
    QObject::connect(removeButton, &QPushButton::clicked, table, [table] { if (table->currentRow() >= 0) table->removeRow(table->currentRow()); });
    auto* error = new QLabel(&dialog); error->setObjectName("audioBusError"); error->setWordWrap(true); layout->addWidget(error);
    auto* buttons = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel, &dialog); layout->addWidget(buttons);
    QObject::connect(buttons, &QDialogButtonBox::rejected, &dialog, &QDialog::reject);
    QVector<AudioBus> result;
    QObject::connect(buttons, &QDialogButtonBox::accepted, &dialog, [&] {
        QVector<AudioBus> buses; QSet<QString> names;
        for (int row = 0; row < table->rowCount(); ++row) {
            const auto name = table->item(row, 0) ? table->item(row, 0)->text().trimmed() : QString{};
            auto* gain = qobject_cast<QDoubleSpinBox*>(table->cellWidget(row, 1)); auto* pan = qobject_cast<QDoubleSpinBox*>(table->cellWidget(row, 2));
            auto* muted = qobject_cast<QCheckBox*>(table->cellWidget(row, 3)); auto* solo = qobject_cast<QCheckBox*>(table->cellWidget(row, 4));
            if (name.isEmpty() || name.size() > 256 || names.contains(name.toCaseFolded())) { error->setText("Bus names must be unique and contain 1–256 characters."); return; }
            names.insert(name.toCaseFolded());
            buses.append({table->item(row, 0)->data(Qt::UserRole).toString(), name, busNumberValue(gain), busNumberValue(pan), muted->isChecked(), solo->isChecked()});
        }
        if (buses.size() > 128) { error->setText("A sequence can contain at most 128 audio buses."); return; }
        result = buses; dialog.accept();
    });
    if (dialog.exec() == QDialog::Accepted) return result;
    return {};
}
}
