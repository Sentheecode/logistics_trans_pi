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
#include <QScrollArea>
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
// 视觉 token 集中在此。父审选定风格后只改这些值，不改模块结构。
// 参考：Carbon Tabs（页签状态与同页切换）、Ant Design Layout（顶栏上下文 + 内容区）、Ant Design Tabs（功能页而非进度条）。
struct Tone {
    const char* canvas="#f3f4f6";
    const char* surface="#ffffff";
    const char* ink="#1f2933";
    const char* muted="#52606d";
    const char* line="#d9e2ec";
    const char* accent="#1d4e89";
    const char* accentSoft="#e6eef6";
    const char* warn="#8a5a12";
    const char* warnSoft="#fbf6ee";
};
const Tone tone;
QString consoleStyle() {
    const auto t=tone;
    return QString(R"(
        QMainWindow, QWidget#centralRoot { background:%1; color:%3; font-size:13px; }
        QWidget#identityBar, QWidget#catalogPane, QWidget#commandPane { background:%2; }
        QWidget#identityBar, QWidget#catalogPane { border-bottom:1px solid %4; }
        QWidget#commandPane { border-left:1px solid %4; }
        QWidget#surface { background:%2; border:1px solid %4; }
        QLabel#title { font-size:16px; font-weight:600; color:%3; }
        QLabel#pageTitle { font-size:18px; font-weight:600; color:%3; }
        QLabel#fieldLabel { color:%5; font-size:12px; }
        QLabel#section { color:%3; font-size:13px; font-weight:600; }
        QLabel#stationTitle { font-size:18px; font-weight:600; color:%3; }
        QLabel#muted { color:%5; font-size:12px; }
        QLabel#phase { font-size:16px; font-weight:600; color:%3; }
        QLabel#statusChip { background:%1; color:%5; padding:1px 6px; font-size:12px; font-weight:600; }
        QLabel#statusChip[state="sim"] { background:%7; color:%6; }
        QLabel#statusChip[state="wait"] { background:%9; color:%8; }
        QFrame#cart { background:%2; border:1px solid %4; border-left:4px solid %6; }
        QFrame#cart[state="wait"] { border-left:4px solid #b7c0cc; }
        QFrame#cart[state="done"] { border-left:4px solid %3; }
        QFrame#cart[state="alert"] { background:%9; border-left:4px solid %8; }
        QComboBox { background:%2; border:1px solid %4; padding:2px 8px; min-height:26px; }
        QComboBox:hover, QComboBox:focus { border:2px solid %6; }
        QPushButton { background:%2; border:1px solid %4; padding:4px 10px; min-height:28px; color:%3; }
        QPushButton:hover { background:%7; border-color:%6; }
        QPushButton:focus { border:2px solid %6; }
        QPushButton#primary, QPushButton#modeButton[emphasis="yes"] { background:%6; color:#ffffff; border:1px solid %6; }
        QPushButton:disabled, QPushButton#primary:disabled { background:%1; color:#8b96a6; border:1px solid %4; }
        QPushButton#stationCard { min-width:52px; }
        QPushButton#stationCard:checked { background:%6; color:#ffffff; border:1px solid %6; }
        QTableWidget { background:%2; alternate-background-color:%1; gridline-color:%4; font-size:12px; selection-background-color:%7; selection-color:%3; }
        QHeaderView::section { background:%1; color:%5; border:none; border-bottom:1px solid %4; padding:4px 6px; font-size:12px; }
        QTabWidget::pane { border:none; background:%1; }
        QTabBar::tab { background:%1; color:%5; padding:8px 16px; min-height:24px; border:none; border-bottom:2px solid transparent; }
        QTabBar::tab:hover { color:%3; background:%7; }
        QTabBar::tab:selected { color:%6; font-weight:600; background:%2; border-bottom:2px solid %6; }
        QTabBar::tab:focus { border-bottom:2px solid %6; }
        QStatusBar { background:%2; color:%5; border-top:1px solid %4; }
        QSplitter::handle { background:%4; }
        QScrollArea { border:none; background:%1; }
    )").arg(t.canvas, t.surface, t.ink, t.line, t.muted, t.accent, t.accentSoft, t.warn, t.warnSoft);
}
QWidget* boundaryCard(const QString& title, const QString& chip, const QString& state, const QString& body, QWidget* parent) {
    auto* card=new QWidget(parent);
    card->setObjectName("surface");
    auto* layout=new QVBoxLayout(card);
    layout->setContentsMargins(12, 10, 12, 10);
    layout->setSpacing(4);
    auto* head=new QHBoxLayout;
    auto* heading=new QLabel(title, card);
    heading->setObjectName("section");
    auto* mark=new QLabel(chip, card);
    mark->setObjectName("statusChip");
    mark->setProperty("state", state);
    mark->setMinimumWidth(mark->fontMetrics().horizontalAdvance(chip)+28);
    head->addWidget(heading);
    head->addStretch(1);
    head->addWidget(mark);
    auto* text=new QLabel(body, card);
    text->setWordWrap(true);
    text->setObjectName("muted");
    layout->addLayout(head);
    layout->addWidget(text);
    return card;
}
QScrollArea* scrollPage(QWidget* content) {
    auto* scroll=new QScrollArea;
    scroll->setWidgetResizable(true);
    scroll->setFrameShape(QFrame::NoFrame);
    scroll->setWidget(content);
    return scroll;
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
    setStyleSheet(consoleStyle());

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

    auto* context=new QWidget;
    context->setObjectName("catalogPane");
    context->setFixedHeight(44);
    auto* contextLayout=new QHBoxLayout(context);
    contextLayout->setContentsMargins(12, 6, 12, 6);
    contextLayout->setSpacing(12);
    auto addCombo=[&](const QString& name, QComboBox** slot, const QString& objectName) {
        auto* label=fieldLabel(name, context);
        *slot=new QComboBox(context);
        (*slot)->setObjectName(objectName);
        (*slot)->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);
        (*slot)->setFocusPolicy(Qt::StrongFocus);
        label->setBuddy(*slot);
        contextLayout->addWidget(label);
        contextLayout->addWidget(*slot, 1);
    };
    addCombo("工厂", &factoryCombo_, "factoryCombo");
    addCombo("区域", &areaCombo_, "areaCombo");
    addCombo("PLC", &plcCombo_, "plcCombo");

    auto* commandPane=new QWidget;
    commandPane->setObjectName("commandPane");
    commandPane->setFixedWidth(228);
    auto* commandLayout=new QVBoxLayout(commandPane);
    commandLayout->setContentsMargins(12, 12, 12, 12);
    commandLayout->setSpacing(6);
    auto* commandHeading=new QLabel("模拟操作", commandPane);
    commandHeading->setObjectName("section");
    commandLayout->addWidget(commandHeading);
    modeButton_=new QPushButton("模式待确认");
    modeButton_->setObjectName("modeButton");
    modeButton_->setFocusPolicy(Qt::StrongFocus);
    step_=new QPushButton("单步推进");
    step_->setObjectName("primary");
    step_->setFocusPolicy(Qt::StrongFocus);
    disconnect_=new QPushButton("模拟断链");
    disconnect_->setObjectName("disconnectButton");
    disconnect_->setFocusPolicy(Qt::StrongFocus);
    controlReason_=new QLabel;
    controlReason_->setObjectName("controlReason");
    controlReason_->setWordWrap(true);
    feedback_=new QLabel;
    feedback_->setObjectName("controlFeedback");
    feedback_->setWordWrap(true);
    commandLayout->addWidget(modeButton_);
    commandLayout->addWidget(step_);
    commandLayout->addWidget(disconnect_);
    commandLayout->addWidget(controlReason_);
    commandLayout->addWidget(feedback_);
    commandLayout->addStretch(1);

    schematic_=new SchematicView;
    schematic_->setFixedHeight(148);
    schematic_->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);
    cardRow_=new QWidget;
    cardRow_->setFixedHeight(44);
    new QHBoxLayout(cardRow_);
    cardRow_->layout()->setContentsMargins(0, 4, 0, 0);
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
    cartStrip_->setFixedHeight(68);
    auto* cartLayout=new QHBoxLayout(cartStrip_);
    cartLayout->setContentsMargins(0, 0, 0, 0);
    cartLayout->setSpacing(8);
    for (int i=0; i<3; ++i) {
        cartFrame_[i]=new QFrame;
        cartFrame_[i]->setObjectName("cart");
        auto* layout=new QVBoxLayout(cartFrame_[i]);
        layout->setContentsMargins(10, 6, 10, 6);
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
    configLine_->hide();
    lineState_->hide();
    peerLine_->hide();
    plcAuto_->hide();

    auto* run=new QWidget;
    auto* runLayout=new QVBoxLayout(run);
    runLayout->setContentsMargins(16, 12, 16, 16);
    runLayout->setSpacing(8);
    auto* runTitle=new QLabel("当前采集", run);
    runTitle->setObjectName("pageTitle");
    auto* runHint=new QLabel("手自动、单步和断链都作用在采集器上。切页不重置模拟，也不改当前 PLC。下面的概览只画这一台 PLC，是通用示意，不是实测布局。", run);
    runHint->setWordWrap(true);
    runHint->setObjectName("muted");
    runLayout->addWidget(runTitle);
    runLayout->addWidget(runHint);
    auto* runBody=new QWidget(run);
    auto* runColumns=new QHBoxLayout(runBody);
    runColumns->setContentsMargins(0, 0, 0, 0);
    runColumns->setSpacing(16);
    auto* facts=new QWidget(runBody);
    auto* factsLayout=new QVBoxLayout(facts);
    factsLayout->setContentsMargins(0, 0, 0, 0);
    factsLayout->setSpacing(6);
    factsLayout->addWidget(stationTitle_);
    factsLayout->addWidget(stationMeta_);
    factsLayout->addWidget(interfaceLine_);
    factsLayout->addWidget(scopeBanner_);
    factsLayout->addStretch(1);
    auto* overview=new QWidget(runBody);
    overview->setObjectName("surface");
    overview->setMinimumWidth(360);
    auto* overviewLayout=new QVBoxLayout(overview);
    overviewLayout->setContentsMargins(8, 8, 8, 8);
    overviewLayout->setSpacing(4);
    auto* overviewLabel=new QLabel("本 PLC 概览", overview);
    overviewLabel->setObjectName("section");
    overviewLayout->addWidget(overviewLabel);
    overviewLayout->addWidget(schematic_);
    overviewLayout->addWidget(cardRow_);
    facts->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Maximum);
    overview->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Maximum);
    runColumns->setAlignment(Qt::AlignTop);
    runColumns->addWidget(facts, 1);
    runColumns->addWidget(overview, 1);
    runLayout->addWidget(runBody);
    runLayout->addWidget(cartStrip_);
    runLayout->addStretch(1);

    detailTable_=new QTableWidget;
    detailTable_->setObjectName("detailTable");
    prepareTable(detailTable_, {"字段", "值"});
    peerTable_=new QTableWidget;
    peerTable_->setObjectName("peerTable");
    prepareTable(peerTable_, {"工位 ID", "工位号", "标记"});
    peerTable_->setSelectionBehavior(QAbstractItemView::SelectRows);
    peerTable_->setSelectionMode(QAbstractItemView::SingleSelection);
    peerTable_->setFocusPolicy(Qt::StrongFocus);
    peerTable_->setMaximumHeight(168);
    stageTable_=new QTableWidget(this);
    stageTable_->setObjectName("stageTable");
    prepareTable(stageTable_, {"小车", "阶段", "说明"});
    stageTable_->hide();
    stationEmpty_=new QLabel("这个 PLC 还没有工位。不借用其他 PLC 的布置，也不能控制。");
    stationEmpty_->setWordWrap(true);
    stationEmpty_->setObjectName("muted");
    auto* stationPage=new QWidget;
    auto* stationLayout=new QVBoxLayout(stationPage);
    stationLayout->setContentsMargins(16, 12, 16, 16);
    stationLayout->setSpacing(8);
    auto* stationHeading=new QLabel("当前 PLC 的工位", stationPage);
    stationHeading->setObjectName("pageTitle");
    auto* stationHint=new QLabel("点击表格或概览编号即可聚焦。没有工位下拉。其他 PLC 的工位不会出现在这里。", stationPage);
    stationHint->setWordWrap(true);
    stationHint->setObjectName("muted");
    stationLayout->addWidget(stationHeading);
    stationLayout->addWidget(stationHint);
    stationLayout->addWidget(stationEmpty_);
    stationLayout->addWidget(peerTable_, 1);
    auto* detailHeading=new QLabel("聚焦工位", stationPage);
    detailHeading->setObjectName("section");
    stationLayout->addWidget(detailHeading);
    stationLayout->addWidget(detailTable_, 1);

    pointTable_=new QTableWidget;
    pointTable_->setObjectName("pointAuditTable");
    prepareTable(pointTable_, {"来源", "段", "名称", "地址", "类型", "方向", "疑点"});
    pointTable_->horizontalHeader()->setStretchLastSection(false);
    for (int column : {0, 1, 3, 4, 5}) pointTable_->horizontalHeader()->setSectionResizeMode(column, QHeaderView::ResizeToContents);
    pointTable_->horizontalHeader()->setSectionResizeMode(2, QHeaderView::Stretch);
    pointTable_->horizontalHeader()->setSectionResizeMode(6, QHeaderView::Stretch);
    pointScope_=new QLabel;
    pointScope_->setWordWrap(true);
    auto* pointPage=new QWidget;
    auto* pointLayout=new QVBoxLayout(pointPage);
    pointLayout->setContentsMargins(16, 12, 16, 16);
    pointLayout->setSpacing(8);
    auto* pointHeading=new QLabel("点表审计", pointPage);
    pointHeading->setObjectName("pageTitle");
    pointLayout->addWidget(pointHeading);
    pointLayout->addWidget(pointScope_);
    pointLayout->addWidget(pointTable_, 1);

    auto* boundaryPage=new QWidget;
    auto* boundaryLayout=new QVBoxLayout(boundaryPage);
    boundaryLayout->setContentsMargins(16, 12, 16, 16);
    boundaryLayout->setSpacing(8);
    auto* boundaryHeading=new QLabel("业务边界", boundaryPage);
    boundaryHeading->setObjectName("pageTitle");
    auto* boundaryIntro=new QLabel("这里只写已经确认的意图和未接通的原因。没有真实订单、数量、地址或放行结果。", boundaryPage);
    boundaryIntro->setWordWrap(true);
    boundaryIntro->setObjectName("muted");
    boundaryLayout->addWidget(boundaryHeading);
    boundaryLayout->addWidget(boundaryIntro);
    boundaryLayout->addWidget(boundaryCard("采集与手自动", "仅模拟", "sim", "采集器独占手动、自动、单步和模拟断链。手动待步不推进；恢复自动必须确认。心跳只说明采集进程还在，不是 PLC 在线。", boundaryPage));
    boundaryLayout->addWidget(boundaryCard("扫码与 SPS 订单", "待补映射", "wait", "分拣意图是扫料车二维码，再向 LWMS 查 SPS 单。首台机械臂扫码，后续机械臂共享，但共享成员未配置。库位、箱、料、订单都还不能显示成已查到。", boundaryPage));
    boundaryLayout->addWidget(boundaryCard("补料", "未完成闭环", "wait", "PLC 请求补料不等于 LWMS 已确认完成。界面不把请求写成回执，也不填假数量。", boundaryPage));
    boundaryLayout->addWidget(boundaryCard("LWMS", "未连接", "wait", "没有实现 LWMS 协议，也不显示库存数量。设备连接保持未探测。", boundaryPage));
    boundaryLayout->addWidget(boundaryCard("分拣 · 库卡通行", "未授权", "wait", "已知顺序是请求进入、同意、进入完成、请求离开、离开完成，并到安全点。自动模式、设备运行或心跳都不能当成放行。进/出完成也不提前当成安全点。", boundaryPage));
    boundaryLayout->addWidget(boundaryCard("排序 · 华睿任务", "协议未定", "wait", "华睿会下发单、箱、料、数量，以及汇总/严控、正序/倒序。完成数量、内衬料架阈值和通知报文都未定，所以这里不填任务。", boundaryPage));
    boundaryLayout->addWidget(boundaryCard("140 点表", "只读审计", "sim", "点表只属于工位 140。重复地址、空白名、方向冲突和 DBX 上的 int/string 保留原样。没有任何可执行写点，也不套用到其他工位。", boundaryPage));
    boundaryLayout->addStretch(1);

    historyTable_=new QTableWidget;
    historyTable_->setObjectName("historyTable");
    prepareTable(historyTable_, {"序号", "接收时间 / 北京时间", "三车阶段", "数据质量", "事件"});
    historyTable_->setSelectionBehavior(QAbstractItemView::SelectRows);
    historyTable_->setSelectionMode(QAbstractItemView::SingleSelection);
    historyTable_->setFocusPolicy(Qt::StrongFocus);
    historyTable_->horizontalHeader()->setSectionResizeMode(QHeaderView::Stretch);
    historyTable_->horizontalHeader()->setSectionResizeMode(0, QHeaderView::ResizeToContents);
    historyTable_->setMinimumHeight(72);
    eventNote_=new QLabel("无实时历史");
    eventNote_->setObjectName("eventNote");
    eventNote_->setWordWrap(true);
    historyEmpty_=new QLabel("没有可显示的帧。未挂接的工位不会复制工位 140 的历史。");
    historyEmpty_->setObjectName("muted");
    historyEmpty_->setWordWrap(true);
    historyFilter_=new QComboBox;
    historyFilter_->setObjectName("historyFilter");
    historyFilter_->addItem("全部", "all");
    historyFilter_->addItem("正常", "good");
    historyFilter_->addItem("不可用", "bad");
    historyFilter_->setFocusPolicy(Qt::StrongFocus);
    auto* historyPage=new QWidget;
    auto* historyLayout=new QVBoxLayout(historyPage);
    historyLayout->setContentsMargins(16, 12, 16, 16);
    historyLayout->setSpacing(8);
    auto* historyHeading=new QLabel("事件历史", historyPage);
    historyHeading->setObjectName("pageTitle");
    auto* filterRow=new QHBoxLayout;
    auto* filterLabel=fieldLabel("质量", historyPage);
    filterRow->addWidget(filterLabel);
    filterRow->addWidget(historyFilter_, 1);
    filterRow->addStretch(1);
    historyLayout->addWidget(historyHeading);
    historyLayout->addWidget(eventNote_);
    historyLayout->addLayout(filterRow);
    historyLayout->addWidget(historyEmpty_);
    historyLayout->addWidget(historyTable_, 1);

    moduleTabs_=new QTabWidget;
    moduleTabs_->setObjectName("moduleTabs");
    moduleTabs_->setFocusPolicy(Qt::StrongFocus);
    moduleTabs_->tabBar()->setFocusPolicy(Qt::StrongFocus);
    moduleTabs_->addTab(scrollPage(run), "运行");
    moduleTabs_->addTab(scrollPage(stationPage), "工位");
    moduleTabs_->addTab(historyPage, "事件");
    moduleTabs_->addTab(pointPage, "点表");
    moduleTabs_->addTab(scrollPage(boundaryPage), "边界");
    moduleTabs_->setCurrentIndex(0);

    mainSplit_=new QSplitter(Qt::Horizontal);
    mainSplit_->setObjectName("mainSplit");
    mainSplit_->setChildrenCollapsible(false);
    mainSplit_->addWidget(moduleTabs_);
    mainSplit_->addWidget(commandPane);
    mainSplit_->setStretchFactor(0, 1);
    mainSplit_->setStretchFactor(1, 0);

    auto* central=new QWidget;
    central->setObjectName("centralRoot");
    auto* root=new QVBoxLayout(central);
    root->setContentsMargins(0, 0, 0, 0);
    root->setSpacing(0);
    root->addWidget(identity);
    root->addWidget(context);
    root->addWidget(mainSplit_, 1);
    setCentralWidget(central);
    auto* closure=new QLabel("关窗不停止采集");
    fit(closure);
    auto* isolated=new QLabel("未连接现场设备");
    fit(isolated);
    statusBar()->addWidget(isolated);
    statusBar()->addPermanentWidget(closure);

    connect(schematic_, &SchematicView::stationActivated, this, [this](const QString& id) { activateSchematicStation(id); });
    connect(peerTable_, &QTableWidget::cellClicked, this, [this](int row, int) {
        auto* item=peerTable_->item(row, 0);
        if (item) activateSchematicStation(item->text());
    });
    connect(historyFilter_, qOverload<int>(&QComboBox::currentIndexChanged), this, [this](int) { applyHistoryFilter(); });
    setTabOrder(factoryCombo_, areaCombo_);
    setTabOrder(areaCombo_, plcCombo_);
    setTabOrder(plcCombo_, moduleTabs_);
    setTabOrder(moduleTabs_, modeButton_);
    setTabOrder(modeButton_, step_);
    setTabOrder(step_, disconnect_);
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
            const int rail=228;
            mainSplit_->setSizes({std::max(480, mainSplit_->width()-rail), rail});
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
    const int rail=228;
    mainSplit_->setSizes({std::max(480, width-rail), rail});
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
void Workbench::applyHistoryFilter() {
    if (!historyTable_ || !historyFilter_) return;
    const auto mode=historyFilter_->currentData().toString();
    int visible=0;
    for (int row=0; row<historyTable_->rowCount(); ++row) {
        auto* quality=historyTable_->item(row, 3);
        const bool match=mode=="all" || (quality && ((mode=="good" && quality->text()=="正常") || (mode=="bad" && quality->text()=="不可用")));
        historyTable_->setRowHidden(row, !match);
        if (match) ++visible;
    }
    if (historyEmpty_) historyEmpty_->setVisible(historyTable_->rowCount()==0 || visible==0);
}
void Workbench::syncModuleChrome() {
    const bool has=!catalog_.stations_of(selection_.plc_id).empty();
    if (stationEmpty_) stationEmpty_->setVisible(!has);
    if (peerTable_) peerTable_->setVisible(has);
    if (has && peerTable_) {
        for (int row=0; row<peerTable_->rowCount(); ++row) {
            auto* item=peerTable_->item(row, 0);
            if (item && item->text().toStdString()==selection_.station_id) peerTable_->selectRow(row);
        }
    }
    applyHistoryFilter();
}

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
    const auto open=[&](const char* title) {
        if (!moduleTabs_) return;
        const QString wanted=QString::fromUtf8(title);
        for (int i=0; i<moduleTabs_->count(); ++i) if (moduleTabs_->tabText(i)==wanted) moduleTabs_->setCurrentIndex(i);
    };
    if (kind=="empty") {
        if (auto selection=catalog_.selection_for_plc(catalog_.empty_plc_id())) setSelection(*selection);
        open("运行");
        return;
    }
    if (kind=="south") { activateSchematicStation(QString::fromUtf8(catalog::kStation20)); open("运行"); return; }
    if (kind=="meter2") { activateSchematicStation(QString::fromUtf8(catalog::kStation40)); open("运行"); return; }
    if (kind=="roster") { activateSchematicStation(QString::fromUtf8(catalog::kStation40)); open("工位"); return; }
    if (kind=="sort") { activateSchematicStation(QString::fromUtf8(catalog::kStation210)); open("运行"); return; }
    if (kind=="peers") { activateSchematicStation(QString::fromUtf8(catalog::kStation150)); open("运行"); return; }
    if (!catalog_.live_station_id().empty()) activateSchematicStation(QString::fromStdString(catalog_.live_station_id()));
    if (kind=="stations") open("工位");
    else if (kind=="events") open("事件");
    else if (kind=="points") open("点表");
    else if (kind=="boundary") open("边界");
    else open("运行");
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
    modeButton_->setProperty("emphasis", usable && !manual && !pending);
    modeButton_->style()->unpolish(modeButton_);
    modeButton_->style()->polish(modeButton_);
    QString reason;
    if (selection_.station_id.empty()) reason="未配置工位，不能控制";
    else if (!live) reason="未挂接模拟帧，不能控制";
    else if (!usable) reason="采集心跳不可用，控制已禁用";
    else if (pending) reason="正在等待采集端确认";
    else if (manual) reason="手动待步，只有点击才推进";
    else reason="自动中。单步和断链只在手动可用";
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
        syncModuleChrome();
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
        syncModuleChrome();
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
    syncModuleChrome();
}

void Workbench::unavailable(const QString& reason) {
    available_=false;
    feedback_->setText(reason);
    render();
    buttons();
}

bool Workbench::captureReady(const QString& kind) const {
    if (observations_<2) return false;
    const auto onTab=[&](const char* title) {
        return moduleTabs_ && moduleTabs_->tabText(moduleTabs_->currentIndex())==QString::fromUtf8(title);
    };
    if (kind=="stations") return onTab("工位") && liveScope();
    if (kind=="events") return onTab("事件") && liveScope() && historyTable_ && historyTable_->rowCount()>0;
    if (kind=="points") return onTab("点表") && liveScope() && pointTable_ && pointTable_->rowCount()>0;
    if (kind=="boundary") return onTab("边界") && liveScope();
    if (kind=="roster") return onTab("工位") && selection_.station_id==catalog::kStation40 && controllerPeerIds().size()==4 && !liveScope();
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
