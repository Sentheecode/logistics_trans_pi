#include "catalog.hpp"
#include "schematic.hpp"
#include "workbench.hpp"
#include <sqlite3.h>
#include <QApplication>
#include <QCheckBox>
#include <QComboBox>
#include <QDialog>
#include <QMenu>
#include <QLineEdit>
#include <QListWidget>
#include <QDir>
#include <QElapsedTimer>
#include <QFile>
#include <QFont>
#include <QFontMetrics>
#include <QLabel>
#include <QMessageBox>
#include <QMouseEvent>
#include <QPushButton>
#include <QScrollArea>
#include <QScrollBar>
#include <QSplitter>
#include <QTabWidget>
#include <QTabBar>
#include <QTableWidget>
#include <QThread>
#include <QTimer>
#include <functional>
#include <iostream>
#include <stdexcept>
#include <string>

void wait(const std::function<bool()>& condition, int timeout=6000) {
    QElapsedTimer clock; clock.start();
    while (clock.elapsed()<timeout) { QCoreApplication::processEvents(); if (condition()) return; QThread::msleep(10); }
    throw std::runtime_error("widget condition timed out");
}
void pump(int ms) { QElapsedTimer timer; timer.start(); wait([&]{ return timer.elapsed()>ms; }, ms+1000); }

QString cell(QTableWidget* table, const QString& key) {
    if (!table) throw std::runtime_error("missing detail table");
    for (int row=0; row<table->rowCount(); ++row) {
        auto* name=table->item(row, 0);
        auto* value=table->item(row, 1);
        if (name && value && name->text()==key) return value->text();
    }
    throw std::runtime_error(("missing row "+key).toStdString());
}
void choose(QComboBox* box, const char* id) {
    const QString wanted=QString::fromUtf8(id);
    const int index=box->findData(wanted);
    if (index<0) throw std::runtime_error(std::string("missing combo id ")+id);
    box->setCurrentIndex(index);
}
struct Snap { std::string mode, boot; long long revision=-1, seq=-1; };
Snap snap(const std::filesystem::path& directory) {
    auto view=sim::read(directory, "status");
    return {view.runtime.mode, view.runtime.boot, view.runtime.revision, view.rows.empty()?-1:view.rows.front().seq};
}
void check_labels(QWidget& window) {
    for (auto* label : window.findChildren<QLabel*>()) {
        if (!label->isVisible() || label->wordWrap()) continue;
        if (label->fontMetrics().horizontalAdvance(label->text())>label->contentsRect().width()+2)
            throw std::runtime_error(("clipped label: "+label->text()).toStdString());
        if (!label->fontMetrics().inFont(QChar(0x6a21))) throw std::runtime_error("missing Chinese glyph");
    }
}
void open_diagnostic(QWidget& window, const QString& name) {
    auto* nav=window.findChild<QTabBar*>("workbenchNavigation");
    auto* tabs=window.findChild<QTabWidget*>("moduleTabs");
    if (!nav || !tabs) throw std::runtime_error("missing navigation");
    for (int index=0;index<nav->count();++index) {
        if (nav->tabText(index)!=name) continue;
        nav->setCurrentIndex(index);
        if (tabs->tabText(tabs->currentIndex())!=name) throw std::runtime_error("wrong page");
        pump(40);
        return;
    }
    throw std::runtime_error("missing diagnostic tab");
}
void assert_point_detail_reachable(QWidget& window) {
    auto* tabs=window.findChild<QTabWidget*>("moduleTabs");
    auto* points=window.findChild<QTableWidget*>("pointAuditTable");
    auto* scroll=window.findChild<QScrollArea*>("pointDetailScroll");
    auto* label=window.findChild<QLabel*>("pointDetail");
    if (!tabs || !points || !scroll || !label || points->rowCount()<1) throw std::runtime_error("missing point detail scroll");
    open_diagnostic(window,"点表");
    points->selectRow(0);
    pump(100);
    if (!label->text().contains("审计码")) throw std::runtime_error("point detail has no audit line");
    const QPoint origin=scroll->mapTo(&window, QPoint(0, 0));
    if (!scroll->isVisible() || origin.y()<0 || origin.y()+scroll->height()>window.height()+2)
        throw std::runtime_error("point detail scroll is outside the window");
    const int need=label->heightForWidth(std::max(1, scroll->viewport()->width()));
    if (label->height()+2<need) throw std::runtime_error("point detail label is shorter than its wrapped text");
    const int hidden=label->height()-scroll->viewport()->height();
    if (hidden>4 && scroll->verticalScrollBar()->maximum()+4<hidden)
        throw std::runtime_error("point detail scroll cannot reach the last line");
    open_diagnostic(window,"运行");
}
void assert_inside(QWidget& window, const char* name) {
    auto* widget=window.findChild<QWidget*>(name);
    if (!widget || !widget->isVisible()) throw std::runtime_error(std::string("missing visible control ")+name);
    const QPoint point=widget->mapTo(&window, QPoint(0, 0));
    if (point.x()<0 || point.y()<0 || point.x()+widget->width()>window.width()+2 || point.y()+widget->height()>window.height()+2)
        throw std::runtime_error(std::string("control outside window: ")+name);
}
void assert_no_live_leak(Workbench& window, bool boundPlc=false) {
    const auto table=[&](const char* id) { return window.findChild<QTableWidget*>(id); };
    const auto label=[&](const char* id) { return window.findChild<QLabel*>(id)->text(); };
    if (table("pointAuditTable")->rowCount() || table("historyTable")->rowCount()
        || table("logTable")->rowCount() || table("collectChanges")->rowCount()) {
        throw std::runtime_error("diagnostics borrowed another station's rows");
    }
    if (!label("pointScope").contains("不套用 140") || !label("collectUnbound").contains("PLC 在线状态未探测")
        || label("collectLink")!="未绑定" || label("collectMode")!="不作用于本工位")
        throw std::runtime_error("point or collection scope language is missing");
    for (const auto& page : {"采集","日志","点表"}) {
        open_diagnostic(window,QString::fromUtf8(page));
        for (auto* child : window.findChildren<QLabel*>()) {
            if (!child->isVisible()) continue;
            const auto value=child->text();
            for (const auto& fragment : {"扫码中","分拣中","本轮完成","三车模拟流","DB1900"}) {
                if (value.contains(QString::fromUtf8(fragment)))
                    throw std::runtime_error("unbound diagnostic leaked a bound frame");
            }
        }
    }
    open_diagnostic(window,"运行");
    if (!boundPlc && (table("stageTable")->isVisible() || table("stageTable")->rowCount()
        || !label("simOrder").contains("未接入")))
        throw std::runtime_error("run page borrowed another PLC's frame");
}
void check_bad_svg() {
    SchematicView blank;
    if (blank.loaded() || !blank.stationAt(QPointF(1, 1)).isEmpty()) throw std::runtime_error("empty layout reported a station");
}
void click_station(SchematicView* schematic, const char* id) {
    const auto rect=schematic->stationRect(QString::fromUtf8(id));
    if (rect.width()<5 || rect.height()<5) throw std::runtime_error("schematic layout empty");
    const auto center=rect.center();
    if (schematic->stationAt(center)!=QString::fromUtf8(id)) throw std::runtime_error("svg hit target does not match id");
    QMouseEvent press(QEvent::MouseButtonPress, center, QPointF(schematic->mapToGlobal(center.toPoint())), Qt::LeftButton, Qt::LeftButton, Qt::NoModifier);
    QCoreApplication::sendEvent(schematic, &press);
}


// This fixture owns an isolated SQLite directory. It can publish heartbeats without
// advancing a frame, letting the screen observe a restart before the next manual step.
void check_station_evidence(Workbench& window, const std::filesystem::path& directory) {
    auto* quality=window.findChild<QLabel*>("collectQuality");
    auto* link=window.findChild<QLabel*>("collectLink");
    auto* mode=window.findChild<QLabel*>("collectMode");
    auto* frame=window.findChild<QLabel*>("collectFrame");
    auto* result=window.findChild<QTableWidget*>("runResultTable");
    auto* step=window.findChild<QPushButton*>("primary");
    if (!quality || !link || !mode || !frame || !result || !step) throw std::runtime_error("missing evidence controls");
    const auto sample=sim::SimulationAdapter().next();
    {
        sim::Writer writer(directory);
        writer.publish(&sample,"manual");
        wait([&]{return step->isEnabled() && quality->text()=="正常";});
        open_diagnostic(window,"采集");
        const auto held=frame->text();
        QElapsedTimer clock; clock.start();
        while (clock.elapsed()<3300) { writer.publish(nullptr,"manual"); pump(150); }
        if (!step->isEnabled() || quality->text()!="正常" || mode->text()!="手动" || frame->text()!=held)
            throw std::runtime_error("manual idle lost the held same-session step");
        open_diagnostic(window,"运行");
    }
    {
        sim::Writer restarted(directory);
        wait([&]{return quality->text()=="未知" && mode->text()=="手动" && step->isEnabled();});
        if (link->text()!="未知" || frame->text()=="无" || cell(result,"采集质量")!="无可信当前值")
            throw std::runtime_error("previous-session evidence presented as current");
        restarted.publish(&sample,"manual");
        wait([&]{return quality->text()=="正常" && cell(result,"采集质量")=="正常";});
    }
    // A live UI reader must invalidate its current projection on its next failed
    // read, even if its last successful snapshot held a healthy heartbeat.
    sqlite3* fixture=nullptr;
    const auto file=(directory/"simulation.sqlite").u8string();
    const int opened=sqlite3_open_v2(file.c_str(),&fixture,SQLITE_OPEN_READWRITE,nullptr);
    if (opened==SQLITE_OK) sqlite3_busy_timeout(fixture,2000);
    const int changed=opened==SQLITE_OK ? sqlite3_exec(fixture,"PRAGMA user_version=99",nullptr,nullptr,nullptr) : opened;
    sqlite3_close(fixture);
    if (changed!=SQLITE_OK) throw std::runtime_error("unable to create incompatible fixture");
    auto* feedback=window.findChild<QLabel*>("controlFeedback");
    wait([&]{return feedback && feedback->text().contains("无法读取") && !step->isEnabled();});
    if (quality->text()!="未知" || link->text()!="未知" || mode->text()!="未知" || frame->text()=="无"
        || cell(result,"采集质量")!="无可信当前值" || window.confirmedMode()!="unknown")
        throw std::runtime_error("last successful read leaked into current evidence");
}

int main(int argc, char** argv) {
    QApplication app(argc, argv);
    if (argc!=3) return 2;
    try {
        check_bad_svg();
        const auto directory=std::filesystem::u8path(argv[1]);
        Workbench window(directory);
        window.resize(1100, 650);
        window.show();
        auto* mode=window.findChild<QPushButton*>("modeButton");
        auto* step=window.findChild<QPushButton*>("primary");
        if (!mode || !step) throw std::runtime_error("missing buttons");
        if (std::string(argv[2])=="evidence") {
            check_station_evidence(window, directory);
            std::cout<<"PASS session, held step and failed read evidence\n";
            return 0;
        }
        auto* area=window.findChild<QComboBox*>("areaCombo");
        auto* plc=window.findChild<QComboBox*>("plcCombo");
        auto* schematic=window.findChild<SchematicView*>("schematicView");
        auto* source=window.findChild<QTableWidget*>("stationSourceTable");
        auto* points=window.findChild<QTableWidget*>("pointAuditTable");
        auto* disconnect=window.findChild<QPushButton*>("disconnectButton");
        auto* nav=window.findChild<QTabBar*>("workbenchNavigation");
        auto* context=window.findChild<QWidget*>("catalogPane");
        if (!area || !plc || !schematic || !source || !points || !disconnect || !nav || !context)
            throw std::runtime_error("missing workbench controls");
        if (window.findChild<QComboBox*>("stationCombo") || window.findChild<QComboBox*>("lineCombo")
            || window.findChild<QPushButton*>("diagnosticButton") || nav->y()>=context->y())
            throw std::runtime_error("scope shortcuts or legacy diagnostics still visible");
        if (std::string(argv[2])=="offline") {
            pump(700);
            if (mode->isEnabled() || step->isEnabled() || window.confirmedMode()!="unknown")
                throw std::runtime_error("offline collector exposed controls");
            choose(plc,catalog::kPlcMeter1);
            assert_no_live_leak(window);
            return 0;
        }

        wait([&]{return mode->isEnabled() && window.confirmedMode()=="automatic";});
        for (auto size : {QSize(1100,650),QSize(1280,800)}) {
            window.resize(size);
            pump(120);
            for (const auto name : {"workbenchNavigation","plcCombo","schematicView","stageTable","stationSourceTable","scopeBanner","commandPane"})
                assert_inside(window,name);
            check_labels(window);
            if (disconnect->isVisible()) throw std::runtime_error("disconnect leaked onto run page");
        }

        const auto before=snap(directory);
        const auto ids=schematic->shapeIds();
        if (ids!=QStringList{catalog::kStation140,catalog::kStation150,catalog::kStation160} || source->rowCount()!=3)
            throw std::runtime_error("exterior roster sequence");
        const auto left=schematic->stationRect(ids[0]);
        const auto middle=schematic->stationRect(ids[1]);
        const auto right=schematic->stationRect(ids[2]);
        if (!(left.right()<middle.left() && middle.right()==right.left())) throw std::runtime_error("exterior anchors");
        for (auto id : {catalog::kStation150,catalog::kStation160}) {
            click_station(schematic,id);
            if (plc->currentData().toString()!=catalog::kPlcPack || !window.liveScope())
                throw std::runtime_error("detail changed PLC context");
            auto* table=window.findChild<QTableWidget*>("detailTable");
            const auto name=cell(table,"图纸标注");
            if ((id==catalog::kStation150 && (!name.contains("前保右") || !name.contains("后保右")))
                || (id==catalog::kStation160 && !name.contains("通风窗")))
                throw std::runtime_error("missing exterior drawing label");
            assert_no_live_leak(window,true);
        }
        choose(plc,catalog::kPlcMeter1);
        if (schematic->shapeIds()!=QStringList{catalog::kStation20,catalog::kStation30,catalog::kStation90})
            throw std::runtime_error("meter 1 placement");
        for (auto id : {catalog::kStation20,catalog::kStation30,catalog::kStation90}) {
            click_station(schematic,id);
            if (!window.findChild<QLabel*>("pointScope")->text().contains("缺 PLC-PC"))
                throw std::runtime_error("meter 1 point evidence missing");
            assert_no_live_leak(window);
        }
        choose(plc,catalog::kPlcMeter2);
        if (schematic->shapeIds()!=QStringList{catalog::kStation40,catalog::kStation10,catalog::kStation70,catalog::kStation80})
            throw std::runtime_error("meter 2 placement");
        assert_no_live_leak(window);
        choose(area,catalog::kAreaSeq);
        const QStringList sortingIds={catalog::kPlcSeq1,catalog::kPlcSeq2,catalog::kPlcSeq3,catalog::kPlcSeq4};
        for (int index=0;index<sortingIds.size();++index) {
            choose(plc,sortingIds[index].toUtf8().constData());
            const QString prefix=QString("stn.%1").arg(170+20*index);
            const QString next=QString("stn.%1").arg(180+20*index);
            if (schematic->shapeIds()!=QStringList{prefix,next}) throw std::runtime_error("sorting placement");
            assert_no_live_leak(window);
        }
        const auto after=snap(directory);
        if (after.boot!=before.boot || after.revision!=before.revision || after.mode!="automatic")
            throw std::runtime_error("directory browsing mutated collector");

        choose(area,catalog::kAreaPick);
        choose(plc,catalog::kPlcPack);
        auto* reserved=window.findChild<QCheckBox*>("pointReserved");
        auto visiblePoints=[&] {
            int count=0;
            for (int row=0;row<points->rowCount();++row) if (!points->isRowHidden(row)) ++count;
            return count;
        };
        open_diagnostic(window,"点表");
        if (!reserved || visiblePoints()!=75) throw std::runtime_error("named point count mismatch");
        reserved->setChecked(true);
        if (visiblePoints()!=116) throw std::runtime_error("original rows lost");
        reserved->setChecked(false);
        auto* search=window.findChild<QLineEdit*>("pointSearch");
        search->setText("DB1900.DBX0.0");
        if (visiblePoints()!=1) throw std::runtime_error("point search crossed the named-row filter");
        search->clear();
        assert_point_detail_reachable(window);

        wait([&]{return mode->isEnabled();});
        mode->click();
        wait([&]{return step->isEnabled() && window.confirmedMode()=="manual";});
        const auto held=snap(directory);
        pump(850);
        if (snap(directory).seq!=held.seq) throw std::runtime_error("manual continued automatically");
        step->click();
        step->click();
        wait([&]{return step->isEnabled() && snap(directory).seq==held.seq+1;});
        open_diagnostic(window,"采集");
        if (!disconnect->isVisible() || !disconnect->isEnabled()) throw std::runtime_error("missing acquisition actions");
        disconnect->click();
        wait([&]{return window.findChild<QLabel*>("collectLink")->text()=="模拟断开";});
        auto* logs=window.findChild<QTableWidget*>("logTable");
        open_diagnostic(window,"日志");
        wait([&]{return logs->rowCount()>0;});
        if (logs->horizontalHeaderItem(4)->text()!="关联帧")
            throw std::runtime_error("log page still presents message bodies");
        logs->selectRow(0);
        const auto* logDetail=window.findChild<QLabel*>("logDetail");
        if (!logDetail->text().contains("本机 diagnose"))
            throw std::runtime_error("log detail does not identify the local CLI");


        open_diagnostic(window,"运行");
        const auto confirm=[&](bool accept) {
            QTimer::singleShot(100,&window,[&]{
                if (auto* dialog=window.findChild<QMessageBox*>("confirmAutomatic"))
                    dialog->button(accept ? QMessageBox::Yes : QMessageBox::No)->click();
            });
            mode->click();
        };
        wait([&]{return mode->isEnabled();});
        confirm(false);
        if (window.confirmedMode()!="manual") throw std::runtime_error("cancel changed mode");
        confirm(true);
        wait([&]{return window.confirmedMode()=="automatic";});
        std::cout<<"PASS PLC scope, static placement, point audit, automatic isolation and manual controls\n";
        return 0;
    } catch(const std::exception& error) {
        std::cerr<<error.what()<<'\n';
        return 1;
    }
}
