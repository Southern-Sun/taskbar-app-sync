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
int main(int argc,char **argv)
{
    QApplication app(argc,argv);
    app.setApplicationName("taskbar-sync-companion-integration");
    app.setQuitOnLastWindowClosed(false);
    TestCorona corona;
    auto panel=corona.createContainment("null");
    Plasma::PluginLoader loader;
    for(int i=0;i<4;++i) {
        auto stock=loader.loadApplet("org.kde.plasma.taskmanager");
        auto a=stock.get();panel->addApplet(std::move(stock));
        if(!PlasmaQuick::AppletQuickItem::itemForApplet(a)) return 2;
    }
    auto companion=loader.loadApplet("org.local.taskbar.sync");
    auto a=companion.get();panel->addApplet(std::move(companion));
    auto item=PlasmaQuick::AppletQuickItem::itemForApplet(a);
    if(!item) { fprintf(stderr,"Companion QML failed to load\n"); return 3; }
    QTest::qWait(1000);
    if(!a->property("taskbars").isValid()) return 4;
    auto list=a->property("taskbars").toList();
    fprintf(stderr,"%s\n",qPrintable(a->property("report").toString()));
    if(list.size()!=4) return 5;
    if(a->property("syncing").toBool()) return 6;
    auto duplicate=loader.loadApplet("org.local.taskbar.sync");
    auto other=duplicate.get();panel->addApplet(std::move(duplicate));
    PlasmaQuick::AppletQuickItem::itemForApplet(other);
    QTest::qWait(50);
    if(!other->property("discoveryError").toString().contains("Another companion")) return 7;
    fprintf(stderr,"PASS: plugin loads, QML loads, discovers four existing roots, observation mode, duplicate rejected\n");
    return 0;
}
