#pragma once

#include <QObject>
#include <QString>

/*!
 * config.json + history.json on disk. The API key lives only on the tablet,
 * entered there by the user; nothing here ever logs it.
 */
class ConfigStore : public QObject
{
    Q_OBJECT
    Q_PROPERTY(QString apiBase READ apiBase WRITE setApiBase NOTIFY changed)
    Q_PROPERTY(QString apiKey READ apiKey WRITE setApiKey NOTIFY changed)
    Q_PROPERTY(QString model READ model WRITE setModel NOTIFY changed)
    Q_PROPERTY(QString systemPrompt READ systemPrompt NOTIFY changed)
    Q_PROPERTY(bool configured READ configured NOTIFY changed)

public:
    explicit ConfigStore(QString dir, QObject *parent = nullptr);

    QString apiBase() const { return m_apiBase; }
    QString apiKey() const { return m_apiKey; }
    QString model() const { return m_model; }
    QString systemPrompt() const { return m_systemPrompt; }
    int sendHistory() const { return m_sendHistory; }
    bool penFlipX() const { return m_penFlipX; }
    bool penFlipY() const { return m_penFlipY; }
    /*! Off by default: the forced e-paper refresh is suspected of
     *  hanging the display pipeline and tripping the watchdog. */
    bool epaperBlink() const { return m_epaperBlink; }
    bool configured() const { return !m_apiKey.isEmpty() && !m_model.isEmpty(); }

    void setApiBase(const QString &v);
    void setApiKey(const QString &v);
    void setModel(const QString &v);

    Q_INVOKABLE void save();

    /*! Length + character classes of the stored key, for diagnosing a
     *  rejected key. Never logs the key itself. */
    Q_INVOKABLE QString keyShape() const;
    void reportKeyShape() const;

    Q_INVOKABLE QString loadHistoryJson() const;   // "[]" if none
    Q_INVOKABLE void saveHistoryJson(const QString &json);
    Q_INVOKABLE void clearHistory();

signals:
    void changed();

private:
    void load();

    QString m_dir;
    QString m_apiBase = QStringLiteral("https://api.openai.com/v1");
    QString m_apiKey;
    QString m_model = QStringLiteral("gpt-4o-mini");
    QString m_systemPrompt;
    int m_sendHistory = 12;
    bool m_penFlipX = false;
    bool m_penFlipY = false;
    bool m_epaperBlink = false;
};
