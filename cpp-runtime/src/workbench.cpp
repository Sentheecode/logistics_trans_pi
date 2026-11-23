#include "workbench.hpp"
#include "control.hpp"
#include "schematic.hpp"
#include <QColor>
#include <QComboBox>
#include <QDateTime>
#include <QFrame>
#include <QHeaderView>
#include <QLabel>
#include <QMessageBox>
#include <QPushButton>
#include <QScrollArea>
#include <QSignalBlocker>
#include <QSizePolicy>
#include <QSplitter>
#include <QStatusBar>
#include <QTableWidget>
#include <QTimer>
#include <QToolBar>
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
    label->setSizePolicy(QSizePolicy::Fixed, QSizePolicy::Fixed);
    label->setMinimumWidth(label->fontMetrics().horizontalAdvance(label->text())+16);
}
QLabel* shortLabel(const QString& text, QWidget* parent) {
    auto* label=new QLabel(text, parent);
    fit(label);
    return label;
}
QComboBox* comboBox(const QString& name) {
    auto* box=new QComboBox;
    box->setObjectName(name);
    box->setMinimumContentsLength(6);
    box->setSizeAdjustPolicy(QComboBox::AdjustToMinimumContentsLengthWithIcon);
    box->setSizePolicy(QSizePolicy::Fixed, QSizePolicy::Fixed);
    box->setFixedWidth(160);
    return box;
}
void prepareTable(QTableWidget* table, const QStringList& headers) {
    table->setColumnCount(headers.size());
    table->setHorizontalHeaderLabels(headers);
    table->setEditTriggers(QAbstractItemView::NoEditTriggers);
    table->setSelectionMode(QAbstractItemView::NoSelection);
    table->verticalHeader()->hide();
    table->verticalHeader()->setDefaultSectionSize(24);
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
QString joinedStations(const std::vector<const catalog::Station*>& rows) {
    QStringList parts;
    for (auto* row : rows) parts.push_back(QString::fromStdString(row->id));
    return parts.isEmpty() ? "未配置" : parts.join("、");
}
}

Workbench::Workbench(std::filesystem::path directory)
    : directory_(std::move(directory)), catalog_(catalog::Directory::synthetic()) {
    const auto error=catalog::Directory::validate(catalog_);
    if (!error.empty()) throw std::runtime_error("catalog fixture: "+error);
    setWindowTitle("物流工作台 · 模拟环境");
    resize(1280, 800);
    setMinimumSize(1100, 650);
    setStyleSheet(R"(
        QMainWindow, QWidget { background:#e8eaed; color:#1c2430; font-size:13px; }
        QLabel { background:transparent; }
        QToolBar { background:#f4f5f7; border:none; border-bottom:1px solid #c5cad3; spacing:6px; padding:2px 6px; }
        QComboBox, QPushButton { background:white; border:1px solid #b7bec8; padding:3px 8px; min-height:22px; }
        QPushButton#primary { background:#245eea; color:white; border:1px solid #1d4ed0; }
        QPushButton:disabled, QPushButton#primary:disabled { background:#eef1f5; color:#8b96a6; border:1px solid #e1e6ee; }
        QTableWidget { background:white; gridline-color:#e1e5eb; selection-background-color:#e7eefc; selection-color:#1c2430; }
        QHeaderView::section { background:#f0f2f5; color:#334155; border:none; border-right:1px solid #e1e5eb; border-bottom:1px solid #d5dbe3; padding:4px 6px; }
        QStatusBar { background:#f4f5f7; border-top:1px solid #c5cad3; }
        QSplitter::handle { background:#d5dbe3; }
        QScrollBar:vertical { background:#f4f5f7; width:10px; }
        QScrollBar::handle:vertical { background:#c5ced8; min-height:24px; }
        QScrollBar::add-line:vertical, QScrollBar::sub-line:vertical { height:0px; }
    )");

    auto* navigation=new QToolBar("目录", this);
    navigation->setObjectName("catalogToolbar");
    navigation->setMovable(false);
    navigation->setFloatable(false);
    navigation->addWidget(shortLabel("工厂", navigation));
    factoryCombo_=comboBox("factoryCombo");
    navigation->addWidget(factoryCombo_);
    navigation->addWidget(shortLabel("业务模式", navigation));
    businessCombo_=comboBox("businessModeCombo");
    navigation->addWidget(businessCombo_);
    navigation->addWidget(shortLabel("线体", navigation));
    lineCombo_=comboBox("lineCombo");
    navigation->addWidget(lineCombo_);
    navigation->addWidget(shortLabel("工位", navigation));
    stationCombo_=comboBox("stationCombo");
    navigation->addWidget(stationCombo_);
    navigation->addSeparator();
    navigation->addWidget(shortLabel("模拟环境", navigation));
    addToolBar(Qt::TopToolBarArea, navigation);
    addToolBarBreak(Qt::TopToolBarArea);

    auto* commands=new QToolBar("模拟操作", this);
    commands->setObjectName("commandToolbar");
    commands->setMovable(false);
    commands->setFloatable(false);
    modeButton_=new QPushButton("模式待确认");
    modeButton_->setObjectName("modeButton");
    step_=new QPushButton("单步推进");
    step_->setObjectName("primary");
    disconnect_=new QPushButton("模拟断链");
    disconnect_->setObjectName("disconnectButton");
    commands->addWidget(modeButton_);
    commands->addWidget(step_);
    commands->addWidget(disconnect_);
    addToolBar(Qt::TopToolBarArea, commands);

    schematic_=new SchematicView;
    schematic_->setMinimumHeight(180);
    auto* caption=new QLabel("点选工位与下拉双向同步。橙色框是同一控制器的其他工位，蓝色框是当前选中。此图为合成示意。");
    caption->setObjectName("schematicCaption");
    caption->setWordWrap(true);
    auto* device=new DeviceFigure;
    device->setFixedHeight(108);
    auto* deviceCaption=new QLabel("设备图 · 示意。连接和库存未知，不探测真实设备。");
    deviceCaption->setWordWrap(true);
    auto* diagram=new QWidget;
    auto* diagramLayout=new QVBoxLayout(diagram);
    diagramLayout->setContentsMargins(6, 6, 6, 6);
    diagramLayout->setSpacing(4);
    diagramLayout->addWidget(schematic_, 1);
    diagramLayout->addWidget(caption);
    diagramLayout->addWidget(device);
    diagramLayout->addWidget(deviceCaption);

    detailTable_=new QTableWidget;
    detailTable_->setObjectName("detailTable");
    prepareTable(detailTable_, {"字段", "值"});
    detailTable_->setWordWrap(true);
    detailTable_->setTextElideMode(Qt::ElideNone);
    detailTable_->verticalHeader()->setSectionResizeMode(QHeaderView::ResizeToContents);
    peerTable_=new QTableWidget;
    peerTable_->setObjectName("peerTable");
    prepareTable(peerTable_, {"工位 ID", "名称", "标记"});
    peerTable_->setMaximumHeight(132);
    stageTable_=new QTableWidget;
    stageTable_->setObjectName("stageTable");
    prepareTable(stageTable_, {"小车", "阶段", "说明"});
    stageTable_->setMaximumHeight(112);
    scopeBanner_=new QLabel;
    scopeBanner_->setObjectName("scopeBanner");
    scopeBanner_->setWordWrap(true);
    feedback_=new QLabel;
    feedback_->setObjectName("controlFeedback");
    feedback_->setWordWrap(true);
    controlReason_=new QLabel;
    controlReason_->setObjectName("controlReason");
    controlReason_->setWordWrap(true);
    auto* peerCaption=new QLabel("同一控制器的工位");
    peerCaption->setWordWrap(true);
    auto* detail=new QWidget;
    auto* detailLayout=new QVBoxLayout(detail);
    detailLayout->setContentsMargins(8, 8, 8, 8);
    detailLayout->setSpacing(6);
    detailLayout->addWidget(detailTable_);
    detailLayout->addWidget(peerCaption);
    detailLayout->addWidget(peerTable_);
    detailLayout->addWidget(stageTable_);
    detailLayout->addWidget(scopeBanner_);
    detailLayout->addWidget(feedback_);
    detailLayout->addWidget(controlReason_);
    detailLayout->addStretch(1);
    auto* scroll=new QScrollArea;
    scroll->setObjectName("detailScroll");
    scroll->setWidgetResizable(true);
    scroll->setFrameShape(QFrame::NoFrame);
    scroll->setWidget(detail);
    scroll->setMinimumWidth(340);

    auto* upper=new QSplitter(Qt::Horizontal);
    upper->setObjectName("mainSplit");
    upper->setChildrenCollapsible(false);
    upper->addWidget(diagram);
    upper->addWidget(scroll);
    upper->setStretchFactor(0, 3);
    upper->setStretchFactor(1, 2);

    auto* history=new QWidget;
    auto* historyLayout=new QVBoxLayout(history);
    historyLayout->setContentsMargins(6, 4, 6, 6);
    historyLayout->setSpacing(4);
    auto* historyHeader=new QHBoxLayout;
    auto* historyTitle=new QLabel("最近事件与历史");
    fit(historyTitle);
    eventNote_=new QLabel("无实时历史");
    eventNote_->setObjectName("eventNote");
    eventNote_->setWordWrap(true);
    historyHeader->addWidget(historyTitle);
    historyHeader->addStretch(1);
    historyHeader->addWidget(eventNote_, 1);
    historyTable_=new QTableWidget;
    historyTable_->setObjectName("historyTable");
    prepareTable(historyTable_, {"序号", "接收时间 / UTC", "三车阶段（01 / 02 / 03）", "数据质量", "事件"});
    historyTable_->setSelectionBehavior(QAbstractItemView::SelectRows);
    historyTable_->setSelectionMode(QAbstractItemView::SingleSelection);
    historyTable_->horizontalHeader()->setSectionResizeMode(QHeaderView::Stretch);
    historyTable_->horizontalHeader()->setSectionResizeMode(0, QHeaderView::ResizeToContents);
    historyTable_->setMinimumHeight(120);
    historyLayout->addLayout(historyHeader);
    historyLayout->addWidget(historyTable_, 1);

    auto* split=new QSplitter(Qt::Vertical);
    split->setObjectName("contentSplit");
    split->setChildrenCollapsible(false);
    split->addWidget(upper);
    split->addWidget(history);
    split->setStretchFactor(0, 3);
    split->setStretchFactor(1, 2);
    auto* central=new QWidget;
    auto* root=new QVBoxLayout(central);
    root->setContentsMargins(0, 0, 0, 0);
    root->setSpacing(0);
    root->addWidget(split);
    setCentralWidget(central);

    processLabel_=shortLabel("采集待确认", this);
    processLabel_->setObjectName("processState");
    runtimeLabel_=shortLabel("运行未知", this);
    runtimeLabel_->setObjectName("runtimeState");
    heartbeatLabel_=shortLabel("无心跳", this);
    heartbeatLabel_->setObjectName("heartbeatState");
    auto* closure=shortLabel("关窗不停止采集", this);
    statusBar()->addWidget(processLabel_);
    statusBar()->addWidget(runtimeLabel_);
    statusBar()->addWidget(heartbeatLabel_);
    statusBar()->addPermanentWidget(closure);

    connect(schematic_, &SchematicView::stationActivated, this, [this](const QString& id) { activateSchematicStation(id); });
    connect(factoryCombo_, qOverload<int>(&QComboBox::currentIndexChanged), this, [this](int) {
        if (updating_) return;
        auto next=selection_;
        next.factory_id=comboId(factoryCombo_).toStdString();
        setSelection(next);
    });
    connect(businessCombo_, qOverload<int>(&QComboBox::currentIndexChanged), this, [this](int) {
        if (updating_) return;
        auto next=selection_;
        next.business_mode_id=comboId(businessCombo_).toStdString();
        setSelection(next);
    });
    connect(lineCombo_, qOverload<int>(&QComboBox::currentIndexChanged), this, [this](int) {
        if (updating_) return;
        auto next=selection_;
        next.line_id=comboId(lineCombo_).toStdString();
        setSelection(next);
    });
    connect(stationCombo_, qOverload<int>(&QComboBox::currentIndexChanged), this, [this](int) {
        if (updating_) return;
        auto next=selection_;
        next.station_id=comboId(stationCombo_).toStdString();
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
    auto* timer=new QTimer(this);
    connect(timer, &QTimer::timeout, this, [this] { refresh(); });
    timer->start(100);
    buttons();
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
        if (items[i].first==selected) index=i;
    }
    if (index<0) throw std::logic_error("combo missing selection");
    box->setCurrentIndex(index);
    box->setToolTip(selected);
}
QString Workbench::comboId(const QComboBox* box) { return box ? box->currentData().toString() : QString(); }

void Workbench::refill() {
    std::vector<std::pair<QString, QString>> factories, modes, lines, stations;
    for (auto& row : catalog_.factories) factories.emplace_back(QString::fromStdString(row.id), QString::fromStdString(row.name));
    for (auto* row : catalog_.modes_of(selection_.factory_id)) modes.emplace_back(QString::fromStdString(row->id), QString::fromStdString(row->name));
    for (auto* row : catalog_.lines_of(selection_.business_mode_id)) lines.emplace_back(QString::fromStdString(row->id), QString::fromStdString(row->name));
    for (auto* row : catalog_.stations_of(selection_.line_id)) stations.emplace_back(QString::fromStdString(row->id), QString::fromStdString(row->name));
    fillCombo(factoryCombo_, factories, QString::fromStdString(selection_.factory_id));
    fillCombo(businessCombo_, modes, QString::fromStdString(selection_.business_mode_id));
    fillCombo(lineCombo_, lines, QString::fromStdString(selection_.line_id));
    fillCombo(stationCombo_, stations, QString::fromStdString(selection_.station_id));
}

void Workbench::setSelection(catalog::Selection selection) {
    if (updating_) return;
    updating_=true;
    selection_=catalog_.reconcile(std::move(selection));
    refill();
    std::vector<std::string> peers;
    std::string zone;
    if (auto* controller=catalog_.controller_of_station(selection_.station_id)) {
        for (auto* station : catalog_.stations_of_controller(controller->id)) peers.push_back(station->id);
    }
    if (auto* zone_row=catalog_.zone_of_station(selection_.station_id)) zone=zone_row->id;
    schematic_->setHighlight(selection_.station_id, peers, zone);
    updating_=false;
    render();
    buttons();
}

void Workbench::activateSchematicStation(const QString& id) {
    auto selection=catalog_.selection_for_station(id.toStdString());
    if (!selection) return;
    setSelection(*selection);
}

void Workbench::prepareCapture(const QString& kind) {
    if (kind=="empty") {
        catalog::Selection next;
        if (auto* line=catalog_.find_line(catalog_.empty_line_id())) {
            next.line_id=line->id;
            next.business_mode_id=line->business_mode_id;
            if (auto* mode=catalog_.find_mode(line->business_mode_id)) next.factory_id=mode->factory_id;
        }
        setSelection(next);
        return;
    }
    if (kind=="south") {
        for (auto& station : catalog_.stations) {
            auto* factory=catalog_.factory_of_station(station.id);
            if (factory && factory->id==catalog::kFactorySouth) { activateSchematicStation(QString::fromStdString(station.id)); return; }
        }
    }
    if (kind=="sort") {
        for (auto& station : catalog_.stations) {
            auto* mode=catalog_.mode_of_station(station.id);
            auto* factory=catalog_.factory_of_station(station.id);
            if (mode && mode->code=="sort" && factory && factory->id==catalog::kFactoryNorth) {
                activateSchematicStation(QString::fromStdString(station.id));
                return;
            }
        }
    }
    if (kind=="peers") {
        const catalog::Controller* best=nullptr;
        std::size_t count=0;
        for (auto& controller : catalog_.controllers) {
            auto rows=catalog_.stations_of_controller(controller.id);
            if (rows.size()>count) { count=rows.size(); best=&controller; }
        }
        if (best && count>=2) {
            auto rows=catalog_.stations_of_controller(best->id);
            activateSchematicStation(QString::fromStdString(rows[1]->id));
            return;
        }
    }
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
    modeButton_->setStyleSheet(usable && !manual && !pending ? "background:#245eea;color:white;border:1px solid #245eea;" : "");
    QString reason;
    if (selection_.station_id.empty()) reason="未配置工位，不能控制";
    else if (!live) reason="当前工位未接入模拟流，不能控制";
    else if (!usable) reason="采集端不可达或心跳过期，控制已禁用";
    else if (pending) reason="正在等待采集端确认，请勿重复操作";
    else if (manual) reason="手动待步：只有点击才推进";
    else reason="自动运行中：单步与断链操作仅限手动模式";
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
    QString process="采集不可用";
    if (available_ && view_.runtime.liveness=="recent") process="采集在线";
    else if (view_.runtime.liveness=="clock_uncertain") process="时钟不确定";
    else if (view_.runtime.liveness=="stale") process="采集过期";
    else if (observations_==0) process="采集待确认";
    processLabel_->setText(process);
    fit(processLabel_);
    QString runtime="运行未知";
    if (available_ && view_.runtime.mode=="manual") runtime="运行手动";
    else if (available_ && view_.runtime.mode=="automatic") runtime="运行自动";
    runtimeLabel_->setText(runtime);
    fit(runtimeLabel_);
    heartbeatLabel_->setText(view_.runtime.heartbeat_ms>0 ? "心跳 "+QDateTime::fromMSecsSinceEpoch(view_.runtime.heartbeat_ms).toUTC().toString("HH:mm:ss") : "无心跳");
    fit(heartbeatLabel_);

    const catalog::Station* station=catalog_.find_station(selection_.station_id);
    const catalog::BusinessMode* business=catalog_.find_mode(selection_.business_mode_id);
    const catalog::Controller* controller=catalog_.controller_of_station(selection_.station_id);
    const catalog::Zone* zone=catalog_.zone_of_station(selection_.station_id);
    QString runtimeValue="未知";
    if (available_ && view_.runtime.mode=="manual") runtimeValue="手动";
    else if (available_ && view_.runtime.mode=="automatic") runtimeValue="自动";
    QString peers="未配置";
    std::vector<const catalog::Station*> peer_rows;
    if (controller) { peer_rows=catalog_.stations_of_controller(controller->id); peers=joinedStations(peer_rows); }
    QString locations="未配置";
    if (zone) {
        QStringList parts;
        for (auto* location : catalog_.locations_of_zone(zone->id)) parts.push_back(QString::fromStdString(location->id+" "+location->name));
        if (!parts.isEmpty()) locations=parts.join("、");
    }
    const bool good=live && available_ && !view_.rows.empty() && view_.rows.front().quality=="good" && view_.freshness!="clock_uncertain" && (view_.runtime.mode=="manual" || view_.freshness=="recent");
    QString sim_quality="未配置";
    QString scope="未配置";
    if (station_selected && !live) { sim_quality="未接入"; scope="未接入"; }
    else if (live) {
        scope="模拟流 "+QString::fromStdString(catalog::kStream);
        if (view_.rows.empty() || !available_) sim_quality="未知";
        else sim_quality=good ? "正常" : "不可用";
    }
    setRows(detailTable_, {
        {"工位", station ? QString::fromStdString(station->id+" "+station->name) : "未配置"},
        {"业务模式", business ? QString::fromStdString(business->name) : "未配置"},
        {"运行模式", runtimeValue},
        {"实时范围", scope},
        {"模拟质量", sim_quality},
        {"设备连接", "未探测"},
        {"库存", "未知"},
        {"控制器", controller ? QString::fromStdString(controller->id+" "+controller->name) : "未配置"},
        {"关联工位", peers},
        {"库区", zone ? QString::fromStdString(zone->id+" "+zone->name) : "未配置"},
        {"库位", locations},
    });
    detailTable_->resizeRowsToContents();
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

    if (!live) {
        stageTable_->setRowCount(0);
        stageTable_->hide();
        historyTable_->setRowCount(0);
        eventNote_->setText(station_selected ? "无实时历史" : "未配置");
        scopeBanner_->setText(station_selected
            ? "未接入当前模拟流：这里只保留静态示意，不显示 SPS 实时帧、历史或控制。"
            : "未配置：此线体没有工位，不能控制。");
        return;
    }
    stageTable_->show();
    stageTable_->setRowCount(3);
    if (view_.rows.empty()) {
        for (int i=0; i<3; ++i) {
            QStringList columns={QString("%1").arg(i+1, 2, 10, QChar('0')), "状态未知", "等待模拟帧"};
            for (int column=0; column<columns.size(); ++column) stageTable_->setItem(i, column, new QTableWidgetItem(columns[column]));
        }
        historyTable_->setRowCount(0);
        eventNote_->setText("尚无历史");
        scopeBanner_->setText("等待模拟帧。库存与设备连接未探测，采集心跳不是设备在线。");
        return;
    }
    const auto& latest=view_.rows.front();
    for (int i=0; i<3; ++i) {
        auto value=good ? latest.carts[i] : "UNKNOWN";
        QString detail=value=="station_complete" ? "当前工位的本轮步骤已结束" : value=="waiting" ? "等待前序小车完成当前步骤" : value=="blocked" ? "合成阻塞事件；当前条件仍活动" : value=="UNKNOWN" ? "无可信当前值，不补零" : "合成数据 · 当前阶段已确认";
        QStringList columns={QString("%1").arg(i+1, 2, 10, QChar('0')), stateText(value), detail};
        for (int column=0; column<columns.size(); ++column) {
            auto* item=new QTableWidgetItem(columns[column]);
            if (value=="UNKNOWN" || value=="blocked") item->setForeground(QColor("#8a5a00"));
            stageTable_->setItem(i, column, item);
        }
    }
    historyTable_->setRowCount(static_cast<int>(view_.rows.size()));
    for (int i=0; i<static_cast<int>(view_.rows.size()); ++i) {
        const auto& row=view_.rows[i];
        QStringList columns={
            QString::number(row.seq),
            QDateTime::fromMSecsSinceEpoch(row.received_ms).toUTC().toString("HH:mm:ss.zzz"),
            stateText(row.carts[0])+" / "+stateText(row.carts[1])+" / "+stateText(row.carts[2]),
            row.quality=="good" ? "正常" : "不可用",
            eventText(row.event),
        };
        for (int column=0; column<columns.size(); ++column) {
            auto* item=new QTableWidgetItem(columns[column]);
            item->setToolTip(columns[column]);
            if (row.quality!="good" && column==3) item->setForeground(QColor("#8a5a00"));
            historyTable_->setItem(i, column, item);
        }
    }
    eventNote_->setText(QString("最近 %1 帧 · 覆盖范围未知 · 时间为 UTC").arg(view_.rows.size()));
    QString age=QString::number(std::max<std::int64_t>(0, (sim::now_ms()-latest.received_ms)/1000));
    if (!good) scopeBanner_->setText(view_.freshness=="clock_uncertain" ? "事件时间不确定，当前值需要重新核对。库存与设备连接未探测。" : !available_ ? "心跳已过期；历史保留原始质量，不能当作当前状态。库存与设备连接未探测。" : latest.reason=="disconnected" ? "模拟链路已断开，当前值未知；采集进程仍在线。库存与设备连接未探测。" : "源数据质量异常，当前值不可用；不会补零或推断完成。库存与设备连接未探测。");
    else scopeBanner_->setText(QString("当前工位接入模拟流。记录 #%1，最近任务事件 %2 秒前。库存与设备连接未探测，采集心跳不是设备在线。").arg(latest.seq).arg(age));
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
        auto* mode=catalog_.mode_of_station(selection_.station_id);
        auto* factory=catalog_.factory_of_station(selection_.station_id);
        return mode && mode->code=="sort" && factory && factory->id==catalog::kFactoryNorth && !liveScope();
    }
    if (kind=="peers") return !liveScope() && controllerPeerIds().size()>=3 && controllerPeerIds().contains(selectedStationId());
    if (kind=="south") {
        auto* factory=catalog_.factory_of_station(selection_.station_id);
        return factory && factory->id==catalog::kFactorySouth;
    }
    if (kind=="empty") return selection_.station_id.empty() && !selection_.line_id.empty();
    if (kind=="dropdown") return liveScope() && businessCombo_ && businessCombo_->count()>=2;
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
    auto* controller=catalog_.controller_of_station(selection_.station_id);
    if (!controller) return ids;
    for (auto* station : catalog_.stations_of_controller(controller->id)) ids.push_back(QString::fromStdString(station->id));
    return ids;
}
bool Workbench::liveScope() const { return catalog_.is_live(selection_.station_id); }
