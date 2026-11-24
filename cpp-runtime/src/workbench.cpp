#include "workbench.hpp"
#include "control.hpp"
#include "point_audit.hpp"
#include "schematic.hpp"
#include <QApplication>
#include <QCheckBox>
#include <QColor>
#include <QComboBox>
#include <QDateTime>
#include <QEvent>
#include <QFile>
#include <QFontDatabase>
#include <QFontInfo>
#include <QFontMetrics>
#include <QFrame>
#include <QGridLayout>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QLabel>
#include <QLineEdit>
#include <QListView>
#include <QListWidget>
#include <QMap>
#include <QPainter>
#include <QMessageBox>
#include <QPushButton>
#include <QScrollArea>
#include <QSignalBlocker>
#include <QSizePolicy>
#include <QStackedWidget>
#include <QStatusBar>
#include <QTabBar>
#include <QToolButton>
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
    table->verticalHeader()->setDefaultSectionSize(32);
    table->setAlternatingRowColors(true);
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
CartMark cartMark(const std::string& value) {
    if (value=="waiting") return CartMark::wait;
    if (value=="scanning" || value=="sorting") return CartMark::run;
    if (value=="station_complete") return CartMark::done;
    return CartMark::alert;
}
QString beijingTime(qint64 epochMs, const QString& pattern) {
    const QTimeZone zone("Asia/Shanghai");
    const QDateTime utc=QDateTime::fromMSecsSinceEpoch(epochMs).toUTC();
    if (!zone.isValid()) return utc.toString(pattern)+" UTC";
    return utc.toTimeZone(zone).toString(pattern);
}
QFont figureFont() {
    QFont font=QApplication::font();
    font.setPixelSize(13);
    return font;
}
void prepareComboPopup(QComboBox* box) {
    auto* view=new QListView(box);
    view->setObjectName("comboPopup");
    view->setFrameShape(QFrame::NoFrame);
    view->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    view->setAutoFillBackground(true);
    QPalette palette=view->palette();
    const QColor ink("#1f2933");
    const QColor paper("#ffffff");
    const QColor selected("#deeeee");
    const QColor selectedInk("#123d42");
    palette.setColor(QPalette::Base, paper);
    palette.setColor(QPalette::Text, ink);
    palette.setColor(QPalette::Window, paper);
    palette.setColor(QPalette::WindowText, ink);
    palette.setColor(QPalette::Highlight, selected);
    palette.setColor(QPalette::HighlightedText, selectedInk);
    view->setPalette(palette);
    view->setStyleSheet(
        "QListView { background:#ffffff; color:#1f2933; border:none; outline:none; }"
        "QListView::item { background:#ffffff; color:#1f2933; min-height:22px; padding:4px 8px; }"
        "QListView::item:hover { background:#e7f3f3; color:#123d42; }"
        "QListView::item:selected { background:#deeeee; color:#123d42; }");
    box->setView(view);
}
void setFigure(QTableWidgetItem* item) {
    item->setFont(figureFont());
}
class RunStack : public QStackedWidget {
public:
    using QStackedWidget::QStackedWidget;
    QSize sizeHint() const override { return currentWidget() ? currentWidget()->sizeHint() : QStackedWidget::sizeHint(); }
    QSize minimumSizeHint() const override { return currentWidget() ? currentWidget()->minimumSizeHint() : QStackedWidget::minimumSizeHint(); }
};
class PageStack : public QStackedWidget {
public:
    using QStackedWidget::QStackedWidget;
    QSize sizeHint() const override { return currentWidget() ? currentWidget()->sizeHint() : QStackedWidget::sizeHint(); }
    QSize minimumSizeHint() const override { return currentWidget() ? currentWidget()->minimumSizeHint() : QStackedWidget::minimumSizeHint(); }
};
class DetailFit : public QObject {
public:
    DetailFit(QScrollArea* scroll, QLabel* label) : QObject(scroll), scroll_(scroll), label_(label) {
        scroll->viewport()->installEventFilter(this);
    }
    bool eventFilter(QObject*, QEvent* event) override {
        if (event->type()==QEvent::Resize || event->type()==QEvent::Show) fit();
        return false;
    }
    void fit() {
        const int width=std::max(1, scroll_->viewport()->width());
        const int height=std::max(label_->heightForWidth(width), label_->sizeHint().height());
        if (label_->minimumHeight()!=height) label_->setMinimumHeight(height);
    }
private:
    QScrollArea* scroll_;
    QLabel* label_;
};
class ViewportFill : public QObject {
public:
    ViewportFill(QScrollArea* area, QWidget* page) : QObject(area), area_(area), page_(page) {
        area->viewport()->installEventFilter(this);
    }
    bool eventFilter(QObject*, QEvent* event) override {
        if (event->type()!=QEvent::Resize) return false;
        const int height=area_->viewport()->height();
        if (height>0 && page_->minimumHeight()!=height) page_->setMinimumHeight(height);
        return false;
    }
private:
    QScrollArea* area_;
    QWidget* page_;
};
void pinText(QWidget* root) {
    for (auto* label : root->findChildren<QLabel*>()) {
        label->setAlignment(label->wordWrap() ? Qt::AlignLeft | Qt::AlignTop : Qt::AlignLeft | Qt::AlignVCenter);
        const auto name=label->objectName();
        if (name=="pageTitle" || name=="title" || name=="section" || name=="fieldLabel" || name=="stationTitle")
            label->setSizePolicy(QSizePolicy::Preferred, QSizePolicy::Maximum);
    }
}
QString loadedUiFamily;
// 视觉 token 集中在此。父审选定风格后只改这些值，不改模块结构。
// 参考：Carbon Tabs（页签状态与同页切换）、Ant Design Layout（顶栏上下文 + 内容区）、Ant Design Tabs（功能页而非进度条）。
struct Tone {
    const char* canvas="#f3f4f6";
    const char* surface="#ffffff";
    const char* ink="#1f2933";
    const char* muted="#52606d";
    const char* line="#d9e2ec";
    const char* accent="#176363";
    const char* accentSoft="#deeeee";
    const char* warn="#8a5a12";
    const char* warnSoft="#fbf6ee";
};
const Tone tone;
QString consoleStyle() {
    const auto t=tone;
    return QString(R"(
        QWidget { @@FONT@@font-size: 13px; }
        QMainWindow, QWidget#centralRoot { background:%1; color:%3; }
        QWidget#identityBar, QWidget#catalogPane { background:%2; }
        QWidget#identityBar { background:#123d42; border-bottom:1px solid #0d3034; }
        QWidget#identityBar QLabel { color:#f2faf9; }
        QWidget#pageTabRow { background:%1; }
        QWidget#commandPane { background:#f5f8f8; border:1px solid #b8cccc; border-radius:10px; }
        QLabel#simOrder { background:#ffffff; color:#214c50; border:1px solid #d9e2ec; padding:6px 8px; }
        QLabel#scopeBanner[state="warn"] { background:#fff2d9; color:#744509; padding:4px; }
        QTableWidget#stageTable::item:selected { background:#d1e9e7; color:#123d42; }
        QLabel#currentStation, QLabel#runtimeState { background:#23565b; border:1px solid #478084; padding:3px 10px; font-weight:700; }
        QLabel#runtimeState[mode="manual"] { background:#d6eeeb; color:#123d42; }
        QWidget#surface { background:%2; border:1px solid %4; border-radius:10px; }
        QLabel#title { font-size:16px; font-weight:600; color:#ffffff; }
        QLabel#pageTitle, QLabel#emptyTitle { font-size:18px; font-weight:600; color:%3; }
        QLabel#emptySource, QLabel#emptyAction { color:%5; font-size:13px; }
        QLabel#fieldLabel { color:%5; font-size:12px; }
        QLabel#section, QLabel#collectChangeLabel { color:%3; font-size:14px; font-weight:600; }
        QLabel#collectQuality[state="ok"], QLabel#collectLink[state="ok"] { color:#1f7a4d; font-weight:600; }
        QLabel#collectQuality[state="warn"], QLabel#collectLink[state="warn"] { color:%8; font-weight:600; }
        QLabel#collectQuality[state="unknown"], QLabel#collectLink[state="unknown"], QLabel#collectPlc[state="unknown"] { color:%5; }
        QLabel#muted, QLabel#controlReason, QLabel#controlFeedback { color:%5; font-size:12px; }
        QLabel#simBadge { background:%6; color:#ffffff; padding:2px 8px; border-radius:8px; font-size:12px; font-weight:600; }
        QLabel#statusChip { background:%1; color:%5; padding:1px 6px; border-radius:8px; font-size:12px; font-weight:600; }
        QLabel#statusChip[state="sim"] { background:%7; color:%6; }
        QLabel#statusChip[state="wait"] { background:%9; color:%8; }
        QComboBox { background:%2; color:%3; border:1px solid %4; border-radius:8px; padding:2px 22px 2px 10px; min-height:26px; }
        QComboBox:hover, QComboBox:focus { border:1px solid %6; }
        QComboBox::drop-down { subcontrol-origin: padding; subcontrol-position: center right; width:22px; border:none; background:transparent; }
        QComboBox::down-arrow { image: url(:/icons/chevron.png); width:14px; height:8px; }
        QComboBox QAbstractItemView { background:%2; color:%3; border:1px solid %4; border-radius:8px; selection-background-color:%7; selection-color:#123d42; outline:0; padding:4px; }
        QComboBox QAbstractItemView::item { color:%3; background:%2; min-height:22px; padding:4px 8px; border-radius:6px; }
        QComboBox QAbstractItemView::item:hover { background:%7; color:#123d42; }
        QComboBox QAbstractItemView::item:selected { background:%7; color:#123d42; }
        QComboBoxPrivateContainer { background:%2; color:%3; border:1px solid %4; border-radius:8px; }
        QMenu { background:%2; color:%3; border:1px solid %4; border-radius:8px; padding:4px; }
        QMenu::item { color:%3; background:transparent; padding:6px 12px; border-radius:6px; }
        QMenu::item:selected { background:%7; color:#123d42; }
        QPushButton { background:%2; border:1px solid %4; border-radius:8px; padding:4px 12px; min-height:28px; color:%3; }
        QPushButton:hover { background:%7; }
        QPushButton:focus { border:1px solid %6; }
        QPushButton#primary, QPushButton#modeButton[emphasis="yes"] { background:%6; color:#ffffff; border:none; }
        QPushButton#textLink { background:transparent; border:none; border-radius:8px; color:%6; font-weight:600; min-height:24px; padding:2px 6px; }
        QPushButton#textLink:hover { background:%7; }
        QPushButton:disabled, QPushButton#primary:disabled, QPushButton#modeButton:disabled { background:%1; color:#8b96a6; border:none; }
        QPushButton#stationCard { min-width:52px; }
        QPushButton#stationCard:checked { background:%6; color:#ffffff; border:1px solid %6; }
        QTableWidget { background:%2; alternate-background-color:#f7f8fa; gridline-color:transparent; border:1px solid %4; border-radius:8px; font-size:13px; selection-background-color:%7; selection-color:%3; outline:none; }
        QTableWidget:focus { border:1px solid %6; }
        QTableWidget::item { padding:4px 8px; border:none; }
        QTableWidget::item:hover { background:#eef1f4; }
        QTableWidget::item:selected { background:%7; color:%3; }
        QTableWidget::item:selected:hover { background:#d9e6f2; }
        QHeaderView::section { background:#eef3f4; color:%5; font-weight:600; border:none; border-bottom:1px solid %4; padding:8px; font-size:12px; }
        QListWidget { background:%2; border:1px solid %4; border-radius:8px; outline:none; font-size:13px; }
        QListWidget:focus { border:1px solid %6; }
        QListWidget::item { padding:8px 12px; border:none; }
        QListWidget::item:hover { background:#eef1f4; }
        QListWidget::item:selected { background:%7; color:%3; }
        QLineEdit { background:%2; border:1px solid %4; border-radius:8px; padding:2px 8px; min-height:26px; color:%3; }
        QLineEdit:focus { border:1px solid %6; }
        QCheckBox { color:%3; spacing:6px; }
        QToolButton { background:transparent; color:%6; border:none; border-radius:8px; font-weight:600; padding:2px 4px; }
        QToolButton:hover { background:%7; }
        QToolButton:focus { border:2px solid %6; }
        QTabBar#moduleTabs::tab { background:transparent; color:%5; padding:4px 12px; min-height:22px; border:none; border-top-left-radius:8px; border-top-right-radius:8px; margin-right:2px; font-size:13px; }
        QTabBar#moduleTabs::tab:hover { color:%3; background:%7; }
        QTabBar#moduleTabs::tab:selected { color:%6; font-weight:600; background:%2; }
        QTabBar#moduleTabs::tab:focus { border-bottom:2px solid %6; }
        QStatusBar { background:%2; color:%5; border-top:1px solid %4; }
        QScrollArea { border:none; background:%1; }
    )").replace("@@FONT@@", loadedUiFamily.isEmpty() ? QString() : QString("font-family: \"%1\"; ").arg(loadedUiFamily))
      .arg(t.canvas, t.surface, t.ink, t.line, t.muted, t.accent, t.accentSoft, t.warn, t.warnSoft);
}
QString doubtSummary(const QString& codes) {
    if (codes.isEmpty()) return "无疑点";
    const QMap<QString, QString> text{
        {"plc-auto-not-host-mode", "PLC 自动不是上位机手自动"},
        {"heartbeat-not-passage", "心跳不能当作通行授权"},
        {"read-not-grant", "这是读取点，不能当成放行"},
        {"handshake-unconfirmed", "握手规则尚未确认"},
        {"replenish-request-not-lwms-complete", "补料请求不是 LWMS 完成"},
        {"duplicate-address", "地址与其他行重复，保持原表"},
        {"reserved", "来源标为保留"},
        {"blank-name", "来源没有名称"},
        {"dbx-non-bool", "DBX 地址上的类型不是 BOOL"},
        {"length-unit-unconfirmed", "长度单位尚未确认"},
        {"shared-cart", "三台小车共用这一地址"},
        {"clear-handshake-unconfirmed", "清空握手尚未确认"},
        {"not-executable-write", "不能当成可执行写点"},
        {"direction-conflict", "方向和所在段不一致"},
        {"do-not-rewrite-db", "不把地址改写到另一个 DB"},
        {"scan-trigger-unconfirmed", "扫码触发尚未确认"},
        {"lower-layer-not-enabled", "下层料仓来源写明不需要"},
        {"cart-complete-scope", "只表示这台小车本轮完成"},
        {"not-complete-write", "不推定为补料完成写入"},
        {"来源纵有写入字样，本程序也不生成写点", "来源有写入字样，这里不生成写点"},
    };
    QStringList parts;
    for (const auto& code : codes.split(';', Qt::SkipEmptyParts)) {
        const auto key=code.trimmed();
        parts.push_back(text.contains(key) ? text.value(key) : "未翻译："+key);
    }
    return parts.join("；");
}
int addFontFile(const QString& path) {
    if (!path.startsWith(QLatin1String(":/")) && !QFile::exists(path)) return -1;
    return QFontDatabase::addApplicationFont(path);
}
void installUiFont() {
    const QString beside=QCoreApplication::applicationDirPath();
    const QStringList files={
        beside+"/fonts/NotoSansCJKsc-Regular.otf",
        beside+"/fonts/NotoSansCJKsc-Bold.otf",
        beside+"/NotoSansCJKsc-Regular.otf",
        beside+"/NotoSansCJKsc-Bold.otf",
        "/usr/share/fonts/opentype/noto/NotoSansCJK-Regular.ttc",
        "/usr/share/fonts/opentype/noto/NotoSansCJK-Bold.ttc",
    };
    QString loaded;
    for (const auto& file : files) {
        const int id=addFontFile(file);
        if (id<0) continue;
        const auto names=QFontDatabase::applicationFontFamilies(id);
        if (loaded.isEmpty()) loaded=names.contains("Noto Sans CJK SC") ? QString("Noto Sans CJK SC") : (names.isEmpty() ? QString() : names.front());
    }
    QStringList families;
    if (!loaded.isEmpty()) families<<loaded;
    families<<"Microsoft YaHei UI"<<"Microsoft YaHei"<<"Noto Sans CJK SC"<<"Noto Sans SC"<<"Source Han Sans SC";
    QFont font;
    font.setFamilies(families);
    font.setStyleHint(QFont::SansSerif, QFont::PreferAntialias);
    font.setPixelSize(13);
    QApplication::setFont(font);
    loadedUiFamily=loaded;
}
QString readableObject(const std::string& object) {
    if(object=="sim.three-cart.v1") return "外装 · 工位 140";
    if(object=="local-sim") return "本地模拟";
    return QString::fromStdString(object);
}
QScrollArea* scrollPage(QWidget* content) {
    content->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Expanding);
    auto* scroll=new QScrollArea;
    scroll->setWidgetResizable(true);
    scroll->setAlignment(Qt::AlignTop | Qt::AlignLeft);
    scroll->setFrameShape(QFrame::NoFrame);
    scroll->setWidget(content);
    new ViewportFill(scroll, content);
    return scroll;
}
QString showLogText(QString text) {
    text.replace("station_complete", "工位完成");
    text.replace("scanning", "扫码中");
    text.replace("sorting", "分拣中");
    text.replace("blocked", "模拟阻塞");
    text.replace("waiting", "等待扫码");
    text.replace("质量 good", "质量正常");
    text.replace("链路 up", "链路接通");
    text.replace("source_bad", "源质量异常");
    text.replace("sim.three-cart.v1", "工位 140");
    text.replace("模拟源 工位 140 模拟源", "模拟源");
    text.replace("这是模拟证据，不是现场设备离线，也没有 S7 重连。", "");
    text.replace("没有真实客户端重连。", "");
    if (text.contains("链路断开") && !text.contains("模拟链路断开")) text.replace("链路断开", "模拟链路断开");
    return text.simplified();
}
void markState(QLabel* label, const QString& state) {
    if (!label) return;
    label->setProperty("state", state);
    label->style()->unpolish(label);
    label->style()->polish(label);
}
}

Workbench::Workbench(std::filesystem::path directory)
    : directory_(std::move(directory)), catalog_(catalog::Directory::qiantang()) {
    const auto error=catalog::Directory::validate(catalog_);
    if (!error.empty()) throw std::runtime_error("catalog fixture: "+error);
    Q_INIT_RESOURCE(fonts);
    QApplication::setStyle("Fusion");
    installUiFont();
    setFont(QApplication::font());
    setWindowTitle("物流 · 模拟工作台");
    resize(1280, 800);
    setMinimumSize(1100, 650);
    setStyleSheet(consoleStyle());

    auto* identity=new QWidget;
    identity->setObjectName("identityBar");
    identity->setFixedHeight(44);
    auto* identityLayout=new QHBoxLayout(identity);
    identityLayout->setContentsMargins(16, 8, 16, 8);
    identityLayout->setSpacing(12);
    auto* title=new QLabel("物流工作台");
    title->setObjectName("title");
    fit(title);
    auto* sim=new QLabel("模拟环境");
    sim->setObjectName("simBadge");
    sim->setToolTip("仅本地合成数据。不表示 PLC、LWMS、华睿或库卡已连接。");
    sim->setMinimumWidth(sim->fontMetrics().horizontalAdvance(sim->text())+20);
    processLabel_=new QLabel("采集心跳 待确认");
    processLabel_->setObjectName("processState");
    runtimeLabel_=new QLabel("上位机 未知");
    runtimeLabel_->setObjectName("runtimeState");
    heartbeatLabel_=new QLabel("无心跳");
    heartbeatLabel_->setObjectName("heartbeatState");
    fit(processLabel_); fit(runtimeLabel_); fit(heartbeatLabel_);
    identityLayout->addWidget(title);
    identityLayout->addWidget(sim);
    stationBadge_=new QLabel("当前工位  未配置");
    stationBadge_->setObjectName("currentStation");
    identityLayout->addWidget(stationBadge_);
    identityLayout->addWidget(runtimeLabel_);
    identityLayout->addStretch(1);
    identityLayout->addWidget(processLabel_);
    identityLayout->addWidget(heartbeatLabel_);

    auto* context=new QWidget;
    context->setObjectName("catalogPane");
    context->setFixedHeight(44);
    auto* contextLayout=new QHBoxLayout(context);
    contextLayout->setContentsMargins(16, 8, 16, 8);
    contextLayout->setSpacing(8);
    auto addCombo=[&](const QString& name, QComboBox** slot, const QString& objectName) {
        auto* label=fieldLabel(name, context);
        *slot=new QComboBox(context);
        (*slot)->setObjectName(objectName);
        prepareComboPopup(*slot);
        (*slot)->setSizePolicy(QSizePolicy::Preferred, QSizePolicy::Fixed);
        (*slot)->setMaximumWidth(168);
        (*slot)->setFocusPolicy(Qt::StrongFocus);
        label->setBuddy(*slot);
        contextLayout->addWidget(label);
        contextLayout->addWidget(*slot);
    };
    addCombo("工厂", &factoryCombo_, "factoryCombo");
    addCombo("业务模式", &areaCombo_, "areaCombo");
    addCombo("线体", &lineCombo_, "lineCombo");
    addCombo("工位", &stationCombo_, "stationCombo");
    addCombo("关联 PLC", &plcCombo_, "plcCombo");
    factoryCombo_->setMaximumWidth(132);
    contextLayout->addStretch(1);

    auto* commandPane=new QWidget;
    commandPane->setObjectName("commandPane");
    auto* commandLayout=new QHBoxLayout(commandPane);
    commandLayout->setContentsMargins(8, 6, 8, 6);
    commandLayout->setSpacing(8);
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
    controlReason_->setWordWrap(false);
    feedback_=new QLabel;
    feedback_->setObjectName("controlFeedback");
    feedback_->setWordWrap(false);
    auto* actionStatus=new QVBoxLayout;
    actionStatus->setSpacing(2);
    actionStatus->addWidget(controlReason_);
    feedback_->setWordWrap(true);
    actionStatus->addWidget(feedback_);
    commandLayout->addLayout(actionStatus, 1);
    for (auto* button : {modeButton_,step_,disconnect_}) {
        button->setMinimumHeight(36);
        commandLayout->addWidget(button);
    }

    schematic_=new SchematicView;
    schematic_->setMinimumHeight(88);
    schematic_->setMaximumHeight(184);
    schematic_->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Preferred);
    cardRow_=new QWidget;
    cardRow_->setFixedHeight(44);
    new QHBoxLayout(cardRow_);
    cardRow_->layout()->setContentsMargins(0, 4, 0, 0);
    auto* workHeading=new QLabel("当前作业");
    workHeading->setObjectName("section");
    stationMeta_=new QLabel;
    stationMeta_->setObjectName("muted");
    stationMeta_->setWordWrap(true);
    interfaceLine_=new QLabel;
    interfaceLine_->setWordWrap(true);
    scopeBanner_=new QLabel;
    scopeBanner_->setObjectName("scopeBanner");
    scopeBanner_->setWordWrap(true);

    auto* run=new QWidget;
    auto* runLayout=new QVBoxLayout(run);
    runLayout->setContentsMargins(16, 12, 16, 8);
    runLayout->setSpacing(8);
    auto* emptyRun=new QWidget(run);
    emptyRun->setObjectName("emptyRun");
    emptyRun->setSizePolicy(QSizePolicy::Preferred, QSizePolicy::Maximum);
    auto* emptyLayout=new QVBoxLayout(emptyRun);
    emptyLayout->setContentsMargins(0, 8, 0, 0);
    emptyLayout->setSpacing(8);
    emptyLayout->setAlignment(Qt::AlignTop);
    auto addEmpty=[&](const QString& text, const char* name){
        auto* label=new QLabel(text, emptyRun);
        label->setObjectName(name);
        label->setWordWrap(true);
        label->setSizePolicy(QSizePolicy::Preferred, QSizePolicy::Maximum);
        emptyLayout->addWidget(label);
    };
    addEmpty("尚未配置工位", "emptyTitle");
    addEmpty("当前 PLC 未绑定模拟源", "emptySource");
    addEmpty("暂无法执行模拟操作", "emptyAction");
    emptyRun->hide();
    auto* workspace=new QWidget(run);
    workspace->setObjectName("runWorkspace");
    auto* workspaceLayout=new QVBoxLayout(workspace);
    workspaceLayout->setContentsMargins(0, 4, 0, 4);
    workspaceLayout->setSpacing(4);
    auto* runStack=new RunStack(run);
    runStack->setObjectName("runStack");
    runStack->addWidget(emptyRun);
    runStack->addWidget(workspace);
    runLayout->addWidget(runStack, 1);
    auto* runTail=new QWidget(run);
    runTail->setObjectName("runTail");
    runTail->setSizePolicy(QSizePolicy::Preferred, QSizePolicy::Expanding);
    runLayout->addWidget(runTail);
    runLayout->addWidget(commandPane);
    auto* runBody=new QWidget(run);
    auto* runColumns=new QHBoxLayout(runBody);
    runColumns->setContentsMargins(0, 0, 0, 0);
    runColumns->setSpacing(16);
    auto* facts=new QWidget(runBody);
    auto* factsLayout=new QVBoxLayout(facts);
    factsLayout->setContentsMargins(0, 0, 0, 0);
    factsLayout->setSpacing(4);
    factsLayout->addWidget(workHeading);
    factsLayout->addWidget(stationMeta_);
    factsLayout->addWidget(interfaceLine_);
    factsLayout->addWidget(scopeBanner_);
    auto* overview=new QWidget(runBody);
    overview->setObjectName("surface");
    overview->setMinimumWidth(295);
    auto* overviewLayout=new QVBoxLayout(overview);
    overviewLayout->setContentsMargins(8, 4, 8, 4);
    overviewLayout->setSpacing(4);
    auto* overviewLabel=new QLabel("工位与料车拓扑", overview);
    overviewLabel->setObjectName("section");
    overviewLayout->addWidget(overviewLabel);
    overviewLayout->addWidget(schematic_);
    overviewLayout->addWidget(cardRow_);
    facts->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Maximum);
    overview->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Maximum);
    auto* left=new QWidget(runBody);
    auto* leftLayout=new QVBoxLayout(left);
    leftLayout->setContentsMargins(0,0,0,0);
    leftLayout->setSpacing(8);
    auto* right=new QWidget(runBody);
    auto* rightLayout=new QVBoxLayout(right);
    rightLayout->setContentsMargins(0,0,0,0);
    rightLayout->setSpacing(8);
    runColumns->addWidget(left, 2);
    runColumns->addWidget(right, 3);
    recentTable_=new QTableWidget(workspace);
    recentTable_->setObjectName("recentTable");
    prepareTable(recentTable_, {"时间", "记录"});
    recentTable_->horizontalHeader()->setSectionResizeMode(1, QHeaderView::Stretch);
    recentTable_->setColumnWidth(0,88);
    recentTable_->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Expanding);
    recentTable_->setMinimumHeight(76);
    recentTable_->verticalHeader()->setDefaultSectionSize(28);
    recentTable_->setVerticalScrollBarPolicy(Qt::ScrollBarAsNeeded);
    auto* recentLabel=new QLabel("近期记录", workspace);
    recentLabel->setObjectName("section");
    auto* recentScope=new QLabel("当前工位没有模拟记录。", workspace);
    recentScope->setObjectName("recentScope");
    recentScope->setWordWrap(true);
    recentScope->hide();
    rightLayout->addWidget(facts);
    simOrder_=new QLabel(right);
    simOrder_->setObjectName("simOrder");
    simOrder_->setWordWrap(true);
    rightLayout->addWidget(simOrder_);
    resultTable_=new QTableWidget(right);
    resultTable_->setObjectName("runResultTable");
    prepareTable(resultTable_, {"执行与采集", "当前结果"});
    resultTable_->verticalHeader()->setDefaultSectionSize(24);
    resultTable_->setFixedHeight(122);
    resultTable_->horizontalHeader()->setSectionResizeMode(1,QHeaderView::Stretch);
    resultTable_->setColumnWidth(0,104);
    rightLayout->addWidget(resultTable_);
    rightLayout->addWidget(recentLabel);
    rightLayout->addWidget(recentScope);
    rightLayout->addWidget(recentTable_, 1);
    workspaceLayout->addWidget(runBody, 1);
    workspace->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Expanding);

    detailTable_=new QTableWidget;
    detailTable_->setObjectName("detailTable");
    prepareTable(detailTable_, {"字段", "值"});
    peerTable_=new QTableWidget;
    peerTable_->setObjectName("peerTable");
    prepareTable(peerTable_, {"工位号", "工位名称", "标记"});
    peerTable_->setSelectionBehavior(QAbstractItemView::SelectRows);
    peerTable_->setSelectionMode(QAbstractItemView::SingleSelection);
    peerTable_->setFocusPolicy(Qt::StrongFocus);
    peerTable_->setMaximumHeight(168);
    auto* stagesTitle=new QLabel("作业阶段",left);
    stagesTitle->setObjectName("section");
    leftLayout->addWidget(overview);
    leftLayout->addWidget(stagesTitle);
    stageTable_=new QTableWidget(left);
    stageTable_->setObjectName("stageTable");
    prepareTable(stageTable_, {"小车", "阶段", "说明"});
    stageTable_->setSelectionBehavior(QAbstractItemView::SelectRows);
    stageTable_->setSelectionMode(QAbstractItemView::SingleSelection);
    stageTable_->verticalHeader()->setDefaultSectionSize(22);
    stageTable_->horizontalHeader()->setStyleSheet("QHeaderView::section { padding:3px 8px; }");
    stageTable_->setFixedHeight(112);
    stageTable_->horizontalHeader()->setSectionResizeMode(2,QHeaderView::Stretch);
    stageTable_->setColumnWidth(0,72);
    stageTable_->setColumnWidth(1,112);
    leftLayout->addWidget(stageTable_);
    stageGuide_=new QLabel(left);
    stageGuide_->setObjectName("stageGuide");
    stageGuide_->setWordWrap(true);
    leftLayout->addWidget(stageGuide_);
    connect(stageTable_,&QTableWidget::currentCellChanged,this,[this]{showStageResult();});
    leftLayout->addStretch(1);
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
    prepareTable(pointTable_, {"名称", "地址", "疑点摘要", "段"});
    pointTable_->setSelectionBehavior(QAbstractItemView::SelectRows);
    pointTable_->setSelectionMode(QAbstractItemView::SingleSelection);
    pointTable_->setFocusPolicy(Qt::StrongFocus);
    pointTable_->horizontalHeader()->setStretchLastSection(false);
    pointTable_->horizontalHeader()->setSectionResizeMode(0, QHeaderView::Stretch);
    pointTable_->horizontalHeader()->setSectionResizeMode(1, QHeaderView::ResizeToContents);
    pointTable_->horizontalHeader()->setSectionResizeMode(2, QHeaderView::Stretch);
    pointTable_->horizontalHeader()->setSectionResizeMode(3, QHeaderView::ResizeToContents);
    pointTable_->verticalHeader()->setDefaultSectionSize(28);
    pointScope_=new QLabel;
    pointScope_->setWordWrap(true);
    pointSearch_=new QLineEdit;
    pointSearch_->setObjectName("pointSearch");
    pointSearch_->setPlaceholderText("搜索名称或地址");
    pointSearch_->setClearButtonEnabled(true);
    pointSearch_->setMaximumWidth(280);
    pointDoubtsOnly_=new QCheckBox("只看有疑点");
    pointDoubtsOnly_->setObjectName("pointDoubtsOnly");
    pointEmpty_=new QLabel("此工位没有点表。");
    pointEmpty_->setObjectName("muted");
    pointEmpty_->setWordWrap(true);
    pointDetail_=new QLabel("选择一行查看来源原文。英文审计码只出现在这里。");
    pointDetail_->setObjectName("pointDetail");
    pointDetail_->setWordWrap(true);
    pointDetail_->setAlignment(Qt::AlignLeft | Qt::AlignTop);
    pointDetail_->setSizePolicy(QSizePolicy::Preferred, QSizePolicy::Minimum);
    auto* pointPage=new QWidget;
    auto* pointLayout=new QVBoxLayout(pointPage);
    pointLayout->setContentsMargins(16, 12, 16, 12);
    pointLayout->setSpacing(8);
    auto* pointHeading=new QLabel("点表", pointPage);
    pointHeading->setObjectName("pageTitle");
    auto* pointTools=new QHBoxLayout;
    pointTools->addWidget(pointSearch_);
    pointTools->addWidget(pointDoubtsOnly_);
    pointTools->addStretch(1);
    auto* detailBox=new QWidget;
    detailBox->setObjectName("surface");
    auto* detailLayout=new QVBoxLayout(detailBox);
    detailLayout->setContentsMargins(12, 8, 12, 8);
    auto* detailTitle=new QLabel("选中行", detailBox);
    detailTitle->setObjectName("section");
    auto* pointDetailScroll=new QScrollArea(detailBox);
    pointDetailScroll->setObjectName("pointDetailScroll");
    pointDetailScroll->setWidgetResizable(true);
    pointDetailScroll->setFrameShape(QFrame::NoFrame);
    pointDetailScroll->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    pointDetailScroll->setVerticalScrollBarPolicy(Qt::ScrollBarAsNeeded);
    pointDetailScroll->setWidget(pointDetail_);
    pointDetailScroll->setMinimumHeight(120);
    pointDetailScroll->setMaximumHeight(220);
    pointDetailScroll->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Preferred);
    new DetailFit(pointDetailScroll, pointDetail_);
    detailLayout->addWidget(detailTitle);
    detailLayout->addWidget(pointDetailScroll);
    pointLayout->addWidget(pointHeading);
    pointLayout->addWidget(pointScope_);
    pointLayout->addLayout(pointTools);
    pointLayout->addWidget(pointEmpty_);
    pointLayout->addWidget(pointTable_, 1);
    pointLayout->addWidget(detailBox);

    auto* boundaryPage=new QWidget;
    auto* boundaryLayout=new QVBoxLayout(boundaryPage);
    boundaryLayout->setContentsMargins(16, 12, 16, 16);
    boundaryLayout->setSpacing(8);
    auto* boundaryHeading=new QLabel("接口状态", boundaryPage);
    boundaryHeading->setObjectName("pageTitle");
    auto* boundaryIntro=new QLabel("只列出业务链上的接口。未连接的项不能配置，也没有保存。", boundaryPage);
    boundaryIntro->setWordWrap(true);
    boundaryIntro->setObjectName("muted");
    auto* interfaceList=new QListWidget(boundaryPage);
    interfaceList->setObjectName("interfaceList");
    interfaceList->setFocusPolicy(Qt::StrongFocus);
    interfaceList->setFixedWidth(168);
    interfaceList->setMinimumHeight(240);
    interfaceList->setSizePolicy(QSizePolicy::Fixed, QSizePolicy::Expanding);
    interfaceList->setVerticalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    struct InterfaceItem { QString name, chip, summary, detail; };
    const InterfaceItem interfaces[]={
        {"采集", "仅模拟", "手自动、单步和断链可用，只作用于工位 140。", "采集器独占这四条模拟命令。手动待步不推进。恢复自动要确认。心跳只说明采集进程还在，不是 PLC 在线。切页和筛选不改变采集。没有启动或停止采集的按钮。"},
        {"扫码查单", "待补映射", "还不能向 LWMS 查询 SPS 订单。", "分拣意图是扫料车二维码后再查单。后续机械臂共享成员未配置。库位、箱、料、订单都不显示成已查到。不能保存映射。"},
        {"补料", "未闭环", "PLC 请求补料还不是完成回执。", "请求补料不等于 LWMS 已确认。这里不填数量，也没有补料完成按钮。"},
        {"LWMS", "未连接", "没有库存数量，也没有连接动作。", "协议未实现。设备连接保持未探测。不能假装连接成功。"},
        {"库卡通行", "未授权", "自动、心跳和设备运行都不能放行。", "已知顺序是请求进入、同意、进入完成、请求离开、离开完成，再到安全点。进/出完成不提前当成安全点。"},
        {"华睿任务", "协议未定", "排序任务的数量和报文都还不能填写。", "来源会有单、箱、料、数量，以及汇总/严控、正序/倒序。完成数量、内衬阈值和通知报文未定。"},
    };
    auto* interfaceChip=new QLabel("仅模拟");
    interfaceChip->setObjectName("statusChip");
    interfaceChip->setProperty("state", "sim");
    auto* interfaceSummary=new QLabel;
    interfaceSummary->setWordWrap(true);
    auto* interfaceDetail=new QLabel;
    interfaceDetail->setWordWrap(true);
    interfaceDetail->setObjectName("muted");
    auto showInterface=[=](int row){
        auto* current=interfaceList->item(row);
        if(!current) return;
        const auto chip=current->data(Qt::UserRole+2).toString();
        interfaceChip->setText(chip);
        interfaceChip->setMinimumWidth(interfaceChip->fontMetrics().horizontalAdvance(chip)+28);
        interfaceSummary->setText(current->data(Qt::UserRole).toString());
        interfaceDetail->setText(current->data(Qt::UserRole+1).toString());
    };
    for(const auto& item: interfaces){
        auto* row=new QListWidgetItem(item.name, interfaceList);
        row->setData(Qt::UserRole, item.summary);
        row->setData(Qt::UserRole+1, item.detail);
        row->setData(Qt::UserRole+2, item.chip);
    }
    interfaceList->setCurrentRow(0);
    showInterface(0);
    QObject::connect(interfaceList, &QListWidget::currentRowChanged, boundaryPage, showInterface);
    auto* interfaceBody=new QHBoxLayout;
    interfaceBody->setSpacing(12);
    interfaceBody->addWidget(interfaceList);
    auto* interfaceDetailBox=new QWidget;
    interfaceDetailBox->setObjectName("surface");
    auto* interfaceDetailLayout=new QVBoxLayout(interfaceDetailBox);
    interfaceDetailLayout->setContentsMargins(12, 12, 12, 12);
    interfaceDetailLayout->addWidget(interfaceChip, 0, Qt::AlignLeft);
    interfaceDetailLayout->addWidget(interfaceSummary);
    interfaceDetailLayout->addWidget(interfaceDetail);
    interfaceDetailLayout->addStretch(1);
    interfaceDetailBox->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Expanding);
    interfaceBody->addWidget(interfaceDetailBox, 1);
    interfaceBody->setAlignment(Qt::AlignTop);
    boundaryLayout->addWidget(boundaryHeading);
    boundaryLayout->addWidget(boundaryIntro);
    boundaryLayout->addLayout(interfaceBody, 1);

    historyTable_=new QTableWidget;
    historyTable_->setObjectName("historyTable");
    prepareTable(historyTable_, {"序号", "时间", "三车阶段", "数据质量", "事件"});
    historyTable_->setSelectionBehavior(QAbstractItemView::SelectRows);
    historyTable_->setSelectionMode(QAbstractItemView::SingleSelection);
    historyTable_->setFocusPolicy(Qt::StrongFocus);
    historyTable_->horizontalHeader()->setSectionResizeMode(QHeaderView::Stretch);
    historyTable_->horizontalHeader()->setSectionResizeMode(0, QHeaderView::ResizeToContents);
    historyTable_->setMinimumHeight(72);
    eventNote_=new QLabel("无实时历史");
    eventNote_->setObjectName("eventNote");
    eventNote_->setWordWrap(true);
    eventSource_=new QLabel("来源：当前没有模拟流");
    eventSource_->setObjectName("muted");
    eventSource_->setWordWrap(true);
    historyEmpty_=new QLabel("没有可显示的帧。未挂接的工位不会复制工位 140 的历史。");
    historyEmpty_->setObjectName("muted");
    historyEmpty_->setWordWrap(true);
    historyFilter_=new QComboBox;
    historyFilter_->setObjectName("historyFilter");
    historyFilter_->addItem("全部", "all");
    historyFilter_->addItem("正常", "good");
    historyFilter_->addItem("不可用", "bad");
    historyFilter_->setFocusPolicy(Qt::StrongFocus);
    prepareComboPopup(historyFilter_);
    auto* historyPage=new QWidget;
    auto* historyLayout=new QVBoxLayout(historyPage);
    historyLayout->setContentsMargins(16, 12, 16, 16);
    historyLayout->setSpacing(8);
    auto* historyHeading=new QLabel("事件历史", historyPage);
    historyHeading->setObjectName("pageTitle");
    auto* historyZone=new QLabel("时间为北京时间（UTC+8），库内是 UTC 毫秒。", historyPage);
    historyZone->setObjectName("muted");
    auto* filterRow=new QHBoxLayout;
    auto* filterLabel=fieldLabel("质量", historyPage);
    filterRow->addWidget(filterLabel);
    filterRow->addWidget(historyFilter_, 1);
    filterRow->addStretch(1);
    historyLayout->addWidget(historyHeading);
    historyLayout->addWidget(historyZone);
    historyLayout->addWidget(eventSource_);
    historyLayout->addWidget(eventNote_);
    historyLayout->addLayout(filterRow);
    historyLayout->addWidget(historyEmpty_);
    historyLayout->addWidget(historyTable_, 1);

    logTable_=new QTableWidget;
    logTable_->setObjectName("logTable");
    prepareTable(logTable_, {"时间", "等级", "类别", "对象", "内容"});
    logTable_->setSelectionBehavior(QAbstractItemView::SelectRows);
    logTable_->setSelectionMode(QAbstractItemView::SingleSelection);
    logTable_->setFocusPolicy(Qt::StrongFocus);
    logTable_->horizontalHeader()->setSectionResizeMode(3, QHeaderView::ResizeToContents);
    logTable_->horizontalHeader()->setSectionResizeMode(4, QHeaderView::Stretch);
    logSearch_=new QLineEdit;
    logSearch_->setObjectName("logSearch");
    logSearch_->setPlaceholderText("检索内容");
    logSearch_->setClearButtonEnabled(true);
    logSearch_->setMaximumWidth(220);
    logCategory_=new QComboBox;
    logCategory_->setObjectName("logCategory");
    logCategory_->addItem("全部类别", "all");
    logCategory_->addItem("操作", "audit");
    logCategory_->addItem("采集", "collector");
    logCategory_->addItem("业务", "business");
    logCategory_->setMaximumWidth(120);
    logLevel_=new QComboBox;
    logLevel_->setObjectName("logLevel");
    logLevel_->addItem("全部等级", "all");
    logLevel_->addItem("信息", "info");
    logLevel_->addItem("警告", "warn");
    logLevel_->setMaximumWidth(120);
    prepareComboPopup(logCategory_);
    prepareComboPopup(logLevel_);
    auto* clearLogs=new QPushButton("清空筛选");
    clearLogs->setObjectName("textLink");
    logEmpty_=new QLabel("还没有日志。");
    logEmpty_->setObjectName("muted");
    logEmpty_->setWordWrap(true);
    logDetail_=new QLabel("选择一行查看详情。存储仍是 UTC 毫秒。");
    logDetail_->setObjectName("logDetail");
    logDetail_->setWordWrap(true);
    logDetail_->setMinimumHeight(168);
    logDetail_->setAlignment(Qt::AlignLeft | Qt::AlignTop);
    auto* logPage=new QWidget;
    auto* logLayout=new QVBoxLayout(logPage);
    logLayout->setContentsMargins(16, 12, 16, 12);
    logLayout->setSpacing(8);
    auto* logHeading=new QLabel("日志", logPage);
    logHeading->setObjectName("pageTitle");
    auto* logHint=new QLabel("时间列是北京时间（UTC+8），库内仍是 UTC 毫秒。链路和质量变化是模拟证据，不是现场设备离线，也没有 S7 重连。", logPage);
    logHint->setWordWrap(true);
    logHint->setObjectName("muted");
    auto* logTools=new QHBoxLayout;
    logTools->addWidget(logSearch_);
    logTools->addWidget(logCategory_);
    logTools->addWidget(logLevel_);
    logTools->addWidget(clearLogs);
    logTools->addStretch(1);
    auto* logDetailBox=new QWidget;
    logDetailBox->setObjectName("surface");
    logDetailBox->setMinimumHeight(208);
    auto* logDetailLayout=new QVBoxLayout(logDetailBox);
    logDetailLayout->setContentsMargins(12, 8, 12, 8);
    auto* logDetailTitle=new QLabel("详情", logDetailBox);
    logDetailTitle->setObjectName("section");
    logDetailLayout->addWidget(logDetailTitle);
    logDetailLayout->addWidget(logDetail_);
    logLayout->addWidget(logHeading);
    logLayout->addWidget(logHint);
    logLayout->addLayout(logTools);
    logLayout->addWidget(logEmpty_);
    logLayout->addWidget(logTable_, 1);
    logLayout->addWidget(logDetailBox);

    auto* collectPage=new QWidget;
    auto* collectLayout=new QVBoxLayout(collectPage);
    collectLayout->setContentsMargins(16, 12, 16, 16);
    collectLayout->setSpacing(8);
    auto* collectHeading=new QLabel("采集", collectPage);
    collectHeading->setObjectName("pageTitle");
    collectHeading->setSizePolicy(QSizePolicy::Preferred, QSizePolicy::Fixed);
    auto* collectBox=new QWidget;
    collectBox->setObjectName("surface");
    collectBox->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Maximum);
    auto* collectGrid=new QGridLayout(collectBox);
    collectGrid->setContentsMargins(12, 12, 12, 12);
    collectGrid->setHorizontalSpacing(16);
    collectGrid->setVerticalSpacing(8);
    auto addFact=[&](int row, const QString& key, QLabel** value){
        auto* name=new QLabel(key, collectBox);
        name->setObjectName("fieldLabel");
        *value=new QLabel("—", collectBox);
        collectGrid->addWidget(name, row, 0);
        collectGrid->addWidget(*value, row, 1);
    };
    addFact(0, "采集所有者", &collectOwner_);
    addFact(1, "模拟源", &collectLive_);
    QLabel* collectSeen=nullptr; QLabel* collectFrame=nullptr; QLabel* collectQuality=nullptr; QLabel* collectLink=nullptr; QLabel* collectMode=nullptr; QLabel* collectPlc=nullptr;
    addFact(2, "最近采集", &collectSeen);
    addFact(3, "帧序号", &collectFrame);
    addFact(4, "质量", &collectQuality);
    addFact(5, "链路", &collectLink);
    addFact(6, "模式", &collectMode);
    addFact(7, "设备连接", &collectPlc);
    collectSeen->setObjectName("collectSeen");
    collectFrame->setObjectName("collectFrame");
    collectQuality->setObjectName("collectQuality");
    collectLink->setObjectName("collectLink");
    collectMode->setObjectName("collectMode");
    collectPlc->setObjectName("collectPlc");
    collectChanges_=new QTableWidget;
    collectChanges_->setObjectName("collectChanges");
    prepareTable(collectChanges_, {"时间", "变化"});
    collectChanges_->horizontalHeader()->setSectionResizeMode(1, QHeaderView::Stretch);
    collectChanges_->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Expanding);
    auto* collectUnbound=new QLabel("当前 PLC 未绑定模拟源。顶栏心跳是采集进程，不是这个 PLC 的数据。", collectPage);
    collectUnbound->setObjectName("collectUnbound");
    collectUnbound->setWordWrap(true);
    collectUnbound->hide();
    auto* changeLabel=new QLabel("最近采集变化", collectPage);
    changeLabel->setObjectName("collectChangeLabel");
    collectLayout->addWidget(collectHeading);
    collectLayout->addWidget(collectBox);
    collectLayout->addWidget(changeLabel);
    collectLayout->addWidget(collectUnbound);
    collectLayout->addWidget(collectChanges_, 1);

    moduleTabs_=new QTabBar;
    moduleTabs_->setObjectName("moduleTabs");
    moduleTabs_->setDrawBase(false);
    moduleTabs_->setExpanding(false);
    moduleTabs_->setDocumentMode(true);
    moduleTabs_->setFocusPolicy(Qt::StrongFocus);
    moduleTabs_->setElideMode(Qt::ElideNone);
    auto* tabRow=new QWidget;
    tabRow->setObjectName("pageTabRow");
    tabRow->setFixedHeight(36);
    auto* tabLayout=new QHBoxLayout(tabRow);
    tabLayout->setContentsMargins(12, 4, 12, 0);
    tabLayout->setSpacing(0);
    tabLayout->addWidget(moduleTabs_);
    tabLayout->addStretch(1);
    moduleStack_=new PageStack;
    moduleStack_->setObjectName("moduleStack");
    moduleStack_->addWidget(run);
    moduleStack_->addWidget(scrollPage(stationPage));
    moduleStack_->addWidget(historyPage);
    moduleStack_->addWidget(pointPage);
    moduleStack_->addWidget(scrollPage(boundaryPage));
    moduleStack_->addWidget(logPage);
    moduleStack_->addWidget(collectPage);
    for (const auto& title : {QString("运行"), QString("工位"), QString("事件"), QString("点表"), QString("接口"), QString("日志"), QString("采集")})
        moduleTabs_->addTab(title);
    connect(moduleTabs_, &QTabBar::currentChanged, moduleStack_, &QStackedWidget::setCurrentIndex);

    auto* central=new QWidget;
    central->setObjectName("centralRoot");
    auto* root=new QVBoxLayout(central);
    root->setContentsMargins(0, 0, 0, 0);
    root->setSpacing(0);
    root->addWidget(identity);
    root->addWidget(tabRow);
    root->addWidget(context);
    root->addWidget(moduleStack_, 1);
    setCentralWidget(central);
    auto* closure=new QLabel("关窗不停止采集");
    fit(closure);
    auto* isolated=new QLabel("未连接现场设备");
    fit(isolated);
    statusBar()->addWidget(isolated);
    statusBar()->addPermanentWidget(closure);

    connect(schematic_, &SchematicView::stationActivated, this, [this](const QString& id) { activateSchematicStation(id); });
    connect(historyFilter_, qOverload<int>(&QComboBox::currentIndexChanged), this, [this](int) { applyHistoryFilter(); });
    connect(pointSearch_, &QLineEdit::textChanged, this, [this](const QString&) { applyPointFilter(); });
    connect(pointDoubtsOnly_, &QCheckBox::toggled, this, [this](bool) { applyPointFilter(); });
    connect(pointTable_, &QTableWidget::itemSelectionChanged, this, [this] { showPointDetail(); });
    pinText(this);
    connect(logSearch_, &QLineEdit::textChanged, this, [this](const QString&){ applyLogFilter(); });
    connect(logCategory_, qOverload<int>(&QComboBox::currentIndexChanged), this, [this](int){ applyLogFilter(); });
    connect(logLevel_, qOverload<int>(&QComboBox::currentIndexChanged), this, [this](int){ applyLogFilter(); });
    connect(clearLogs, &QPushButton::clicked, this, [this]{
        logSearch_->clear();
        logCategory_->setCurrentIndex(0);
        logLevel_->setCurrentIndex(0);
        applyLogFilter();
    });
    connect(logTable_, &QTableWidget::itemSelectionChanged, this, [this]{ showLogDetail(); });
    setTabOrder(moduleTabs_, factoryCombo_);
    setTabOrder(factoryCombo_, areaCombo_);
    setTabOrder(areaCombo_, lineCombo_);
    setTabOrder(lineCombo_, stationCombo_);
    setTabOrder(stationCombo_, plcCombo_);
    setTabOrder(plcCombo_, modeButton_);
    setTabOrder(modeButton_, step_);
    setTabOrder(step_, disconnect_);
    connect(factoryCombo_, qOverload<int>(&QComboBox::currentIndexChanged), this, [this](int) {
        if (updating_) return;
        auto next=selection_;
        next.factory_id=comboId(factoryCombo_).toStdString();
        next.area_id.clear();
        next.line_id.clear();
        next.plc_id.clear();
        setSelection(next);
    });
    connect(areaCombo_, qOverload<int>(&QComboBox::currentIndexChanged), this, [this](int) {
        if (updating_) return;
        auto next=selection_;
        next.area_id=comboId(areaCombo_).toStdString();
        next.line_id.clear();
        next.plc_id.clear();
        setSelection(next);
    });
    connect(lineCombo_, qOverload<int>(&QComboBox::currentIndexChanged), this, [this](int) {
        if (updating_) return;
        auto next=selection_;
        next.line_id=comboId(lineCombo_).toStdString();
        setSelection(next);
    });
    connect(stationCombo_, qOverload<int>(&QComboBox::currentIndexChanged), this, [this](int) {
        if (!updating_) activateSchematicStation(comboId(stationCombo_));
    });
    connect(plcCombo_, qOverload<int>(&QComboBox::currentIndexChanged), this, [this](int) {
        if (updating_) return;
        auto next=selection_;
        next.plc_id=comboId(plcCombo_).toStdString();
        next.line_id.clear();
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

void Workbench::openDiagnostic(const QString& title) {
    if (!moduleTabs_) return;
    for (int index=0; index<moduleTabs_->count(); ++index) {
        if (moduleTabs_->tabText(index)!=title) continue;
        moduleTabs_->setCurrentIndex(index);
        return;
    }
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
    if (selected.isEmpty()) box->addItem("未配置", QString());
    for (const auto& item : items) {
        box->addItem(item.second, item.first);
        box->setItemData(box->count()-1, item.first, Qt::ToolTipRole);
    }
    const int index=selected.isEmpty() ? 0 : box->findData(selected);
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
    if (historyEmpty_) {
        historyEmpty_->setVisible(historyTable_->rowCount()==0 || visible==0);
        if (historyTable_->rowCount()==0) historyEmpty_->setText("本工位没有模拟历史，也不会显示工位 140 的帧。");
        else if (visible==0) historyEmpty_->setText("当前质量筛选没有匹配帧。源历史仍在，切回全部即可看到。");
    }
}
void Workbench::applyPointFilter() {
    if (!pointTable_ || !pointSearch_ || !pointDoubtsOnly_) return;
    const auto query=pointSearch_->text().trimmed();
    const bool only=pointDoubtsOnly_->isChecked();
    int visible=0;
    for (int row=0; row<pointTable_->rowCount(); ++row) {
        auto* name=pointTable_->item(row, 0);
        auto* address=pointTable_->item(row, 1);
        const bool has=name && !name->data(Qt::UserRole+1).toString().isEmpty();
        const bool match=(!only || has) && (query.isEmpty() || (name && name->text().contains(query, Qt::CaseInsensitive)) || (address && address->text().contains(query, Qt::CaseInsensitive)));
        pointTable_->setRowHidden(row, !match);
        if (match) ++visible;
    }
    if (pointEmpty_) {
        pointEmpty_->setVisible(pointTable_->rowCount()==0 || visible==0);
        if (pointTable_->rowCount()==0) pointEmpty_->setText("此工位没有点表，不套用 140。");
        else if (visible==0) pointEmpty_->setText("没有符合筛选的点。源表没有删除。");
    }
    showPointDetail();
}
void Workbench::refillLogs(const std::vector<sim::LogRecord>& logs) {
    if(!logTable_) return;
    if(!liveScope()){
        shown_log_id_=-1; shown_log_count_=-1;
        if(recentTable_) recentTable_->setRowCount(0);
        if(collectChanges_) collectChanges_->setRowCount(0);
        logTable_->setRowCount(0);
        if(logEmpty_){ logEmpty_->setVisible(true); logEmpty_->setText("当前工位未绑定模拟源，此范围没有日志。"); }
        if(logDetail_) logDetail_->setText("当前工位没有可查看的日志。");
        return;
    }
    const auto id=logs.empty()?0:logs.front().id;
    const auto count=static_cast<int>(logs.size());
    if(id==shown_log_id_ && count==shown_log_count_) { applyLogFilter(); return; }
    shown_log_id_=id; shown_log_count_=count;
    if(recentTable_){
        recentTable_->setRowCount(count);
        for(int i=0;i<count;++i){
            const auto& row=logs[static_cast<std::size_t>(i)];
            auto* time=new QTableWidgetItem(beijingTime(row.received_ms,"HH:mm:ss"));
            auto* text=new QTableWidgetItem(showLogText(QString::fromStdString(row.text)));
            time->setFlags(Qt::ItemIsEnabled);
            text->setFlags(Qt::ItemIsEnabled);
            setFigure(time);
            text->setToolTip(text->text());
            if(row.level=="warn") text->setForeground(QColor("#8a5a12"));
            recentTable_->setItem(i, 0, time);
            recentTable_->setItem(i, 1, text);
        }
    }
    if(collectChanges_){
        int shown=0;
        for(const auto& row: logs) if(row.category=="collector") ++shown;
        collectChanges_->setRowCount(shown);
        int index=0;
        for(const auto& row: logs){
            if(row.category!="collector") continue;
            auto* time=new QTableWidgetItem(beijingTime(row.received_ms,"HH:mm:ss"));
            auto* text=new QTableWidgetItem(showLogText(QString::fromStdString(row.text)));
            time->setFlags(Qt::ItemIsEnabled);
            text->setFlags(Qt::ItemIsEnabled);
            setFigure(time);
            text->setToolTip(text->text());
            if(row.level=="warn") text->setForeground(QColor("#8a5a12"));
            collectChanges_->setItem(index, 0, time);
            collectChanges_->setItem(index, 1, text);
            ++index;
        }
    }
    const auto* selected=logTable_->item(logTable_->currentRow(),0);
    const auto selectedId=selected ? selected->data(Qt::UserRole+4).toLongLong() : -1;
    int selectedRow=-1;
    logTable_->setRowCount(count);
    for(int i=0;i<count;++i){
        const auto& row=logs[static_cast<std::size_t>(i)];
        const auto level=row.level=="warn"?"警告":"信息";
        const auto category=row.category=="audit"?"操作":row.category=="collector"?"采集":"业务";
        const auto brief=showLogText(QString::fromStdString(row.text));
        const auto detail=QString("时间：%1（北京时间 UTC+8）\nUTC 毫秒：%2\n等级：%3\n类别：%4\n对象：%5\n对象原文：%6\n关联帧：%7\n%8")
            .arg(beijingTime(row.received_ms,"HH:mm:ss.zzz"), QString::number(row.received_ms), level, category,
                 readableObject(row.object), QString::fromStdString(row.object), row.has_frame?QString::number(row.frame_seq):QString("无"), brief);
        QStringList columns={beijingTime(row.received_ms,"HH:mm:ss"), level, category, readableObject(row.object), brief};
        for(int column=0; column<columns.size(); ++column){
            auto* item=new QTableWidgetItem(columns[column]);
            item->setFlags(Qt::ItemIsEnabled|Qt::ItemIsSelectable);
            item->setToolTip(columns[column]);
            if(column==0){
                setFigure(item);
                item->setData(Qt::UserRole, detail);
                item->setData(Qt::UserRole+1, QString::fromStdString(row.category));
                item->setData(Qt::UserRole+2, QString::fromStdString(row.level));
                item->setData(Qt::UserRole+3, QString::fromStdString(row.text));
                item->setData(Qt::UserRole+4, QVariant::fromValue<qlonglong>(row.id));
                if(row.id==selectedId) selectedRow=i;
            }
            if(row.level=="warn" && column==1) item->setForeground(QColor("#8a5a12"));
            logTable_->setItem(i, column, item);
        }
    }
    logTable_->setCurrentCell(selectedRow, selectedRow>=0 ? 0 : -1);
    applyLogFilter();
}
void Workbench::applyLogFilter() {
    if(!logTable_ || !logSearch_ || !logCategory_ || !logLevel_) return;
    const auto query=logSearch_->text().trimmed();
    const auto category=logCategory_->currentData().toString();
    const auto level=logLevel_->currentData().toString();
    int visible=0;
    for(int row=0; row<logTable_->rowCount(); ++row){
        auto* anchor=logTable_->item(row, 0);
        const auto text=anchor?anchor->data(Qt::UserRole+3).toString():QString();
        const auto rowCategory=anchor?anchor->data(Qt::UserRole+1).toString():QString();
        const auto rowLevel=anchor?anchor->data(Qt::UserRole+2).toString():QString();
        const bool match=(category=="all" || rowCategory==category) && (level=="all" || rowLevel==level) && (query.isEmpty() || text.contains(query, Qt::CaseInsensitive) || (logTable_->item(row,3) && logTable_->item(row,3)->text().contains(query, Qt::CaseInsensitive)));
        logTable_->setRowHidden(row, !match);
        if(match) ++visible;
    }
    if(logEmpty_){
        logEmpty_->setVisible(logTable_->rowCount()==0 || visible==0);
        logEmpty_->setText(logTable_->rowCount()==0 ? "还没有日志。模拟动作发生后才会出现。" : "没有符合筛选的记录。源日志仍在。");
    }
    showLogDetail();
}
void Workbench::showLogDetail() {
    if(!logDetail_ || !logTable_) return;
    const int row=logTable_->currentRow();
    auto* item=row<0?nullptr:logTable_->item(row,0);
    if(!item || logTable_->isRowHidden(row)) logDetail_->setText("选择一行查看详情。时间显示为北京时间，库里仍是 UTC 毫秒。");
    else logDetail_->setText(item->data(Qt::UserRole).toString());
}
void fitPointDetail(QScrollArea* scroll, QLabel* label) {
    if (!scroll || !label) return;
    const int width=std::max(1, scroll->viewport()->width());
    const int height=std::max(label->heightForWidth(width), label->sizeHint().height());
    if (label->minimumHeight()!=height) label->setMinimumHeight(height);
}
void Workbench::showPointDetail() {
    if (!pointDetail_ || !pointTable_) return;
    const int row=pointTable_->currentRow();
    auto* name=row<0 ? nullptr : pointTable_->item(row, 0);
    if (!name || pointTable_->isRowHidden(row)) {
        pointDetail_->setText(pointTable_->rowCount()==0 ? "没有可查看的源项。" : "选择一行查看完整源项。英文审计码只出现在这里。");
    } else pointDetail_->setText(name->data(Qt::UserRole).toString());
    fitPointDetail(findChild<QScrollArea*>("pointDetailScroll"), pointDetail_);
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
    applyPointFilter();
    if (recentTable_) {
        const bool live=liveScope();
        recentTable_->setVisible(live);
        if (auto* note=findChild<QLabel*>("recentScope")) note->setVisible(!live);
    }
    auto setFact=[&](const char* name, const QString& text){ if(auto* label=findChild<QLabel*>(name)) label->setText(text); };
    if(collectOwner_) collectOwner_->setText(view_.runtime.liveness=="recent" ? "独立采集进程 · 心跳正常" : "独立采集进程 · 心跳不可用");
    const bool bound=liveScope();
    if(collectLive_) collectLive_->setText(bound ? "工位 140 · sim.three-cart.v1" : "当前 PLC 未绑定");
    const bool frame=bound && !view_.rows.empty();
    const auto age=view_.runtime.heartbeat_ms>0 ? QString::number(std::max<qint64>(0, (QDateTime::currentMSecsSinceEpoch()-view_.runtime.heartbeat_ms)/1000))+" 秒" : QString("无");
    setFact("collectSeen", view_.runtime.heartbeat_ms>0 ? beijingTime(view_.runtime.heartbeat_ms,"HH:mm:ss")+" · 年龄 "+age+" · 采集进程" : "无");
    setFact("collectFrame", frame ? QString("#%1").arg(view_.rows.front().seq) : "无");
    const QString qualityText=frame ? QString(view_.rows.front().quality=="good"?"正常":"不可用") : "无";
    const QString linkText=!frame ? "无" : view_.rows.front().link=="down" ? "模拟断开" : "接通";
    setFact("collectQuality", qualityText);
    setFact("collectLink", linkText);
    setFact("collectMode", QString(view_.runtime.mode=="manual" ? "手动" : "自动")+" · 上位机");
    setFact("collectPlc", "未连接");
    markState(findChild<QLabel*>("collectQuality"), !frame ? "unknown" : view_.rows.front().quality=="good" ? "ok" : "warn");
    markState(findChild<QLabel*>("collectLink"), !frame ? "unknown" : view_.rows.front().link=="down" ? "warn" : "ok");
    markState(findChild<QLabel*>("collectPlc"), "unknown");
    if(auto* note=findChild<QLabel*>("collectUnbound")) note->setVisible(!bound);
    if(auto* heading=findChild<QLabel*>("collectChangeLabel")) heading->setVisible(bound);
    if(collectChanges_) collectChanges_->setVisible(bound);
    refillLogs(view_.logs);
}

void Workbench::refill() {
    std::vector<std::pair<QString, QString>> factories, areas, plcs, lines, stations;
    for (auto& row : catalog_.factories) factories.emplace_back(QString::fromStdString(row.id), QString::fromStdString(row.name));
    for (auto* row : catalog_.areas_of(selection_.factory_id)) areas.emplace_back(QString::fromStdString(row->id), QString::fromStdString(row->name));
    for (auto* row : catalog_.plcs_of(selection_.area_id)) plcs.emplace_back(QString::fromStdString(row->id), QString::fromStdString(row->name));
    for (auto* row : catalog_.lines_of(selection_.area_id)) lines.emplace_back(QString::fromStdString(row->id), QString::fromStdString(row->name));
    for (auto* row : catalog_.stations_on_line(selection_.line_id)) stations.emplace_back(QString::fromStdString(row->id), QString::fromStdString(catalog::station_label(*row)));
    fillCombo(lineCombo_, lines, QString::fromStdString(selection_.line_id));
    fillCombo(stationCombo_, stations, QString::fromStdString(selection_.station_id));
    fillCombo(factoryCombo_, factories, QString::fromStdString(selection_.factory_id));
    fillCombo(areaCombo_, areas, QString::fromStdString(selection_.area_id));
    fillCombo(plcCombo_, plcs, QString::fromStdString(selection_.plc_id));
}

void Workbench::rebuildCards() {
    auto* layout=qobject_cast<QHBoxLayout*>(cardRow_->layout());
    if (!layout) return;
    while (QLayoutItem* item=layout->takeAt(0)) { delete item->widget(); delete item; }
    for (auto* station : catalog_.stations_of(selection_.plc_id)) {
        auto* button=new QPushButton(QString::fromStdString(station->code), cardRow_);
        button->setToolTip(QString::fromStdString(catalog::station_label(*station)));
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
    const auto open=[this](const char* title){openDiagnostic(QString::fromUtf8(title));};
    if (kind=="empty") {
        if (auto selection=catalog_.selection_for_plc(catalog_.empty_plc_id())) setSelection(*selection);
        open("运行");
        return;
    }
    if (kind=="demo-north") { activateSchematicStation("stn.demo-n1"); open("运行"); return; }
    if (kind=="south") { activateSchematicStation(QString::fromUtf8(catalog::kStation20)); open("运行"); return; }
    if (kind=="meter2") { activateSchematicStation(QString::fromUtf8(catalog::kStation40)); open("运行"); return; }
    if (kind=="roster") { activateSchematicStation(QString::fromUtf8(catalog::kStation40)); open("工位"); return; }
    if (kind=="sort") { activateSchematicStation(QString::fromUtf8(catalog::kStation210)); open("运行"); return; }
    if (kind=="peers") { activateSchematicStation(QString::fromUtf8(catalog::kStation150)); open("运行"); return; }
    if (!catalog_.live_station_id().empty()) activateSchematicStation(QString::fromStdString(catalog_.live_station_id()));
    if (kind=="stations") open("工位");
    else if (kind=="events") open("事件");
    else if (kind=="eventbad") { if (historyFilter_) historyFilter_->setCurrentIndex(historyFilter_->findData("bad")); open("事件"); }
    else if (kind=="points") open("点表");
    else if (kind=="pointfind") {
        if (pointSearch_) pointSearch_->setText("DB1900.DBX0.0");
        if (pointTable_ && pointTable_->rowCount()>0) pointTable_->selectRow(0);
        open("点表");
    }
    else if (kind=="boundary") open("接口");
    else if (kind=="logs") open("日志");
    else if (kind=="collect" || kind=="collectdown") open("采集");
    else if (kind=="emptystations") {
        if (auto selection=catalog_.selection_for_plc(catalog_.empty_plc_id())) setSelection(*selection);
        open("工位");
    }
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
    modeButton_->setProperty("emphasis", (usable && !manual && !pending) ? "yes" : "no");
    modeButton_->style()->unpolish(modeButton_);
    modeButton_->style()->polish(modeButton_);
    QString reason;
    if (selection_.station_id.empty()) reason="未配置工位，不能控制";
    else if (!live) reason="只推进工位 140，不控制当前工位";
    else if (!usable) reason="采集心跳不可用";
    else if (pending) reason="等待采集端确认";
    else if (manual) reason="作用于工位 140，手动待步";
    else reason="作用于工位 140，自动中不可单步";
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
        catch (const std::exception& error) { unavailable(QString::fromUtf8("无法读取本地采集数据：") + QString::fromUtf8(error.what())); }
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
    if (available_ && view_.runtime.mode=="manual") runtime="运行模式  手动";
    else if (available_ && view_.runtime.mode=="automatic") runtime="运行模式  自动";
    runtimeLabel_->setText(runtime);
    const auto manual=available_ && view_.runtime.mode=="manual";
    if (runtimeLabel_->property("mode").toString()!=(manual ? "manual" : "other")) {
        runtimeLabel_->setProperty("mode",manual ? "manual" : "other");
        runtimeLabel_->style()->unpolish(runtimeLabel_);
        runtimeLabel_->style()->polish(runtimeLabel_);
    }
    runtimeLabel_->setMinimumWidth(runtimeLabel_->fontMetrics().horizontalAdvance(runtimeLabel_->text())+26);
    heartbeatLabel_->setText(view_.runtime.heartbeat_ms>0 ? "心跳 "+beijingTime(view_.runtime.heartbeat_ms, "HH:mm:ss")+" · 北京时间" : "无心跳");
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
    stationBadge_->setText("当前工位  "+(station ? QString::fromStdString(station->code) : QString("未配置")));
    stationBadge_->setToolTip(station ? QString::fromStdString(catalog::station_label(*station)) : QString("未配置"));
    stationBadge_->setMinimumWidth(stationBadge_->fontMetrics().horizontalAdvance(stationBadge_->text())+26);
    const auto* line=catalog_.find_line(selection_.line_id);
    const QString lineName=line ? QString::fromStdString(line->name) : "未配置";
    const QString storage=line ? QString::fromStdString(line->storage) : "未配置";
    const QString zone=line ? QString::fromStdString(line->zone) : "未配置";
    stationMeta_->setText(lineName+" · "+storage+" · "+zone);
    stationMeta_->setToolTip(plc ? QString::fromStdString(plc->name)+" · PLC 自动未读取" : "PLC 未配置");
    interfaceLine_->setText(selection_.factory_id!=catalog::kFactory ? "关联系统 · 未配置" : area && area->code=="seq"
        ? "华睿：未连接 · 汇总/严控与完成协议未定 · 不填假数量"
        : "库卡：未授权通行 · LWMS：未连接 · 库位映射待补");
    // One projection of the selected station's current frame feeds the order and the shared lane.
    CartLane lane;
    lane.visible=live;
    int completed=0;
    for (int index=0;index<3;++index) {
        const auto raw=good ? view_.rows.front().carts[index] : "UNKNOWN";
        lane.states[index]=stateText(raw);
        lane.marks[index]=good ? cartMark(raw) : CartMark::none;
        if (raw=="scanning" || raw=="sorting" || raw=="blocked") lane.target=index;
        if (raw=="station_complete") ++completed;
    }
    simOrder_->setVisible(live);
    const QString target=lane.target>=0 ? QString("料车 %1").arg(lane.target+1)
        : good ? "当前无活动料车" : "目标未知";
    const QString progress=good ? QString("%1 / 3 台工位完成").arg(completed) : "进度未知";
    simOrder_->setText(live ? QString("模拟 LWMS 分拣单 SIM-SPS-01 · 模拟物料 A\n取料工位 %1 → %2 · %3")
        .arg(QString::fromStdString(station->name),target,progress) : QString());
    std::vector<StationGlyph> glyphs;
    for (auto* item : catalog_.stations_of(selection_.plc_id))
        glyphs.push_back({item->id,QString::fromStdString(item->code)});
    schematic_->setMinimumHeight(live ? 142 : 108);
    schematic_->setMaximumHeight(live ? 142 : 108);
    schematic_->setScene(plc ? QString::fromStdString(plc->name) : QString(), glyphs, selection_.station_id,lane);
    if (cardRow_) for (auto* button : cardRow_->findChildren<QPushButton*>()) button->setChecked(button->property("stationId").toString().toStdString()==selection_.station_id);
    setRows(detailTable_, {
        {"工位", station ? QString::fromStdString(catalog::station_label(*station)) : "未配置"},
        {"业务模式", area ? QString::fromStdString(area->name) : "未配置"},
        {"运行模式", runtimeValue},
        {"实时范围", scope},
        {"模拟质量", sim_quality},
        {"设备连接", "未探测"},
        {"库存", "未知"},
        {"控制器", plc ? QString::fromStdString(plc->id+" "+plc->name) : "未配置"},
        {"关联工位", peers},
        {"线体", lineName},
        {"仓库", storage},
        {"库区", zone},
        {"库位", "待补映射"},
    });
    peerTable_->setColumnCount(3);
    peerTable_->setRowCount(static_cast<int>(peer_rows.size()));
    for (int i=0; i<static_cast<int>(peer_rows.size()); ++i) {
        const bool current=peer_rows[i]->id==selection_.station_id;
        QStringList columns={QString::fromStdString(peer_rows[i]->code), QString::fromStdString(peer_rows[i]->name), current ? "当前" : "关联"};
        for (int column=0; column<columns.size(); ++column) {
            auto* item=new QTableWidgetItem(columns[column]);
            item->setFlags(Qt::ItemIsEnabled | Qt::ItemIsSelectable);
            item->setToolTip(columns[column]);
            peerTable_->setItem(i, column, item);
        }
    }
    const auto audit_rows=audit::for_station(selection_.station_id);
    pointScope_->setText(audit_rows.empty() ? "此工位没有点表，不套用 140。" : "只读。源表不改写，没有可执行写点。");
    pointTable_->setRowCount(static_cast<int>(audit_rows.size()));
    for (int i=0; i<static_cast<int>(audit_rows.size()); ++i) {
        const auto& row=audit_rows[static_cast<std::size_t>(i)];
        const auto name=row.name.empty() ? QString("（空白名）") : QString::fromStdString(row.name);
        const auto address=QString::fromStdString(row.address);
        const auto codes=QString::fromStdString(row.doubts);
        const auto summary=doubtSummary(codes);
        const auto detail=QString("名称：%1\n地址：%2\n段：%3 · 来源行：%4\n类型：%5 · 方向：%6 · 长度：%7\n备注：%8\n疑点：%9\n审计码：%10")
            .arg(name, address, QString::fromStdString(row.section), QString::fromStdString(row.source_row),
                 QString::fromStdString(row.type_text), QString::fromStdString(row.direction_text),
                 QString::fromStdString(row.length_text), row.note.empty() ? "无" : QString::fromStdString(row.note),
                 summary, codes.isEmpty() ? "无" : codes);
        QStringList columns={name, address, summary, QString::fromStdString(row.section)};
        for (int column=0; column<columns.size(); ++column) {
            auto* item=new QTableWidgetItem(columns[column]);
            item->setFlags(Qt::ItemIsEnabled | Qt::ItemIsSelectable);
            item->setToolTip(columns[column]);
            if (column==0) {
                item->setData(Qt::UserRole, detail);
                item->setData(Qt::UserRole+1, codes);
            }
            if (!codes.isEmpty() && column==2) item->setForeground(QColor("#8a5a12"));
            pointTable_->setItem(i, column, item);
        }
    }
    if (eventSource_) eventSource_->setText(live ? "来源：工位 140 · 模拟流 sim.three-cart.v1 · 北京时间 UTC+8" : "来源：当前 PLC 未绑定模拟源。");
    const bool unconfigured=selection_.station_id.empty();
    markState(scopeBanner_, live && (!good || (!view_.rows.empty() && view_.rows.front().alarm=="active")) ? "warn" : "normal");
    if (auto* emptySource=findChild<QLabel*>("emptySource"))
        emptySource->setText(plc ? "当前 PLC 未绑定模拟源" : "当前选择未绑定模拟源");
    if (auto* stack=findChild<QStackedWidget*>("runStack")) {
        stack->setCurrentIndex(unconfigured ? 0 : 1);
        if (unconfigured) stack->setFixedHeight(std::max(1, stack->currentWidget()->sizeHint().height()));
        else { stack->setMinimumHeight(0); stack->setMaximumHeight(QWIDGETSIZE_MAX); stack->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Expanding); }
        stack->updateGeometry();
    }
    if (auto* tail=findChild<QWidget*>("runTail")) {
        tail->setVisible(unconfigured);
        if (auto* layout=qobject_cast<QVBoxLayout*>(tail->parentWidget()->layout())) {
            layout->setStretchFactor(tail, unconfigured ? 1 : 0);
            if (auto* stack=findChild<QStackedWidget*>("runStack")) layout->setStretchFactor(stack, unconfigured ? 0 : 1);
        }
    }

    if (!live) {
        showStageResult();
        stageTable_->setRowCount(0);
        stageTable_->hide();
        historyTable_->setRowCount(0);
        eventNote_->setText(station_selected ? "无实时历史" : "未配置");
        scopeBanner_->setText(station_selected
            ? "此工位未绑定模拟源。不显示工位 140 的帧、日志或颜色。"
            : "当前范围没有工位，不能控制。");
        syncModuleChrome();
        return;
    }
    if (view_.rows.empty()) {
        stageTable_->show();
        stageTable_->setRowCount(0);
        showStageResult();
        historyTable_->setRowCount(0);
        eventNote_->setText("尚无历史");
        scopeBanner_->setText("工位 140 还没有模拟帧。库存未知，心跳不是 PLC 在线。");
        syncModuleChrome();
        return;
    }
    const auto& latest=view_.rows.front();
    const int selectedStage=stageTable_->currentRow();
    stageTable_->setRowCount(3);
    for (int i=0; i<3; ++i) {
        auto value=good ? latest.carts[i] : "UNKNOWN";
        QString note=value=="station_complete" ? "本工位本轮步骤结束" : value=="waiting" ? "等待前序小车" : value=="blocked" ? "模拟阻塞仍活动" : value=="UNKNOWN" ? "无可信值，不补零" : "模拟阶段已确认";
        QStringList columns={QString("小车%1").arg(i+1), stateText(value), note};
        for (int column=0; column<columns.size(); ++column) stageTable_->setItem(i, column, new QTableWidgetItem(columns[column]));
    }
    if (stageTable_->currentRow()<0) stageTable_->setCurrentCell(std::max(0,selectedStage),0);
    stageTable_->show();
    showStageResult();
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
            if (column==0) item->setTextAlignment(Qt::AlignRight | Qt::AlignVCenter);
            if (column==0 || column==1) setFigure(item);
            if (row.quality!="good" && column==3) item->setForeground(QColor("#8a5a12"));
            historyTable_->setItem(i, column, item);
        }
    }
    eventNote_->setText(QString("最近 %1 帧 · 覆盖未知").arg(view_.rows.size()));
    const auto age=std::max<std::int64_t>(0, (sim::now_ms()-latest.received_ms)/1000);
    if (!good) {
        if (!available_) scopeBanner_->setText("采集异常 · 心跳不可用，当前状态未知。");
        else if (latest.reason=="disconnected") scopeBanner_->setText("采集异常 · 模拟链路断开，当前值未知。");
        else if (view_.freshness=="clock_uncertain") scopeBanner_->setText("采集异常 · 事件时间不确定。");
        else if (latest.quality!="good") scopeBanner_->setText("采集异常 · 源质量异常，当前值未知。");
        else scopeBanner_->setText("采集异常 · 当前帧已过期。");
    }
    else if (latest.alarm=="active") scopeBanner_->setText("模拟告警 · 小车阻塞，检查当前阶段。");
    else scopeBanner_->setText(QString("模拟帧 #%1 · %2 秒前").arg(latest.seq).arg(age));
    syncModuleChrome();
}

void Workbench::showStageResult() {
    const bool bound=liveScope();
    const auto row=stageTable_->currentRow();
    const auto* cart=bound ? stageTable_->item(row,0) : nullptr;
    const auto* phase=bound ? stageTable_->item(row,1) : nullptr;
    const auto* note=bound ? stageTable_->item(row,2) : nullptr;
    stageGuide_->setText(note ? "当前步骤说明  ·  "+note->text()
                             : bound ? "等待首个模拟采集结果。" : "此范围仅提供配置和示意。");
    QString quality="未绑定模拟源";
    QString link="未探测";
    if (bound) {
        quality="无可信当前值";
        link="心跳不可用或尚无结果";
        if (available_ && !view_.rows.empty()) {
            const auto& frame=view_.rows.front();
            if (view_.freshness=="recent" || (view_.runtime.mode=="manual" && view_.freshness!="clock_uncertain")) {
                quality=frame.quality=="good" ? "正常" : "不可用";
                link=frame.link=="down" ? "模拟断链" : "模拟链路正常";
            }
        }
    }
    setRows(resultTable_,{
        {"所选步骤",cart && phase ? cart->text()+" · "+phase->text() : "无当前步骤"},
        {"采集质量",quality},
        {"链路证据",link},
    });
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
    if (kind=="demo-north") return selection_.station_id=="stn.demo-n1" && !liveScope();
    if (kind=="stations") return onTab("工位") && liveScope();
    if (kind=="events") return onTab("事件") && liveScope() && historyTable_ && historyTable_->rowCount()>0;
    if (kind=="eventbad") return onTab("事件") && liveScope() && historyFilter_ && historyFilter_->currentData().toString()=="bad" && historyTable_ && historyTable_->rowCount()>0;
    if (kind=="points") return onTab("点表") && liveScope() && pointTable_ && pointTable_->rowCount()>0;
    if (kind=="pointfind") return onTab("点表") && pointSearch_ && pointSearch_->text()=="DB1900.DBX0.0" && pointDetail_ && pointDetail_->text().contains("DB1900.DBX0.0");
    if (kind=="emptystations") return onTab("工位") && selection_.station_id.empty();
    if (kind=="boundary") return onTab("接口") && liveScope();
    if (kind=="logs") return onTab("日志") && logTable_ && logTable_->rowCount()>0;
    if (kind=="collect") return onTab("采集") && liveScope();
    if (kind=="collectdown") return onTab("采集") && liveScope() && !view_.rows.empty() && view_.rows.front().link=="down";
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
    if (kind=="alarm") return available_ && row.alarm=="active" && row.quality=="good" && (view_.freshness=="recent" || (view_.runtime.mode=="manual" && view_.freshness!="clock_uncertain"));
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
