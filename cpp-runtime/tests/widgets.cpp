#include "catalog.hpp"
#include "schematic.hpp"
#include "workbench.hpp"
#include <QApplication>
#include <QComboBox>
#include <QDir>
#include <QElapsedTimer>
#include <QFile>
#include <QFont>
#include <QFontMetrics>
#include <QLabel>
#include <QMessageBox>
#include <QMouseEvent>
#include <QPushButton>
#include <QSplitter>
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
void assert_inside(QWidget& window, const char* name) {
    auto* widget=window.findChild<QWidget*>(name);
    if (!widget || !widget->isVisible()) throw std::runtime_error(std::string("missing visible control ")+name);
    const QPoint point=widget->mapTo(&window, QPoint(0, 0));
    if (point.x()<0 || point.y()<0 || point.x()+widget->width()>window.width()+2 || point.y()+widget->height()>window.height()+2)
        throw std::runtime_error(std::string("control outside window: ")+name);
}
void assert_no_live_leak(Workbench& window) {
    auto* history=window.findChild<QTableWidget*>("historyTable");
    auto* stage=window.findChild<QTableWidget*>("stageTable");
    auto* detail=window.findChild<QTableWidget*>("detailTable");
    auto* peers=window.findChild<QTableWidget*>("peerTable");
    auto* banner=window.findChild<QLabel*>("scopeBanner");
    if (!history || history->rowCount()!=0) throw std::runtime_error("non-live station showed history");
    if (!stage || stage->isVisible() || stage->rowCount()!=0) throw std::runtime_error("non-live station showed stages");
    const auto scope=cell(detail, "实时范围");
    if (scope!="未接入" && scope!="未配置") throw std::runtime_error(("unexpected scope "+scope).toStdString());
    if (cell(detail, "模拟质量")=="正常") throw std::runtime_error("non-live station reused simulation quality");
    if (cell(detail, "设备连接")!="未探测" || cell(detail, "库存")!="未知") throw std::runtime_error("fabricated device or stock value");
    const QStringList banned={"扫码中", "分拣中", "工位完成", "模拟阻塞"};
    for (auto* table : {history, stage, detail, peers}) {
        if (!table) continue;
        for (int row=0; row<table->rowCount(); ++row) for (int column=0; column<table->columnCount(); ++column) {
            auto* item=table->item(row, column);
            if (!item) continue;
            for (const auto& word : banned) if (item->text().contains(word)) throw std::runtime_error(("leaked live text "+word).toStdString());
        }
    }
    if (banner) for (const auto& word : banned) if (banner->text().contains(word)) throw std::runtime_error("leaked live banner");
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

int main(int argc, char** argv) {
    QApplication app(argc, argv);
    QFont font; font.setFamilies({"Noto Sans CJK SC", "Microsoft YaHei UI", "Microsoft YaHei", "sans-serif"}); font.setPixelSize(13); app.setFont(font);
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
        if (std::string(argv[2])=="offline") {
            pump(700);
            auto* factory=window.findChild<QComboBox*>("factoryCombo");
            auto* schematic=window.findChild<SchematicView*>("schematicView");
            if (!factory || factory->count()!=1 || !factory->isEnabled()) throw std::runtime_error("offline catalog disabled");
            if (!schematic || !schematic->loaded()) throw std::runtime_error("offline schematic missing");
            if (mode->isEnabled() || step->isEnabled()) throw std::runtime_error("offline controls enabled");
            std::cout<<"PASS offline controls disabled\n";
            return 0;
        }
        auto* factory=window.findChild<QComboBox*>("factoryCombo");
        auto* area=window.findChild<QComboBox*>("areaCombo");
        auto* plc=window.findChild<QComboBox*>("plcCombo");
        auto* detail=window.findChild<QTableWidget*>("detailTable");
        auto* history=window.findChild<QTableWidget*>("historyTable");
        auto* points=window.findChild<QTableWidget*>("pointAuditTable");
        auto* schematic=window.findChild<SchematicView*>("schematicView");
        if (!factory || !area || !plc || !detail || !history || !points || !schematic) throw std::runtime_error("missing native chrome");
        if (window.findChild<QComboBox*>("stationCombo")) throw std::runtime_error("station dropdown is not a navigation level");
        if (!window.findChild<QWidget*>("catalogPane") || !window.findChild<QWidget*>("commandPane") || !window.findChild<QSplitter*>("mainSplit"))
            throw std::runtime_error("missing navigation pane or splitter");
        if (QString(schematic->metaObject()->className())!="SchematicView" || !schematic->loaded())
            throw std::runtime_error("schematic resource did not load");
        const auto shown=schematic->shapeIds();
        if (shown.size()!=3 || !shown.contains(catalog::kStation140) || !shown.contains(catalog::kStation150) || !shown.contains(catalog::kStation160) || shown.contains(catalog::kStation210) || shown.contains(catalog::kStation20))
            throw std::runtime_error("layout showed stations outside the current PLC");
        wait([&]{ return mode->isEnabled(); });
        wait([&]{ return history->rowCount()>0; });
        if (window.confirmedMode()!="automatic" || step->isEnabled() || !window.liveScope()) throw std::runtime_error("auto mode controls incorrect");
        if (factory->count()!=1 || area->count()!=2 || plc->count()!=3) throw std::runtime_error("catalog dropdowns incomplete");
        for (int i=0; i<area->count(); ++i) if (area->itemText(i).contains("手动") || area->itemText(i).contains("自动")) throw std::runtime_error("runtime mode mixed into area dropdown");
        if (cell(detail, "设备连接")!="未探测" || cell(detail, "库存")!="未知" || cell(detail, "库位")!="待补映射") throw std::runtime_error("live station fabricated mapping");
        if (cell(detail, "业务模式")==cell(detail, "运行模式") || !cell(detail, "实时范围").contains(QString::fromUtf8(catalog::kStream))) throw std::runtime_error("area and runtime modes not independent");
        if (points->rowCount()<10) throw std::runtime_error("station 140 audit missing");
        if (window.controllerPeerIds().size()!=3 || schematic->markerColor(QString::fromUtf8(catalog::kStation140))==schematic->markerColor(QString::fromUtf8(catalog::kStation150)) || !schematic->markerColor(QString::fromUtf8(catalog::kStation150)).isValid() || schematic->markerColor(QString::fromUtf8(catalog::kStation210)).isValid())
            throw std::runtime_error("plc highlight incorrect");
        mode->click();
        wait([&]{ return window.confirmedMode()=="manual" && step->isEnabled(); });
        auto before=snap(directory);
        step->click(); step->click(); step->click();
        wait([&]{ return step->isEnabled(); });
        auto after=snap(directory);
        if (after.seq!=before.seq+1 || after.revision!=before.revision+1) throw std::runtime_error("rapid clicks advanced more than once");
        pump(1700);
        if (!step->isEnabled() || snap(directory).seq!=after.seq) throw std::runtime_error("manual wait treated as disconnected or advanced");
        auto* link=window.findChild<QPushButton*>("disconnectButton");
        auto* banner=window.findChild<QLabel*>("scopeBanner");
        auto* clock=window.findChild<QLabel*>("heartbeatState");
        if (!link || !link->isEnabled() || !banner || !clock) throw std::runtime_error("missing disconnect or clock");
        if (!clock->text().contains("北京") && !clock->text().contains("UTC")) throw std::runtime_error("clock has no timezone");
        const auto before_break=snap(directory);
        link->click();
        wait([&]{ return link->isEnabled() && snap(directory).seq==before_break.seq+1; });
        auto broken=snap(directory);
        if (broken.mode!="manual" || broken.revision!=before_break.revision+1 || broken.boot!=before_break.boot || !banner->text().contains("模拟链路断开"))
            throw std::runtime_error("manual disconnect did not stay on the same collector");
        for (int guard=0; guard<4 && banner->text().contains("模拟链路断开"); ++guard) {
            const auto current=snap(directory);
            step->click();
            wait([&]{ return step->isEnabled() && snap(directory).seq==current.seq+1; });
        }
        if (banner->text().contains("模拟链路断开") || snap(directory).mode!="manual" || snap(directory).boot!=before_break.boot)
            throw std::runtime_error("step after disconnect did not resume the same manual collector");
        const auto held=snap(directory);
        choose(area, catalog::kAreaSeq);
        choose(plc, catalog::kPlcSeq1);
        click_station(schematic, catalog::kStation220);
        if (window.selectedStationId()!=catalog::kStation220 || area->currentData().toString()!=catalog::kAreaSeq || plc->currentData().toString()!=catalog::kPlcSeq1)
            throw std::runtime_error("svg click did not sync dropdowns");
        if (schematic->shapeIds().contains(catalog::kStation140) || schematic->shapeIds().size()!=2)
            throw std::runtime_error("sequence layout still showed another PLC");
        if (window.confirmedMode()!="manual" || step->isEnabled() || mode->isEnabled()) throw std::runtime_error("non-live station enabled control");
        if (cell(detail, "业务模式")!="排序区" || cell(detail, "运行模式")!="手动") throw std::runtime_error("sequence selection changed runtime mode");
        if (points->rowCount()!=0) throw std::runtime_error("140 point sheet applied outside station 140");
        assert_no_live_leak(window);
        step->click(); mode->click();
        QPointF miss;
        bool found_miss=false;
        for (int y=0; y<schematic->height() && !found_miss; y+=9) for (int x=0; x<schematic->width() && !found_miss; x+=9)
            if (schematic->stationAt(QPointF(x, y)).isEmpty()) { miss=QPointF(x, y); found_miss=true; }
        if (!found_miss) throw std::runtime_error("no schematic miss point");
        const auto selected=window.selectedStationId();
        QMouseEvent miss_press(QEvent::MouseButtonPress, miss, QPointF(schematic->mapToGlobal(miss.toPoint())), Qt::LeftButton, Qt::LeftButton, Qt::NoModifier);
        QCoreApplication::sendEvent(schematic, &miss_press);
        if (window.selectedStationId()!=selected) throw std::runtime_error("miss click changed selection");
        choose(area, catalog::kAreaPick);
        choose(plc, catalog::kPlcPack);
        click_station(schematic, catalog::kStation150);
        if (schematic->shapeIds().contains(catalog::kStation210) || schematic->shapeIds().size()!=3)
            throw std::runtime_error("pack layout showed another PLC");
        if (schematic->selectedId()!=catalog::kStation150 || schematic->markerColor(QString::fromUtf8(catalog::kStation150))==schematic->markerColor(QString::fromUtf8(catalog::kStation140)) || !schematic->markerColor(QString::fromUtf8(catalog::kStation160)).isValid())
            throw std::runtime_error("diagram click did not focus the station");
        const auto peers=cell(detail, "关联工位");
        if (!peers.contains(catalog::kStation140) || !peers.contains(catalog::kStation150) || !peers.contains(catalog::kStation160) || peers.contains(catalog::kStation210))
            throw std::runtime_error("plc peer list incorrect");
        if (step->isEnabled() || history->rowCount()!=0) throw std::runtime_error("sibling station repeated the live frame");
        assert_no_live_leak(window);
        choose(area, catalog::kAreaSeq);
        choose(plc, catalog::kPlcSeq2);
        if (!window.selectedStationId().isEmpty() || !schematic->shapeIds().isEmpty() || cell(detail, "实时范围")!="未配置" || step->isEnabled())
            throw std::runtime_error("empty plc was not unconfigured");
        for (int i=0; i<20; ++i) choose(area, i%2 ? catalog::kAreaPick : catalog::kAreaSeq);
        if (area->currentData().toString()!=catalog::kAreaPick || window.selectedStationId()!=catalog::kStation140)
            throw std::runtime_error("parent switch did not select a legal child by id");
        const auto browsed=snap(directory);
        if (browsed.mode!=held.mode || browsed.revision!=held.revision || browsed.seq!=held.seq || browsed.boot!=held.boot)
            throw std::runtime_error("directory browsing changed collector state");
        if (!step->isEnabled() || !window.liveScope()) throw std::runtime_error("live control did not return");
        const auto before_step=snap(directory);
        step->click();
        wait([&]{ return step->isEnabled() && snap(directory).seq==before_step.seq+1; });
        if (snap(directory).revision!=before_step.revision+1) throw std::runtime_error("returned live step did not commit once");
        QTimer::singleShot(50, [&]{ auto* box=window.findChild<QMessageBox*>("confirmAutomatic"); if (box) box->button(QMessageBox::No)->click(); });
        mode->click();
        pump(300);
        if (window.confirmedMode()!="manual") throw std::runtime_error("cancel resumed automatic");
        QTimer::singleShot(50, [&]{ auto* box=window.findChild<QMessageBox*>("confirmAutomatic"); if (box) box->button(QMessageBox::Yes)->click(); });
        mode->click();
        wait([&]{ return window.confirmedMode()=="automatic" && mode->isEnabled(); });
        if (step->isEnabled()) throw std::runtime_error("step enabled in automatic");
        const auto running=snap(directory);
        choose(area, catalog::kAreaSeq);
        wait([&]{
            if (history->rowCount()!=0) throw std::runtime_error("sequence view showed live history");
            const auto now=snap(directory);
            if (now.mode!="automatic" || now.revision!=running.revision || now.boot!=running.boot) throw std::runtime_error("browsing changed automatic collector");
            return now.seq>running.seq;
        });
        choose(area, catalog::kAreaPick);
        choose(plc, catalog::kPlcPack);
        click_station(schematic, catalog::kStation140);
        wait([&]{ return history->rowCount()>0 && window.liveScope(); });
        for (auto size : {QSize(1100, 650), QSize(1280, 800)}) {
            window.resize(size);
            pump(250);
            if (window.size()!=size) throw std::runtime_error("size not applied: "+std::to_string(window.width())+"x"+std::to_string(window.height()));
            check_labels(window);
            for (auto name : {"factoryCombo", "areaCombo", "plcCombo", "modeButton", "primary", "disconnectButton"}) assert_inside(window, name);
        }
        choose(area, catalog::kAreaSeq);
        window.resize(1100, 650);
        pump(200);
        check_labels(window);
        choose(plc, catalog::kPlcSeq2);
        pump(150);
        check_labels(window);
        choose(area, catalog::kAreaPick);
        choose(plc, catalog::kPlcMeter2);
        const auto meter=schematic->shapeIds();
        if (meter.size()!=4 || !meter.contains(catalog::kStation40) || !meter.contains(catalog::kStation10) || !meter.contains(catalog::kStation70) || !meter.contains(catalog::kStation80) || meter.contains(catalog::kStation140))
            throw std::runtime_error("meter2 layout was not exactly its four stations");
        click_station(schematic, catalog::kStation80);
        if (window.selectedStationId()!=catalog::kStation80 || schematic->selectedId()!=catalog::kStation80) throw std::runtime_error("meter2 graphic click missed station 80");
        bool card_clicked=false;
        for (auto* button : window.findChildren<QPushButton*>("stationCard")) {
            if (button->property("stationId").toString()!=QString::fromUtf8(catalog::kStation10)) continue;
            button->click();
            card_clicked=true;
        }
        if (!card_clicked || window.selectedStationId()!=catalog::kStation10 || cell(detail, "工位").contains(catalog::kStation140) || points->rowCount()!=0)
            throw std::runtime_error("meter2 number focus reused station 140");
        assert_no_live_leak(window);
        for (auto size : {QSize(1280, 800), QSize(1100, 650)}) {
            window.resize(size);
            pump(200);
            check_labels(window);
            for (auto name : {"factoryCombo", "areaCombo", "plcCombo", "modeButton", "primary", "disconnectButton"}) assert_inside(window, name);
        }
        const auto before_empty=snap(directory);
        for (auto plc_id : {catalog::kPlcSeq2, catalog::kPlcSeq3, catalog::kPlcSeq4}) {
            choose(area, catalog::kAreaSeq);
            choose(plc, plc_id);
            if (!window.selectedStationId().isEmpty() || !schematic->shapeIds().isEmpty() || cell(detail, "实时范围")!="未配置" || step->isEnabled())
                throw std::runtime_error("empty sequence plc showed another layout");
            check_labels(window);
        }
        const auto after_empty=snap(directory);
        if (after_empty.mode!=before_empty.mode || after_empty.revision!=before_empty.revision || after_empty.boot!=before_empty.boot)
            throw std::runtime_error("empty plc switching changed collector ownership");
        choose(area, catalog::kAreaPick);
        choose(plc, catalog::kPlcPack);
        click_station(schematic, catalog::kStation150);
        pump(150);
        check_labels(window);
        assert_no_live_leak(window);
        std::cout<<"PASS widgets: Qiantang navigation, manual/automatic retained, svg sync, plc fan-out, simulated scope\n";
        return 0;
    } catch (const std::exception& error) {
        std::cerr<<error.what()<<'\n';
        return 1;
    }
}
