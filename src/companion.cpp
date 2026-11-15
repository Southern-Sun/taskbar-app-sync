#include "syncengine.h"
#include <Plasma/Applet>
#include <Plasma/Containment>
#include <Plasma/Corona>
#include <PlasmaQuick/AppletQuickItem>
#include <KConfigGroup>
#include <KConfigPropertyMap>
#include <KPluginFactory>
#include <QChildEvent>
#include <QJsonDocument>
#include <QSet>
#include <QDBusAbstractAdaptor>
#include <QDBusConnection>

// A read-only report on the existing plasmashell D-Bus connection.
class ReportAdaptor : public QDBusAbstractAdaptor
{
    Q_OBJECT
    Q_CLASSINFO("D-Bus Interface", "org.local.TaskbarSync")
    Q_PROPERTY(QString report READ report)
public:
    explicit ReportAdaptor(QObject *parent) : QDBusAbstractAdaptor(parent) {}
    QString report() const { return parent()->property("report").toString(); }
};

class Companion : public Plasma::Applet
{
    Q_OBJECT
    Q_PROPERTY(QVariantList taskbars READ taskbars NOTIFY statusChanged)
    Q_PROPERTY(QString report READ report NOTIFY statusChanged)
    Q_PROPERTY(bool syncing READ syncing WRITE setSyncing NOTIFY statusChanged)
    Q_PROPERTY(QString discoveryError READ discoveryError NOTIFY statusChanged)
public:
    Companion(QObject *parent, const KPluginMetaData &data, const QVariantList &args)
        : Plasma::Applet(parent, data, args), m_engine(this)
    {
        m_discover.setSingleShot(true);
        m_discover.setInterval(0);
        connect(&m_discover, &QTimer::timeout, this, &Companion::discover);
        connect(&m_engine, &SyncEngine::changed, this, &Companion::statusChanged);
    }
    ~Companion() override
    {
        if (s_owner == this) QDBusConnection::sessionBus().unregisterObject("/TaskbarSync");
    }
    void init() override
    {
        Plasma::Applet::init();
        if (s_owner && s_owner != this) {
            m_error = "Another companion instance is already active. Remove this duplicate.";
            emit statusChanged(); return;
        }
        s_owner = this;
        new ReportAdaptor(this);
        QDBusConnection::sessionBus().registerObject("/TaskbarSync", this, QDBusConnection::ExportAdaptors);
        auto c = containment();
        m_corona = c ? c->corona() : nullptr;
        if (!m_corona) { m_error = "No Plasma corona is available"; emit statusChanged(); return; }
        connect(m_corona, &Plasma::Corona::containmentAdded, this, [this] { m_discover.start(); });
        connect(m_corona, &Plasma::Corona::containmentUiReadyChanged, this, [this] { m_discover.start(); });
        m_engine.setEnabled(config().group("General").readEntry("syncing", false));
        m_discover.start();
    }
    void configChanged() override
    {
        Plasma::Applet::configChanged();
        const bool enabled = config().group("General").readEntry("syncing", false);
        if (s_owner == this && enabled != syncing()) m_engine.setEnabled(enabled);
    }
    QVariantList taskbars() const { return m_engine.diagnostics(); }
    QString report() const { return QString::fromUtf8(QJsonDocument::fromVariant(taskbars()).toJson(QJsonDocument::Indented)); }
    QString discoveryError() const { return m_error; }
    bool syncing() const { return m_engine.enabled(); }
    void setSyncing(bool enabled)
    {
        if (s_owner != this) return;
        config().group("General").writeEntry("syncing", enabled);
        emit configNeedsSaving();
        m_engine.setEnabled(enabled);
    }
    Q_INVOKABLE void rescan() { m_discover.start(); }
signals:
    void statusChanged();
protected:
    bool eventFilter(QObject *, QEvent *event) override
    {
        if (event->type() == QEvent::ChildAdded || event->type() == QEvent::ChildRemoved)
            m_discover.start();
        return false;
    }
private:
    inline static QPointer<Companion> s_owner;
    SyncEngine m_engine;
    QPointer<Plasma::Corona> m_corona;
    QTimer m_discover;
    QSet<QObject *> m_watched;
    QHash<uint, QPointer<QAbstractItemModel>> m_attached;
    QString m_error;
    void watch(QObject *object)
    {
        if (!object || m_watched.contains(object)) return;
        m_watched.insert(object);
        object->installEventFilter(this);
        connect(object, &QObject::destroyed, this, [this, object] {
            m_watched.remove(object); m_discover.start();
        });
    }
    void discover()
    {
        if (!m_corona || s_owner != this) return;
        QSet<uint> present;
        QStringList errors;
        for (auto c : m_corona->containments()) {
            if (!m_watched.contains(c)) {
                watch(c);
                connect(c, &Plasma::Containment::appletAdded, this, [this] { m_discover.start(); });
                connect(c, &Plasma::Containment::appletRemoved, this, [this] { m_discover.start(); });
                connect(c, &Plasma::Containment::screenChanged, this, [this] { m_discover.start(); });
            }
            for (auto a : c->applets()) {
                const auto id = a->pluginMetaData().pluginId();
                if (id != "org.kde.plasma.taskmanager" && id != "org.kde.plasma.icontasks") continue;
                present.insert(a->id());
                watch(a);
                // Never instantiate a replacement UI or load an applet's QML ourselves.
                if (!PlasmaQuick::AppletQuickItem::hasItemForApplet(a)) {
                    errors << QString("Widget %1: waiting for existing QML item").arg(a->id()); continue;
                }
                auto root = PlasmaQuick::AppletQuickItem::itemForApplet(a);
                watch(root);
                auto model = qobject_cast<QAbstractItemModel *>(root->property("tasksModel").value<QObject *>());
                if (!model) {
                    errors << QString("Widget %1: unsupported tasksModel API").arg(a->id()); continue;
                }
                if (m_attached.value(a->id()) != model) {
                    m_engine.detach(a->id());
                    m_attached[a->id()] = model;
                    m_engine.attach(a->id(), c->screen(), root, model, a->configuration());
                } else m_engine.setScreen(a->id(), c->screen());
            }
        }
        for (auto id : m_attached.keys()) if (!present.contains(id)) {
            m_engine.detach(id); m_attached.remove(id);
        }
        m_error = errors.join('\n');
        emit statusChanged();
    }
};
K_PLUGIN_CLASS_WITH_JSON(Companion, "../package/metadata.json")
#include "companion.moc"
