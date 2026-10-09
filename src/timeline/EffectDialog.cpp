#include "PropertyDialogs.h"
#include "project/Effects.h"
#include <QCheckBox>
#include <QColorDialog>
#include <QComboBox>
#include <QDialog>
#include <QDialogButtonBox>
#include <QDoubleSpinBox>
#include <QFormLayout>
#include <QFileDialog>
#include <QHBoxLayout>
#include <QLabel>
#include <QLineEdit>
#include <QListWidget>
#include <QPushButton>
#include <QScrollArea>
#include <QSignalBlocker>
#include <QTableWidget>
#include <QHeaderView>
#include <QJsonArray>
#include <QVBoxLayout>
#include <algorithm>
#include <functional>

namespace editor::timeline {
std::optional<QVector<Effect>> effectsDialog(QWidget* parent, const Clip& clip, qint64 localFrame, TimeFormat time) {
    QDialog dialog(parent); dialog.setObjectName("effectProperties"); dialog.setWindowTitle("Effects — " + clip.name); dialog.resize(930, 720);
    auto working = clip.effects;
    QVBoxLayout layout(&dialog); auto* body = new QHBoxLayout; layout.addLayout(body, 1);
    auto* stackColumn = new QVBoxLayout; body->addLayout(stackColumn, 1);
    auto* list = new QListWidget(&dialog); list->setObjectName("effectStack"); stackColumn->addWidget(list, 1);
    auto* type = new QComboBox(&dialog); type->setObjectName("effectType");
    for (const auto& name : effectTypes()) {
        if (clip.kind == "audio" && name != "speed") continue;
        if (clip.kind == "title" && name == "speed") continue;
        type->addItem(name == "videoFade" ? "Video fades" : name, name);
    }
    stackColumn->addWidget(type);
    auto button = [&](const char* name, const QString& text, QLayout* box) {
        auto* b = new QPushButton(text, &dialog); b->setObjectName(name); box->addWidget(b); return b;
    };
    auto* add = button("effectAdd", "Add effect", stackColumn);
    auto* remove = button("effectRemove", "Remove", stackColumn);
    auto* up = button("effectUp", "Move up", stackColumn); auto* down = button("effectDown", "Move down", stackColumn);
    auto* reset = button("effectReset", "Reset to defaults", stackColumn);
    auto* scroll = new QScrollArea(&dialog); scroll->setWidgetResizable(true); body->addWidget(scroll, 3);
    auto* error = new QLabel(&dialog); error->setObjectName("effectError"); error->setWordWrap(true); layout.addWidget(error);
    auto* buttons = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel, &dialog); layout.addWidget(buttons);
    QObject::connect(buttons, &QDialogButtonBox::rejected, &dialog, &QDialog::reject);
    int selected = -1; std::function<bool()> save = [] { return true; };
    std::function<void(int)> show;
    auto populate = [&](int row) {
        QSignalBlocker blocker(list); list->clear();
        for (const auto& e : working) list->addItem((e.enabled ? "" : "[bypassed] ") + e.type + " v" + QString::number(e.version));
        list->setCurrentRow(row);
    };
    show = [&](int row) {
        selected = row; save = [] { return true; }; error->clear();
        delete scroll->takeWidget(); auto* panel = new QWidget; auto* form = new QFormLayout(panel); scroll->setWidget(panel);
        if (row < 0 || row >= working.size()) { form->addRow(new QLabel("Add an effect to this clip. Effects run from top to bottom.", panel)); return; }
        const auto original = working[row];
        auto* enabled = new QCheckBox("Enabled (uncheck to bypass)", panel); enabled->setObjectName("effectEnabled"); enabled->setChecked(original.enabled); form->addRow(enabled);
        if (!supportedEffect(original)) {
            form->addRow(new QLabel("This effect/version is preserved but cannot be rendered. Bypass or remove it before export.", panel));
            save = [&, row, enabled] { working[row].enabled = enabled->isChecked(); return true; }; return;
        }
        QMap<QString, QDoubleSpinBox*> numbers;
        for (const auto& spec : effectParameters(original.type)) {
            auto* box = new QDoubleSpinBox(panel); box->setObjectName("effect_" + spec.name); box->setDecimals(6); box->setRange(spec.low, spec.high); box->setSingleStep(0.05);
            const auto exact = original.parameters.value(spec.name).toDouble(spec.initial); box->setValue(exact);
            box->setProperty("initial", box->value()); box->setProperty("exact", exact); numbers[spec.name] = box; form->addRow(spec.label, box);
        }
        QMap<QString, QLineEdit*> texts; QMap<QString, QComboBox*> choices;
        QCheckBox* invert = nullptr;
        if (original.type == "mask") { invert = new QCheckBox("Invert rectangular mask", panel); invert->setObjectName("effect_invert"); invert->setChecked(original.parameters.value("invert").toBool()); form->addRow(invert); }
        if (original.type == "lut") {
            auto* path = new QLineEdit(original.parameters.value("path").toString(), panel); path->setObjectName("effect_path"); texts["path"] = path; form->addRow("Optional .cube LUT file", path);
            auto* browse = new QPushButton("Choose LUT…", panel); browse->setObjectName("effectLutBrowse"); form->addRow(browse);
            QObject::connect(browse, &QPushButton::clicked, &dialog, [&, path] { const auto chosen = QFileDialog::getOpenFileName(&dialog, "Choose SDR 3D LUT", {}, "3D LUT (*.cube)"); if (!chosen.isEmpty()) path->setText(chosen); });
            auto* note = new QLabel("Normalized SDR 3D .cube tables, 2–33 points. The table is embedded in the project. Clear the path to reset to identity. HDR/log LUTs are unsupported.", panel); note->setWordWrap(true); form->addRow(note);
        }
        if (original.type == "chromaKey") {
            auto* color = new QLineEdit(original.parameters.value("color").toString("#ff00ff00"), panel); color->setObjectName("effect_color"); texts["color"] = color; form->addRow("Key color (#ffRRGGBB)", color);
            auto* pick = new QPushButton("Choose key color…", panel); form->addRow(pick);
            QObject::connect(pick, &QPushButton::clicked, &dialog, [&, color] { const auto chosen = QColorDialog::getColor(QColor(color->text()), &dialog, "Choose key color"); if (chosen.isValid()) color->setText(chosen.name(QColor::HexArgb)); });
        }
        if (original.type == "speed") { auto* note = new QLabel("One forward speed effect per media clip. Rate keyframes create speed ramps. Applying, bypassing or resetting recalculates clip duration while keeping its selected source range. Other clips stay in place. Audio speed preserves pitch. Video and audio are separate clips: apply matching speed settings to both when needed.", panel); note->setWordWrap(true); form->addRow(note); }
        if (original.type == "color") { auto* note = new QLabel("Creative correction on full-range encoded 8-bit SDR RGB. Temperature/tint are channel gains, not calibrated kelvin. No HDR, log conversion or professional color management.", panel); note->setWordWrap(true); form->addRow(note); }
        auto choice = [&](const QString& key, const QStringList& items, const QString& initial, const QString& caption) {
            auto* box = new QComboBox(panel); box->setObjectName("effect_" + key); box->addItems(items); box->setCurrentText(original.parameters.value(key).toString(initial)); choices[key] = box; form->addRow(caption, box);
        };
        if (original.type == "composite") choice("mode", {"sourceOver", "multiply", "screen"}, "sourceOver", "Blend mode (last enabled mode wins)");
        if (original.type == "videoFade") {
            for (const auto& edge : {QString("in"), QString("out")}) {
                const auto caption = edge == "in" ? QString("Fade in") : QString("Fade out");
                auto* frames = new QLineEdit(panel); time.initialize(frames, original.parameters.value(edge + "Frames").toString("0").toLongLong()); frames->setObjectName("effect_" + edge + "Frames"); texts[edge + "Frames"] = frames;
                form->addRow(time.caption(caption + " duration"), frames);
                choice(edge + "Mode", {"opacity", "color"}, "opacity", caption + " kind");
                choice(edge + "Curve", {"hold", "linear", "eased"}, "linear", caption + " curve");
                auto* colorRow = new QWidget(panel); auto* line = new QHBoxLayout(colorRow); line->setContentsMargins(0, 0, 0, 0);
                auto* color = new QLineEdit(original.parameters.value(edge + "Color").toString("#ff000000"), colorRow); color->setObjectName("effect_" + edge + "Color"); texts[edge + "Color"] = color;
                auto* pick = new QPushButton("Choose…", colorRow); line->addWidget(color); line->addWidget(pick);
                QObject::connect(pick, &QPushButton::clicked, &dialog, [&, color] { const auto c = QColorDialog::getColor(QColor(color->text()), &dialog, "Choose solid fade color"); if (c.isValid()) color->setText(c.name(QColor::HexArgb)); });
                form->addRow(caption + " solid color (#ffRRGGBB)", colorRow);
            }
            auto* help = new QLabel(QString("0 disables a fade; each duration is at most %1. First/last images reach the fade endpoint. Overlapping fades apply in then out. Opacity reveals lower tracks or black. Splits keep only outer fades.").arg(time.text(clip.durationFrames)), panel); help->setWordWrap(true); form->addRow(help);
        }
        QTableWidget* table = nullptr;
        if (!numbers.isEmpty()) {
            auto* note = new QLabel(QString("Keyframe times are relative to the original clip content origin. Current origin offset: %1; playhead content time: %2. Curves belong to the outgoing segment. Values hold before/after the first/last key. Trims and splits retain animation.").arg(time.text(original.timeOffsetFrames), time.text(localFrame + original.timeOffsetFrames)), panel); note->setWordWrap(true); form->addRow(note);
            table = new QTableWidget(0, 4, panel); table->setObjectName("effectKeyframes"); table->setHorizontalHeaderLabels({"Parameter", time.seconds() ? "Content time (seconds)" : "Content frame", "Value", "Outgoing curve"}); table->horizontalHeader()->setSectionResizeMode(QHeaderView::Stretch); table->setMinimumHeight(190);
            const auto specs = effectParameters(original.type);
            auto insert = [table, panel, specs, time](const QString& parameter, const Keyframe& k) {
                const auto r = table->rowCount(); table->insertRow(r);
                auto* param = new QComboBox(panel); for (const auto& spec : specs) param->addItem(spec.name); param->setCurrentText(parameter); table->setCellWidget(r, 0, param);
                auto* timing = new QLineEdit(panel); time.initialize(timing, k.frame); table->setCellWidget(r, 1, timing);
                auto* value = new QDoubleSpinBox(panel); value->setDecimals(6); value->setRange(-3600, 3600); value->setValue(k.value); value->setProperty("initial", value->value()); value->setProperty("exact", k.value); table->setCellWidget(r, 2, value);
                auto* curve = new QComboBox(panel); curve->addItems({"hold", "linear", "eased"}); curve->setCurrentText(k.curve); table->setCellWidget(r, 3, curve);
            };
            for (auto it = original.keyframes.begin(); it != original.keyframes.end(); ++it) for (const auto& k : *it) insert(it.key(), k);
            form->addRow(table);
            auto* keyRow = new QWidget(panel); auto* keyLayout = new QHBoxLayout(keyRow);
            auto* parameter = new QComboBox(panel); parameter->setObjectName("keyframeParameter"); for (const auto& spec : specs) parameter->addItem(spec.name); keyLayout->addWidget(parameter);
            auto* newKey = new QPushButton("Add at playhead", panel); newKey->setObjectName("keyframeAdd"); keyLayout->addWidget(newKey);
            auto* delKey = new QPushButton("Delete selected key", panel); delKey->setObjectName("keyframeRemove"); keyLayout->addWidget(delKey); form->addRow(keyRow);
            QObject::connect(newKey, &QPushButton::clicked, &dialog, [=] { insert(parameter->currentText(), {std::max<qint64>(0, localFrame + original.timeOffsetFrames), effectValue(original, parameter->currentText(), localFrame), "linear"}); });
            QObject::connect(delKey, &QPushButton::clicked, &dialog, [table] { if (table->currentRow() >= 0) table->removeRow(table->currentRow()); });
        }
        save = [&, row, original, enabled, numbers, texts, choices, table, invert] {
            auto e = original; e.enabled = enabled->isChecked();
            if (invert) e.parameters["invert"] = invert->isChecked();
            for (auto it = numbers.begin(); it != numbers.end(); ++it) if (original.parameters.contains(it.key()) || it.value()->value() != it.value()->property("initial").toDouble())
                e.parameters[it.key()] = it.value()->value() == it.value()->property("initial").toDouble() ? it.value()->property("exact").toDouble() : it.value()->value();
            for (auto it = texts.begin(); it != texts.end(); ++it) {
                if (it.key().endsWith("Frames")) {
                    qint64 frames = 0;
                    if (!time.read(it.value(), frames)) { error->setText(time.seconds() ? "Fade durations must be nonnegative seconds." : "Fade durations must be nonnegative whole sequence frames."); return false; }
                    e.parameters[it.key()] = QString::number(frames);
                } else e.parameters[it.key()] = it.value()->text();
            }
            for (auto it = choices.begin(); it != choices.end(); ++it) e.parameters[it.key()] = it.value()->currentText();
            if (e.type == "lut" && e.parameters.value("path") != original.parameters.value("path")) {
                if (e.parameters.value("path").toString().isEmpty()) { e.parameters["size"] = 0; e.parameters["table"] = QJsonArray{}; }
                else if (const auto failure = importCube(e, e.parameters.value("path").toString()); !failure.isEmpty()) { error->setText(failure); return false; }
            }
            if (table) {
                e.keyframes.clear();
                for (int r = 0; r < table->rowCount(); ++r) {
                    qint64 frame = 0;
                    if (!time.read(qobject_cast<QLineEdit*>(table->cellWidget(r, 1)), frame)) { error->setText(time.seconds() ? "Keyframe times must be nonnegative seconds." : "Keyframe times must be nonnegative whole sequence frames."); return false; }
                    const auto* value = qobject_cast<QDoubleSpinBox*>(table->cellWidget(r, 2));
                    e.keyframes[qobject_cast<QComboBox*>(table->cellWidget(r, 0))->currentText()].append({frame,
                        value->value() == value->property("initial").toDouble() ? value->property("exact").toDouble() : value->value(), qobject_cast<QComboBox*>(table->cellWidget(r, 3))->currentText()});
                }
                for (auto& keys : e.keyframes) std::sort(keys.begin(), keys.end(), [](const auto& a, const auto& b) { return a.frame < b.frame; });
            }
            const auto failure = validateEffect(e, clip.durationFrames);
            if (!failure.isEmpty()) { error->setText(failure); return false; }
            working[row] = e; return true;
        };
    };
    QObject::connect(list, &QListWidget::currentRowChanged, &dialog, [&](int row) {
        if (!save()) { QSignalBlocker blocker(list); list->setCurrentRow(selected); return; }
        show(row);
    });
    QObject::connect(add, &QPushButton::clicked, &dialog, [&] {
        if (!save()) return;
        if (working.size() >= 64) { error->setText("The stack limit is 64 effects."); return; }
        working.append(defaultEffect(type->currentData().toString())); populate(static_cast<int>(working.size() - 1)); show(static_cast<int>(working.size() - 1));
    });
    QObject::connect(remove, &QPushButton::clicked, &dialog, [&] { if (selected < 0) return; working.removeAt(selected); const auto r = std::min(selected, static_cast<int>(working.size() - 1)); populate(r); show(r); });
    auto move = [&](int delta) { if (selected < 0 || selected + delta < 0 || selected + delta >= working.size() || !save()) return; const auto r = selected + delta; working.swapItemsAt(selected, r); populate(r); show(r); };
    QObject::connect(up, &QPushButton::clicked, &dialog, [&] { move(-1); }); QObject::connect(down, &QPushButton::clicked, &dialog, [&] { move(1); });
    QObject::connect(reset, &QPushButton::clicked, &dialog, [&] { if (selected < 0 || !supportedEffect(working[selected])) return; auto e = defaultEffect(working[selected].type, working[selected].id); e.enabled = working[selected].enabled; working[selected] = e; show(selected); });
    QObject::connect(buttons, &QDialogButtonBox::accepted, &dialog, [&] { if (save()) dialog.accept(); });
    populate(working.isEmpty() ? -1 : 0); show(working.isEmpty() ? -1 : 0);
    const auto result = dialog.exec();
    // List selection can emit while Qt destroys its model. Disconnect captured local state first.
    for (auto* child : dialog.findChildren<QObject*>()) QObject::disconnect(child, nullptr, &dialog, nullptr);
    if (result == QDialog::Accepted) return working;
    return {};
}
}
