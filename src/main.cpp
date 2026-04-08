#include <QApplication>
#include <QStyleFactory>
#include <QDebug>
#include <QFile>
#include <QTextStream>
#include <QDateTime>
#include <iostream>
#include "mainwindow.h"

static QFile gLogFile;

static void qtMessageOutput(QtMsgType type, const QMessageLogContext &context, const QString &msg)
{
    if (!gLogFile.isOpen()) {
        gLogFile.setFileName("WattsFun_debug.log");
        gLogFile.open(QIODevice::Append | QIODevice::Text);
    }

    QTextStream out(&gLogFile);
    QString level;
    switch (type) {
        case QtDebugMsg: level = "DEBUG"; break;
        case QtInfoMsg: level = "INFO"; break;
        case QtWarningMsg: level = "WARN"; break;
        case QtCriticalMsg: level = "ERROR"; break;
        case QtFatalMsg: level = "FATAL"; break;
    }
    out << QDateTime::currentDateTime().toString("hh:mm:ss.zzz")
        << " [" << level << "] "
        << msg << " (" << context.file << ":" << context.line << ")\n";
    out.flush();

    std::cerr << msg.toStdString() << std::endl;

    if (type == QtFatalMsg) {
        abort();
    }
}

static void logToFile(const QString &msg) {
    QFile log("WattsFun_debug.log");
    if (log.open(QIODevice::Append | QIODevice::Text)) {
        QTextStream out(&log);
        out << QDateTime::currentDateTime().toString("hh:mm:ss.zzz") << " " << msg << "\n";
        log.close();
    }
    std::cerr << msg.toStdString() << std::endl;
}

int main(int argc, char *argv[])
{
    qInstallMessageHandler(qtMessageOutput);
    qDebug() << "[MAIN] Message handler installed";

    try {
        logToFile("[MAIN] Application starting...");
        
        // High-DPI support
        QApplication::setHighDpiScaleFactorRoundingPolicy(
            Qt::HighDpiScaleFactorRoundingPolicy::PassThrough);

        logToFile("[MAIN] Creating QApplication...");
        QApplication app(argc, argv);
        
        logToFile("[MAIN] Setting app properties...");
        app.setApplicationName("WattsFun");
        app.setOrganizationName("WattsFun");
        app.setApplicationVersion("1.0.0");

        // Use the Fusion style as a solid cross-platform base;
        // the dark theme is applied via QSS in MainWindow.
        app.setStyle(QStyleFactory::create("Fusion"));

        // Dark tooltip style (tooltips are top-level, must be set at app level)
        app.setStyleSheet(
            "QToolTip { color: #cdd6f4; background-color: #313244;"
            "  border: 1px solid #45475a; padding: 4px; font-size: 12px; }");

        logToFile("[MAIN] Creating MainWindow...");
        MainWindow w;
        
        logToFile("[MAIN] Showing MainWindow...");
        w.show();
        
        logToFile("[MAIN] Entering event loop...");
        int result = app.exec();
        logToFile("[MAIN] Application exiting normally with code: " + QString::number(result));
        return result;
        
    } catch (const std::exception &e) {
        logToFile(QString("[ERROR] std::exception: %1").arg(e.what()));
        return 1;
    } catch (...) {
        logToFile("[ERROR] Unknown exception caught");
        return 1;
    }
}
