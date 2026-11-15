#include "syncengine.h"
#include <QApplication>
#include <QtTest>
#include <Plasma/Corona>
#include <Plasma/PluginLoader>
#include <PlasmaQuick/AppletQuickItem>
#include <QJsonDocument>

class TestCorona : public Plasma::Corona {
public:
    QRect screenGeometry(uint) const override { return {0,0,1920,1080}; }
};
int main(int argc, char **argv)
{
    QApplication app(argc,argv);
    app.setApplicationName("taskbar-sync-integration");
    app.setQuitOnLastWindowClosed(false);
    TestCorona corona;
    auto panel=corona.createContainment("null");
    Plasma::PluginLoader loader;
    QList<QObject*> roots;
    QList<QAbstractItemModel*> models;
    for (int i=0;i<4;++i) {
        auto widget=loader.loadApplet("org.kde.plasma.taskmanager");
        auto stock=widget.get();
        panel->addApplet(std::move(widget));
        auto item=PlasmaQuick::AppletQuickItem::itemForApplet(stock);
        if (!item) return 2;
        roots << item;
        auto model=qobject_cast<QAbstractItemModel*>(item->property("tasksModel").value<QObject*>());
        if (!model) return 3;
        models << model;
        model->setProperty("sortMode",1); model->setProperty("groupMode",0);
        model->setProperty("filterByScreen",false);model->setProperty("filterByActivity",false);
        model->setProperty("filterByVirtualDesktop",false); model->setProperty("filterByCurrentVirtualDesktop",false);
    }
    QTest::qWait(1000);
    SyncEngine engine;
    for(int i=0;i<4;++i) engine.attach(i+1,i,roots[i],models[i]);
    QTest::qWait(50);
    auto report=engine.diagnostics();
    fprintf(stderr,"%s\n",QJsonDocument::fromVariant(report).toJson().constData());
    for (auto value:report) if(!value.toMap()["error"].toString().isEmpty()) return 4;
    engine.setEnabled(true);
    auto sequences=[&] {
        QStringList first=engine.diagnostics().first().toMap()["sequence"].toStringList();
        for(auto value:engine.diagnostics()) {
            auto map=value.toMap();
            if(!map["error"].toString().isEmpty() || map["sequence"].toStringList()!=first) return false;
        }
        return true;
    };
    QTest::qWait(50);
    if (!sequences()) return 5;
    // All four actual stock QML models can originate a movement.
    for(auto model:models) {
        const auto keys=engine.diagnostics().first().toMap()["sequence"].toStringList();
        int from=-1;
        for(int row=0;row<keys.size();++row) if(keys[row].startsWith("w:")){from=row;break;}
        if(from<0) from=0;
        if(model->rowCount()<2) return 6;
        bool moved=false;
        if(!QMetaObject::invokeMethod(model,"move",Q_RETURN_ARG(bool,moved),Q_ARG(int,from),
                Q_ARG(int,model->rowCount()-1),Q_ARG(QModelIndex,QModelIndex())) || !moved) return 7;
        QTest::qWait(60);
        if(!sequences()) return 8;
    }
    models[2]->setProperty("launcherList",QStringList{"applications:org.kde.dolphin.desktop","applications:org.kde.konsole.desktop"});
    QTest::qWait(80);
    if(!sequences()) return 9;
    for(auto model:models) if(model->property("launcherList")!=models[2]->property("launcherList")) return 10;
    models[1]->setProperty("launcherList",QStringList{});
    QTest::qWait(80);
    if(!sequences()) return 11;
    for(auto model:models) if(!model->property("launcherList").toStringList().isEmpty()) return 12;
    fprintf(stderr,"PASS: four stock QML models: identities, moves from every source, pin and unpin\n");
    return 0;
}
