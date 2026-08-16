#include "chatclient.h"
#include "configstore.h"
#include "epaper.h"
#include "penmouse.h"
#include "penreader.h"

#include <QCommandLineOption>
#include <QCommandLineParser>
#include <QGuiApplication>
#include <QQmlApplicationEngine>
#include <QQmlContext>
#include <QStyleHints>

int main(int argc, char *argv[])
{
    QGuiApplication app(argc, argv);
    app.setApplicationName("rm_chat");

    // A blinking cursor makes e-paper flash twice a second.
    app.styleHints()->setCursorFlashTime(0);

    QCommandLineParser parser;
    parser.addHelpOption();
    QCommandLineOption dirOption("dir", "Data directory (config + history).",
                                 "path", "/home/root/rm-chat");
    QCommandLineOption penOption("pen-device",
                                 "Pen input device (default: autodetect).", "path");
    QCommandLineOption screenshotOption("screenshot",
                                        "Render one frame to <file> and exit.", "file");
    QCommandLineOption autoSendOption("auto-send-ms",
                                      "TEST HOOK: send the canvas after N ms.", "ms");
    QCommandLineOption autoClearOption("auto-clear-ms",
                                       "TEST HOOK: press Clear after N ms.", "ms");
    QCommandLineOption shotDelayOption("screenshot-delay-ms",
                                       "TEST HOOK: delay the screenshot.", "ms");
    QCommandLineOption selfTestOption("selftest",
                                      "Check the stored credentials and exit. "
                                      "Reports the key's length and shape, never "
                                      "the key itself.");
    parser.addOption(dirOption);
    parser.addOption(penOption);
    parser.addOption(screenshotOption);
    parser.addOption(autoSendOption);
    parser.addOption(autoClearOption);
    parser.addOption(shotDelayOption);
    parser.addOption(selfTestOption);
    parser.process(app);

    ConfigStore config(parser.value(dirOption));
    ChatClient chat;
    Epaper epaper;

    if (parser.isSet(selfTestOption)) {
        config.reportKeyShape();
        QObject::connect(&chat, &ChatClient::testResult, &app,
                         [](bool ok, const QString &detail) {
                             qInfo("selftest: %s %s", ok ? "PASS" : "FAIL",
                                   qPrintable(detail));
                             QCoreApplication::exit(ok ? 0 : 1);
                         });
        qInfo("selftest: GET %s/models", qPrintable(config.apiBase()));
        chat.testConnection(config.apiBase(), config.apiKey());
        return app.exec();
    }

    PenReader pen(parser.value(penOption),
                  config.penFlipX(), config.penFlipY());

    // Pen taps double as mouse clicks (buttons, fields); the canvas gets the
    // raw samples separately for inking.
    PenMouse penMouse;
    QObject::connect(&pen, &PenReader::sample, &penMouse, &PenMouse::sample);
    pen.start();

    QQmlApplicationEngine engine;
    engine.rootContext()->setContextProperty("config", &config);
    engine.rootContext()->setContextProperty("chat", &chat);
    engine.rootContext()->setContextProperty("pen", &pen);
    engine.rootContext()->setContextProperty("epaper", &epaper);
    engine.rootContext()->setContextProperty("cfgEpaperBlink",
                                             config.epaperBlink());
    engine.rootContext()->setContextProperty("cfgHistoryLimit", config.sendHistory());
    engine.rootContext()->setContextProperty("cfgOffscreen",
                                             app.platformName() == "offscreen");
    engine.rootContext()->setContextProperty("cfgScreenshot",
                                             parser.value(screenshotOption));
    engine.rootContext()->setContextProperty("cfgAutoSendMs",
                                             parser.value(autoSendOption).toInt());
    engine.rootContext()->setContextProperty("cfgAutoClearMs",
                                             parser.value(autoClearOption).toInt());
    engine.rootContext()->setContextProperty("cfgShotDelayMs",
                                             parser.value(shotDelayOption).toInt());

    QObject::connect(&engine, &QQmlApplicationEngine::objectCreationFailed, &app,
                     []() { QCoreApplication::exit(-1); }, Qt::QueuedConnection);
    engine.loadFromModule("rm_chat_module", "Main");
    return app.exec();
}
