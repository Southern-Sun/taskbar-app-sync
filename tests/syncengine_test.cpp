#include "syncengine.h"
#include <QtTest>
#include <QUuid>

class Root : public QObject {
    Q_OBJECT
    Q_PROPERTY(QObject *dragSource MEMBER drag NOTIFY dragSourceChanged)
public:
    QObject *drag = nullptr;
    void dragging(bool on) { drag = on ? this : nullptr; emit dragSourceChanged(); }
signals:
    void dragSourceChanged();
};
class Model : public QAbstractListModel {
    Q_OBJECT
    Q_PROPERTY(QStringList launcherList READ launchers WRITE setLaunchers NOTIFY launcherListChanged)
public:
    QStringList keys, pins;
    int moves = 0;
    bool rejectMoves = false;
    Model(QStringList initial = {}) : keys(initial) {
        for (auto name : {"filterByVirtualDesktop", "filterByCurrentVirtualDesktop", "filterByScreen",
             "filterByActivity", "filterByRegion", "filterMinimized", "filterNotMinimized",
             "filterNotMaximized", "filterHidden", "separateLaunchers", "launchInPlace", "hideActivatedLaunchers"})
            setProperty(name, false);
        setProperty("sortMode", 1); setProperty("groupMode", 0);
        setProperty("taskReorderingEnabled", true);
        setProperty("virtualDesktop", "desktop"); setProperty("activity", "activity");
    }
    enum { Window = Qt::UserRole, Ids, Url, Launcher, Startup };
    QHash<int, QByteArray> roleNames() const override {
        return {{Window,"IsWindow"},{Ids,"WinIdList"},{Url,"LauncherUrlWithoutIcon"},{Launcher,"IsLauncher"},{Startup,"IsStartup"}};
    }
    int rowCount(const QModelIndex &parent = {}) const override { return parent.isValid() ? 0 : keys.size(); }
    QVariant data(const QModelIndex &i, int role) const override {
        if (!i.isValid() || i.row() >= keys.size()) return {};
        auto key = keys[i.row()];
        switch (role) {
        case Window: return key.startsWith("w:");
        case Ids: return QVariantList{QUuid(key.mid(2))};
        case Url: return QUrl(key.mid(2));
        case Launcher: return key.startsWith("l:");
        case Startup: return key.startsWith("s:");
        default: return {};
        }
    }
    QStringList launchers() const { return pins; }
    void setLaunchers(const QStringList &urls) {
        if (pins == urls) return;
        pins = urls;
        for (int i = keys.size()-1; i >= 0; --i) if (keys[i].startsWith("l:") && !pins.contains(keys[i].mid(2))) {
            beginRemoveRows({}, i, i); keys.removeAt(i); endRemoveRows();
        }
        for (const auto &url : urls) if (!keys.contains("l:"+url)) {
            beginInsertRows({}, keys.size(), keys.size()); keys << "l:"+url; endInsertRows();
        }
        emit launcherListChanged();
    }
    Q_INVOKABLE bool move(int from, int to, const QModelIndex &parent = {}) {
        if (rejectMoves || from == to || from < 0 || to < 0 || from >= keys.size() || to >= keys.size()) return false;
        if (!beginMoveRows(parent, from, from, parent, to > from ? to+1 : to)) return false;
        keys.move(from,to); ++moves; endMoveRows(); return true;
    }
    Q_INVOKABLE void syncLaunchers() {
        QStringList result;
        for (const auto &key : keys) if (key.startsWith("l:")) result << key.mid(2);
        if (result != pins) { pins = result; emit launcherListChanged(); }
    }
    void add(const QString &key, int at) { beginInsertRows({},at,at); keys.insert(at,key); endInsertRows(); }
    void remove(const QString &key) { int at=keys.indexOf(key); if(at<0)return; beginRemoveRows({},at,at); keys.removeAt(at); endRemoveRows(); }
    void titleChanged() { emit dataChanged(index(0),index(keys.size()-1),{Qt::DisplayRole}); }
signals:
    void launcherListChanged();
};
static QString w(int number) { return "w:" + QUuid(0,0,0,0,0,0,0,0,0,0,number).toString(QUuid::WithoutBraces); }
class EngineTest : public QObject {
    Q_OBJECT
private slots:
    void fourTaskbarsAndDrag() {
        SyncEngine engine; Root roots[4];
        Model a({w(1),w(2),w(3)}), b({w(3),w(2),w(1)}), c(b.keys), d(b.keys);
        QList<Model*> models{&a,&b,&c,&d};
        for (int i=0;i<4;++i) engine.attach(i+1,i,&roots[i],models[i]);
        QTest::qWait(40); QCOMPARE(b.moves,0); // observation is read only
        engine.setEnabled(true);
        QTRY_COMPARE_WITH_TIMEOUT(b.keys,a.keys,250);
        for(int i=0;i<4;++i) {
            roots[i].dragging(true);
            auto before=models[(i+1)%4]->keys;
            QVERIFY(models[i]->move(0,2));
            QTest::qWait(40);
            QCOMPARE(models[(i+1)%4]->keys,before);
            roots[i].dragging(false);
            for(auto m:models) QTRY_COMPARE_WITH_TIMEOUT(m->keys,models[i]->keys,250);
        }
        int count=0; for(auto m:models)count+=m->moves;
        QTest::qWait(80);
        int after=0;for(auto m:models)after+=m->moves;
        QCOMPARE(after,count);
    }
    void pinsKeyboardAndRemoval() {
        SyncEngine engine; Root r1,r2; Model a({w(1),w(2)}),b(a.keys);
        engine.attach(1,0,&r1,&a); engine.attach(2,1,&r2,&b); engine.setEnabled(true);
        QTest::qWait(40);
        a.setLaunchers({"applications:org.kde.dolphin.desktop","applications:org.kde.konsole.desktop"});
        QTRY_COMPARE_WITH_TIMEOUT(b.keys,a.keys,250);
        QVERIFY(b.move(3,0)); b.syncLaunchers();
        QTRY_COMPARE_WITH_TIMEOUT(a.keys,b.keys,250);
        QCOMPARE(a.pins,b.pins);
        a.setLaunchers({"applications:org.kde.konsole.desktop"});
        QTRY_COMPARE_WITH_TIMEOUT(b.keys,a.keys,250);
        QCOMPARE(b.pins,a.pins);
        engine.detach(2); auto before=b.keys;
        QVERIFY(a.move(0,a.keys.size()-1)); QTest::qWait(40); QCOMPARE(b.keys,before);
    }
    void survivalAndNewPeer() {
        SyncEngine engine; Root r1,r2,r3; Model a({w(2),w(1),w(3)}),b(a.keys),c({w(3),w(2),w(1)});
        engine.attach(1,0,&r1,&a); engine.attach(2,1,&r2,&b);engine.setEnabled(true);QTest::qWait(40);
        a.add(w(4),1); b.add(w(4),3);
        QTRY_COMPARE_WITH_TIMEOUT(b.keys,a.keys,250);
        QCOMPARE(a.keys,QStringList({w(2),w(4),w(1),w(3)}));
        a.remove(w(1));b.remove(w(1));a.titleChanged();
        QTest::qWait(40); QCOMPARE(a.keys,QStringList({w(2),w(4),w(3)}));
        c.remove(w(1));c.add(w(4),0);engine.attach(3,2,&r3,&c);
        QTRY_COMPARE_WITH_TIMEOUT(c.keys,a.keys,250);
        engine.setScreen(2,-1);auto before=b.keys;
        QVERIFY(a.move(0,2));QTest::qWait(40);QCOMPARE(b.keys,before);
        engine.setScreen(2,1);QTRY_COMPARE_WITH_TIMEOUT(b.keys,a.keys,250);
    }
    void incompatibilityAndFailure() {
        SyncEngine engine; Root r1,r2; Model a({w(1),w(2)}),b({w(2),w(1)});
        b.setProperty("groupMode",1);
        engine.attach(1,0,&r1,&a);engine.attach(2,1,&r2,&b);engine.setEnabled(true);
        QTest::qWait(40);QCOMPARE(b.moves,0);
        QVERIFY(!engine.diagnostics()[1].toMap()["error"].toString().isEmpty());
        b.setProperty("groupMode",0); b.rejectMoves=true;
        a.titleChanged(); QTest::qWait(40);
        QVERIFY(engine.diagnostics()[1].toMap()["error"].toString().contains("converge"));
        b.rejectMoves=false;a.titleChanged();QTest::qWait(40);QCOMPARE(b.moves,0);
        engine.detach(2); engine.attach(2,1,&r2,&b);
        QTRY_COMPARE_WITH_TIMEOUT(b.keys,a.keys,250);
    }
    void primaryDisconnectAndTransientStartup() {
        SyncEngine engine; Root r1,r2,r3; Model a({w(1),w(2)}),b(a.keys),c(a.keys);
        engine.attach(1,0,&r1,&a);engine.attach(2,1,&r2,&b);engine.attach(3,2,&r3,&c);
        engine.setEnabled(true);QTest::qWait(40);
        engine.setScreen(1,-1);
        QVERIFY(b.move(0,1));QTRY_COMPARE_WITH_TIMEOUT(c.keys,b.keys,250);
        engine.setScreen(1,0);QTRY_COMPARE_WITH_TIMEOUT(a.keys,b.keys,250);
        c.add("s:applications:org.kde.konsole.desktop",0);QTest::qWait(40);
        QVERIFY(engine.diagnostics()[2].toMap()["error"].toString().startsWith("Waiting"));
        c.remove("s:applications:org.kde.konsole.desktop");
        QVERIFY(a.move(0,1));QTRY_COMPARE_WITH_TIMEOUT(c.keys,a.keys,250);
        QSignalSpy changed(&engine,&SyncEngine::changed);
        QTest::qWait(80);QCOMPARE(changed.size(),0);
    }
    void destructionDuringQueuedUpdate() {
        SyncEngine engine; Root r1,r2; Model a({w(1),w(2)});
        auto b = new Model({w(2),w(1)});
        engine.attach(1,0,&r1,&a); engine.attach(2,1,&r2,b); engine.setEnabled(true);
        delete b;QTest::qWait(40);QCOMPARE(engine.diagnostics().size(),1);
    }
};
QTEST_GUILESS_MAIN(EngineTest)
#include "syncengine_test.moc"
