#pragma once
#include <QAbstractItemModel>
#include <QPointer>
#include <QTimer>
#include <QVariantMap>

// One engine belongs to one companion. All calls run on Plasma's GUI thread.
class SyncEngine : public QObject
{
    Q_OBJECT
public:
    explicit SyncEngine(QObject *parent = nullptr);
    void attach(uint id, int screen, QObject *root, QAbstractItemModel *model,
                QObject *configuration = nullptr);
    void detach(uint id);
    void setScreen(uint id, int screen);
    void setEnabled(bool enabled);
    bool enabled() const { return m_enabled; }
    QVariantList diagnostics() const;
    static QStringList merge(const QStringList &surviving, const QStringList &observed);
signals:
    void changed();
private slots:
    void settle();
    void rootChanged();
    void policyChanged();
private:
    struct Peer {
        uint id;
        int screen;
        QPointer<QObject> root, configuration;
        QPointer<QAbstractItemModel> model;
        QList<QMetaObject::Connection> connections;
        QString error;
    };
    QList<Peer> m_peers;
    QStringList m_order, m_launchers;
    QTimer m_timer;
    uint m_source = 0;
    bool m_move = false, m_pins = false, m_applying = false;
    bool m_enabled = false, m_seeded = false;
    Peer *peer(uint id);
    bool read(const Peer &peer, QStringList &keys, QString &error) const;
    QString compatibility(const Peer &peer, const Peer *reference) const;
    void schedule(uint source = 0, bool moved = false, bool pins = false);
    void fail(Peer &peer, const QString &error);
};
