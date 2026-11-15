#include "syncengine.h"
#include <QMetaProperty>
#include <QScopedValueRollback>
#include <QSet>
#include <QUrl>
#include <QUuid>
#include <algorithm>

namespace {
const QList<QByteArray> policyProperties = {
    "sortMode", "groupMode", "taskReorderingEnabled", "filterByVirtualDesktop",
    "filterByCurrentVirtualDesktop", "filterByScreen", "filterByActivity", "filterByRegion",
    "filterMinimized", "filterNotMinimized", "filterNotMaximized", "filterHidden",
    "virtualDesktop", "activity", "separateLaunchers", "launchInPlace", "hideActivatedLaunchers"
};
QString normalized(const QVariant &value)
{
    auto url = value.toUrl().adjusted(QUrl::NormalizePathSegments | QUrl::RemoveFragment);
    return url.toString(QUrl::FullyEncoded);
}
bool invokeMove(QObject *model, int from, int to)
{
    bool moved = false;
    return QMetaObject::invokeMethod(model, "move", Qt::DirectConnection, Q_RETURN_ARG(bool, moved),
        Q_ARG(int, from), Q_ARG(int, to), Q_ARG(QModelIndex, QModelIndex())) && moved;
}
}
SyncEngine::SyncEngine(QObject *parent) : QObject(parent)
{
    m_timer.setSingleShot(true);
    m_timer.setInterval(20);
    connect(&m_timer, &QTimer::timeout, this, &SyncEngine::settle);
}
SyncEngine::Peer *SyncEngine::peer(uint id)
{
    for (auto &p : m_peers) if (p.id == id) return &p;
    return nullptr;
}
void SyncEngine::attach(uint id, int screen, QObject *root, QAbstractItemModel *model, QObject *configuration)
{
    if (peer(id) || !root || !model) return;
    m_peers.append({id, screen, root, configuration, model, {}, {}});
    auto &p = m_peers.last();
    auto watch = [&](auto signal, auto callback) {
        p.connections << connect(model, signal, this, callback);
    };
    watch(&QAbstractItemModel::rowsMoved, [this, id] { schedule(id, true); });
    watch(&QAbstractItemModel::layoutChanged, [this] { schedule(); });
    watch(&QAbstractItemModel::rowsInserted, [this] { schedule(); });
    watch(&QAbstractItemModel::rowsRemoved, [this] { schedule(); });
    watch(&QAbstractItemModel::modelReset, [this] { schedule(); });
    watch(&QAbstractItemModel::dataChanged, [this] { schedule(); });
    p.connections << connect(model, &QObject::destroyed, this, [this, id] { detach(id); });
    p.connections << connect(root, &QObject::destroyed, this, [this, id] { detach(id); });
    auto propertySignal = [&](QObject *object, const char *name, const char *slot) {
        auto property = object->metaObject()->property(object->metaObject()->indexOfProperty(name));
        if (property.isValid() && property.hasNotifySignal())
            p.connections << connect(object, property.notifySignal(), this,
                metaObject()->method(metaObject()->indexOfSlot(slot)));
    };
    propertySignal(root, "dragSource", "rootChanged()");
    propertySignal(model, "launcherList", "rootChanged()");
    for (const auto &name : policyProperties) propertySignal(model, name.constData(), "policyChanged()");
    schedule();
}
void SyncEngine::detach(uint id)
{
    if (m_applying) {
        if (auto p = peer(id)) { p->model = nullptr; p->root = nullptr; }
        QTimer::singleShot(0, this, [this, id] { detach(id); });
        return;
    }
    for (qsizetype i = 0; i < m_peers.size(); ++i) {
        if (m_peers[i].id != id) continue;
        for (const auto &c : m_peers[i].connections) disconnect(c);
        m_peers.removeAt(i);
        if (m_source == id) { m_source = 0; m_move = m_pins = false; }
        schedule();
        break;
    }
}
void SyncEngine::setScreen(uint id, int screen)
{
    if (auto p = peer(id)) p->screen = screen;
    schedule();
}
void SyncEngine::setEnabled(bool enabled)
{
    m_enabled = enabled;
    m_source = 0;
    m_move = m_pins = false;
    // Observation never carries edits into a later enabled session.
    m_seeded = false;
    m_order.clear();
    schedule();
    emit changed();
}
void SyncEngine::schedule(uint source, bool moved, bool pins)
{
    if (m_applying) return;
    if (source) {
        if (m_source != source) m_move = m_pins = false;
        m_source = source;
        m_move |= moved;
        m_pins |= pins;
    }
    m_timer.start();
}
void SyncEngine::rootChanged()
{
    for (const auto &p : std::as_const(m_peers)) {
        if (sender() == p.model) { schedule(p.id, false, true); return; }
        if (sender() == p.root) {
            if (!p.root->property("dragSource").value<QObject *>()) schedule(p.id, true);
            else schedule();
            return;
        }
    }
}
void SyncEngine::policyChanged()
{
    // Policy failures may recover after the user changes settings. Runtime API
    // failures remain quarantined until detach/re-add.
    schedule();
}
QString SyncEngine::compatibility(const Peer &p, const Peer *ref) const
{
    if (!p.model || !p.root) return "Widget was destroyed";
    auto m = p.model.data();
    if (m->property("sortMode").toInt() != 1) return "Manual sorting is required";
    if (m->property("groupMode").toInt() != 0) return "Disable grouping";
    if (!m->property("taskReorderingEnabled").toBool()) return "Task reordering is disabled";
    if (p.root->metaObject()->indexOfProperty("dragSource") < 0) return "Unsupported dragSource API";
    if (m->metaObject()->indexOfMethod("move(int,int,QModelIndex)") < 0 ||
        m->metaObject()->indexOfMethod("syncLaunchers()") < 0) return "Unsupported task movement API";
    for (const auto &name : policyProperties) {
        if (m->metaObject()->indexOfProperty(name) < 0 && !m->dynamicPropertyNames().contains(name))
            return "Missing model property: " + QString::fromLatin1(name);
        if (name == "virtualDesktop" && !m->property("filterByVirtualDesktop").toBool()) continue;
        if (name == "activity" && !m->property("filterByActivity").toBool()) continue;
        if (ref && name != "sortMode" && name != "groupMode" &&
            m->property(name) != ref->model->property(name))
            return "Settings differ from screen 0: " + QString::fromLatin1(name);
    }
    if (m->property("filterByScreen").toBool() || m->property("filterByRegion").toInt())
        return "Screen/region filtering prevents a shared window sequence";
    return {};
}
bool SyncEngine::read(const Peer &p, QStringList &keys, QString &error) const
{
    if (!p.model) { error = "Missing model"; return false; }
    auto roles = p.model->roleNames();
    auto role = [&](const QByteArray &name) { return roles.key(name, -1); };
    const int win = role("IsWindow"), ids = role("WinIdList");
    const int url = role("LauncherUrlWithoutIcon"), launcher = role("IsLauncher");
    const int startup = role("IsStartup");
    if (win < 0 || ids < 0 || url < 0 || launcher < 0 || startup < 0) {
        error = "Unsupported task identity roles"; return false;
    }
    for (int row = 0; row < p.model->rowCount(); ++row) {
        auto index = p.model->index(row, 0);
        QString key;
        if (index.data(win).toBool()) {
            const auto values = index.data(ids).toList();
            if (values.size() != 1 || values.first().toUuid().isNull()) {
                error = "Window has no unique Wayland UUID (groups/X11 sessions unsupported)"; return false;
            }
            key = "w:" + values.first().toUuid().toString(QUuid::WithoutBraces);
        } else if (index.data(startup).toBool()) {
            error = "Waiting for startup tasks to become windows"; return false;
        } else if (index.data(launcher).toBool()) {
            auto value = normalized(index.data(url));
            if (value.isEmpty()) { error = "Task has no launcher URL"; return false; }
            key = "l:" + value;
        } else { error = "Unknown task type"; return false; }
        if (keys.contains(key)) { error = "Ambiguous duplicate task identity"; return false; }
        keys << key;
    }
    return true;
}
QStringList SyncEngine::merge(const QStringList &surviving, const QStringList &observed)
{
    QStringList result = surviving;
    // Insert new identities beside their nearest observed predecessor. Existing
    // windows keep their relative order during creation/closure/title changes.
    for (qsizetype i = 0; i < observed.size(); ++i) {
        if (result.contains(observed[i])) continue;
        int at = 0;
        if (i > 0) at = result.indexOf(observed[i - 1]) + 1;
        result.insert(at, observed[i]);
    }
    return result;
}
void SyncEngine::fail(Peer &p, const QString &error)
{
    p.error = error;
    qWarning("Taskbar Sync: widget %u: %s", p.id, qPrintable(error));
}
void SyncEngine::settle()
{
    // A drag can last arbitrarily long. Its completion notification restarts
    // this single-shot; no timer runs while waiting for drop.
    for (const auto &p : std::as_const(m_peers))
        if (p.root && p.root->property("dragSource").value<QObject *>()) return;
    Peer *reference = nullptr;
    for (auto &p : m_peers) if (p.screen == 0 && p.model && p.error.isEmpty()) {
        if (!reference || p.id < reference->id) reference = &p;
    }
    if (!reference && m_seeded) {
        for (auto &p : m_peers) if (p.screen >= 0 && p.model && p.error.isEmpty() &&
            compatibility(p, nullptr).isEmpty() && (!reference || p.screen < reference->screen)) reference = &p;
    }
    if (!reference || !compatibility(*reference, nullptr).isEmpty()) { emit changed(); return; }
    QList<Peer *> valid;
    QHash<uint, QStringList> sequences;
    for (auto &p : m_peers) {
        if (p.screen < 0 || !p.error.isEmpty() || !compatibility(p, reference).isEmpty()) continue;
        QStringList keys; QString error;
        if (!read(p, keys, error)) {
            if (!error.startsWith("Waiting")) fail(p, error);
            continue;
        }
        valid << &p;
        sequences.insert(p.id, keys);
    }
    if (!valid.contains(reference)) { emit changed(); return; }
    if (!m_enabled) { emit changed(); return; }
    QScopedValueRollback<bool> applying(m_applying, true);
    if (!m_seeded) {
        m_order = sequences.value(reference->id);
        m_launchers = reference->model->property("launcherList").toStringList();
        m_seeded = true;
    } else if (auto source = peer(m_source); source && valid.contains(source)) {
        if (m_move) m_order = merge(sequences.value(source->id), m_order);
        if (m_pins || m_move) m_launchers = source->model->property("launcherList").toStringList();
    }
    m_source = 0; m_move = m_pins = false;
    QSet<QString> present;
    for (auto p : valid) for (const auto &key : sequences.value(p->id)) present.insert(key);
    m_order.removeIf([&](const QString &key) { return !present.contains(key); });
    for (auto p : valid) m_order = merge(m_order, sequences.value(p->id));
    // Reconcile the reference first so its syncLaunchers() defines persisted pin order.
    valid.removeOne(reference);
    valid.prepend(reference);
    for (auto p : valid) {
        if (!p->model) continue;
        if (p->model->property("launcherList").toStringList() != m_launchers) {
            if (!p->model->setProperty("launcherList", m_launchers)) {
                fail(*p, "Cannot update launcherList"); continue;
            }
        }
        QStringList current; QString error;
        if (!read(*p, current, error)) {
            if (!error.startsWith("Waiting")) fail(*p, error);
            continue;
        }
        // Setting pins may create/remove launcher rows synchronously.
        m_order = merge(m_order, current);
        QStringList desired;
        for (const auto &key : std::as_const(m_order)) if (current.contains(key)) desired << key;
        bool okay = true;
        for (int to = 0; to < desired.size(); ++to) {
            const int from = current.indexOf(desired[to]);
            if (from == to) continue;
            if (!invokeMove(p->model, from, to)) { okay = false; break; }
            current.clear();
            if (!read(*p, current, error)) { okay = false; break; }
        }
        if (!p->model) continue;
        if (!okay || current != desired) { fail(*p, "move() failed to converge; detached from synchronization"); continue; }
        if (!QMetaObject::invokeMethod(p->model, "syncLaunchers", Qt::DirectConnection)) {
            fail(*p, "syncLaunchers() is unavailable"); continue;
        }
        // The stock widget normally handles launcherListChanged. Write through
        // its KConfigPropertyMap too, so the normal configuration owns persistence.
        if (!p->model) continue;
        if (p->configuration) p->configuration->setProperty("launchers", p->model->property("launcherList"));
        if (p == reference) m_launchers = p->model->property("launcherList").toStringList();
    }
    emit changed();
}
QVariantList SyncEngine::diagnostics() const
{
    QVariantList result;
    const Peer *reference = nullptr;
    for (const auto &p : m_peers) if (p.screen == 0 && p.model && (!reference || p.id < reference->id)) reference = &p;
    if (!reference && m_seeded) {
        for (const auto &p : m_peers) if (p.screen >= 0 && p.model && p.error.isEmpty() &&
            compatibility(p, nullptr).isEmpty() && (!reference || p.screen < reference->screen)) reference = &p;
    }
    for (const auto &p : m_peers) {
        QString error = p.error;
        if (error.isEmpty()) error = compatibility(p, reference);
        if (error.isEmpty() && !reference) error = "Waiting for screen 0 Task Manager";
        if (error.isEmpty() && p.screen < 0) error = "Screen is disconnected";
        QStringList keys; QString readError;
        if (p.model && !read(p, keys, readError) && error.isEmpty()) error = readError;
        result << QVariantMap{{"id", p.id}, {"screen", p.screen}, {"error", error},
            {"sequence", keys}, {"launchers", p.model ? p.model->property("launcherList") : QVariant()},
            {"state", error.isEmpty() ? (m_enabled ? "Synchronizing" : "Observation only") : "Incompatible"}};
    }
    return result;
}
