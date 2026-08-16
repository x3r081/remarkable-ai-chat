#include "configstore.h"

#include <QDir>
#include <QFile>
#include <QJsonDocument>
#include <QJsonObject>
#include <QLoggingCategory>

Q_LOGGING_CATEGORY(lcConfig, "rmchat.config")

namespace {
const char *kDefaultSystemPrompt =
    "You are a helpful assistant running on a reMarkable e-paper tablet. "
    "The user's messages may arrive as images of handwriting, which may be "
    "messy - read it carefully and use context to resolve unclear words. "
    "When the message is an image, ALWAYS begin your reply with exactly one "
    "line of the form: Read: \"<your best transcription of the handwriting>\" "
    "followed by a blank line, then your actual response. "
    "Keep responses concise and plain: no markdown tables, no emoji - the "
    "display is slow black-and-white e-paper.";
}

ConfigStore::ConfigStore(QString dir, QObject *parent)
    : QObject(parent), m_dir(std::move(dir))
{
    m_systemPrompt = QString::fromUtf8(kDefaultSystemPrompt);
    QDir().mkpath(m_dir);
    load();
}

void ConfigStore::load()
{
    QFile f(m_dir + "/config.json");
    if (!f.open(QIODevice::ReadOnly))
        return;
    const auto o = QJsonDocument::fromJson(f.readAll()).object();
    m_apiBase = o.value("api_base").toString(m_apiBase);
    m_apiKey = o.value("api_key").toString(m_apiKey);
    m_model = o.value("model").toString(m_model);
    m_systemPrompt = o.value("system_prompt").toString(m_systemPrompt);
    m_sendHistory = o.value("send_history").toInt(m_sendHistory);
    m_penFlipX = o.value("pen_flip_x").toBool(m_penFlipX);
    m_penFlipY = o.value("pen_flip_y").toBool(m_penFlipY);
    m_epaperBlink = o.value("epaper_blink").toBool(m_epaperBlink);
    qCInfo(lcConfig) << "loaded config: base=" << m_apiBase
                     << "model=" << m_model
                     << "key=" << (m_apiKey.isEmpty() ? "unset" : "set");
    emit changed();
}

void ConfigStore::setApiBase(const QString &v)
{
    if (m_apiBase == v)
        return;
    m_apiBase = v.trimmed();
    emit changed();
}

void ConfigStore::setApiKey(const QString &v)
{
    if (m_apiKey == v)
        return;
    m_apiKey = v.trimmed();
    emit changed();
}

void ConfigStore::setModel(const QString &v)
{
    if (m_model == v)
        return;
    m_model = v.trimmed();
    emit changed();
}

void ConfigStore::save()
{
    const QJsonObject o{
        {"api_base", m_apiBase},
        {"api_key", m_apiKey},
        {"model", m_model},
        {"system_prompt", m_systemPrompt},
        {"send_history", m_sendHistory},
        {"pen_flip_x", m_penFlipX},
        {"pen_flip_y", m_penFlipY},
        {"epaper_blink", m_epaperBlink},
    };
    QFile f(m_dir + "/config.json");
    if (!f.open(QIODevice::WriteOnly | QIODevice::Truncate)) {
        qCWarning(lcConfig) << "cannot write config.json";
        return;
    }
    f.write(QJsonDocument(o).toJson(QJsonDocument::Indented));
    f.setPermissions(QFileDevice::ReadOwner | QFileDevice::WriteOwner);
    qCInfo(lcConfig) << "config saved";
}

QString ConfigStore::keyShape() const
{
    if (m_apiKey.isEmpty())
        return QStringLiteral("no key stored");

    int upper = 0, lower = 0, digit = 0, dash = 0, underscore = 0, other = 0;
    bool space = false;
    for (const QChar c : m_apiKey) {
        if (c.isSpace())            { space = true; other++; }
        else if (c.isUpper())       upper++;
        else if (c.isLower())       lower++;
        else if (c.isDigit())       digit++;
        else if (c == '-')          dash++;
        else if (c == '_')          underscore++;
        else                        other++;
    }

    // A prefix like "sk-proj-" is not secret and identifies the key type.
    const int prefixLen = qMin(7, m_apiKey.size());
    QString shape = QString("length=%1 prefix=%2... "
                            "upper=%3 lower=%4 digits=%5 dashes=%6 underscores=%7")
                        .arg(m_apiKey.size())
                        .arg(m_apiKey.left(prefixLen))
                        .arg(upper).arg(lower).arg(digit).arg(dash).arg(underscore);
    if (space)
        shape += "  *** CONTAINS WHITESPACE ***";
    if (other > (space ? 1 : 0))
        shape += QString("  *** %1 unexpected character(s) ***").arg(other);
    return shape;
}

void ConfigStore::reportKeyShape() const
{
    qInfo("selftest: base=%s model=%s", qPrintable(m_apiBase), qPrintable(m_model));
    qInfo("selftest: key %s", qPrintable(keyShape()));
}

QString ConfigStore::loadHistoryJson() const
{
    QFile f(m_dir + "/history.json");
    if (!f.open(QIODevice::ReadOnly))
        return QStringLiteral("[]");
    return QString::fromUtf8(f.readAll());
}

void ConfigStore::saveHistoryJson(const QString &json)
{
    QFile f(m_dir + "/history.json");
    if (f.open(QIODevice::WriteOnly | QIODevice::Truncate))
        f.write(json.toUtf8());
}

void ConfigStore::clearHistory()
{
    QFile::remove(m_dir + "/history.json");
}
