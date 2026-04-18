#include <QApplication>
#include <QCoreApplication>
#include <QGuiApplication>

#include "app/application_controller.h"
#include "platform/windows/dpi_awareness.h"
#include "ui/icon_factory.h"

int main(int argc, char* argv[]) {
    (void)ais::platform::windows::enablePerMonitorDpiV2();

    QApplication app(argc, argv);
    app.setQuitOnLastWindowClosed(false);

    QCoreApplication::setOrganizationName(QStringLiteral("AiScreenshotTool"));
    QCoreApplication::setApplicationName(QStringLiteral("AiScreenshotTool"));
    QGuiApplication::setApplicationDisplayName(ais::ui::brandDisplayName());
    app.setWindowIcon(ais::ui::createAppIcon());

    ais::app::ApplicationController controller;
    if (!controller.initialize()) {
        return 1;
    }

    return app.exec();
}
