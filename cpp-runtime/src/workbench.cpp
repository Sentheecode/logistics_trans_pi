#include "workbench.hpp"
#include "control.hpp"
#include "point_audit.hpp"
#include "schematic.hpp"
#include <QApplication>
#include <QColor>
#include <QComboBox>
#include <QDateTime>
#include <QDialog>
#include <QFrame>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QLabel>
#include <QMessageBox>
#include <QPushButton>
#include <QShowEvent>
#include <QSignalBlocker>
#include <QSizePolicy>
#include <QSplitter>
#include <QStatusBar>
#include <QTabWidget>
#include <QTableWidget>
#include <QTimeZone>
#include <QTimer>
#include <QVBoxLayout>
#include <algorithm>
#include <iostream>
#include <stdexcept>

namespace {
QString stateText(const std::string& value) {
    if (value=="waiting") return "等待扫码";
    if (value=="scanning") return "扫码中";
    if (value=="sorting") return "分拣中";
    if (value=="station_complete") return "工位完成";
    if (value=="blocked") return "模拟阻塞";
    return "状态未知";
}
QString eventText(const std::string& code) {
    if (code=="SIM_TASK_BLOCKED") return "模拟异常：小车 02 阻塞";
    if (code=="SIM_BAD_QUALITY") return "源数据质量异常，值不可用";
    if (code=="SIM_LINK_DOWN" || code=="SIM_MANUAL_DISCONNECT") return "模拟链路断开，值不可用";
    if (code=="SIM_LINK_RECOVERED") return "模拟链路恢复";
    if (code=="SIM_ALARM_CLEARED") return "模拟阻塞解除";
    if (code=="SIM_STATION_COMPLETE") return "本轮模拟工位完成";
    return "模拟任务阶段更新";
}
QString commandFeedback(const std::string& result) {
    if (result=="ok") return "采集端已执行，正在核对最新状态…";
    if (result=="stale_revision" || result=="session_changed") return "操作未执行：采集端状态已变化，请核对后重新操作。";
    if (result=="manual_required") return "操作未执行：请先切换到手动模式。";
    if (result=="expired") return "操作未执行：请求已过期。";
    return "采集端拒绝操作："+QString::fromStdString(result);
}
void fit(QLabel* label) {
    label->setWordWrap(false);
    label->setMinimumWidth(label->fontMetrics().horizontalAdvance(label->text())+8);
}
QLabel* fieldLabel(const QString& text, QWidget* parent) {
    auto* label=new QLabel(text, parent);
    label->setObjectName("fieldLabel");
    return label;
}
void prepareTable(QTableWidget* table, const QStringList& headers) {
    table->setColumnCount(headers.size());
    table->setHorizontalHeaderLabels(headers);
    table->setEditTriggers(QAbstractItemView::NoEditTriggers);
    table->setSelectionMode(QAbstractItemView::NoSelection);
    table->verticalHeader()->hide();
    table->verticalHeader()->setDefaultSectionSize(22);
    table->horizontalHeader()->setStretchLastSection(true);
    table->horizontalHeader()->setSectionResizeMode(0, QHeaderView::ResizeToContents);
    table->setWordWrap(false);
}
void setRows(QTableWidget* table, const std::vector<std::pair<QString, QString>>& rows) {
    table->setRowCount(static_cast<int>(rows.size()));
    for (int i=0; i<static_cast<int>(rows.size()); ++i) {
        auto* key=new QTableWidgetItem(rows[i].first);
        auto* value=new QTableWidgetItem(rows[i].second);
        key->setFlags(Qt::ItemIsEnabled);
        value->setFlags(Qt::ItemIsEnabled);
        value->setToolTip(rows[i].second);
        table->setItem(i, 0, key);
        table->setItem(i, 1, value);
    }
}
QString joinedIds(const std::vector<const catalog::Station*>& rows) {
    QStringList parts;
    for (auto* row : rows) parts.push_back(QString::fromStdString(row->id));
    return parts.isEmpty() ? "未配置" : parts.join("、");
}
QString joinedNumbers(const std::vector<const catalog::Station*>& rows, const std::string& current) {
    if (rows.empty()) return "无工位";
    QStringList parts;
    for (auto* row : rows) {
        auto text=QString::fromStdString(row->name);
        if (row->id==current) text += " 当前";
        parts.push_back(text);
    }
    return parts.join("  ·  ");
}
void markCart(QFrame* frame, const QString& state) {
    frame->setProperty("state", state);
    frame->style()->unpolish(frame);
    frame->style()->polish(frame);
}
CartMark cartMark(const std::string& value) {
    if (value=="waiting") return CartMark::wait;
    if (value=="scanning" || value=="sorting") return CartMark::run;
    if (value=="station_complete") return CartMark::done;
    return CartMark::alert;
}
QString cardState(const std::string& value) {
    const auto mark=cartMark(value);
    if (mark==CartMark::wait) return "wait";
    if (mark==CartMark::done) return "done";
    if (mark==CartMark::alert) return "alert";
    return "run";
}
QString beijingTime(qint64 epochMs, const QString& pattern) {
    const QTimeZone zone("Asia/Shanghai");
    const QDateTime utc=QDateTime::fromMSecsSinceEpoch(epochMs, Qt::UTC);
    if (!zone.isValid()) return utc.toString(pattern)+" UTC";
    return utc.toTimeZone(zone).toString(pattern)+" 北京";
}
}

Workbench::Workbench(std::filesystem::path directory)
    : directory_(std::move(directory)), catalog_(catalog::Directory::qiantang()) {
    const auto error=catalog::Directory::validate(catalog_);
    if (!error.empty()) throw std::runtime_error("catalog fixture: "+error);
    QApplication::setStyle("Fusion");
    setWindowTitle("钱塘二厂 · 模拟工作台");
    resize(1280, 800);
    setMinimumSize(1100, 650);
    setStyleSheet(R"(
        QMainWindow, QWidget#centralRoot { background:#eef1f4; color:#1c2430; font-size:13px; }
        QWidget#identityBar, QWidget#catalogPane { background:#f7f8fa; }
        QWidget#identityBar { border-bottom:1px solid #d5dbe3; }
        QWidget#catalogPane { border-right:1px solid #d5dbe3; }
        QLabel#title { font-size:16px; font-weight:600; color:#1c2430; }
        QLabel#fieldLabel, QLabel#section { color:#5c6b7c; font-size:12px; }
        QLabel#stationTitle { font-size:20px; font-weight:600; color:#1c2430; }
        QLabel#muted { color:#5c6b7c; font-size:12px; }
        QLabel#phase { font-size:18px; font-weight:600; color:#1c2430; }
        QFrame#cart { background:#ffffff; border:1px solid #d5dbe3; border-left:4px solid #1d4e89; }
        QFrame#cart[state="wait"] { border-left:4px solid #b7c0cc; }
        QFrame#cart[state="done"] { border-left:4px solid #1c2430; }
        QFrame#cart[state="alert"] { background:#fbf6ee; border-left:4px solid #8a5a12; }
        QComboBox { background:#ffffff; border:1px solid #c5ced8; padding:2px 8px; min-height:26px; }
        QComboBox:hover { border-color:#1d4e89; }
        QPushButton { background:#ffffff; border:1px solid #c5ced8; padding:4px 10px; min-height:28px; }
        QPushButton#primary { background:#1d4e89; color:#ffffff; border:1px solid #1d4e89; }
        QPushButton:disabled, QPushButton#primary:disabled { background:#eef1f4; color:#8b96a6; border:1px solid #e1e6ee; }
        QTableWidget { background:#ffffff; alternate-background-color:#f7f8fa; gridline-color:#e6ebf0; font-size:12px; }
        QHeaderView::section { background:#f7f8fa; color:#5c6b7c; border:none; border-bottom:1px solid #d5dbe3; padding:4px 6px; font-size:12px; }
        QTabWidget::pane { border:none; border-top:1px solid #d5dbe3; background:#ffffff; }
        QTabBar::tab { background:#f7f8fa; color:#5c6b7c; padding:6px 14px; border:none; border-bottom:2px solid transparent; }
        QTabBar::tab:selected { color:#1c2430; border-bottom:2px solid #1d4e89; background:#ffffff; }
        QStatusBar { background:#f7f8fa; color:#5c6b7c; border-top:1px solid #d5dbe3; }
        QSplitter::handle { background:#d5dbe3; }
    )");

    auto* identity=new QWidget;
    identity->setObjectName("identityBar");
    identity->setFixedHeight(44);
    auto* identityLayout=new QHBoxLayout(identity);
    identityLayout->setContentsMargins(12, 6, 12, 6);
    identityLayout->setSpacing(12);
    auto* title=new QLabel("物流工作台");
    title->setObjectName("title");
    fit(title);
    auto* sim=new QLabel("模拟环境");
    sim->setObjectName("simBadge");
    sim->setStyleSheet("background:#1d4e89;color:#ffffff;padding:3px 8px;font-weight:600;");
    sim->setToolTip("仅本地合成数据。不表示 PLC、LWMS、华睿或库卡已连接。");
    fit(sim);
    processLabel_=new QLabel("采集心跳 待确认");
    processLabel_->setObjectName("processState");
    runtimeLabel_=new QLabel("上位机 未知");
    runtimeLabel_->setObjectName("runtimeState");
    heartbeatLabel_=new QLabel("无心跳");
    heartbeatLabel_->setObjectName("heartbeatState");
    fit(processLabel_); fit(runtimeLabel_); fit(heartbeatLabel_);
    identityLayout->addWidget(title);
    identityLayout->addWidget(sim);
    identityLayout->addStretch(1);
    identityLayout->addWidget(processLabel_);
    identityLayout->addWidget(runtimeLabel_);
    identityLayout->addWidget(heartbeatLabel_);

    auto* rail=new QWidget;
    rail->setObjectName("catalogPane");
    rail->setMinimumWidth(232);
    rail->setMaximumWidth(260);
    auto* railLayout=new QVBoxLayout(rail);
    railLayout->setContentsMargins(12, 12, 12, 12);
    railLayout->setSpacing(4);
    auto addCombo=[&](const QString& name, QComboBox** slot, const QString& objectName) {
        auto* label=fieldLabel(name, rail);
        *slot=new QComboBox(rail);
        (*slot)->setObjectName(objectName);
        (*slot)->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);
        label->setBuddy(*slot);
        railLayout->addWidget(label);
        railLayout->addWidget(*slot);
        railLayout->addSpacing(6);
    };
    auto* directoryHeading=fieldLabel("目录", rail);
    directoryHeading->setObjectName("section");
    railLayout->addWidget(directoryHeading);
    addCombo("工厂", &factoryCombo_, "factoryCombo");
    addCombo("区域", &areaCombo_, "areaCombo");
    addCombo("PLC", &plcCombo_, "plcCombo");
    auto* commandPane=new QWidget(rail);
    commandPane->setObjectName("commandPane");
    auto* commandLayout=new QVBoxLayout(commandPane);
    commandLayout->setContentsMargins(0, 8, 0, 0);
    commandLayout->setSpacing(6);
    auto* commandHeading=fieldLabel("模拟操作", commandPane);
    commandHeading->setObjectName("section");
    commandLayout->addWidget(commandHeading);
    modeButton_=new QPushButton("模式待确认");
    modeButton_->setObjectName("modeButton");
    step_=new QPushButton("单步推进");
    step_->setObjectName("primary");
    disconnect_=new QPushButton("模拟断链");
    disconnect_->setObjectName("disconnectButton");
    auto* stepRow=new QHBoxLayout;
    stepRow->setSpacing(6);
    stepRow->addWidget(step_, 1);
    stepRow->addWidget(disconnect_, 1);
    controlReason_=new QLabel;
    controlReason_->setObjectName("controlReason");
    controlReason_->setWordWrap(true);
    feedback_=new QLabel;
    feedback_->setObjectName("controlFeedback");
    feedback_->setWordWrap(true);
    commandLayout->addWidget(modeButton_);
    commandLayout->addLayout(stepRow);
    commandLayout->addWidget(controlReason_);
    commandLayout->addWidget(feedback_);
    railLayout->addWidget(commandPane);
    railLayout->addStretch(1);

    schematic_=new SchematicView;
    schematic_->setMinimumHeight(220);
    schematic_->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Expanding);
    cardRow_=new QWidget;
    cardRow_->setFixedHeight(46);
    new QHBoxLayout(cardRow_);
    cardRow_->layout()->setContentsMargins(8, 4, 8, 4);
    stationTitle_=new QLabel("未配置");
    stationTitle_->setObjectName("stationTitle");
    fit(stationTitle_);
    stationMeta_=new QLabel;
    stationMeta_->setObjectName("muted");
    stationMeta_->setWordWrap(true);
    configLine_=new QLabel;
    configLine_->setObjectName("muted");
    configLine_->setWordWrap(true);
    lineState_=new QLabel("线体：未配置，不按 PLC 推断");
    lineState_->setWordWrap(true);
    peerLine_=new QLabel;
    peerLine_->setWordWrap(true);
    interfaceLine_=new QLabel;
    interfaceLine_->setWordWrap(true);
    plcAuto_=new QLabel("PLC 自动：未读取");
    plcAuto_->setObjectName("muted");
    fit(plcAuto_);
    cartStrip_=new QWidget;
    cartStrip_->setFixedHeight(64);
    auto* cartLayout=new QHBoxLayout(cartStrip_);
    cartLayout->setContentsMargins(0, 0, 0, 0);
    cartLayout->setSpacing(8);
    for (int i=0; i<3; ++i) {
        cartFrame_[i]=new QFrame;
        cartFrame_[i]->setObjectName("cart");
        auto* layout=new QVBoxLayout(cartFrame_[i]);
        layout->setContentsMargins(10, 8, 10, 8);
        layout->setSpacing(2);
        auto* index=new QLabel(QString("小车 %1").arg(i+1));
        index->setObjectName("fieldLabel");
        cartPhase_[i]=new QLabel("状态未知");
        cartPhase_[i]->setObjectName("phase");
        cartNote_[i]=new QLabel;
        cartNote_[i]->setWordWrap(false);
        cartNote_[i]->setObjectName("muted");
        layout->addWidget(index);
        layout->addWidget(cartPhase_[i]);
        layout->addWidget(cartNote_[i]);
        cartLayout->addWidget(cartFrame_[i], 1);
    }
    scopeBanner_=new QLabel;
    scopeBanner_->setObjectName("scopeBanner");
    scopeBanner_->setWordWrap(true);
    auto* stage=new QWidget;
    auto* stageLayout=new QVBoxLayout(stage);
    stageLayout->setContentsMargins(12, 8, 12, 8);
    stageLayout->setSpacing(4);
    stageLayout->addWidget(stationTitle_);
    stageLayout->addWidget(stationMeta_);
    stageLayout->addWidget(interfaceLine_);
    stageLayout->addWidget(cartStrip_);
    stageLayout->addWidget(scopeBanner_);
    configLine_->hide();
    lineState_->hide();
    peerLine_->hide();
    plcAuto_->hide();
    stage->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Minimum);
    auto* diagram=new QWidget;
    auto* diagramLayout=new QVBoxLayout(diagram);
    diagramLayout->setContentsMargins(8, 4, 8, 4);
    diagramLayout->setSpacing(4);
    diagramLayout->addWidget(schematic_, 1);
    diagramLayout->addWidget(cardRow_);
    diagramLayout->addWidget(stage);

    detailTable_=new QTableWidget;
    detailTable_->setObjectName("detailTable");
    prepareTable(detailTable_, {"字段", "值"});
    peerTable_=new QTableWidget;
    peerTable_->setObjectName("peerTable");
    prepareTable(peerTable_, {"工位 ID", "工位号", "标记"});
    stageTable_=new QTableWidget(this);
    stageTable_->setObjectName("stageTable");
    prepareTable(stageTable_, {"小车", "阶段", "说明"});
    stageTable_->hide();
    auto* identityPage=new QWidget;
    auto* identityPageLayout=new QVBoxLayout(identityPage);
    identityPageLayout->setContentsMargins(8, 8, 8, 8);
    identityPageLayout->addWidget(detailTable_, 1);
    identityPageLayout->addWidget(peerTable_);

    pointTable_=new QTableWidget;
    pointTable_->setObjectName("pointAuditTable");
    prepareTable(pointTable_, {"来源", "段", "名称", "地址", "类型", "方向", "疑点"});
    pointScope_=new QLabel;
    pointScope_->setWordWrap(true);
    auto* pointPage=new QWidget;
    auto* pointLayout=new QVBoxLayout(pointPage);
    pointLayout->setContentsMargins(8, 8, 8, 8);
    pointLayout->addWidget(pointScope_);
    pointLayout->addWidget(pointTable_, 1);

    auto* boundary=new QLabel(
        "已确认的是业务意图，不是已实现的现场协议。\n"
        "分拣：上位机让 PLC 扫料车码，再用二维码向 LWMS 查 SPS 单；首台机械臂扫码，后续机械臂共享，但共享成员未配置。分拣完成回传物料明细。PLC 请求补料不等于 LWMS 已确认完成；库位、箱、料或订单缺失时标为待补映射。\n"
        "排序：华睿 AGV 会下发单、箱、料、数量，以及汇总/严控、正序/倒序。完成数量、内衬料架阈值和通知报文都未定。\n"
        "库卡通行要经过请求进入、同意、进入完成、请求离开、离开完成并到安全点。不能用设备运行或心跳直接放行，也不能把进/出完成提前当成安全点。\n"
        "140 点表只属于工位 140，不能套到其他工位。方向冲突和 DBX 上的 int/string 只作审计，不能写成真实地址。");
    boundary->setWordWrap(true);
    boundary->setObjectName("boundaryNote");
    auto* boundaryPage=new QWidget;
    auto* boundaryLayout=new QVBoxLayout(boundaryPage);
    boundaryLayout->setContentsMargins(12, 12, 12, 12);
    boundaryLayout->addWidget(boundary);
    boundaryLayout->addStretch(1);

    historyTable_=new QTableWidget;
    historyTable_->setObjectName("historyTable");
    prepareTable(historyTable_, {"序号", "接收时间 / 北京时间", "三车阶段", "数据质量", "事件"});
    historyTable_->setSelectionBehavior(QAbstractItemView::SelectRows);
    historyTable_->setSelectionMode(QAbstractItemView::SingleSelection);
    historyTable_->horizontalHeader()->setSectionResizeMode(QHeaderView::Stretch);
    historyTable_->horizontalHeader()->setSectionResizeMode(0, QHeaderView::ResizeToContents);
    historyTable_->setMinimumHeight(72);
    eventNote_=new QLabel("无实时历史");
    eventNote_->setObjectName("eventNote");
    eventNote_->setWordWrap(true);
    auto* historyPage=new QWidget;
    auto* historyLayout=new QVBoxLayout(historyPage);
    historyLayout->setContentsMargins(8, 6, 8, 6);
    historyLayout->setSpacing(4);
    historyLayout->addWidget(eventNote_);
    historyLayout->addWidget(historyTable_, 1);
    auto* tabs=new QTabWidget;
    tabs->setObjectName("detailTabs");
    tabs->addTab(historyPage, "历史");
    tabs->addTab(identityPage, "标识");
    tabs->addTab(pointPage, "点表");
    tabs->addTab(boundaryPage, "边界");
    auto* records=new QDialog(this);
    records->setWindowTitle("记录、点表与边界");
    records->resize(880, 460);
    auto* recordLayout=new QVBoxLayout(records);
    recordLayout->addWidget(tabs);
    records->hide();
    auto* openRecords=new QPushButton("记录 / 点表");
    openRecords->setObjectName("openRecords");
    commandLayout->addWidget(openRecords);
    connect(openRecords, &QPushButton::clicked, records, &QDialog::show);

    mainSplit_=new QSplitter(Qt::Horizontal);
    mainSplit_->setObjectName("mainSplit");
    mainSplit_->setChildrenCollapsible(false);
    mainSplit_->addWidget(rail);
    mainSplit_->addWidget(diagram);
    mainSplit_->setStretchFactor(0, 0);
    mainSplit_->setStretchFactor(1, 1);

    auto* central=new QWidget;
    central->setObjectName("centralRoot");
    auto* root=new QVBoxLayout(central);
    root->setContentsMargins(0, 0, 0, 0);
    root->setSpacing(0);
    root->addWidget(identity);
    root->addWidget(mainSplit_, 1);
    setCentralWidget(central);
    auto* closure=new QLabel("关窗不停止采集");
    fit(closure);
    auto* isolated=new QLabel("未连接现场设备");
    fit(isolated);
    statusBar()->addWidget(isolated);
    statusBar()->addPermanentWidget(closure);

    connect(schematic_, &SchematicView::stationActivated, this, [this](const QString& id) { activateSchematicStation(id); });
    connect(factoryCombo_, qOverload<int>(&QComboBox::currentIndexChanged), this, [this](int) {
        if (updating_) return;
        auto next=selection_;
        next.factory_id=comboId(factoryCombo_).toStdString();
        setSelection(next);
    });
    connect(areaCombo_, qOverload<int>(&QComboBox::currentIndexChanged), this, [this](int) {
        if (updating_) return;
        auto next=selection_;
        next.area_id=comboId(areaCombo_).toStdString();
        setSelection(next);
    });
    connect(plcCombo_, qOverload<int>(&QComboBox::currentIndexChanged), this, [this](int) {
        if (updating_) return;
        auto next=selection_;
        next.plc_id=comboId(plcCombo_).toStdString();
        setSelection(next);
    });
    connect(step_, &QPushButton::clicked, this, [this] { command("step"); });
    connect(disconnect_, &QPushButton::clicked, this, [this] { command("disconnect"); });
    connect(modeButton_, &QPushButton::clicked, this, [this] {
        if (view_.runtime.mode=="manual") {
            QMessageBox confirm(QMessageBox::Question, "恢复自动模拟", "恢复后，模拟任务将按采集器节奏连续推进。\n请确认当前调试已结束。", QMessageBox::Yes|QMessageBox::No, this);
            confirm.setObjectName("confirmAutomatic");
            confirm.setDefaultButton(QMessageBox::No);
            confirm.button(QMessageBox::Yes)->setText("确认恢复自动");
            confirm.button(QMessageBox::No)->setText("继续手动调试");
            if (confirm.exec()!=QMessageBox::Yes) return;
            command("automatic_confirmed");
        } else command("manual");
    });
    setSelection(catalog_.live_selection());
    QTimer::singleShot(0, this, [this] {
        if (!split_applied_ && mainSplit_->width()>480) {
            split_applied_=true;
            mainSplit_->setSizes({248, mainSplit_->width()-248});
        }
    });
    auto* timer=new QTimer(this);
    connect(timer, &QTimer::timeout, this, [this] { refresh(); });
    timer->start(100);
    buttons();
}

void Workbench::showEvent(QShowEvent* event) {
    QMainWindow::showEvent(event);
    if (split_applied_) return;
    const int width=mainSplit_->width();
    if (width<480) return;
    split_applied_=true;
    mainSplit_->setSizes({248, width-248});
}

void Workbench::fillCombo(QComboBox* box, const std::vector<std::pair<QString, QString>>& items, const QString& selected) {
    QSignalBlocker blocker(box);
    box->clear();
    if (items.empty()) {
        if (!selected.isEmpty()) throw std::logic_error("combo selection without items");
        box->addItem("未配置", QString());
        box->setToolTip("未配置");
        return;
    }
    int index=-1;
    for (int i=0; i<static_cast<int>(items.size()); ++i) {
        box->addItem(items[i].second, items[i].first);
        box->setItemData(i, items[i].first, Qt::ToolTipRole);
        if (items[i].first==selected) index=i;
    }
    if (index<0) throw std::logic_error("combo missing selection");
    box->setCurrentIndex(index);
    box->setToolTip(selected);
}
QString Workbench::comboId(const QComboBox* box) { return box ? box->currentData().toString() : QString(); }

void Workbench::refill() {
    std::vector<std::pair<QString, QString>> factories, areas, plcs;
    for (auto& row : catalog_.factories) factories.emplace_back(QString::fromStdString(row.id), QString::fromStdString(row.name));
    for (auto* row : catalog_.areas_of(selection_.factory_id)) areas.emplace_back(QString::fromStdString(row->id), QString::fromStdString(row->name));
    for (auto* row : catalog_.plcs_of(selection_.area_id)) plcs.emplace_back(QString::fromStdString(row->id), QString::fromStdString(row->name));
    fillCombo(factoryCombo_, factories, QString::fromStdString(selection_.factory_id));
    fillCombo(areaCombo_, areas, QString::fromStdString(selection_.area_id));
    fillCombo(plcCombo_, plcs, QString::fromStdString(selection_.plc_id));
}

void Workbench::rebuildCards() {
    auto* layout=qobject_cast<QHBoxLayout*>(cardRow_->layout());
    if (!layout) return;
    while (QLayoutItem* item=layout->takeAt(0)) { delete item->widget(); delete item; }
    for (auto* station : catalog_.stations_of(selection_.plc_id)) {
        auto* button=new QPushButton(QString::fromStdString(station->name), cardRow_);
        button->setCheckable(true);
        button->setProperty("stationId", QString::fromStdString(station->id));
        button->setObjectName("stationCard");
        button->setMinimumHeight(28);
        layout->addWidget(button);
        const auto id=station->id;
        connect(button, &QPushButton::clicked, this, [this, id]{ activateSchematicStation(QString::fromStdString(id)); });
    }
    layout->addStretch(1);
}

void Workbench::setSelection(catalog::Selection selection) {
    if (updating_) return;
    updating_=true;
    selection_=catalog_.reconcile(std::move(selection));
    refill();
    if (cards_plc_!=selection_.plc_id) { cards_plc_=selection_.plc_id; rebuildCards(); }
    updating_=false;
    render();
    buttons();
}

void Workbench::activateSchematicStation(const QString& id) {
    const auto key=id.toStdString();
    if (auto selection=catalog_.selection_for_station(key)) { setSelection(*selection); return; }
    if (auto selection=catalog_.selection_for_plc(key)) setSelection(*selection);
}

void Workbench::prepareCapture(const QString& kind) {
    if (kind=="empty") {
        if (auto selection=catalog_.selection_for_plc(catalog_.empty_plc_id())) setSelection(*selection);
        return;
    }
    if (kind=="south") { activateSchematicStation(QString::fromUtf8(catalog::kStation20)); return; }
    if (kind=="meter2") { activateSchematicStation(QString::fromUtf8(catalog::kStation40)); return; }
    if (kind=="sort") { activateSchematicStation(QString::fromUtf8(catalog::kStation210)); return; }
    if (kind=="peers") { activateSchematicStation(QString::fromUtf8(catalog::kStation150)); return; }
    if (!catalog_.live_station_id().empty()) activateSchematicStation(QString::fromStdString(catalog_.live_station_id()));
}

void Workbench::buttons() {
    const bool live=liveScope();
    const bool pending=command_.valid() || waiting_confirmation_;
    const bool usable=available_ && view_.runtime.liveness=="recent" && live;
    const bool manual=view_.runtime.mode=="manual";
    modeButton_->setEnabled(usable && !pending);
    step_->setEnabled(usable && manual && !pending);
    disconnect_->setEnabled(usable && manual && !pending);
    modeButton_->setText(!usable ? "模式不可用" : manual ? "恢复自动…" : "切换手动");
    modeButton_->setStyleSheet(usable && !manual && !pending ? "background:#1d4e89;color:white;border:1px solid #1d4e89;" : "");
    QString reason;
    if (selection_.station_id.empty()) reason="未配置工位，不能控制";
    else if (!live) reason="当前工位没有挂接模拟帧，不能控制";
    else if (!usable) reason="采集心跳不可用，控制已禁用";
    else if (pending) reason="正在等待采集端确认，请勿重复操作";
    else if (manual) reason="手动待步：只有点击才推进";
    else reason="自动运行中：单步与断链只在手动可用";
    controlReason_->setText(reason);
    modeButton_->setToolTip(reason);
    step_->setToolTip(reason);
    disconnect_->setToolTip(reason);
}

void Workbench::command(const std::string& name) {
    if (command_.valid() || waiting_confirmation_ || !available_ || view_.runtime.liveness!="recent" || !liveScope()) return;
    const auto state=view_.runtime;
    command_revision_=state.revision;
    command_=std::async(std::launch::async, [dir=directory_, state, name] { return sim::send_control(dir, state.boot, state.revision, name); });
    feedback_->setText("请求已发送，模式以采集端确认结果为准…");
    buttons();
}

void Workbench::refresh() {
    if (command_.valid() && command_.wait_for(std::chrono::milliseconds(0))==std::future_status::ready) {
        try { auto result=command_.get(); feedback_->setText(commandFeedback(result)); waiting_confirmation_=result=="ok"; }
        catch (const std::exception&) { feedback_->setText("控制端未确认：可能未执行或结果未知。请刷新状态后核对，不会自动重试。"); }
    }
    if (read_.valid() && read_.wait_for(std::chrono::milliseconds(0))==std::future_status::ready) {
        try { display(read_.get()); }
        catch (const std::exception&) { unavailable("无法读取本地采集数据，请先独立启动模拟采集器。"); }
    }
    if (!read_.valid()) read_=std::async(std::launch::async, [dir=directory_] { return sim::read(dir, "recent", 40); });
    buttons();
}

void Workbench::display(const sim::View& view) {
    view_=view;
    available_=view.runtime.liveness=="recent";
    ++observations_;
    if (waiting_confirmation_ && (view.runtime.revision>command_revision_ || !available_)) {
        waiting_confirmation_=false;
        feedback_->setText(available_ ? "操作完成 · 已核对采集端状态" : "操作后心跳中断，请核对采集端状态。");
    }
    if (liveScope() && !view.rows.empty()) {
        const auto& row=view.rows.front();
        if (row.seq!=last_seq_ || view.runtime.mode!=last_mode_) {
            last_seq_=row.seq;
            last_mode_=view.runtime.mode;
            std::cout<<"render seq="<<row.seq<<" alarm="<<row.alarm<<" quality="<<row.quality<<" link="<<row.link<<" mode="<<view.runtime.mode<<" process="<<view.runtime.liveness<<'\n'<<std::flush;
        }
    }
    render();
    buttons();
}

void Workbench::render() {
    const bool live=liveScope();
    const bool station_selected=!selection_.station_id.empty();
    QString process="采集心跳 不可用";
    if (available_ && view_.runtime.liveness=="recent") process="采集心跳 正常";
    else if (view_.runtime.liveness=="clock_uncertain") process="采集时钟不确定";
    else if (view_.runtime.liveness=="stale") process="采集心跳 过期";
    else if (observations_==0) process="采集心跳 待确认";
    processLabel_->setText(process);
    fit(processLabel_);
    QString runtime="上位机 未知";
    if (available_ && view_.runtime.mode=="manual") runtime="上位机 手动";
    else if (available_ && view_.runtime.mode=="automatic") runtime="上位机 自动";
    runtimeLabel_->setText(runtime);
    fit(runtimeLabel_);
    heartbeatLabel_->setText(view_.runtime.heartbeat_ms>0 ? "心跳 "+beijingTime(view_.runtime.heartbeat_ms, "HH:mm:ss") : "无心跳");
    fit(heartbeatLabel_);

    const catalog::Station* station=catalog_.find_station(selection_.station_id);
    const catalog::Area* area=station ? catalog_.area_of_station(station->id) : catalog_.find_area(selection_.area_id);
    const catalog::Plc* plc=station ? catalog_.plc_of_station(station->id) : catalog_.find_plc(selection_.plc_id);
    QString runtimeValue="未知";
    if (available_ && view_.runtime.mode=="manual") runtimeValue="手动";
    else if (available_ && view_.runtime.mode=="automatic") runtimeValue="自动";
    std::vector<const catalog::Station*> peer_rows;
    if (plc) peer_rows=catalog_.stations_of(plc->id);
    const QString peers=joinedIds(peer_rows);
    const bool good=live && available_ && !view_.rows.empty() && view_.rows.front().quality=="good" && view_.freshness!="clock_uncertain" && (view_.runtime.mode=="manual" || view_.freshness=="recent");
    QString sim_quality="未配置";
    QString scope="未配置";
    if (station_selected && !live) { sim_quality="未接入"; scope="未接入"; }
    else if (live) {
        scope="模拟流 "+QString::fromStdString(catalog::kStream);
        sim_quality=view_.rows.empty() || !available_ ? "未知" : good ? "正常" : "不可用";
    }
    stationTitle_->setText(station ? "工位 "+QString::fromStdString(station->name) : "未配置");
    fit(stationTitle_);
    stationMeta_->setText(area && plc ? QString::fromStdString(area->name+" · "+plc->name)+" · 线体未配置 · PLC自动未读取" : "未配置");
    configLine_->hide();
    lineState_->hide();
    plcAuto_->hide();
    peerLine_->setText(plc ? "同一 PLC  "+joinedNumbers(peer_rows, selection_.station_id) : "无 PLC");
    interfaceLine_->setText(area && area->code=="seq"
        ? "华睿：未连接 · 汇总/严控与完成协议未定 · 不填假数量"
        : "库卡：未授权通行 · LWMS：未连接 · 库位映射待补");
    const bool carts_known=available_ && !view_.rows.empty() && view_.rows.front().quality=="good" && view_.freshness!="clock_uncertain" && (view_.runtime.mode=="manual" || view_.freshness=="recent");
    std::vector<StationGlyph> glyphs;
    for (auto* item : catalog_.stations_of(selection_.plc_id)) {
        StationGlyph glyph;
        glyph.id=item->id;
        glyph.number=QString::fromStdString(item->name);
        glyph.live=catalog_.is_live(item->id);
        if (glyph.live && !view_.rows.empty()) {
            glyph.carts_known=true;
            for (int index=0; index<3; ++index) {
                const auto raw=carts_known ? view_.rows.front().carts[index] : "UNKNOWN";
                glyph.cart[index]=carts_known ? stateText(raw) : QString("状态未知");
                glyph.cart_mark[index]=cartMark(raw);
            }
        }
        glyphs.push_back(glyph);
    }
    schematic_->setScene(plc ? QString::fromStdString(plc->name) : QString(), area ? QString::fromStdString(area->code) : QString(), glyphs, selection_.station_id);
    if (cardRow_) for (auto* button : cardRow_->findChildren<QPushButton*>()) button->setChecked(button->property("stationId").toString().toStdString()==selection_.station_id);
    setRows(detailTable_, {
        {"工位", station ? QString::fromStdString(station->id+" "+station->name) : "未配置"},
        {"业务模式", area ? QString::fromStdString(area->name) : "未配置"},
        {"运行模式", runtimeValue},
        {"实时范围", scope},
        {"模拟质量", sim_quality},
        {"设备连接", "未探测"},
        {"库存", "未知"},
        {"控制器", plc ? QString::fromStdString(plc->id+" "+plc->name) : "未配置"},
        {"关联工位", peers},
        {"库区", "未配置"},
        {"库位", "待补映射"},
    });
    peerTable_->setColumnCount(3);
    peerTable_->setRowCount(static_cast<int>(peer_rows.size()));
    for (int i=0; i<static_cast<int>(peer_rows.size()); ++i) {
        const bool current=peer_rows[i]->id==selection_.station_id;
        QStringList columns={QString::fromStdString(peer_rows[i]->id), QString::fromStdString(peer_rows[i]->name), current ? "当前" : "关联"};
        for (int column=0; column<columns.size(); ++column) {
            auto* item=new QTableWidgetItem(columns[column]);
            item->setFlags(Qt::ItemIsEnabled | Qt::ItemIsSelectable);
            item->setToolTip(columns[column]);
            peerTable_->setItem(i, column, item);
        }
    }
    const auto audit_rows=audit::for_station(selection_.station_id);
    pointScope_->setText(audit_rows.empty() ? "此工位未提供点表，不套用 140 模板。" : "只展示工位 140 的来源点表。重复项保留，冲突已标记，没有任何可执行写地址。");
    pointTable_->setRowCount(static_cast<int>(audit_rows.size()));
    for (int i=0; i<static_cast<int>(audit_rows.size()); ++i) {
        const auto& row=audit_rows[static_cast<std::size_t>(i)];
        QStringList columns={
            QString::fromStdString(row.source_row),
            QString::fromStdString(row.section),
            row.name.empty() ? "（空白名）" : QString::fromStdString(row.name),
            QString::fromStdString(row.address),
            QString::fromStdString(row.type_text),
            QString::fromStdString(row.direction_text),
            QString::fromStdString(row.doubts),
        };
        for (int column=0; column<columns.size(); ++column) {
            auto* item=new QTableWidgetItem(columns[column]);
            item->setFlags(Qt::ItemIsEnabled);
            item->setToolTip(columns[column]);
            if (!row.doubts.empty() && column==6) item->setForeground(QColor("#8a5a12"));
            pointTable_->setItem(i, column, item);
        }
    }

    if (!live) {
        stageTable_->setRowCount(0);
        stageTable_->hide();
        cartStrip_->hide();
        historyTable_->setRowCount(0);
        eventNote_->setText(station_selected ? "无实时历史" : "未配置");
        scopeBanner_->setText(station_selected
            ? "未挂接模拟帧。同一 PLC 的采集不复制到每个工位，也不显示工位 140 的三车数据。"
            : "未配置：这个 PLC 还没有工位，不能控制。");
        return;
    }
    cartStrip_->show();
    if (view_.rows.empty()) {
        for (int i=0; i<3; ++i) {
            cartPhase_[i]->setText("状态未知");
            cartNote_[i]->setText("等待模拟帧");
            markCart(cartFrame_[i], "wait");
        }
        stageTable_->setRowCount(0);
        historyTable_->setRowCount(0);
        eventNote_->setText("尚无历史");
        scopeBanner_->setText("等待模拟帧。库存未知，设备未探测，采集心跳不是 PLC 在线。");
        return;
    }
    const auto& latest=view_.rows.front();
    stageTable_->setRowCount(3);
    for (int i=0; i<3; ++i) {
        auto value=good ? latest.carts[i] : "UNKNOWN";
        QString note=value=="station_complete" ? "本工位本轮步骤结束" : value=="waiting" ? "等待前序小车" : value=="blocked" ? "模拟阻塞仍活动" : value=="UNKNOWN" ? "无可信值，不补零" : "模拟阶段已确认";
        cartPhase_[i]->setText(stateText(value));
        cartNote_[i]->setText(note);
        markCart(cartFrame_[i], cardState(value));
        QStringList columns={QString("小车%1").arg(i+1), stateText(value), note};
        for (int column=0; column<columns.size(); ++column) stageTable_->setItem(i, column, new QTableWidgetItem(columns[column]));
    }
    stageTable_->hide();
    historyTable_->setRowCount(static_cast<int>(view_.rows.size()));
    for (int i=0; i<static_cast<int>(view_.rows.size()); ++i) {
        const auto& row=view_.rows[i];
        QStringList columns={
            QString::number(row.seq),
            beijingTime(row.received_ms, "HH:mm:ss.zzz"),
            stateText(row.carts[0])+" / "+stateText(row.carts[1])+" / "+stateText(row.carts[2]),
            row.quality=="good" ? "正常" : "不可用",
            eventText(row.event),
        };
        for (int column=0; column<columns.size(); ++column) {
            auto* item=new QTableWidgetItem(columns[column]);
            item->setToolTip(columns[column]);
            if (row.quality!="good" && column==3) item->setForeground(QColor("#8a5a12"));
            historyTable_->setItem(i, column, item);
        }
    }
    eventNote_->setText(QString("最近 %1 帧 · 覆盖未知 · 北京时间 UTC+8").arg(view_.rows.size()));
    const auto age=std::max<std::int64_t>(0, (sim::now_ms()-latest.received_ms)/1000);
    if (!good) scopeBanner_->setText(!available_ ? "采集心跳不可用。历史不能当作当前状态。" : latest.reason=="disconnected" ? "模拟链路断开，当前值未知。这不是现场设备离线。" : view_.freshness=="clock_uncertain" ? "事件时间不确定，需要核对。" : "源质量异常，当前值不可用，不补零。");
    else scopeBanner_->setText(QString("工位 140 的本地模拟帧 #%1，任务事件 %2 秒前。不是 LWMS 回执。").arg(latest.seq).arg(age));
}

void Workbench::unavailable(const QString& reason) {
    available_=false;
    feedback_->setText(reason);
    render();
    buttons();
}

bool Workbench::captureReady(const QString& kind) const {
    if (observations_<2) return false;
    if (kind=="sort") {
        auto* area=catalog_.area_of_station(selection_.station_id);
        return area && area->code=="seq" && !liveScope() && !selection_.station_id.empty();
    }
    if (kind=="peers") return selection_.station_id==catalog::kStation150 && controllerPeerIds().size()>=3 && !liveScope();
    if (kind=="south") return selection_.station_id==catalog::kStation20 && !liveScope();
    if (kind=="meter2") return selection_.station_id==catalog::kStation40 && controllerPeerIds().size()==4 && !liveScope();
    if (kind=="empty") return selection_.station_id.empty() && !selection_.plc_id.empty();
    if (kind=="dropdown") return liveScope() && areaCombo_ && areaCombo_->count()>=2;
    if (view_.rows.empty() || !liveScope()) return false;
    const auto& row=view_.rows.front();
    if (kind=="manual") return available_ && view_.runtime.mode=="manual";
    if (kind=="normal") return available_ && view_.runtime.mode=="automatic" && row.quality=="good" && row.alarm=="cleared";
    if (kind=="alarm") return available_ && row.alarm=="active";
    return !available_ || row.quality!="good";
}
QString Workbench::confirmedMode() const { return QString::fromStdString(view_.runtime.mode); }
QString Workbench::selectedStationId() const { return QString::fromStdString(selection_.station_id); }
QStringList Workbench::controllerPeerIds() const {
    QStringList ids;
    auto* plc=catalog_.plc_of_station(selection_.station_id);
    if (!plc) plc=catalog_.find_plc(selection_.plc_id);
    if (!plc) return ids;
    for (auto* station : catalog_.stations_of(plc->id)) ids.push_back(QString::fromStdString(station->id));
    return ids;
}
bool Workbench::liveScope() const { return catalog_.is_live(selection_.station_id); }
