#include "Capabilities.h"
#include <QCoreApplication>
#include <QFile>
#include <QJsonDocument>
#include <cstdio>
int main(int argc, char** argv) {
    QCoreApplication app(argc, argv); QJsonObject result;
    if (app.arguments().size() == 2 && app.arguments()[1] == "--inventory") result = editor::exporting::compiledCapabilities();
    else if (app.arguments().size() == 3 && app.arguments()[1] == "--device") result = editor::exporting::localDeviceProbe(app.arguments()[2]);
    else if (app.arguments().size() == 2 && app.arguments()[1] == "--encoder") {
        QFile input; if (!input.open(stdin, QIODevice::ReadOnly)) return 3;
        result = editor::exporting::localEncoderProbe(QJsonDocument::fromJson(input.readAll()).object());
    } else return 2;
    const auto bytes = QJsonDocument(result).toJson(QJsonDocument::Compact);
    std::fwrite(bytes.constData(), 1, static_cast<size_t>(bytes.size()), stdout); return 0;
}
