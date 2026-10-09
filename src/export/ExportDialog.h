#pragma once
#include "ExportWorker.h"
#include <QDialog>

class QLineEdit;
class QSpinBox;
class QComboBox;
class QCheckBox;
class QProgressBar;
class QLabel;
class QPushButton;
class QTimer;
class QTableWidget;
class QDoubleSpinBox;
class QTabWidget;
namespace editor::exporting {
class ExportDialog final : public QDialog {
    Q_OBJECT
public:
    ExportDialog(playback::RenderDescription description, project::ExportSettings settings, QWidget* parent = nullptr);
    ~ExportDialog() override;
    void reject() override;
signals:
    void settingsChosen(editor::project::ExportSettings settings);
private:
    void startExport();
    void setBusy(bool busy);
    project::ExportSettings selectedSettings() const;
    void settingsEdited();
    void launchCheck();
    void showValidation();
    void updateEncoders();
    void updateControls(bool reset);
    void changeExtension();
    void syncSimple();
    bool simpleCompatible() const;
    void useCompatibleDefaults();
    playback::RenderDescription description_;
    project::ExportSettings settings_;
    project::Rational automaticFrameRate_{30, 1};
    ExportWorker* worker_ = nullptr;
    QWidget* options_;
    QLineEdit* path_;
    QSpinBox *width_, *height_, *rateNumerator_, *rateDenominator_;
    QDoubleSpinBox* quality_;
    QComboBox *format_, *speed_, *encoderChoice_, *pixel_, *container_, *qualityMode_, *audioEncoder_;
    QTabWidget* tabs_;
    QWidget* simpleControls_;
    QComboBox *simpleTarget_, *simpleSize_, *simpleQuality_, *audioBitrate_;
    QCheckBox *simpleAudio_, *automaticRate_;
    QLabel* simpleNotice_;
    QTableWidget* nativeOptions_;
    QLabel* qualityLabel_;
    QCheckBox* audio_;
    QProgressBar* progress_;
    QLabel* message_;
    QPushButton *start_, *cancel_, *close_;
    QLabel* capabilityMessage_;
    CapabilityWorker* capabilityWorker_ = nullptr;
    QTimer* checkTimer_;
    QByteArray checkedKey_, runningKey_;
    QByteArray requestedKey_;
    QByteArray customEncoderCatalogKey_;
    QJsonArray customEncoderCatalog_;
    quint64 checkGeneration_ = 0;
    QJsonObject capabilities_;
    QJsonArray deviceProbes_;
    bool refreshDevices_ = true;
    bool forceRefreshCapabilities_ = false;
    bool closing_ = false;
    bool rebuilding_ = false;
};
}
