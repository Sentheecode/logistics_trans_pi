#include "catalog.hpp"
#include "schematic.hpp"
#include "workbench.hpp"
#include <QApplication>
#include <QCheckBox>
#include <QComboBox>
#include <QDialog>
#include <QMenu>
#include <QLineEdit>
#include <QStackedWidget>
#include <QTabBar>
#include <QDir>
#include <QElapsedTimer>
#include <QFile>
#include <QFont>
#include <QFontInfo>
#include <QFontMetrics>
#include <QHeaderView>
#include <QLabel>
#include <QMessageBox>
#include <QMouseEvent>
#include <QPushButton>
#include <QScrollArea>
#include <QScrollBar>
#include <QSplitter>
#include <QTabWidget>
#include <QTableWidget>
#include <QThread>
#include <QTimer>
#include <algorithm>
#include <cstdlib>
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
void require_glyphs(const QFontMetrics& metrics, const QString& text, const QString& where) {
    for (const auto& ch : text) {
        if (ch.isSpace() || ch.category()==QChar::Other_Control) continue;
        if (!metrics.inFont(ch)) throw std::runtime_error(("missing glyph "+QString(ch)+" in "+where).toStdString());
    }
}
void check_labels(QWidget& window) {
    for (auto* label : window.findChildren<QLabel*>()) {
        if (!label->isVisible() || label->text().isEmpty()) continue;
        if (!label->wordWrap() && label->fontMetrics().horizontalAdvance(label->text())>label->contentsRect().width()+2)
            throw std::runtime_error(("clipped label: "+label->text()).toStdString());
        require_glyphs(label->fontMetrics(), label->text(), label->objectName().isEmpty() ? label->text() : label->objectName());
    }
    for (auto* button : window.findChildren<QPushButton*>()) {
        if (!button->isVisible() || button->text().isEmpty()) continue;
        require_glyphs(button->fontMetrics(), button->text(), button->objectName().isEmpty() ? button->text() : button->objectName());
    }
    if (auto* tabs=window.findChild<QTabBar*>("moduleTabs")) {
        const QFontMetrics metrics(tabs->font());
        for (int index=0; index<tabs->count(); ++index) require_glyphs(metrics, tabs->tabText(index), "moduleTabs");
    }
}
void show_page(QWidget& window, const QString& name) {
    auto* tabs=window.findChild<QTabBar*>("moduleTabs");
    auto* stack=window.findChild<QStackedWidget*>("moduleStack");
    if (!tabs || !stack) throw std::runtime_error("missing page tabs");
    if (window.findChild<QPushButton*>("diagnosticButton") || window.findChild<QMenu*>("diagnosticMenu") || window.findChild<QDialog*>("diagnosticsDialog"))
        throw std::runtime_error("diagnostic popup still exists");
    for (int index=0; index<tabs->count(); ++index) {
        if (tabs->tabText(index)!=name) continue;
        tabs->setCurrentIndex(index);
        QCoreApplication::processEvents();
        if (stack->currentIndex()!=index) throw std::runtime_error("tab did not switch page");
        return;
    }
    throw std::runtime_error(("missing page "+name).toStdString());
}
void assert_tabs_above_factory(QWidget& window) {
    auto* tabs=window.findChild<QTabBar*>("moduleTabs");
    auto* factory=window.findChild<QComboBox*>("factoryCombo");
    if (!tabs || !factory || !tabs->isVisible() || !factory->isVisible()) throw std::runtime_error("tabs or factory hidden");
    const int tabBottom=tabs->mapTo(&window, QPoint(0, tabs->height())).y();
    const int factoryTop=factory->mapTo(&window, QPoint(0, 0)).y();
    if (tabBottom>factoryTop+1) throw std::runtime_error("page tabs are not above the factory selector");
}
void assert_combo_contrast(const QComboBox* box) {
    auto* view=box ? box->view() : nullptr;
    if (!view) throw std::runtime_error("combo has no popup view");
    const auto text=view->palette().color(QPalette::Text);
    const auto base=view->palette().color(QPalette::Base);
    const auto selected=view->palette().color(QPalette::Highlight);
    const auto selectedText=view->palette().color(QPalette::HighlightedText);
    if (text.lightness()>80 || base.lightness()<220) throw std::runtime_error("combo popup text and background do not contrast");
    if (selectedText.lightness()>120 || selected.lightness()<160) throw std::runtime_error("combo selection text and background do not contrast");
    if (!view->styleSheet().contains("color:#1b1f23") || !view->styleSheet().contains("background:#ffffff"))
        throw std::runtime_error("combo popup stylesheet lost explicit colors");
}
void assert_point_detail_reachable(QWidget& window) {
    auto* tabs=window.findChild<QTabBar*>("moduleTabs");
    auto* points=window.findChild<QTableWidget*>("pointAuditTable");
    auto* scroll=window.findChild<QScrollArea*>("pointDetailScroll");
    auto* label=window.findChild<QLabel*>("pointDetail");
    if (!tabs || !points || !scroll || !label || points->rowCount()<1) throw std::runtime_error("missing point detail scroll");
    show_page(window,"点表");
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
    show_page(window,"运行");
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
bool board_has(QWidget& window, const QString& text) {
    auto* table=window.findChild<QTableWidget*>("stationBoard");
    if (!table) return false;
    for (int row=0; row<table->rowCount(); ++row) for (int column=0; column<table->columnCount(); ++column) {
        auto* item=table->item(row, column);
        if (item && item->text().contains(text)) return true;
    }
    return false;
}
QString board_row(QWidget& window, const QString& code, int column) {
    auto* table=window.findChild<QTableWidget*>("stationBoard");
    if (!table) throw std::runtime_error("missing station board");
    for (int row=0; row<table->rowCount(); ++row) {
        auto* key=table->item(row, 0);
        auto* value=table->item(row, column);
        if (key && value && key->text()==code) return value->text();
    }
    throw std::runtime_error(("missing station row "+code).toStdString());
}
void assert_log_rows_whole(QTableWidget* logs, const char* where, bool requireVisible) {
    if (!logs || !logs->viewport()) throw std::runtime_error(std::string("missing log table at ")+where);
    if (logs->verticalScrollMode()!=QAbstractItemView::ScrollPerItem)
        throw std::runtime_error(std::string("log table scrolls by pixels at ")+where);
    int sample=logs->verticalHeader()->defaultSectionSize();
    for (int row=0; row<logs->rowCount(); ++row) {
        if (!logs->isRowHidden(row)) { sample=logs->rowHeight(row); break; }
    }
    const int viewportHeight=logs->viewport()->height();
    const int slack=sample>1 && viewportHeight>sample ? viewportHeight%sample : 0;
    if (sample<28) throw std::runtime_error(("log row height was compressed to "+std::to_string(sample)+" at "+where).c_str());
    const int pixels=logs->font().pixelSize();
    if (pixels>0 && pixels<14) throw std::runtime_error(("log font was compressed to "+std::to_string(pixels)+"px at "+where).c_str());
    if (slack!=0 || viewportHeight<sample*4)
        throw std::runtime_error(QString("log viewport is not a whole number of rows at %1 vh=%2 rh=%3")
            .arg(where).arg(viewportHeight).arg(sample).toStdString());
    int visible=0;
    int first=9999;
    for (int row=0; row<logs->rowCount(); ++row) {
        if (logs->isRowHidden(row) || !logs->item(row, 0)) continue;
        const int top=logs->rowViewportPosition(row);
        const int height=logs->rowHeight(row);
        if (height!=sample) throw std::runtime_error(("log row height varies at "+std::string(where)).c_str());
        if (top+height<=0 || top>=viewportHeight) continue;
        if (top<0 || top+height>viewportHeight)
            throw std::runtime_error(QString("log row is cut at %1 row %2 top %3 height %4 vh %5").arg(where).arg(row).arg(top).arg(height).arg(viewportHeight).toStdString());
        first=std::min(first, top);
        ++visible;
    }
    if (requireVisible && visible==0) throw std::runtime_error(std::string("log viewport has no complete row at ")+where);
    if (visible>0 && first!=0) throw std::runtime_error(QString("log rows are not aligned to the top at %1 offset %2").arg(where).arg(first).toStdString());
}
void place_log_scroll(QTableWidget* logs, int value, const char* where) {
    auto* bar=logs->verticalScrollBar();
    logs->setCurrentCell(-1, -1);
    QCoreApplication::processEvents();
    const int target=std::clamp(value, bar->minimum(), bar->maximum());
    bar->setValue(target);
    if (bar->maximum()>0 && bar->value()!=target)
        throw std::runtime_error(QString("log scroll did not stay at %1 target %2 value %3 max %4").arg(where).arg(target).arg(bar->value()).arg(bar->maximum()).toStdString());
    assert_log_rows_whole(logs, where, true);
}
void save_log_shot(QWidget& window, QTableWidget* logs, const QString& path) {
    if (path.isEmpty()) return;
    window.grab().save(path+".png");
    logs->viewport()->grab().save(path+"-viewport.png");
    QFile meta(path+".txt");
    if (!meta.open(QIODevice::WriteOnly|QIODevice::Truncate)) return;
    const QRect table(logs->mapTo(&window, QPoint(0, 0)), logs->size());
    const QRect viewport(logs->viewport()->mapTo(&window, QPoint(0, 0)), logs->viewport()->size());
    meta.write(QString("table %1 %2 %3 %4\nviewport %5 %6 %7 %8\nrow %9\nscroll %10 %11\n")
        .arg(table.x()).arg(table.y()).arg(table.width()).arg(table.height())
        .arg(viewport.x()).arg(viewport.y()).arg(viewport.width()).arg(viewport.height())
        .arg(logs->rowHeight(0))
        .arg(logs->verticalScrollBar()->value()).arg(logs->verticalScrollBar()->maximum()).toUtf8());
}
void exercise_log_rows(QWidget& window, const QString& shotPrefix) {
    auto* logs=window.findChild<QTableWidget*>("logTable");
    auto* search=window.findChild<QLineEdit*>("logSearch");
    auto* category=window.findChild<QComboBox*>("logCategory");
    auto* level=window.findChild<QComboBox*>("logLevel");
    auto* follow=window.findChild<QPushButton*>("logFollow");
    if (!logs || !search || !category || !level || !follow) throw std::runtime_error("missing log controls");
    if (follow->text()=="暂停跟随") follow->click();
    if (follow->text()!="恢复跟随") throw std::runtime_error("log follow did not pause");
    auto* bar=logs->verticalScrollBar();
    place_log_scroll(logs, 0, "top");
    save_log_shot(window, logs, shotPrefix.isEmpty() ? QString() : shotPrefix+"-top");
    place_log_scroll(logs, bar->maximum()/2, "middle");
    save_log_shot(window, logs, shotPrefix.isEmpty() ? QString() : shotPrefix+"-middle");
    place_log_scroll(logs, bar->maximum(), "end");
    save_log_shot(window, logs, shotPrefix.isEmpty() ? QString() : shotPrefix+"-end");
    const auto categoryId=logs->item(0, 0)->data(Qt::UserRole+1).toString().toStdString();
    choose(category, categoryId.c_str());
    pump(40);
    place_log_scroll(logs, 0, "category-filter");
    for (int row=0; row<logs->rowCount(); ++row) {
        auto* item=logs->item(row, 0);
        if (!logs->isRowHidden(row) && (!item || item->data(Qt::UserRole+1).toString()!=QString::fromStdString(categoryId)))
            throw std::runtime_error("log category filter leaked another category");
    }
    std::string levelId;
    for (int row=0; row<logs->rowCount(); ++row) {
        auto* item=logs->item(row, 0);
        if (!item || logs->isRowHidden(row)) continue;
        const auto rowLevel=item->data(Qt::UserRole+2).toString().toStdString();
        if (levelId.empty()) levelId=rowLevel;
        if (rowLevel=="warn") { levelId="warn"; break; }
    }
    if (levelId.empty()) throw std::runtime_error("category filter hid every log");
    choose(level, levelId.c_str());
    pump(40);
    place_log_scroll(logs, logs->verticalScrollBar()->maximum(), "level-filter");
    save_log_shot(window, logs, shotPrefix.isEmpty() ? QString() : shotPrefix+"-filtered");
    for (int row=0; row<logs->rowCount(); ++row) {
        auto* item=logs->item(row, 0);
        if (!logs->isRowHidden(row) && (!item || item->data(Qt::UserRole+2).toString()!=QString::fromStdString(levelId) || item->data(Qt::UserRole+1).toString()!=QString::fromStdString(categoryId)))
            throw std::runtime_error("log level filter leaked another row");
    }
    search->setText("没有这个日志ZZZ");
    pump(30);
    assert_log_rows_whole(logs, "empty-filter", false);
    search->clear();
    choose(category, "all");
    choose(level, "all");
    pump(40);
    place_log_scroll(logs, 0, "filter-cleared");
    if (follow->text()=="恢复跟随") follow->click();
    if (follow->text()!="暂停跟随" || logs->currentRow()!=0) throw std::runtime_error("log follow did not resume at the newest row");
    assert_log_rows_whole(logs, "follow-resumed", true);
    save_log_shot(window, logs, shotPrefix.isEmpty() ? QString() : shotPrefix+"-following");
}
QPushButton* station_button(QWidget& window, const char* id) {
    auto* table=window.findChild<QTableWidget*>("stationBoard");
    if (!table) throw std::runtime_error("missing station board");
    for (int row=0; row<table->rowCount(); ++row) {
        auto* item=table->item(row, 0);
        if (!item || item->data(Qt::UserRole).toString()!=QString::fromUtf8(id)) continue;
        auto* cell=table->cellWidget(row, 3);
        auto* button=cell ? cell->findChild<QPushButton*>() : nullptr;
        if (!button) throw std::runtime_error(std::string("missing station detail button ")+id);
        return button;
    }
    throw std::runtime_error(std::string("missing station row ")+id);
}
void set_station_open(QWidget& window, const char* id, bool open) {
    auto* button=station_button(window, id);
    const bool opened=button->text()=="收起";
    if (opened!=open) button->click();
    pump(60);
}
void assert_same_collector(const Snap& before, const Snap& after, const char* where, bool seqStable) {
    if (after.mode!=before.mode || after.boot!=before.boot || after.revision!=before.revision || (seqStable && after.seq!=before.seq))
        throw std::runtime_error(std::string("collector changed at ")+where);
}
void assert_stage_matches(QTableWidget* stages, const std::filesystem::path& directory, bool trustworthy) {
    const auto view=sim::read(directory, "recent", 5);
    if (!stages || stages->rowCount()!=3 || view.rows.empty()) throw std::runtime_error("stage table is not showing the current frame");
    const auto& frame=view.rows.front();
    for (int i=0; i<3; ++i) {
        const auto value=trustworthy ? frame.carts[i] : std::string("UNKNOWN");
        int current=0;
        if (value=="scanning") current=1;
        else if (value=="sorting") current=2;
        else if (value=="station_complete") current=3;
        for (int column=1; column<=3; ++column) {
            auto* item=stages->item(i, column);
            const QString expect=(column==current && current>0) ? (column==1 ? "读码" : column==2 ? "分拣" : "本工位完成") : "—";
            if (!item || item->text()!=expect)
                throw std::runtime_error(("stage cell does not match the collector frame row "+std::to_string(i)+" col "+std::to_string(column)).c_str());
        }
        auto* note=stages->item(i, 4);
        if (!trustworthy && (!note || note->text()!="无可信值")) throw std::runtime_error("disconnected frame still showed a phase");
    }
}
void assert_preview_isolated(Workbench& window) {
    for (auto page : {"运行", "日志", "采集"}) {
        show_page(window, QString::fromUtf8(page));
        pump(40);
        if (board_has(window, "140") || !board_has(window, "210") || !board_has(window, "220"))
            throw std::runtime_error(std::string(page)+" leaked station 140 while previewing sort 3");
        auto* pane=window.findChild<QWidget*>("commandPane");
        if (pane && pane->isVisible()) throw std::runtime_error(std::string(page)+" showed pack controls during sort 3 preview");
        if (QString::fromUtf8(page)=="运行") assert_no_live_leak(window);
        if (QString::fromUtf8(page)=="日志") {
            auto* logs=window.findChild<QTableWidget*>("logTable");
            auto* detail=window.findChild<QLabel*>("logDetail");
            if (!logs || logs->rowCount()!=0) throw std::runtime_error("sort 3 log page kept pack frames");
            if (detail && (detail->text().contains("140") || detail->text().contains("扫码")))
                throw std::runtime_error("sort 3 log detail kept a pack frame");
        }
        if (QString::fromUtf8(page)=="采集") {
            auto* source=window.findChild<QLabel*>("collectLive");
            auto* changes=window.findChild<QTableWidget*>("collectChanges");
            if (source && (source->text().contains("140") || source->text().contains("sim.three-cart")))
                throw std::runtime_error("sort 3 collection borrowed the pack frame");
            if (changes && changes->rowCount()!=0) throw std::runtime_error("sort 3 collection kept pack changes");
        }
    }
}
void preview_sort3(Workbench& window, QComboBox* area, QComboBox* plc, const std::filesystem::path& directory, const Snap& origin, const char* where) {
    choose(area, catalog::kAreaSeq);
    choose(plc, catalog::kPlcSeq3);
    wait([&]{ return board_has(window, "210") && board_has(window, "220") && !board_has(window, "140"); });
    assert_same_collector(origin, snap(directory), where, true);
    assert_preview_isolated(window);
    assert_same_collector(origin, snap(directory), where, true);
    choose(area, catalog::kAreaPick);
    choose(plc, catalog::kPlcPack);
    wait([&]{ return window.liveScope() && board_has(window, "140") && board_has(window, "150") && board_has(window, "160"); });
    if (window.confirmedMode()!=QString::fromUtf8(origin.mode.c_str())) throw std::runtime_error(std::string("returning from sort 3 reset the mode at ")+where);
    assert_same_collector(origin, snap(directory), where, true);
}
void click_station(QWidget& window, const char* id) {
    auto* table=window.findChild<QTableWidget*>("stationBoard");
    if (!table) throw std::runtime_error("missing station board");
    for (int row=0; row<table->rowCount(); ++row) {
        auto* item=table->item(row, 0);
        if (!item || item->data(Qt::UserRole).toString()!=QString::fromUtf8(id)) continue;
        const QPoint center=table->visualItemRect(item).center();
        QMouseEvent press(QEvent::MouseButtonPress, QPointF(center), QPointF(table->viewport()->mapToGlobal(center)), Qt::LeftButton, Qt::LeftButton, Qt::NoModifier);
        QCoreApplication::sendEvent(table->viewport(), &press);
        return;
    }
    throw std::runtime_error(std::string("missing station row ")+id);
}

int main(int argc, char** argv) {
    QApplication app(argc, argv);
    QFont font("Noto Sans CJK SC"); font.setStyleHint(QFont::SansSerif, QFont::PreferAntialias); font.setFamilies({"Noto Sans CJK SC", "Noto Sans CJK JP", "Microsoft YaHei UI", "Microsoft YaHei"}); font.setPixelSize(13); app.setFont(font);
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
            if (!factory || factory->count()!=3 || !factory->isEnabled()) throw std::runtime_error("offline catalog disabled");
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
        if (window.findChild<QComboBox*>("lineCombo") || window.findChild<QLabel*>("currentStation"))
            throw std::runtime_error("line or current-station control still exists");
        if (window.findChild<QComboBox*>("stationCombo") || window.findChild<QLabel*>("currentStation") || window.findChild<QPushButton*>("stationCard"))
            throw std::runtime_error("global station selector still exists");
        auto* board=window.findChild<QTableWidget*>("stationBoard");
        if (!board || board->selectionMode()!=QAbstractItemView::NoSelection) throw std::runtime_error("station board is missing or selectable");
        if (!window.findChild<QWidget*>("catalogPane") || !window.findChild<QWidget*>("commandPane"))
            throw std::runtime_error("missing context or operations");
        if (window.findChild<QWidget*>("sideNav")) throw std::runtime_error("sidebar still exists");
        if (QString(schematic->metaObject()->className())!="SchematicView" || !schematic->loaded())
            throw std::runtime_error("schematic resource did not load");
        auto* stages=window.findChild<QTableWidget*>("stageTable");
        auto* result=window.findChild<QTableWidget*>("runResultTable");
        auto* order=window.findChild<QLabel*>("simOrder");
        if (!stages || !result || !order) throw std::runtime_error("missing stage or result view");
        const auto shown=schematic->shapeIds();
        if (shown.size()!=3 || !shown.contains(catalog::kStation140) || !shown.contains(catalog::kStation150) || !shown.contains(catalog::kStation160) || shown.contains(catalog::kStation210) || shown.contains(catalog::kStation20))
            throw std::runtime_error("layout showed stations outside the current PLC");
        wait([&]{ return mode->isEnabled(); });
        wait([&]{ return history->rowCount()>0; });
        stages->setCurrentCell(1,0);
        pump(200);
        if (stages->rowCount()!=3 || stages->currentRow()!=1 || !order->isVisible())
            throw std::runtime_error("stage rows did not stay visible after refresh");
        for (auto size : {QSize(1100,650), QSize(1280,800)}) {
            window.resize(size);
            pump(120);
            for (auto name : {"moduleTabs","pageTabRow","stationBoard","runtimeState","catalogPane","stageTable","simOrder"})
                assert_inside(window,name);
            assert_tabs_above_factory(window);
            auto bounds=[&](const char* name){
                auto* widget=window.findChild<QWidget*>(name);
                if (!widget || !widget->isVisible()) throw std::runtime_error(std::string("missing visible control ")+name);
                return QRect(widget->mapTo(&window, QPoint(0, 0)), widget->size());
            };
            const QRect boardBox=bounds("stationBoard");
            const QRect stageBox=bounds("stageTable");
            auto* actions=window.findChild<QWidget*>("commandPane");
            if (!boardBox.contains(stageBox) || (actions && actions->isVisible()))
                throw std::runtime_error("run page sections overlap");
            const int sourceLeft=board->mapTo(&window, QPoint(board->horizontalHeader()->sectionViewportPosition(3), 0)).x();
            if (sourceLeft-stageBox.right()>64)
                throw std::runtime_error(("stage table does not use the station detail width source "+std::to_string(sourceLeft)+" stageRight "+std::to_string(stageBox.right())+" stageW "+std::to_string(stageBox.width())+" boardW "+std::to_string(boardBox.width())).c_str());
            for (int column=0; column<std::min(3, board->columnCount()); ++column) {
                auto* item=board->item(0, column);
                auto* header=board->horizontalHeader();
                if (!item || !header || std::abs(board->visualItemRect(item).x()-header->sectionViewportPosition(column))>3)
                    throw std::runtime_error("station column header is not aligned with its cells");
            }
        }
        show_page(window,"日志");
        const QString shotRoot=QString::fromLocal8Bit(qgetenv("LOGISTICS_SHOTS"));
        for (auto size : {QSize(1100,650), QSize(1280,800)}) {
            window.resize(size);
            pump(160);
            auto* logs=window.findChild<QTableWidget*>("logTable");
            auto* levels=logs ? logs->itemDelegateForColumn(1) : nullptr;
            if (!logs || logs->rowCount()<1 || !levels || levels->objectName()!="levelDelegate")
                throw std::runtime_error("log levels are not distinctly painted");
            wait([&]{ return logs->verticalScrollBar()->maximum()>=3; }, 20000);
            exercise_log_rows(window, shotRoot.isEmpty() ? QString() : shotRoot+"/logs-"+QString::number(size.width()));
        }
        auto* module=window.findChild<QTabBar*>("moduleTabs");
        if (module->tabText(module->currentIndex())!="日志" || stages->isVisible())
            throw std::runtime_error("log tab did not replace the run view");
        assert_tabs_above_factory(window);
        show_page(window,"运行");
        if (!stages->isVisible() || stages->currentRow()!=1 || !window.liveScope())
            throw std::runtime_error("returning to the run page changed station or stage");
        window.resize(1100,650);
        if (stages->verticalScrollBar()->maximum()!=0)
            throw std::runtime_error("current stages require scrolling");
        if (window.confirmedMode()!="automatic" || step->isEnabled() || !window.liveScope()) throw std::runtime_error("auto mode controls incorrect");
        auto* autoReason=window.findChild<QLabel*>("controlReason");
        auto* actions=window.findChild<QWidget*>("commandPane");
        if (!autoReason || autoReason->text()!="本 PLC 的模拟只作用于工位 140，自动中不可单步") throw std::runtime_error("automatic disable reason is wrong");
        show_page(window, "采集");
        if (!actions || !actions->isVisible() || !mode->isVisible() || !step->isVisible()) throw std::runtime_error("automatic controls are not on the collection page");
        show_page(window, "运行");
        if (actions->isVisible()) throw std::runtime_error("automatic controls leaked onto the run page");
        assert_stage_matches(stages, directory, true);
        auto* title=window.findChild<QLabel*>("title");
        auto requireSans=[&](QWidget* widget, const char* name) {
            if (!widget) throw std::runtime_error(std::string("missing font target ")+name);
            const QFontInfo info(widget->font());
            if (info.family()!="Noto Sans CJK SC") throw std::runtime_error(std::string(name)+" resolved "+info.family().toStdString()+" / "+info.styleName().toStdString());
            if (info.styleName()!="Regular" && info.styleName()!="Bold") throw std::runtime_error(std::string(name)+" unexpected style "+info.styleName().toStdString());
        };
        requireSans(title, "title");
        requireSans(mode, "mode");
        requireSans(factory, "factory");
        requireSans(points, "points");
        if (QApplication::font().styleStrategy() & QFont::NoFontMerging)
            throw std::runtime_error("font merging is disabled");
        if (!QFontMetrics(QApplication::font()).inFont(QChar(0x6a21)))
            throw std::runtime_error("Chinese glyph is missing from the active font");
        if (!window.styleSheet().contains("border-radius:8px") || window.styleSheet().contains("qlineargradient") || window.styleSheet().contains("box-shadow"))
            throw std::runtime_error("controls are not rounded");
        for (auto* box : {factory, area, plc}) assert_combo_contrast(box);
        if (area->isVisible()) throw std::runtime_error("business mode dropdown is still visible");
        if (factory->count()!=3 || area->count()!=2 || plc->count()!=9) throw std::runtime_error("catalog dropdowns incomplete");
        if (plc->findText("分拣")<0 || plc->findText("排序区")<0) throw std::runtime_error("plc menu has no area groups");
        for (int i=0; i<area->count(); ++i) if (area->itemText(i).contains("手动") || area->itemText(i).contains("自动")) throw std::runtime_error("runtime mode mixed into area dropdown");
        if (cell(detail, "设备连接")!="未探测" || cell(detail, "库存")!="未知" || cell(detail, "库位")!="待补映射") throw std::runtime_error("live station fabricated mapping");
        if (cell(detail, "业务模式")==cell(detail, "运行模式") || !cell(detail, "实时范围").contains(QString::fromUtf8(catalog::kStream))) throw std::runtime_error("area and runtime modes not independent");
        if (points->rowCount()<10) throw std::runtime_error("station 140 audit missing");
        const QColor focusBlue(29, 78, 137);
        if (window.controllerPeerIds().size()!=3 || schematic->markerColor(QString::fromUtf8(catalog::kStation140))==focusBlue || !schematic->markerColor(QString::fromUtf8(catalog::kStation140)).isValid() || !schematic->markerColor(QString::fromUtf8(catalog::kStation150)).isValid() || schematic->markerColor(QString::fromUtf8(catalog::kStation210)).isValid())
            throw std::runtime_error("plc overview highlighted a single station or hid a sibling");
        mode->click();
        wait([&]{ return window.confirmedMode()=="manual" && step->isEnabled(); });
        auto before=snap(directory);
        step->click(); step->click(); step->click();
        wait([&]{ return step->isEnabled(); });
        auto after=snap(directory);
        if (after.seq!=before.seq+1 || after.revision!=before.revision+1) throw std::runtime_error("rapid clicks advanced more than once");
        auto* logs=window.findChild<QTableWidget*>("logTable");
        auto* logCategory=window.findChild<QComboBox*>("logCategory");
        if (!logs || !logCategory) throw std::runtime_error("missing log view");
        if (logs->rowCount()>80) throw std::runtime_error("log query exceeded 80 rows");
        if (!logs->horizontalHeaderItem(0) || logs->horizontalHeaderItem(0)->text().contains("北京")) throw std::runtime_error("timezone belongs in the page note, not each column");
        wait([&]{
            for (int row=0; row<logs->rowCount(); ++row) {
                auto* item=logs->item(row, 0);
                if (item && item->data(Qt::UserRole+3).toString().contains("step") && item->data(Qt::UserRole+1).toString()=="audit") return true;
            }
            return false;
        });
        const auto before_scope=snap(directory);
        if (plc->currentData().toString()!=QString::fromUtf8(catalog::kPlcPack) || !board_has(window, "140") || !board_has(window, "150") || !board_has(window, "160"))
            throw std::runtime_error("plc context hid the other stations");
        if (board_row(window, "150", 3)!="未接入" || board_row(window, "160", 3)!="未接入" || board_row(window, "140", 3)!="模拟预览")
            throw std::runtime_error("station source column is wrong");
        if (board_row(window, "140", 1)!="名称未登记" || board_row(window, "150", 1)!="名称未登记" || board_row(window, "160", 1)!="名称未登记")
            throw std::runtime_error("unregistered station name was invented");
        if (board_row(window, "150", 2)!="—" || board_row(window, "160", 2)!="—" || board_row(window, "140", 2)!="三车分拣 · 本轮")
            throw std::runtime_error("sibling station reused station 140 records");
        if (!autoReason || autoReason->text()!="本 PLC 的模拟只作用于工位 140，手动待步") throw std::runtime_error("manual reason is wrong");
        show_page(window, "采集");
        auto* linkButton=window.findChild<QPushButton*>("disconnectButton");
        if (!actions || !actions->isVisible() || !step->isEnabled() || !linkButton || !linkButton->isEnabled()) throw std::runtime_error("manual controls are not on the collection page");
        show_page(window, "运行");
        if (actions->isVisible()) throw std::runtime_error("manual controls leaked onto the run page");
        assert_stage_matches(stages, directory, true);
        const auto before_expand=snap(directory);
        set_station_open(window, catalog::kStation150, true);
        if (station_button(window, catalog::kStation150)->text()!="收起") throw std::runtime_error("station 150 did not expand");
        bool foundNote=false;
        for (auto* label : window.findChildren<QLabel*>()) {
            if (label->isVisible() && label->text().contains("没有模拟源")) foundNote=true;
        }
        if (!foundNote || stages->isVisible()) throw std::runtime_error("station 150 expansion borrowed the live stage");
        assert_same_collector(before_expand, snap(directory), "expand 150", true);
        set_station_open(window, catalog::kStation160, true);
        assert_same_collector(before_expand, snap(directory), "expand 160", true);
        if (cell(detail, "设备连接")!="未探测") throw std::runtime_error("expanded station invented a device connection");
        set_station_open(window, catalog::kStation140, true);
        wait([&]{ return stages->isVisible() && stages->rowCount()==3; });
        assert_same_collector(before_expand, snap(directory), "restore 140", true);
        assert_stage_matches(stages, directory, true);
        preview_sort3(window, area, plc, directory, before_expand, "manual preview");
        show_page(window, "运行");
        if (!stages->isVisible()) throw std::runtime_error("returning from manual preview hid the live stage");
        std::cout<<"PASS candidate manual preview and local expand\n";
        const auto plc_before=plc->currentData().toString();
        click_station(window,catalog::kStation150);
        if (plc->currentData().toString()!=plc_before || !board_has(window, "140"))
            throw std::runtime_error("schematic click changed the PLC or hid a station");
        choose(factory,"fac.demo-north");
        if (!board_has(window, "N1") || !board_has(window, "N2") || cell(detail,"仓库")!="示例立库 N")
            throw std::runtime_error("factory did not show both demo stations");
        assert_no_live_leak(window);
        choose(area,"area.demo-north.sort");
        if (plc->currentData().toString()!=QString::fromUtf8(catalog::kPlcEmpty) || board_has(window, "N1"))
            throw std::runtime_error("empty plc retained another scope");
        assert_no_live_leak(window);
        choose(factory,"fac.demo-south");
        if (board_has(window, "140")) throw std::runtime_error("empty business mode retained station 140");
        choose(area,"area.demo-south.sort");
        if (!board_has(window, "S1")) throw std::runtime_error("south station missing");
        assert_no_live_leak(window);
        choose(factory,catalog::kFactory);
        choose(plc,catalog::kPlcPack);
        wait([&]{return logs->rowCount()>0 && board_has(window, "140") && board_has(window, "150");});
        const auto after_scope=snap(directory);
        if (after_scope.mode!=before_scope.mode || after_scope.seq!=before_scope.seq || after_scope.revision!=before_scope.revision)
            throw std::runtime_error("catalog navigation altered collector");
        const auto before_log_filter=snap(directory);
        choose(logCategory, "audit");
        pump(40);
        for (int row=0; row<logs->rowCount(); ++row) {
            auto* item=logs->item(row, 0);
            if (!logs->isRowHidden(row) && (!item || item->data(Qt::UserRole+1).toString()!="audit")) throw std::runtime_error("log category filter leaked another category");
        }
        choose(logCategory, "all");
        if (snap(directory).revision!=before_log_filter.revision || snap(directory).boot!=before_log_filter.boot || snap(directory).mode!=before_log_filter.mode)
            throw std::runtime_error("log filter changed collector ownership");
        pump(1700);
        if (!step->isEnabled() || snap(directory).seq!=after.seq) throw std::runtime_error("manual wait treated as disconnected or advanced");
        auto* link=window.findChild<QPushButton*>("disconnectButton");
        auto* banner=window.findChild<QLabel*>("scopeBanner");
        auto* clock=window.findChild<QLabel*>("heartbeatState");
        if (!link || !link->isEnabled() || !banner || !clock) throw std::runtime_error("missing disconnect or clock");
        if (!clock->text().contains("北京") && !clock->text().contains("UTC")) throw std::runtime_error("clock has no timezone");
        auto* follow=window.findChild<QPushButton*>("logFollow");
        auto* logDetail=window.findChild<QLabel*>("logDetail");
        if (!follow || follow->text()!="暂停跟随" || logs->currentRow()<0 || !logDetail || logDetail->text().contains("选择一行"))
            throw std::runtime_error("log detail was empty while rows existed");
        follow->click();
        if (follow->text()!="恢复跟随") throw std::runtime_error("pause follow did not stick");
        logs->setCurrentCell(0,0);
        const auto selectedLog=logs->item(0,0)->data(Qt::UserRole+4);
        if (schematic->showsCarts() || order->text().contains("SIM-01") || (!order->text().contains("取料工位") && !order->text().contains("当前无活动料车")) || !stages->isVisible())
            throw std::runtime_error("live order should stay on station 140, not a shared lane");
        const auto before_break=snap(directory);
        link->click();
        wait([&]{ return link->isEnabled() && snap(directory).seq==before_break.seq+1; });
        if (!selectedLog.isValid() || logs->currentRow()<=0 ||
            logs->item(logs->currentRow(),0)->data(Qt::UserRole+4)!=selectedLog)
            throw std::runtime_error("new logs changed the selected record");
        if (!order->text().contains("进度未知") || !order->text().contains("目标未知"))
            throw std::runtime_error("bad frame reported an inferred target or progress");
        auto broken=snap(directory);
        if (broken.mode!="manual" || broken.revision!=before_break.revision+1 || broken.boot!=before_break.boot || !banner->text().contains("模拟链路断开"))
            throw std::runtime_error("manual disconnect did not stay on the same collector");
        show_page(window, "运行");
        assert_stage_matches(stages, directory, false);
        if (actions->isVisible()) throw std::runtime_error("disconnect controls leaked onto the run page");
        preview_sort3(window, area, plc, directory, broken, "disconnect preview");
        show_page(window, "运行");
        wait([&]{ return banner->text().contains("模拟链路断开") && stages->isVisible() && stages->rowCount()==3; });
        assert_stage_matches(stages, directory, false);
        assert_same_collector(broken, snap(directory), "after disconnect preview", true);
        std::cout<<"PASS candidate disconnect preview\n";
        for (int guard=0; guard<4 && banner->text().contains("模拟链路断开"); ++guard) {
            const auto current=snap(directory);
            step->click();
            wait([&]{ return step->isEnabled() && snap(directory).seq==current.seq+1; });
        }
        if (banner->text().contains("模拟链路断开") || snap(directory).mode!="manual" || snap(directory).boot!=before_break.boot)
            throw std::runtime_error("step after disconnect did not resume the same manual collector");
        choose(area, catalog::kAreaSeq);
        choose(plc, catalog::kPlcSeq1);
        pump(120);
        auto* reason=window.findChild<QLabel*>("controlReason");
        if (step->isEnabled() || !reason || reason->text().contains("140") || !reason->text().contains("不能推进")) throw std::runtime_error("another PLC looked controllable");
        if (schematic->showsCarts() || schematic->targetCart()!=-1 || order->isVisible() || !order->text().isEmpty() || board_has(window, "140"))
            throw std::runtime_error("another PLC borrowed station 140");
        const auto frozen=snap(directory);
        step->click();
        pump(400);
        const auto still=snap(directory);
        if (still.seq!=frozen.seq || still.revision!=frozen.revision || still.mode!="manual" || still.boot!=frozen.boot)
            throw std::runtime_error("disabled step on another PLC advanced station 140");
        choose(area, catalog::kAreaPick);
        choose(plc, catalog::kPlcPack);
        wait([&]{ return step->isEnabled() && window.liveScope() && board_has(window, "140"); });
        const auto held=snap(directory);
        choose(area, catalog::kAreaSeq);
        choose(plc, catalog::kPlcSeq1);
        if (area->currentData().toString()!=catalog::kAreaSeq || plc->currentData().toString()!=catalog::kPlcSeq1)
            throw std::runtime_error("sequence plc did not stay selected");
        if (!board_has(window, "170") || !board_has(window, "180") || board_has(window, "140") || schematic->shapeIds().size()!=2)
            throw std::runtime_error("sequence layout still showed another PLC");
        if (board_row(window, "180", 1)!="右后门手柄机械臂排序区")
            throw std::runtime_error("sequence station name missing");
        const auto seq_plc=plc->currentData().toString();
        click_station(window, catalog::kStation180);
        if (plc->currentData().toString()!=seq_plc) throw std::runtime_error("schematic click changed the PLC");
        if (window.confirmedMode()!="manual" || step->isEnabled() || mode->isEnabled()) throw std::runtime_error("non-live station enabled control");
        if (cell(detail, "业务模式")!="排序区" || cell(detail, "运行模式")!="手动") throw std::runtime_error("sequence selection changed runtime mode");
        if (points->rowCount()!=0) throw std::runtime_error("140 point sheet applied outside station 140");
        assert_no_live_leak(window);
        step->click(); mode->click();
        const auto selected=window.selectedStationId();
        if (!title) throw std::runtime_error("missing title");
        QMouseEvent miss_press(QEvent::MouseButtonPress, QPointF(2, 2), QPointF(title->mapToGlobal(QPoint(2, 2))), Qt::LeftButton, Qt::LeftButton, Qt::NoModifier);
        QCoreApplication::sendEvent(title, &miss_press);
        if (window.selectedStationId()!=selected) throw std::runtime_error("miss click changed selection");
        choose(area, catalog::kAreaPick);
        choose(plc, catalog::kPlcPack);
        if (schematic->shapeIds().size()!=3 || !board_has(window, "140") || !board_has(window, "150") || !board_has(window, "160"))
            throw std::runtime_error("pack layout did not show all three stations");
        if (board_row(window, "150", 3)!="未接入" || board_row(window, "140", 3)!="模拟预览" || board_row(window, "150", 2).contains("140"))
            throw std::runtime_error("pack board leaked or hid station 140");
        const auto peers=cell(detail, "关联工位");
        if (!peers.contains(catalog::kStation140) || !peers.contains(catalog::kStation150) || !peers.contains(catalog::kStation160) || peers.contains(catalog::kStation210))
            throw std::runtime_error("plc peer list incorrect");
        wait([&]{ return window.liveScope() && history->rowCount()>0; });
        choose(area, catalog::kAreaSeq);
        choose(plc, catalog::kPlcSeq2);
        if (!board_has(window, "190") || !board_has(window, "200") || schematic->shapeIds().size()!=2 || schematic->shapeIds().contains(catalog::kStation140) || schematic->shapeIds().contains(catalog::kStation170))
            throw std::runtime_error("sequence plc 2 showed another machine");
        if (cell(detail, "实时范围")!="未接入" || step->isEnabled() || !window.findChild<QLabel*>("scopeBanner")->text().contains("未绑定"))
            throw std::runtime_error("named sorting station looked unconfigured or live");
        auto* emptyRun=window.findChild<QWidget*>("emptyRun");
        if (emptyRun && emptyRun->isVisible()) throw std::runtime_error("named sorting station used the unconfigured empty state");
        assert_no_live_leak(window);
        choose(factory, "fac.demo-north");
        choose(area, "area.demo-north.sort");
        choose(plc, catalog::kPlcEmpty);
        if (!window.selectedStationId().isEmpty() || !schematic->shapeIds().isEmpty() || cell(detail, "实时范围")!="未配置" || step->isEnabled())
            throw std::runtime_error("empty plc was not unconfigured");
        pump(80);
        auto* runHint=window.findChild<QLabel*>("runHint");
        auto* runWorkspace=window.findChild<QWidget*>("runWorkspace");
        if (!emptyRun || !emptyRun->isVisible() || (runHint && runHint->isVisible()) || (runWorkspace && runWorkspace->isVisible()))
            throw std::runtime_error("unconfigured plc still showed the live run card");
        auto* emptyTitle=window.findChild<QLabel*>("emptyTitle");
        auto* emptyAction=window.findChild<QLabel*>("emptyAction");
        if (!emptyTitle || !emptyAction) throw std::runtime_error("unconfigured copy missing");
        const int top=emptyTitle->mapTo(&window, QPoint(0, 0)).y();
        const int bottom=emptyAction->mapTo(&window, QPoint(0, emptyAction->height())).y();
        if (top>220 || bottom-top>140) throw std::runtime_error("unconfigured run state is not a short block at the top y="+std::to_string(top)+" span="+std::to_string(bottom-top));
        if (window.findChild<QLabel*>("emptyTitle")->text()!="尚未配置工位" || window.findChild<QLabel*>("emptySource")->text()!="当前 PLC 未绑定模拟源" || window.findChild<QLabel*>("emptyAction")->text()!="暂无法执行模拟操作")
            throw std::runtime_error("unconfigured copy changed");
        choose(factory, catalog::kFactory);
        choose(area, catalog::kAreaPick);
        choose(plc, catalog::kPlcPack);
        for (int i=0; i<20; ++i) choose(area, i%2 ? catalog::kAreaPick : catalog::kAreaSeq);
        if (area->currentData().toString()!=catalog::kAreaPick || !board_has(window, "140") || !board_has(window, "160"))
            throw std::runtime_error("parent switch did not show the whole PLC");
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
        click_station(window, catalog::kStation140);
        wait([&]{ return history->rowCount()>0 && window.liveScope(); });
        auto* filter=window.findChild<QComboBox*>("historyFilter");
        auto* search=window.findChild<QLineEdit*>("pointSearch");
        auto* doubts=window.findChild<QCheckBox*>("pointDoubtsOnly");
        auto* detailText=window.findChild<QLabel*>("pointDetail");
        if (!filter || !search || !doubts || !detailText) throw std::runtime_error("missing filter controls");
        const auto before_filter=snap(directory);
        choose(filter, "bad");
        pump(50);
        int bad_visible=0;
        for (int row=0; row<history->rowCount(); ++row) {
            auto* quality=history->item(row, 3);
            const bool hidden=history->isRowHidden(row);
            if (!hidden) {
                if (!quality || quality->text()!="不可用") throw std::runtime_error("history filter kept a good frame");
                ++bad_visible;
            }
        }
        if (bad_visible==0) throw std::runtime_error("history filter matched nothing");
        choose(filter, "all");
        pump(50);
        for (int row=0; row<history->rowCount(); ++row) if (history->isRowHidden(row)) throw std::runtime_error("history filter did not restore rows");
        if (snap(directory).mode!=before_filter.mode || snap(directory).revision!=before_filter.revision || snap(directory).boot!=before_filter.boot)
            throw std::runtime_error("history filter changed collector ownership");
        search->setText("DB1900.DBX0.0");
        pump(50);
        int matched=0;
        for (int row=0; row<points->rowCount(); ++row) if (!points->isRowHidden(row)) ++matched;
        if (matched!=1 || !points->item(0, 0) || points->item(0, 0)->text()!="设备运行中") throw std::runtime_error("point search did not isolate one address");
        points->selectRow(0);
        pump(30);
        if (!detailText->text().contains("plc-auto-not-host-mode") || !detailText->text().contains("DB1900.DBX0.0")) throw std::runtime_error("point detail hid the source code");
        for (auto size : {QSize(1280, 800), QSize(1100, 650)}) {
            window.resize(size);
            pump(200);
            assert_point_detail_reachable(window);
        }
        search->setText("没有这个地址ZZZ");
        pump(30);
        for (int row=0; row<points->rowCount(); ++row) if (!points->isRowHidden(row)) throw std::runtime_error("point search kept a row");
        search->clear();
        doubts->setChecked(true);
        pump(30);
        for (int row=0; row<points->rowCount(); ++row) {
            auto* name=points->item(row, 0);
            const bool has=name && !name->data(Qt::UserRole+1).toString().isEmpty();
            if (points->isRowHidden(row)==has) throw std::runtime_error("doubt filter did not follow source doubts");
        }
        doubts->setChecked(false);
        if (snap(directory).revision!=before_filter.revision || snap(directory).boot!=before_filter.boot)
            throw std::runtime_error("point filter changed collector ownership");
        show_page(window, "采集");
        for (auto size : {QSize(1100, 650), QSize(1280, 800)}) {
            window.resize(size);
            pump(250);
            if (window.size()!=size) throw std::runtime_error("size not applied: "+std::to_string(window.width())+"x"+std::to_string(window.height()));
            check_labels(window);
            for (auto name : {"factoryCombo", "plcCombo", "modeButton", "primary", "disconnectButton", "commandPane", "scopeBanner", "controlReason"}) assert_inside(window, name);
            auto* reason=window.findChild<QLabel*>("controlReason");
            if (!reason || reason->text().isEmpty()) throw std::runtime_error("automatic mode hid the control reason");
            bool saw140=false;
            for (auto* label : window.findChildren<QLabel*>("rosterCode")) {
                if (label->isVisible() && label->text()=="140") saw140=true;
            }
            if (!saw140) throw std::runtime_error("pack collection roster hid station 140");
        }
        show_page(window, "运行");
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
        if (!board_has(window, "40") || !board_has(window, "10") || !board_has(window, "70") || !board_has(window, "80") || board_has(window, "140"))
            throw std::runtime_error("meter2 board did not show exactly its stations");
        const auto meter_plc=plc->currentData().toString();
        click_station(window, catalog::kStation80);
        if (plc->currentData().toString()!=meter_plc || cell(detail, "工位").contains("140") || points->rowCount()!=0)
            throw std::runtime_error("meter2 reused station 140");
        assert_no_live_leak(window);
        for (auto size : {QSize(1280, 800), QSize(1100, 650)}) {
            window.resize(size);
            pump(200);
            check_labels(window);
            for (auto name : {"factoryCombo", "stationBoard", "plcCombo"}) assert_inside(window, name);
            if (window.findChild<QWidget*>("commandPane") && window.findChild<QWidget*>("commandPane")->isVisible())
                throw std::runtime_error("unbound plc still showed the control bar");
        }
        const auto before_empty=snap(directory);
        const char* seq_stations[][2]={
            {catalog::kPlcSeq1, catalog::kStation170},
            {catalog::kPlcSeq2, catalog::kStation190},
            {catalog::kPlcSeq3, catalog::kStation210},
            {catalog::kPlcSeq4, catalog::kStation230},
        };
        for (auto pair : seq_stations) {
            choose(area, catalog::kAreaSeq);
            choose(plc, pair[0]);
            const QString code=QString::fromUtf8(pair[1]).section('.', -1);
            const bool documented=std::string(pair[0])!=catalog::kPlcSeq3;
            const QString source=board_row(window, code, 3);
            if (schematic->shapeIds().size()!=2 || schematic->shapeIds().contains(catalog::kStation140) || !board_has(window, code) || cell(detail, "实时范围")!="未接入" || step->isEnabled())
                throw std::runtime_error("sorting plc mixed another machine or simulation");
            if (documented) {
                if (source!="已登记点表 · 未接入") throw std::runtime_error("candidate workbook was not shown as documentation only");
            } else if (source!="未接入") throw std::runtime_error("sequence plc 3 gained a point-table registration");
            if (source.contains("已绑定") || source.contains("已连接") || source.contains("已确认"))
                throw std::runtime_error("documentation status was described as binding");
            if (std::string(pair[0])==catalog::kPlcSeq4) {
                show_page(window, "点表");
                auto* scope=window.findChild<QLabel*>("pointScope");
                if (!scope || !scope->text().contains("工位对应待确认") || scope->text().contains("已绑定") || scope->text().contains("未由用户点名确认"))
                    throw std::runtime_error("point page did not keep workbook correspondence in the audit");
                show_page(window, "采集");
                for (auto* label : window.findChildren<QLabel*>()) {
                    if (!label->isVisible()) continue;
                    if (label->text().contains("140") || label->text().contains("sim.three-cart"))
                        throw std::runtime_error(("sequence plc 4 collection leaked station 140 via "+label->objectName().toStdString()+" / "+label->text().toStdString()).c_str());
                }
                if (board_row(window, "230", 3)!="已登记点表 · 未接入" || board_row(window, "240", 3)!="已登记点表 · 未接入")
                    throw std::runtime_error("sequence plc 4 lost its documentation-only source");
                show_page(window, "运行");
            }
            for (auto* label : window.findChildren<QLabel*>()) {
                if (label->text().contains("未由用户点名确认")) throw std::runtime_error("run page assigned confirmation to the user");
                if (label->isVisible() && label->text().contains("工位对应待确认")) throw std::runtime_error("audit wording appeared on the run page");
            }
            assert_no_live_leak(window);
            check_labels(window);
        }
        const auto after_empty=snap(directory);
        if (after_empty.mode!=before_empty.mode || after_empty.revision!=before_empty.revision || after_empty.boot!=before_empty.boot)
            throw std::runtime_error("empty plc switching changed collector ownership");
        choose(area, catalog::kAreaSeq);
        choose(plc, catalog::kPlcSeq3);
        pump(150);
        check_labels(window);
        assert_no_live_leak(window);
        if (!board_has(window, "210") || !board_has(window, "220") || board_row(window, "210", 1)!="前悬架右前下摆臂总成")
            throw std::runtime_error("sequence plc 3 did not show both named stations");
        if (step->isEnabled() || !window.findChild<QLabel*>("controlReason") || window.findChild<QLabel*>("controlReason")->text().contains("140") || !window.findChild<QLabel*>("controlReason")->text().contains("不能推进"))
            throw std::runtime_error("sorting plc looked controllable");
        if (window.findChild<QLabel*>("recentLabel") && window.findChild<QLabel*>("recentLabel")->isVisible())
            throw std::runtime_error("sorting plc still shows the live recent-record heading");
        if (window.findChild<QLabel*>("scopeBanner") && window.findChild<QLabel*>("scopeBanner")->text().contains("140"))
            throw std::runtime_error("sorting plc banner still names station 140");
        auto* owner=window.findChild<QLabel*>("acquisitionOwner");
        auto* hint=window.findChild<QLabel*>("previewHint");
        if (!owner || owner->text()!="采集归属 外装" || !hint || !hint->isVisible() || hint->text()!="仅预览 · 采集归属仍为外装" || hint->text().contains("140"))
            throw std::runtime_error("preview changed the acquisition owner");
        window.resize(1100, 650);
        pump(80);
        auto* note=window.findChild<QLabel*>("readinessNote");
        if (!note || !note->isVisible() || !board) throw std::runtime_error("sorting readiness missing");
        const int boardBottom=board->mapTo(&window, QPoint(0, board->height())).y();
        const int noteTop=note->mapTo(&window, QPoint(0, 0)).y();
        if (noteTop<boardBottom-1) throw std::runtime_error("readiness note covers the station table");
        if (std::abs(note->mapTo(&window, QPoint(0, 0)).x()-board->mapTo(&window, QPoint(0, 0)).x())>12)
            throw std::runtime_error("readiness note is not left aligned with the station table");
        if (board_row(window, "220", 2)!="—" || board_row(window, "220", 3)!="未接入" || board_row(window, "210", 1)!="前悬架右前下摆臂总成")
            throw std::runtime_error("station 220 row is incomplete");
        for (int row=0; row<detail->rowCount(); ++row)
            if (detail->item(row, 0) && detail->item(row, 0)->text()=="线体")
                throw std::runtime_error("line is still a visible configuration field");
        auto* modules=window.findChild<QTabBar*>("moduleTabs");
        if (!modules || modules->count()!=5 || modules->tabText(0)!="运行" || modules->tabText(1)!="日志") throw std::runtime_error("module tabs missing");
        for (int index=0; index<modules->count(); ++index) {
            const auto text=modules->tabText(index);
            if (text=="工位" || text=="事件" || text=="业务模式") throw std::runtime_error("retired tab is still visible");
        }
        const auto before_tabs=snap(directory);
        for (auto title : {"日志", "采集", "点表", "接口"}) {
            show_page(window,QString::fromUtf8(title));
            pump(80);
            check_labels(window);
            assert_tabs_above_factory(window);
            if (modules->tabText(modules->currentIndex())!=QString::fromUtf8(title))
                throw std::runtime_error("page tab opened the wrong page");
            if (step->isEnabled()) throw std::runtime_error("diagnostic changed controls");
            if (plc->currentData().toString()!=QString::fromUtf8(catalog::kPlcSeq3) || !board_has(window, "210") || !board_has(window, "220") || board_has(window, "140"))
                throw std::runtime_error("module switch changed the PLC scope");
            if (history->rowCount()!=0 || points->rowCount()!=0) throw std::runtime_error("module switch copied station 140 records");
            if (QString::fromUtf8(title)=="采集") {
                auto* source=window.findChild<QLabel*>("collectLive");
                if (!source || source->text().contains("140") || source->text().contains("sim.three-cart"))
                    throw std::runtime_error("sorting collect page borrowed station 140");
            }
        }
        const auto after_tabs=snap(directory);
        if (after_tabs.mode!=before_tabs.mode || after_tabs.revision!=before_tabs.revision || after_tabs.boot!=before_tabs.boot)
            throw std::runtime_error("module switch changed collector ownership");
        std::cout<<"PASS widgets: Qiantang navigation, manual/automatic retained, svg sync, plc fan-out, simulated scope\n";
        return 0;
    } catch (const std::exception& error) {
        std::cerr<<error.what()<<'\n';
        return 1;
    }
}
