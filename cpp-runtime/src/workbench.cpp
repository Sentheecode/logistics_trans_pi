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
#include <QPen>
#include <QStandardItemModel>
#include <QStyledItemDelegate>
#include <QMessageBox>
#include <QPushButton>
#include <QScrollArea>
#include <QScrollBar>
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
    QFont font;
    font.setFamilies({"Noto Sans Mono CJK SC", "Sarasa Mono SC", "Cascadia Mono", "Consolas", "DejaVu Sans Mono"});
    font.setStyleHint(QFont::TypeWriter, QFont::PreferAntialias);
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
    const QColor ink("#1b1f23");
    const QColor paper("#ffffff");
    const QColor selected("#eef1fb");
    const QColor selectedInk("#1b1f23");
    palette.setColor(QPalette::Base, paper);
    palette.setColor(QPalette::Text, ink);
    palette.setColor(QPalette::Window, paper);
    palette.setColor(QPalette::WindowText, ink);
    palette.setColor(QPalette::Highlight, selected);
    palette.setColor(QPalette::HighlightedText, selectedInk);
    view->setPalette(palette);
    view->setStyleSheet(
        "QListView { background:#ffffff; color:#1b1f23; border:none; outline:none; }"
        "QListView::item { background:#ffffff; color:#1b1f23; min-height:22px; padding:4px 8px; }"
        "QListView::item:hover { background:#eef1fb; color:#1b1f23; }"
        "QListView::item:selected { background:#eef1fb; color:#1b1f23; }");
    box->setView(view);
}
void setFigure(QTableWidgetItem* item) {
    item->setFont(figureFont());
}
class SnapTable : public QTableWidget {
public:
    SnapTable() {
        setVerticalScrollMode(QAbstractItemView::ScrollPerItem);
        verticalHeader()->setSectionResizeMode(QHeaderView::Fixed);
        connect(verticalScrollBar(), &QScrollBar::valueChanged, this, [this](int){ alignOffset(); });
    }
    void snap() { updateGeometries(); }
protected:
    // QTableView clears the bottom margin, and scrolling to the end uses the
    // header height rather than this viewport. Both can leave a partial row.
    void updateGeometries() override {
        if (locking_) return;
        locking_=true;
        QTableWidget::updateGeometries();
        const int sample=rowSample();
        const int height=viewport()->height();
        const int slack=(sample>1 && height>sample) ? height%sample : 0;
        if (slack>0) {
            const QMargins margins=viewportMargins();
            setViewportMargins(margins.left(), margins.top(), margins.right(), margins.bottom()+slack);
        }
        alignOffset();
        locking_=false;
    }
private:
    int rowSample() const {
        const int fallback=std::max(1, verticalHeader()->defaultSectionSize());
        for (int row=0; row<rowCount(); ++row) {
            if (!isRowHidden(row)) return std::max(1, rowHeight(row));
        }
        return fallback;
    }
    void alignOffset() {
        if (aligning_) return;
        const int sample=rowSample();
        const int view=viewport()->height();
        if (sample<=1 || view<=sample) return;
        const int maxOffset=std::max(0, verticalHeader()->length()-view);
        int offset=std::clamp(verticalHeader()->offset(), 0, maxOffset);
        offset-=offset%sample;
        if (offset==verticalHeader()->offset()) return;
        aligning_=true;
        verticalHeader()->setOffset(offset);
        aligning_=false;
    }
    bool locking_=false;
    bool aligning_=false;
};
void fitRows(QTableWidget* table, int rowHeight) {
    if (!table) return;
    table->verticalHeader()->setSectionResizeMode(QHeaderView::Fixed);
    table->verticalHeader()->setDefaultSectionSize(rowHeight);
    int header=table->horizontalHeader()->isHidden() ? 0 : table->horizontalHeader()->height();
    if (header<8) header=table->horizontalHeader()->sizeHint().height();
    const int rows=std::max(1, table->rowCount());
    table->setFixedHeight(header+rows*rowHeight+table->frameWidth()*2+2);
}
class BoardDelegate : public QStyledItemDelegate {
public:
    using QStyledItemDelegate::QStyledItemDelegate;
    void paint(QPainter* painter, const QStyleOptionViewItem& option, const QModelIndex& index) const override {
        QStyleOptionViewItem opt(option);
        initStyleOption(&opt, index);
        const bool current=index.data(Qt::UserRole+2).toBool();
        const bool grouped=index.data(Qt::UserRole+3).toBool();
        painter->fillRect(opt.rect, grouped ? QColor("#f7f8fa") : QColor("#ffffff"));
        if (!grouped) {
            painter->setPen(QColor("#e6e8eb"));
            painter->drawLine(opt.rect.bottomLeft(), opt.rect.bottomRight());
        }
        if (index.column()>=4) return;
        if (index.column()==3) {
            const bool live=index.data(Qt::UserRole+2).toBool();
            painter->setPen(live ? QColor("#3b5bdb") : QColor("#5c6370"));
            QFont font=opt.font;
            font.setPixelSize(14);
            font.setWeight(live ? QFont::DemiBold : QFont::Normal);
            painter->setFont(font);
            painter->drawText(opt.rect.adjusted(8, 0, -8, 0), Qt::AlignLeft | Qt::AlignVCenter, opt.text);
            return;
        }
        QColor ink("#1b1f23");
        QFont font=opt.font;
        if (index.column()==0) { font.setPixelSize(20); font.setWeight(QFont::DemiBold); }
        painter->setPen(current ? QColor("#1b1f23") : ink);
        painter->setFont(font);
        const int align=opt.displayAlignment | Qt::AlignVCenter;
        painter->drawText(opt.rect.adjusted(8, 0, -8, 0), align, opt.text);
    }
};
class LevelDelegate : public QStyledItemDelegate {
public:
    using QStyledItemDelegate::QStyledItemDelegate;
    void paint(QPainter* painter, const QStyleOptionViewItem& option, const QModelIndex& index) const override {
        QStyleOptionViewItem opt(option);
        initStyleOption(&opt, index);
        const bool warn=opt.text==QString("警告");
        painter->fillRect(opt.rect, (opt.state & QStyle::State_Selected) ? QColor("#eef1fb") : QColor("#ffffff"));
        if (!warn) {
            painter->setPen(QColor("#5c6370"));
            painter->setFont(opt.font);
            painter->drawText(opt.rect.adjusted(8, 0, -8, 0), Qt::AlignVCenter | Qt::AlignLeft, opt.text);
            return;
        }
        QFont bold=opt.font;
        bold.setBold(true);
        const QFontMetrics metrics(bold);
        const QRect chip(opt.rect.left()+8, opt.rect.center().y()-10, metrics.horizontalAdvance(opt.text)+16, 20);
        painter->setPen(Qt::NoPen);
        painter->setBrush(QColor("#fff4d6"));
        painter->drawRoundedRect(chip, 6, 6);
        painter->setPen(QColor("#8a5a00"));
        painter->setFont(bold);
        painter->drawText(chip, Qt::AlignCenter, opt.text);
    }
};
class ModuleTabs : public QTabBar {
public:
    using QTabBar::QTabBar;
protected:
    void paintEvent(QPaintEvent* event) override {
        QTabBar::paintEvent(event);
        const int index=currentIndex();
        if (index<0) return;
        const QRect tab=tabRect(index);
        QPainter painter(this);
        painter.setRenderHint(QPainter::Antialiasing);
        painter.setPen(Qt::NoPen);
        painter.setBrush(QColor("#3b5bdb"));
        const int barWidth=std::max(24, tab.width()-8);
        painter.fillRect(QRect(tab.left()+4, height()-4, barWidth, 4), QColor("#3b5bdb"));
    }
};
void paintArm(QPainter& painter, const QPointF& origin) {
    painter.save();
    painter.setRenderHint(QPainter::Antialiasing);
    painter.setPen(QPen(QColor("#8b95a7"), 1.6, Qt::SolidLine, Qt::RoundCap, Qt::RoundJoin));
    painter.drawLine(origin, origin+QPointF(0, -12));
    painter.drawLine(origin+QPointF(0, -12), origin+QPointF(16, -18));
    painter.drawLine(origin+QPointF(8, 2), origin+QPointF(22, 2));
    painter.restore();
}
class ShopPlan final : public QWidget {
public:
    explicit ShopPlan(QWidget* parent=nullptr) : QWidget(parent) {
        setObjectName("shopPlan");
        hide();
    }
    void apply(const catalog::Plc* plc, const std::vector<const catalog::Station*>& rows) {
        stations_=rows;
        kind_=plc ? QString::fromUtf8(catalog::shop_layout(plc->id)) : QString();
        title_=plc ? QString::fromStdString(plc->name) : QString();
        setProperty("layout", kind_);
        if (kind_.isEmpty()) { hide(); return; }
        setFixedHeight(kind_=="pack-right" ? 108 : kind_=="seq3-sides" ? 100 : 96);
        show();
        update();
    }
protected:
    void paintEvent(QPaintEvent*) override {
        QPainter painter(this);
        painter.setRenderHint(QPainter::Antialiasing);
        painter.fillRect(rect(), QColor("#ffffff"));
        if (kind_.isEmpty()) return;
        QFont caption=font();
        caption.setPixelSize(12);
        painter.setFont(caption);
        painter.setPen(QColor("#5c6370"));
        const QString head=kind_=="pack-right"
            ? "外装右 · 140 在左 · 150 与 160 端部相接 · 布置图平面 · 不是实时位置 · 不表示跨工位行驶"
            : kind_=="seq3-sides"
            ? "排序区三 · 布置图平面 · 210 在左、220 在右 · 不是实时位置"
            : title_+" · 布置图顺序 · 已有布置图·点表仍缺·未接入 · 不是实时位置";
        painter.drawText(QRectF(8, 2, width()-16, 18), Qt::AlignLeft|Qt::AlignVCenter, head);
        if (kind_=="pack-right") paintPack(painter);
        else if (kind_=="seq3-sides") paintSeq3(painter);
        else paintMeter(painter);
    }
private:
    void card(QPainter& painter, const QRectF& box) const {
        painter.setPen(QPen(QColor("#e6e8eb"), 1));
        painter.setBrush(QColor("#f7f8fa"));
        painter.drawRoundedRect(box, 8, 8);
    }
    void namesAt(QPainter& painter, const QRectF& box, const std::string& id, bool arm) const {
        const auto names=catalog::drawing_names(id);
        QFont code=font();
        code.setPixelSize(16);
        code.setWeight(QFont::DemiBold);
        painter.setFont(code);
        painter.setPen(QColor("#1b1f23"));
        const QString number=QString::fromStdString(id).section('.', -1);
        painter.drawText(box.adjusted(10, 6, -28, 0), Qt::AlignLeft|Qt::AlignTop, number);
        if (arm) paintArm(painter, QPointF(box.right()-26, box.top()+28));
        QFont body=font();
        body.setPixelSize(14);
        body.setWeight(QFont::Normal);
        painter.setFont(body);
        painter.setPen(QColor("#1b1f23"));
        QStringList lines;
        for (const auto& name : names) lines<<QString::fromStdString(name);
        if (lines.isEmpty()) {
            const auto* known=id==catalog::kStation210 ? "前悬架右前下摆臂总成" : id==catalog::kStation220 ? "前悬架左前下摆臂总成" : "";
            if (known[0]) lines<<QString::fromUtf8(known);
        }
        const int step=std::max(16, painter.fontMetrics().lineSpacing());
        int top=static_cast<int>(box.top())+24;
        for (const auto& line : lines) {
            if (top+step>box.bottom()) break;
            painter.drawText(QRectF(box.left()+10, top, box.width()-36, step), Qt::AlignLeft|Qt::AlignVCenter, line);
            top+=step;
        }
    }
    void paintPack(QPainter& painter) const {
        const QRectF body(8, 22, width()-16, height()-28);
        const double leftWidth=std::max(180.0, body.width()*0.32);
        const QRectF left(body.left(), body.top(), leftWidth, body.height());
        const QRectF right(left.right()+12, body.top(), body.right()-left.right()-12, body.height());
        card(painter, left);
        card(painter, right);
        namesAt(painter, left, catalog::kStation140, true);
        const QRectF west=right.adjusted(0, 0, -right.width()/2, 0);
        const QRectF east=right.adjusted(right.width()/2, 0, 0, 0);
        namesAt(painter, west, catalog::kStation150, true);
        namesAt(painter, east, catalog::kStation160, true);
        const double seam=right.center().x();
        painter.setPen(QPen(QColor("#c5ccd6"), 1, Qt::DashLine));
        painter.drawLine(QPointF(seam, right.top()+6), QPointF(seam, right.top()+20));
        painter.drawLine(QPointF(seam, right.bottom()-16), QPointF(seam, right.bottom()-6));
        painter.setPen(Qt::NoPen);
        painter.setBrush(QColor("#8b95a7"));
        painter.drawRoundedRect(QRectF(seam-28, right.bottom()-8, 56, 5), 2, 2);
    }
    void paintSeq3(QPainter& painter) const {
        const QRectF body(8, 22, width()-16, height()-40);
        const QRectF left(body.left(), body.top(), (body.width()-12)/2, body.height());
        const QRectF right(left.right()+12, body.top(), body.right()-left.right()-12, body.height());
        card(painter, left);
        card(painter, right);
        namesAt(painter, left, catalog::kStation210, false);
        namesAt(painter, right, catalog::kStation220, false);
        QFont note=font();
        note.setPixelSize(12);
        painter.setFont(note);
        painter.setPen(QColor("#5c6370"));
        painter.drawText(QRectF(8, height()-18, width()-16, 16), Qt::AlignLeft|Qt::AlignVCenter,
            "排序料架 · 待排序料架 · 使用位 · 缓存料架 · 未接入 · 无占位");
    }
    void paintMeter(QPainter& painter) const {
        if (stations_.empty()) return;
        const QRectF body(8, 22, width()-16, height()-28);
        const int count=static_cast<int>(stations_.size());
        const double gap=8;
        const double bay=(body.width()-gap*(count-1))/count;
        for (int index=0; index<count; ++index) {
            const QRectF box(body.left()+index*(bay+gap), body.top(), bay, body.height());
            card(painter, box);
            namesAt(painter, box, stations_[static_cast<std::size_t>(index)]->id, false);
        }
    }
    QString kind_, title_;
    std::vector<const catalog::Station*> stations_;
};
class RoundRack final : public QWidget {
public:
    explicit RoundRack(QWidget* parent=nullptr) : QWidget(parent) {
        setObjectName("roundRack");
        setFixedHeight(34);
    }
    void setFrame(const QString& a, const QString& b, const QString& c, int active) {
        states_[0]=a; states_[1]=b; states_[2]=c; active_=active; update();
    }
protected:
    void paintEvent(QPaintEvent*) override {
        QPainter painter(this);
        painter.setRenderHint(QPainter::Antialiasing);
        QFont note=font();
        note.setPixelSize(12);
        painter.setFont(note);
        painter.setPen(QColor("#5c6370"));
        const double gap=6;
        const double bay=(width()-gap*2)/3.0;
        for (int index=0; index<3; ++index) {
            const QRectF box(index*(bay+gap), 2, bay, 30);
            const bool active=index==active_;
            painter.setPen(QPen(active ? QColor("#3b5bdb") : QColor("#d5dbe3"), active ? 2 : 1));
            painter.setBrush(QColor("#ffffff"));
            painter.drawRoundedRect(box, 6, 6);
            QFont body=font();
            body.setPixelSize(12);
            body.setWeight(active ? QFont::DemiBold : QFont::Normal);
            painter.setFont(body);
            painter.setPen(active ? QColor("#3b5bdb") : QColor("#1b1f23"));
            painter.drawText(box.adjusted(6, 0, -6, 0), Qt::AlignVCenter|Qt::AlignLeft,
                QString("小车 %1  %2").arg(index+1).arg(states_[index]));
        }
    }
private:
    QString states_[3]={"—", "—", "—"};
    int active_=-1;
};
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
// 浅色工作台：安静底、4px 节奏、圆角。不使用渐变、发光或玻璃效果。
struct Tone {
    const char* canvas="#f5f6f8";
    const char* surface="#ffffff";
    const char* ink="#1b1f23";
    const char* muted="#5c6370";
    const char* line="#e6e8eb";
    const char* accent="#3b5bdb";
    const char* accentSoft="#eef1fb";
    const char* warn="#8a5a00";
    const char* warnSoft="#fff4d6";
};
const Tone tone;
QString consoleStyle() {
    const auto t=tone;
    return QString(R"(
        QWidget { @@FONT@@font-size: 14px; }
        QMainWindow, QWidget#centralRoot { background:%1; color:%3; }
        QWidget#identityBar, QWidget#pageTabRow { background:%2; }
        QWidget#identityBar { border:none; }
        QWidget#pageTabRow { border-bottom:1px solid %4; }
        QWidget#catalogPane { background:%1; border:none; }
        QWidget#identityBar QLabel { color:%3; }
        QWidget#runPage, QWidget#runCanvas, QWidget#logPage, QWidget#collectPage, QWidget#pointPage, QWidget#boundaryPage { background:%1; }
        QWidget#workSurface { background:%2; border:1px solid %4; border-radius:10px; }
        QWidget#commandPane { background:transparent; border:none; }
        QLabel#simOrder { background:transparent; color:%3; border:none; padding:0; }
        QLabel#scopeBanner[state="warn"] { background:%9; color:%8; border-radius:6px; padding:4px 8px; }
        QLabel#runtimeState { background:%1; color:%3; border:1px solid %4; border-radius:8px; padding:2px 8px; font-weight:600; }
        QLabel#runtimeState[mode="manual"] { background:%9; color:%8; }
        QWidget#surface, QWidget#collectWorkspace { background:%2; border:1px solid %4; border-radius:10px; }
        QLabel#title { font-size:16px; font-weight:600; color:%3; }
        QLabel#pageTitle, QLabel#emptyTitle { font-size:22px; font-weight:600; color:%3; }
        QLabel#previewHint { color:%6; font-size:13px; background:transparent; border:none; padding:0; }
        QLabel#pageScope, QLabel#readyMore { color:%5; font-size:13px; }
        QLabel#readinessNote { font-size:14px; font-weight:600; color:%3; }
        QLabel#sourceChip { background:transparent; color:%5; border:none; padding:0; font-size:14px; font-weight:400; }
        QLabel#sourceChip[state="live"] { color:%6; font-weight:600; }
        QLabel#sourceChip[state="idle"], QLabel#sourceChip[state="doc"] { color:%5; font-weight:400; }
        QLabel#roundProgress { color:%6; font-size:14px; font-weight:600; }
        QFrame#faceRule { color:%4; background:%4; max-width:1px; }
        QLabel#acquisitionOwner { color:%5; font-size:13px; background:transparent; border:none; padding:0; }
        QLabel#emptySource, QLabel#emptyAction { color:%5; font-size:13px; }
        QLabel#fieldLabel { color:%5; font-size:12px; }
        QLabel#section, QLabel#collectChangeLabel, QLabel#stageTitle, QLabel#workHeading, QLabel#recentLabel { color:%3; font-size:14px; font-weight:600; }
        QLabel#fieldLabel, QLabel#muted, QLabel#controlReason, QLabel#controlFeedback { font-size:13px; }
        QLabel#collectQuality[state="ok"], QLabel#collectLink[state="ok"] { color:#1f7a4d; font-weight:600; }
        QLabel#collectQuality[state="warn"], QLabel#collectLink[state="warn"] { color:%8; font-weight:600; }
        QLabel#collectQuality[state="unknown"], QLabel#collectLink[state="unknown"], QLabel#collectPlc[state="unknown"] { color:%5; }
        QLabel#muted, QLabel#controlReason, QLabel#controlFeedback { color:%5; font-size:13px; }
        QLabel#simBadge { background:%7; color:%6; border:none; padding:1px 6px; border-radius:6px; font-size:12px; font-weight:600; }
        QLabel#simNote { color:%5; font-size:12px; }
        QLabel#statusChip { background:%1; color:%5; padding:1px 6px; border-radius:8px; font-size:12px; font-weight:600; }
        QLabel#statusChip[state="sim"] { background:%7; color:%6; }
        QLabel#statusChip[state="wait"] { background:%9; color:%8; }
        QComboBox { background:%2; color:%3; border:1px solid %4; border-radius:8px; padding:2px 22px 2px 10px; min-height:26px; }
        QComboBox:hover, QComboBox:focus { border:1px solid %6; }
        QComboBox#factoryCombo, QComboBox#plcCombo { background:transparent; border:none; border-radius:4px; padding:0 18px 0 2px; min-height:22px; font-weight:600; }
        QComboBox#factoryCombo:hover, QComboBox#plcCombo:hover, QComboBox#factoryCombo:focus, QComboBox#plcCombo:focus { background:%7; border:none; }
        QComboBox::drop-down { subcontrol-origin: padding; subcontrol-position: center right; width:22px; border:none; background:transparent; }
        QComboBox::down-arrow { image: url(:/icons/chevron.png); width:14px; height:8px; }
        QComboBox QAbstractItemView { background:%2; color:%3; border:1px solid %4; border-radius:10px; selection-background-color:%7; selection-color:%3; outline:0; padding:4px; }
        QComboBox QAbstractItemView::item { color:%3; background:%2; min-height:22px; padding:4px 8px; border-radius:6px; }
        QComboBox QAbstractItemView::item:hover { background:%7; color:%3; }
        QComboBox QAbstractItemView::item:selected { background:%7; color:%3; }
        QComboBoxPrivateContainer { background:%2; color:%3; border:1px solid %4; border-radius:8px; }
        QMenu { background:%2; color:%3; border:1px solid %4; border-radius:8px; padding:4px; }
        QMenu::item { color:%3; background:transparent; padding:6px 12px; border-radius:6px; }
        QMenu::item:selected { background:%7; color:%3; }
        QPushButton { background:%2; border:1px solid %4; border-radius:8px; padding:2px 12px; min-height:26px; color:%3; }
        QPushButton:hover { background:%7; }
        QPushButton:focus { border:1px solid %6; }
        QPushButton#primary, QPushButton#modeButton[emphasis="yes"] { background:%6; color:#ffffff; border:none; }
        QPushButton#textLink { background:transparent; border:none; border-radius:8px; color:%6; font-weight:600; min-height:24px; padding:2px 6px; }
        QPushButton#textLink:hover { background:%7; }
        QPushButton:disabled, QPushButton#primary:disabled, QPushButton#modeButton:disabled { background:%1; color:#8b96a6; border:none; }
        QPushButton#stationCard { min-width:52px; }
        QPushButton#stationCard:checked { background:%6; color:#ffffff; border:1px solid %6; }
        QTableWidget { background:%2; alternate-background-color:%2; gridline-color:%4; border:none; border-radius:0; font-size:14px; selection-background-color:%7; selection-color:%3; outline:none; }
        QTableWidget#stationBoard { background:%2; alternate-background-color:%2; }
        QTableWidget#stationBoard::item { background:%2; }
        QWidget#sourceCell { background:transparent; border:none; }
        QWidget#liveFace { background:#f7f8fa; border:none; }
        QWidget#bindColumn { background:transparent; border:none; border-left:1px solid %4; }
        QTableWidget#stageTable, QTableWidget#collectFields { background:transparent; border:none; gridline-color:transparent; }
        QTableWidget#stageTable::item, QTableWidget#collectFields::item { background:transparent; border:none; }
        QTableWidget#stageTable QHeaderView::section, QTableWidget#collectFields QHeaderView::section { background:transparent; border:none; border-bottom:1px solid %4; font-size:12px; font-weight:600; }
        QTableWidget:focus { border:none; }
        QTableWidget::item { padding:4px 8px; border:none; }
        QTableWidget::item:hover { background:#f5f6f8; }
        QTableWidget::item:selected { background:%7; color:%3; }
        QTableWidget::item:selected:hover { background:%7; }
        QTableWidget#stageTable::item:selected { background:%7; color:%3; }
        QHeaderView::section { background:%2; color:%5; font-weight:600; border:none; border-bottom:1px solid %4; padding:4px 8px; font-size:12px; }
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
        QTabBar#moduleTabs { background:transparent; }
        QTabBar#moduleTabs::tab { background:transparent; color:%5; padding:8px 14px 6px 14px; min-height:24px; border:none; border-bottom:2px solid transparent; margin-right:2px; font-size:14px; }
        QTabBar#moduleTabs::tab:hover { color:%3; background:transparent; border-bottom:2px solid %4; }
        QTabBar#moduleTabs::tab:selected { color:%6; font-weight:600; background:transparent; border-bottom:2px solid %6; }
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
    font.setPixelSize(14);
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

class Mark : public QWidget {
public:
    Mark() { setFixedSize(22, 22); }
protected:
    void paintEvent(QPaintEvent*) override {
        QPainter painter(this);
        painter.setRenderHint(QPainter::Antialiasing);
        painter.setPen(Qt::NoPen);
        painter.setBrush(QColor("#3b5bdb"));
        painter.drawRoundedRect(rect().adjusted(0, 0, -1, -1), 6, 6);
        painter.setPen(QPen(Qt::white, 1.4, Qt::SolidLine, Qt::RoundCap));
        painter.drawLine(6, 8, 15, 8);
        painter.drawLine(6, 11, 15, 11);
        painter.drawLine(6, 14, 12, 14);
    }
};

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
    identity->setFixedHeight(40);
    auto* identityLayout=new QHBoxLayout(identity);
    identityLayout->setContentsMargins(16, 4, 16, 4);
    identityLayout->setSpacing(8);
    auto* mark=new Mark;
    auto* title=new QLabel("物流工作台");
    title->setObjectName("title");
    fit(title);
    auto* sim=new QLabel("模拟环境");
    sim->setObjectName("simBadge");
    sim->setToolTip("仅本地合成数据。不表示 PLC、LWMS、华睿或库卡已连接。");
    sim->setMinimumWidth(sim->fontMetrics().horizontalAdvance(sim->text())+20);
    auto* simNote=new QLabel("仅本地模拟 · 不是现场设备");
    simNote->setObjectName("simNote");
    processLabel_=new QLabel("采集心跳 待确认");
    processLabel_->setObjectName("processState");
    runtimeLabel_=new QLabel("上位机 未知");
    runtimeLabel_->setObjectName("runtimeState");
    heartbeatLabel_=new QLabel("无心跳");
    heartbeatLabel_->setObjectName("heartbeatState");
    fit(processLabel_); fit(runtimeLabel_); fit(heartbeatLabel_);
    acquisitionOwner_=new QLabel("采集归属 未绑定");
    acquisitionOwner_->setObjectName("acquisitionOwner");
    identityLayout->addWidget(mark);
    identityLayout->addWidget(title);
    identityLayout->addWidget(sim);
    identityLayout->addWidget(simNote);
    identityLayout->addStretch(1);
    identityLayout->addWidget(acquisitionOwner_);

    auto* context=new QWidget;
    context->setObjectName("catalogPane");
    context->setFixedHeight(36);
    auto* contextLayout=new QHBoxLayout(context);
    contextLayout->setContentsMargins(16, 2, 16, 2);
    contextLayout->setSpacing(6);
    auto addTextCombo=[&](QComboBox** slot, const QString& objectName) {
        *slot=new QComboBox(context);
        (*slot)->setObjectName(objectName);
        prepareComboPopup(*slot);
        (*slot)->setSizeAdjustPolicy(QComboBox::AdjustToContents);
        (*slot)->setSizePolicy(QSizePolicy::Preferred, QSizePolicy::Fixed);
        (*slot)->setMaximumWidth(240);
        (*slot)->setFocusPolicy(Qt::StrongFocus);
    };
    addTextCombo(&factoryCombo_, "factoryCombo");
    addTextCombo(&plcCombo_, "plcCombo");
    auto* slash=new QLabel("/", context);
    slash->setObjectName("simNote");
    auto* plcWord=new QLabel("PLC", context);
    plcWord->setObjectName("fieldLabel");
    contextLayout->addWidget(factoryCombo_);
    contextLayout->addWidget(slash);
    contextLayout->addWidget(plcWord);
    contextLayout->addWidget(plcCombo_);
    areaCombo_=new QComboBox(this);
    areaCombo_->setObjectName("areaCombo");
    prepareComboPopup(areaCombo_);
    areaCombo_->hide();
    previewHint_=new QLabel(context);
    previewHint_->setObjectName("previewHint");
    auto* hostLabel=new QLabel("本机工作台", context);
    hostLabel->setObjectName("muted");
    contextLayout->addWidget(previewHint_);
    contextLayout->addStretch(1);
    contextLayout->addWidget(hostLabel);

    auto* commandPane=new QWidget;
    commandPane->setObjectName("commandPane");
    auto* commandLayout=new QHBoxLayout(commandPane);
    commandLayout->setContentsMargins(0, 2, 0, 0);
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
        button->setMinimumHeight(26);
        commandLayout->addWidget(button);
    }

    schematic_=new SchematicView(this);
    schematic_->hide();
    stationBoard_=new QTableWidget;
    stationBoard_->setItemDelegate(new BoardDelegate(stationBoard_));
    stationBoard_->setObjectName("stationBoard");
    prepareTable(stationBoard_, {"工位", "名称", "当前作业", "数据来源", ""});
    stationBoard_->setSelectionMode(QAbstractItemView::NoSelection);
    stationBoard_->setFocusPolicy(Qt::NoFocus);
    stationBoard_->setEditTriggers(QAbstractItemView::NoEditTriggers);
    stationBoard_->verticalHeader()->setDefaultSectionSize(48);
    stationBoard_->setAlternatingRowColors(false);
    stationBoard_->setVerticalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    stationBoard_->horizontalHeader()->setStretchLastSection(false);
    stationBoard_->horizontalHeader()->setDefaultAlignment(Qt::AlignLeft | Qt::AlignVCenter);
    stationBoard_->horizontalHeader()->setSectionResizeMode(0, QHeaderView::Fixed);
    stationBoard_->horizontalHeader()->setSectionResizeMode(1, QHeaderView::Stretch);
    stationBoard_->horizontalHeader()->setSectionResizeMode(2, QHeaderView::Stretch);
    stationBoard_->horizontalHeader()->setSectionResizeMode(3, QHeaderView::Fixed);
    stationBoard_->horizontalHeader()->setSectionResizeMode(4, QHeaderView::Fixed);
    stationBoard_->setShowGrid(false);
    stationBoard_->setColumnWidth(0, 88);
    stationBoard_->setColumnWidth(3, 232);
    stationBoard_->setColumnWidth(4, 64);
    auto* workHeading=new QLabel("模拟作业");
    workHeading->setObjectName("workHeading");
    stationMeta_=new QLabel;
    stationMeta_->setObjectName("muted");
    stationMeta_->setWordWrap(true);
    interfaceLine_=new QLabel;
    interfaceLine_->setWordWrap(true);
    scopeBanner_=new QLabel;
    scopeBanner_->setObjectName("scopeBanner");
    scopeBanner_->setWordWrap(true);

    auto* run=new QWidget;
    run->setObjectName("runPage");
    auto* runLayout=new QVBoxLayout(run);
    runLayout->setContentsMargins(16, 8, 16, 4);
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
    workspace->setObjectName("workSurface");
    workspace->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Maximum);
    auto* workspaceLayout=new QVBoxLayout(workspace);
    workspaceLayout->setContentsMargins(12, 2, 12, 2);
    workspaceLayout->setSpacing(4);
    auto* runStack=new RunStack(run);
    runStack->setObjectName("runStack");
    auto* runCanvas=new QWidget(run);
    runCanvas->setObjectName("runCanvas");
    auto* canvasLayout=new QVBoxLayout(runCanvas);
    canvasLayout->setContentsMargins(0, 0, 0, 0);
    canvasLayout->setSpacing(8);
    auto* pageHead=new QWidget(runCanvas);
    auto* pageHeadLayout=new QHBoxLayout(pageHead);
    pageHeadLayout->setContentsMargins(0, 4, 0, 0);
    auto* pageTitle=new QLabel("工位作业", pageHead);
    pageTitle->setObjectName("pageTitle");
    pageScope_=new QLabel(pageHead);
    pageScope_->setObjectName("pageScope");
    pageScope_->setProperty("class", "muted");
    auto* openPoints=new QPushButton("查阅点表", pageHead);
    openPoints->setObjectName("textLink");
    pageHeadLayout->addWidget(pageTitle);
    pageHeadLayout->addWidget(pageScope_);
    pageHeadLayout->addStretch(1);
    pageHeadLayout->addWidget(openPoints);
    canvasLayout->addWidget(pageHead);
    canvasLayout->addWidget(workspace, 0, Qt::AlignTop);
    connect(openPoints, &QPushButton::clicked, this, [this]{ openDiagnostic("点表"); });
    canvasLayout->addStretch(1);
    runStack->addWidget(emptyRun);
    runStack->addWidget(runCanvas);
    runLayout->addWidget(runStack, 0);
    auto* runTail=new QWidget(run);
    runTail->setObjectName("runTail");
    runTail->setSizePolicy(QSizePolicy::Preferred, QSizePolicy::Expanding);
    runLayout->addWidget(runTail);
    runLayout->addStretch(1);
    auto* runBody=new QWidget(workspace);
    runBody->setObjectName("runBody");
    auto* runColumns=new QHBoxLayout(runBody);
    runColumns->setContentsMargins(0, 0, 0, 0);
    runColumns->setSpacing(16);
    auto* facts=new QWidget(runBody);
    auto* factsLayout=new QVBoxLayout(facts);
    factsLayout->setContentsMargins(0, 0, 0, 0);
    factsLayout->setSpacing(2);
    factsLayout->addWidget(workHeading);
    factsLayout->addWidget(stationMeta_);
    factsLayout->addWidget(interfaceLine_);
    factsLayout->addWidget(scopeBanner_);
    readinessNote_=new QLabel(workspace);
    readinessNote_->setObjectName("readinessNote");
    readinessNote_->setWordWrap(true);
    readinessNote_->setAlignment(Qt::AlignLeft | Qt::AlignTop);
    readinessNote_->hide();
    facts->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Maximum);
    auto* left=new QWidget(runBody);
    auto* leftLayout=new QVBoxLayout(left);
    leftLayout->setContentsMargins(0,0,0,0);
    leftLayout->setSpacing(4);
    auto* right=new QWidget(runBody);
    auto* rightLayout=new QVBoxLayout(right);
    rightLayout->setContentsMargins(0,0,0,0);
    rightLayout->setSpacing(2);
    runColumns->addWidget(left, 1, Qt::AlignTop);
    runColumns->addWidget(right, 1, Qt::AlignTop);
    recentTable_=new QTableWidget(this);
    recentTable_->setObjectName("recentTable");
    prepareTable(recentTable_, {"时间", "记录"});
    recentTable_->hide();
    auto* recentLabel=new QLabel("近期记录", this);
    recentLabel->setObjectName("recentLabel");
    recentLabel->hide();
    auto* recentScope=new QLabel(this);
    recentScope->setObjectName("recentScope");
    recentScope->hide();
    simOrder_=new QLabel("当前无活动料车");
    simOrder_->setObjectName("simOrder");
    simOrder_->setWordWrap(false);
    roundRack_=new RoundRack(this);
    roundRack_->hide();
    rackNote_=new QLabel("不是左右位置", this);
    rackNote_->setObjectName("muted");
    resultTable_=new QTableWidget(this);
    resultTable_->setObjectName("runResultTable");
    prepareTable(resultTable_, {"执行与采集", "当前结果"});
    resultTable_->hide();
    runBody->hide();
    workspaceLayout->setContentsMargins(0, 0, 0, 0);
    workspaceLayout->setSpacing(0);
    shopPlan_=new ShopPlan(workspace);
    workspaceLayout->addWidget(shopPlan_);
    workspaceLayout->addWidget(stationBoard_);
    auto* readyBox=new QWidget(workspace);
    readyBox->setObjectName("readyBox");
    auto* readyLayout=new QHBoxLayout(readyBox);
    readyLayout->setContentsMargins(8, 8, 8, 8);
    auto* readyText=new QVBoxLayout;
    readyText->setSpacing(2);
    readinessNote_->setParent(readyBox);
    readinessNote_->setText("等待工位信号");
    auto* readyMore=new QLabel("未绑定模拟源。设备许可、读码与补料信息将在接入后呈现。", readyBox);
    readyMore->setObjectName("readyMore");
    readyMore->setWordWrap(true);
    readyText->addWidget(readinessNote_);
    readyText->addWidget(readyMore);
    readyLayout->addLayout(readyText, 1);
    auto* openAccess=new QPushButton("查看接入信息", readyBox);
    openAccess->setObjectName("textLink");
    readyLayout->addWidget(openAccess, 0, Qt::AlignRight | Qt::AlignVCenter);
    workspaceLayout->addWidget(readyBox);
    auto* foot=new QWidget(workspace);
    auto* footLayout=new QVBoxLayout(foot);
    footLayout->setContentsMargins(12, 0, 12, 2);
    footLayout->setSpacing(4);
    scopeBanner_->setParent(foot);
    auto* rangeRow=new QHBoxLayout;
    auto* range=new QLabel("作业展示范围：各工位本轮", foot);
    range->setObjectName("muted");
    auto* openCollect=new QPushButton("采集详情", foot);
    openCollect->setObjectName("textLink");
    rangeRow->addWidget(range);
    rangeRow->addStretch(1);
    rangeRow->addWidget(openCollect);
    footLayout->addLayout(rangeRow);
    workspaceLayout->addWidget(foot);
    workspace->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Maximum);
    connect(openAccess, &QPushButton::clicked, this, [this]{ openDiagnostic("接口"); });
    connect(openCollect, &QPushButton::clicked, this, [this]{ openDiagnostic("采集"); });

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
    stageTable_=new QTableWidget;
    stageTable_->setObjectName("stageTable");
    prepareTable(stageTable_, {"小车", "读码", "分拣", "本工位完成", "本轮"});
    stageTable_->setSelectionMode(QAbstractItemView::NoSelection);
    stageTable_->horizontalHeader()->setStretchLastSection(true);
    stageTable_->horizontalHeader()->setSectionResizeMode(0, QHeaderView::Fixed);
    stageTable_->horizontalHeader()->setSectionResizeMode(1, QHeaderView::Stretch);
    stageTable_->horizontalHeader()->setSectionResizeMode(2, QHeaderView::Stretch);
    stageTable_->horizontalHeader()->setSectionResizeMode(3, QHeaderView::Stretch);
    stageTable_->horizontalHeader()->setSectionResizeMode(4, QHeaderView::Stretch);
    stageTable_->verticalHeader()->setDefaultSectionSize(28);
    stageTable_->setColumnWidth(0, 72);
    stageTable_->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);
    stageTable_->setShowGrid(false);
    stageTable_->setFrameShape(QFrame::NoFrame);
    stageTable_->viewport()->setAutoFillBackground(false);
    stageTable_->setStyleSheet("QTableWidget { background: #f7f8fa; border: none; } QTableWidget::item { background: transparent; border: none; } QHeaderView::section { background: #f7f8fa; color: #5c6370; border: none; padding: 1px 4px; font-size: 12px; font-weight: 600; }");
    stageTable_->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    stageTable_->setFixedHeight(112);
    stageGuide_=new QLabel(this);
    stageGuide_->setObjectName("stageGuide");
    stageGuide_->hide();
    progressLabel_=new QLabel("进度未知");
    progressLabel_->setObjectName("roundProgress");
    liveFace_=new QWidget(this);
    liveFace_->setObjectName("liveFace");
    auto* faceLayout=new QHBoxLayout(liveFace_);
    faceLayout->setContentsMargins(28, 8, 16, 12);
    faceLayout->setSpacing(16);
    auto* doc=new QWidget(liveFace_);
    auto* docLayout=new QVBoxLayout(doc);
    docLayout->setContentsMargins(0, 0, 0, 0);
    docLayout->setSpacing(6);
    auto* docTitle=new QHBoxLayout;
    auto* roundName=new QLabel("本轮", doc);
    roundName->setObjectName("section");
    auto* simChip=new QLabel("模拟", doc);
    simChip->setObjectName("sourceChip");
    docTitle->addWidget(roundName);
    docTitle->addWidget(simChip);
    docTitle->addStretch(1);
    docLayout->addLayout(docTitle);
    docLayout->addWidget(simOrder_);
    auto* openLogs=new QPushButton("查看模拟记录", doc);
    openLogs->setObjectName("textLink");
    docLayout->addWidget(openLogs, 0, Qt::AlignLeft);
    docLayout->addStretch(1);
    auto* rule=new QFrame(liveFace_);
    rule->setObjectName("faceRule");
    rule->setFrameShape(QFrame::VLine);
    rule->setFixedWidth(1);
    auto* track=new QWidget(liveFace_);
    track->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Preferred);
    auto* trackLayout=new QVBoxLayout(track);
    trackLayout->setContentsMargins(0, 0, 0, 0);
    trackLayout->setSpacing(4);
    auto* rackRow=new QHBoxLayout;
    rackRow->setContentsMargins(0, 0, 0, 0);
    rackRow->addWidget(progressLabel_);
    rackRow->addWidget(rackNote_);
    rackRow->addWidget(roundRack_, 1);
    trackLayout->addLayout(rackRow);
    trackLayout->addWidget(stageTable_);
    faceLayout->addWidget(doc);
    faceLayout->addWidget(rule);
    faceLayout->addWidget(track, 1);
    liveFace_->hide();
    connect(stageTable_,&QTableWidget::currentCellChanged,this,[this]{showStageResult();});
    connect(stationBoard_->horizontalHeader(), &QHeaderView::sectionResized, this, [this](int, int, int){
        if (!liveFace_ || !stationBoard_) return;
        auto* header=stationBoard_->horizontalHeader();
        const int span=header->count()>0 ? header->sectionPosition(header->count()-1)+header->sectionSize(header->count()-1)-header->sectionPosition(0) : 0;
        if (span>240) liveFace_->setFixedWidth(span);
    });
    connect(openLogs, &QPushButton::clicked, this, [this]{ openDiagnostic("日志"); });
    stationEmpty_=new QLabel("这个 PLC 还没有工位。不借用其他 PLC 的布置，也不能控制。");
    stationEmpty_->setWordWrap(true);
    stationEmpty_->setObjectName("muted");
    auto* stationPage=new QWidget;
    auto* stationLayout=new QVBoxLayout(stationPage);
    stationLayout->setContentsMargins(16, 12, 16, 16);
    stationLayout->setSpacing(8);
    auto* stationHeading=new QLabel("当前 PLC 的工位", stationPage);
    stationHeading->setObjectName("pageTitle");
    auto* stationHint=new QLabel("这里同时列出当前 PLC 的全部工位。没有工位选择，点表格也不会改采集。其他 PLC 的工位不会出现。", stationPage);
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
    pointScope_->setObjectName("pointScope");
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
        {"采集", "仅模拟", "手自动、单步和断链只作用于已绑定模拟采集的 PLC。", "采集器独占这四条模拟命令。未绑定的 PLC 不能推进。手动待步不推进。恢复自动要确认。心跳只说明采集进程还在，不是 PLC 在线。切页和筛选不改变采集。没有启动或停止采集的按钮。"},
        {"扫码查单", "待补映射", "还不能向 LWMS 查询分拣订单。", "分拣意图是扫料车二维码后再查单。后续机械臂共享成员未配置。库位、箱、料、订单都不显示成已查到。不能保存映射。"},
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
    historyEmpty_=new QLabel("当前 PLC 没有模拟历史。");
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

    logTable_=new SnapTable;
    logTable_->setObjectName("logTable");
    prepareTable(logTable_, {"时间", "等级", "类别", "对象", "内容"});
    logTable_->setSelectionBehavior(QAbstractItemView::SelectRows);
    logTable_->setSelectionMode(QAbstractItemView::SingleSelection);
    logTable_->setFocusPolicy(Qt::StrongFocus);
    logTable_->verticalHeader()->setDefaultSectionSize(28);
    auto* levels=new LevelDelegate(logTable_);
    levels->setObjectName("levelDelegate");
    logTable_->setItemDelegateForColumn(1, levels);
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
    logDetail_->setAlignment(Qt::AlignLeft | Qt::AlignTop);
    auto* logPage=new QWidget;
    logPage->setObjectName("logPage");
    auto* logLayout=new QVBoxLayout(logPage);
    logLayout->setContentsMargins(16, 12, 16, 12);
    logLayout->setSpacing(8);
    auto* logHeading=new QLabel("日志", logPage);
    logHeading->setObjectName("pageTitle");
    auto* logHint=new QLabel("时间列是北京时间（UTC+8），库内仍是 UTC 毫秒。链路和质量变化是模拟证据，不是现场设备离线，也没有 S7 重连。", logPage);
    logHint->setWordWrap(true);
    logHint->setObjectName("muted");
    logFollowButton_=new QPushButton("暂停跟随");
    logFollowButton_->setObjectName("logFollow");
    logFollowButton_->setFocusPolicy(Qt::StrongFocus);
    auto* logTools=new QHBoxLayout;
    logTools->addWidget(logSearch_);
    logTools->addWidget(logCategory_);
    logTools->addWidget(logLevel_);
    logTools->addWidget(clearLogs);
    logTools->addWidget(logFollowButton_);
    logTools->addStretch(1);
    auto* logDetailBox=new QWidget;
    logDetailBox->setObjectName("surface");
    logDetailBox->setMinimumWidth(300);
    logDetailBox->setMaximumWidth(360);
    auto* logScroll=new QScrollArea(logDetailBox);
    logScroll->setObjectName("logDetailScroll");
    logScroll->setWidgetResizable(true);
    logScroll->setFrameShape(QFrame::NoFrame);
    logScroll->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    auto* logInner=new QWidget;
    auto* logDetailLayout=new QVBoxLayout(logInner);
    logDetailLayout->setContentsMargins(12, 8, 12, 8);
    logDetailLayout->setSpacing(4);
    logDetailLayout->setAlignment(Qt::AlignTop);
    auto* logMeta=new QLabel(logInner);
    logMeta->setObjectName("logMeta");
    logMeta->setWordWrap(true);
    logMeta->setSizePolicy(QSizePolicy::Preferred, QSizePolicy::Maximum);
    auto* logTechButton=new QPushButton("+ 技术细节", logInner);
    logTechButton->setObjectName("logTechButton");
    logTechButton->setFont(QApplication::font());
    logTechButton->setCursor(Qt::PointingHandCursor);
    logTechButton->setSizePolicy(QSizePolicy::Maximum, QSizePolicy::Fixed);
    logTechButton->setStyleSheet("QPushButton#logTechButton { background:transparent; border:none; color:#3b5bdb; font-weight:600; padding:0 2px; min-height:22px; text-align:left; }");
    auto* logTech=new QLabel(logInner);
    logTech->setObjectName("logTech");
    logTech->setWordWrap(true);
    logTech->setSizePolicy(QSizePolicy::Preferred, QSizePolicy::Maximum);
    logTech->hide();
    logDetail_->setSizePolicy(QSizePolicy::Preferred, QSizePolicy::Maximum);
    auto* logDetailTitle=new QLabel("详情", logInner);
    logDetailTitle->setObjectName("section");
    logDetailTitle->setSizePolicy(QSizePolicy::Preferred, QSizePolicy::Maximum);
    logDetailLayout->addWidget(logDetailTitle);
    logDetailLayout->addWidget(logDetail_);
    logDetailLayout->addWidget(logMeta);
    logDetailLayout->addWidget(logTechButton, 0, Qt::AlignLeft);
    logDetailLayout->addWidget(logTech);
    logDetailLayout->addStretch(1);
    logScroll->setWidget(logInner);
    auto* logBoxLayout=new QVBoxLayout(logDetailBox);
    logBoxLayout->setContentsMargins(0, 0, 0, 0);
    logBoxLayout->addWidget(logScroll);
    connect(logTechButton, &QPushButton::clicked, this, [this]{
        auto* tech=findChild<QLabel*>("logTech");
        auto* button=findChild<QPushButton*>("logTechButton");
        if (!tech || !button) return;
        const bool open=!tech->isVisible();
        tech->setVisible(open);
        button->setText(open ? "- 技术细节" : "+ 技术细节");
        if (open) {
            const int width=std::max(180, tech->width()>40 ? tech->width() : 280);
            tech->setMinimumHeight(std::max(tech->fontMetrics().lineSpacing(), tech->heightForWidth(width)));
        }
    });
    logLayout->addWidget(logHeading);
    logLayout->addWidget(logHint);
    logLayout->addLayout(logTools);
    logLayout->addWidget(logEmpty_);
    auto* logSplit=new QHBoxLayout;
    logSplit->setSpacing(12);
    logSplit->addWidget(logTable_, 1);
    logSplit->addWidget(logDetailBox);
    logLayout->addLayout(logSplit, 1);

    auto* collectPage=new QWidget;
    collectPage->setObjectName("collectPage");
    auto* collectLayout=new QVBoxLayout(collectPage);
    collectPage->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Expanding);
    collectLayout->setContentsMargins(16, 8, 16, 8);
    collectLayout->setSpacing(6);
    collectLayout->setAlignment(Qt::AlignTop);
    auto* collectHeading=new QLabel("采集", collectPage);
    collectHeading->setObjectName("pageTitle");
    collectHeading->setSizePolicy(QSizePolicy::Preferred, QSizePolicy::Fixed);
    auto* collectRow=new QHBoxLayout;
    collectRow->setSpacing(12);
    auto* collectBox=new QWidget;
    collectBox->setObjectName("collectMain");
    collectBox->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Maximum);
    auto* snapLayout=new QVBoxLayout(collectBox);
    snapLayout->setContentsMargins(12, 8, 12, 8);
    snapLayout->setSpacing(4);
    auto* snapHead=new QHBoxLayout;
    auto* snapTitle=new QLabel("最近快照", collectBox);
    snapTitle->setObjectName("section");
    auto* snapChip=new QLabel("模拟", collectBox);
    snapChip->setObjectName("collectSnapChip");
    QLabel* collectSeen=new QLabel("—", collectBox);
    collectSeen->setObjectName("collectSeen");
    collectSeen->setAlignment(Qt::AlignRight | Qt::AlignVCenter);
    snapHead->addWidget(snapTitle);
    snapHead->addWidget(snapChip);
    snapHead->addStretch(1);
    snapHead->addWidget(collectSeen);
    auto* metricRow=new QHBoxLayout;
    auto addMetric=[&](const QString& key, QLabel** value, bool large){
        auto* box=new QWidget(collectBox);
        auto* layout=new QVBoxLayout(box);
        layout->setContentsMargins(0, 0, 0, 0);
        layout->setSpacing(2);
        auto* name=new QLabel(key, box);
        name->setObjectName("fieldLabel");
        *value=new QLabel("—", box);
        if (large) {
            QFont frameFont=figureFont();
            frameFont.setPixelSize(16);
            frameFont.setWeight(QFont::DemiBold);
            (*value)->setFont(frameFont);
        }
        layout->addWidget(name);
        layout->addWidget(*value);
        metricRow->addWidget(box, 1);
    };
    QLabel* collectFrame=nullptr; QLabel* collectQuality=nullptr; QLabel* collectLink=nullptr;
    addMetric("帧号", &collectFrame, true);
    addMetric("采集质量", &collectQuality, false);
    addMetric("模拟链路", &collectLink, false);
    collectFrame->setObjectName("collectFrame");
    collectQuality->setObjectName("collectQuality");
    collectLink->setObjectName("collectLink");
    collectOwner_=new QLabel("—", collectBox);
    collectOwner_->setObjectName("muted");
    auto* bindBox=new QWidget;
    bindBox->setObjectName("bindColumn");
    bindBox->setMinimumWidth(236);
    bindBox->setSizePolicy(QSizePolicy::Preferred, QSizePolicy::Maximum);
    auto* bindLayout=new QVBoxLayout(bindBox);
    bindLayout->setContentsMargins(16, 12, 16, 12);
    bindLayout->setSpacing(8);
    auto* bindTitle=new QLabel("数据源绑定", bindBox);
    bindTitle->setObjectName("section");
    auto* bindGrid=new QGridLayout;
    bindGrid->setHorizontalSpacing(12);
    bindGrid->setVerticalSpacing(6);
    bindGrid->setColumnStretch(1, 1);
    auto addBind=[&](int row, const QString& key, QLabel** value){
        auto* name=new QLabel(key, bindBox);
        name->setObjectName("fieldLabel");
        *value=new QLabel("—", bindBox);
        (*value)->setAlignment(Qt::AlignRight | Qt::AlignVCenter);
        bindGrid->addWidget(name, row, 0);
        bindGrid->addWidget(*value, row, 1);
    };
    QLabel* collectMode=nullptr; QLabel* collectPlc=nullptr;
    addBind(0, "采集归属", &collectLive_);
    collectLive_->setObjectName("collectLive");
    addBind(1, "运行模式", &collectMode);
    collectMode->setObjectName("collectMode");
    auto* rosterTitle=new QLabel("工位", bindBox);
    rosterTitle->setObjectName("fieldLabel");
    auto* collectRoster=new QWidget(bindBox);
    collectRoster->setObjectName("collectRoster");
    auto* rosterLayout=new QVBoxLayout(collectRoster);
    rosterLayout->setContentsMargins(0, 0, 0, 0);
    rosterLayout->setSpacing(4);
    collectPlc=new QLabel("未探测", bindBox);
    collectPlc->setObjectName("collectPlc");
    collectPlc->setAlignment(Qt::AlignRight | Qt::AlignVCenter);
    auto* plcKey=new QLabel("现场连接", bindBox);
    plcKey->setObjectName("fieldLabel");
    auto* plcRow=new QHBoxLayout;
    plcRow->addWidget(plcKey);
    plcRow->addStretch(1);
    plcRow->addWidget(collectPlc);
    auto* collectFields=new QTableWidget(collectBox);
    collectFields->setObjectName("collectFields");
    prepareTable(collectFields, {"字段", "当前值", "范围"});
    collectFields->horizontalHeader()->hide();
    collectFields->horizontalHeader()->setSectionResizeMode(1, QHeaderView::Stretch);
    collectFields->horizontalHeader()->setSectionResizeMode(2, QHeaderView::Stretch);
    collectFields->setColumnWidth(0, 120);
    collectFields->verticalHeader()->setDefaultSectionSize(28);
    collectFields->setShowGrid(false);
    collectFields->setFrameShape(QFrame::NoFrame);
    collectFields->viewport()->setAutoFillBackground(false);
    collectFields->setStyleSheet("QTableWidget { background: transparent; border: none; } QTableWidget::item { background: transparent; border: none; } QHeaderView::section { background: transparent; color: #5c6370; border: none; border-bottom: 1px solid #e6e8eb; padding: 2px 4px; font-size: 12px; font-weight: 600; }");
    collectFields->setSizePolicy(QSizePolicy::Ignored, QSizePolicy::Fixed);
    collectFields->setVerticalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    snapLayout->addLayout(snapHead);
    snapLayout->addLayout(metricRow);
    snapLayout->addWidget(collectFields);
    snapLayout->addWidget(collectOwner_);
    bindLayout->addWidget(bindTitle);
    bindLayout->addLayout(bindGrid);
    bindLayout->addWidget(rosterTitle);
    bindLayout->addWidget(collectRoster);
    bindLayout->addLayout(plcRow);
    collectBox->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Maximum);
    bindBox->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Maximum);
    collectRow->setAlignment(Qt::AlignTop);
    collectRow->addWidget(collectBox, 3, Qt::AlignTop);
    collectRow->addWidget(bindBox, 2, Qt::AlignTop);
    collectChanges_=new SnapTable;
    collectChanges_->setObjectName("collectChanges");
    collectChanges_->verticalHeader()->setSectionResizeMode(QHeaderView::Fixed);
    collectChanges_->verticalHeader()->setDefaultSectionSize(32);
    prepareTable(collectChanges_, {"时间", "变化"});
    collectChanges_->verticalHeader()->setDefaultSectionSize(28);
    collectChanges_->horizontalHeader()->setSectionResizeMode(1, QHeaderView::Stretch);
    collectChanges_->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Expanding);
    auto* collectUnbound=new QLabel("当前 PLC 未绑定模拟源。进程心跳不是这个 PLC 在线，也不是通行授权。", collectPage);
    collectUnbound->setObjectName("collectUnbound");
    collectUnbound->setWordWrap(true);
    collectUnbound->hide();
    auto* changeLabel=new QLabel("最近采集变化", collectPage);
    changeLabel->setObjectName("collectChangeLabel");
    auto* collectControls=new QWidget(collectPage);
    collectControls->setObjectName("collectControls");
    auto* controlLayout=new QVBoxLayout(collectControls);
    controlLayout->setContentsMargins(12, 8, 12, 8);
    controlLayout->setSpacing(4);
    scopeBanner_->setParent(collectControls);
    scopeBanner_->setVisible(true);
    commandPane->setParent(collectControls);
    commandPane->setVisible(true);
    controlLayout->addWidget(scopeBanner_);
    controlLayout->addWidget(commandPane);
    snapLayout->addWidget(collectControls);
    auto* collectWorkspace=new QWidget(collectPage);
    collectWorkspace->setObjectName("collectWorkspace");
    collectWorkspace->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Maximum);
    auto* collectWorkspaceLayout=new QVBoxLayout(collectWorkspace);
    collectWorkspaceLayout->setContentsMargins(4, 4, 4, 8);
    collectWorkspaceLayout->setSpacing(8);
    collectWorkspaceLayout->addLayout(collectRow);
    collectWorkspaceLayout->addWidget(changeLabel);
    collectWorkspaceLayout->addWidget(collectUnbound);
    collectWorkspaceLayout->addWidget(collectChanges_, 1);
    collectLayout->addWidget(collectHeading, 0, Qt::AlignTop);
    collectLayout->addWidget(collectWorkspace, 0, Qt::AlignTop);
    collectLayout->addStretch(1);

    moduleTabs_=new ModuleTabs;
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
    tabLayout->setContentsMargins(12, 0, 12, 0);
    tabLayout->setSpacing(0);
    tabLayout->addWidget(moduleTabs_);
    tabLayout->addStretch(1);
    moduleStack_=new PageStack;
    moduleStack_->setObjectName("moduleStack");
    stationPage->setParent(this);
    stationPage->hide();
    historyPage->setParent(this);
    historyPage->hide();
    moduleStack_->addWidget(run);
    moduleStack_->addWidget(logPage);
    moduleStack_->addWidget(collectPage);
    moduleStack_->addWidget(pointPage);
    moduleStack_->addWidget(scrollPage(boundaryPage));
    for (const auto& title : {QString("运行"), QString("日志"), QString("采集"), QString("点表"), QString("接口")})
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
    statusBar()->addWidget(processLabel_);
    statusBar()->addWidget(runtimeLabel_);
    statusBar()->addWidget(heartbeatLabel_, 1);
    statusBar()->addPermanentWidget(closure);

    connect(schematic_, &SchematicView::stationActivated, this, [this](const QString&) {});
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
    connect(logTable_, &QTableWidget::itemClicked, this, [this]{
        logFollow_=false;
        if (logFollowButton_) logFollowButton_->setText("恢复跟随");
        showLogDetail();
    });
    connect(logFollowButton_, &QPushButton::clicked, this, [this]{
        logFollow_=!logFollow_;
        if (logFollowButton_) logFollowButton_->setText(logFollow_ ? "暂停跟随" : "恢复跟随");
        if (logFollow_ && logTable_ && logTable_->rowCount()>0) {
            applyingLogSelection_=true;
            logTable_->setCurrentCell(0, 0);
            applyingLogSelection_=false;
            showLogDetail();
        }
    });
    setTabOrder(moduleTabs_, factoryCombo_);
    setTabOrder(factoryCombo_, plcCombo_);
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
    connect(plcCombo_, qOverload<int>(&QComboBox::currentIndexChanged), this, [this](int) {
        if (updating_) return;
        auto next=selection_;
        const auto id=comboId(plcCombo_).toStdString();
        if (id.empty()) return;
        next.plc_id=id;
        if (const auto* plc=catalog_.find_plc(id)) next.area_id=plc->area_id;
        next.line_id.clear();
        next.station_id.clear();
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
        if (historyTable_->rowCount()==0) historyEmpty_->setText("当前 PLC 没有模拟历史。");
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
        if (pointTable_->rowCount()==0) pointEmpty_->setText("当前 PLC 没有点表。");
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
        if(logEmpty_){ logEmpty_->setVisible(true); logEmpty_->setText("当前 PLC 没有模拟日志。"); }
        if(logDetail_) logDetail_->setText("当前 PLC 没有可查看的日志。");
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
    applyingLogSelection_=true;
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
            if (column==1 && row.level=="warn") {
                item->setForeground(QColor("#8a5a00"));
                item->setBackground(QColor("#fff4d6"));
                QFont bold=item->font();
                bold.setBold(true);
                item->setFont(bold);
            } else if (column==1) item->setForeground(QColor("#5c6370"));
            logTable_->setItem(i, column, item);
        }
    }
    if (logFollow_ && count>0) logTable_->setCurrentCell(0, 0);
    else if (selectedRow>=0) logTable_->setCurrentCell(selectedRow, 0);
    else logTable_->setCurrentCell(-1, -1);
    applyingLogSelection_=false;
    if (logFollowButton_) logFollowButton_->setText(logFollow_ ? "暂停跟随" : "恢复跟随");
    applyLogFilter();
    if (auto* table=dynamic_cast<SnapTable*>(logTable_)) table->snap();
    if (auto* table=dynamic_cast<SnapTable*>(collectChanges_)) table->snap();
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
    if (auto* table=dynamic_cast<SnapTable*>(logTable_)) table->snap();
}
void Workbench::showLogDetail() {
    if(!logDetail_ || !logTable_) return;
    auto* meta=findChild<QLabel*>("logMeta");
    auto* tech=findChild<QLabel*>("logTech");
    auto* techButton=findChild<QPushButton*>("logTechButton");
    const int row=logTable_->currentRow();
    auto* item=row<0?nullptr:logTable_->item(row,0);
    const bool empty=!item || logTable_->isRowHidden(row);
    if (meta) meta->setVisible(!empty);
    if (techButton) techButton->setVisible(!empty);
    if (empty) {
        logDetail_->setText("选择一行查看详情。时间显示为北京时间，库里仍是 UTC 毫秒。");
        if (meta) meta->clear();
        if (tech) { tech->clear(); tech->hide(); }
        if (techButton) techButton->setText("+ 技术细节");
        return;
    }
    const QString level=logTable_->item(row, 1) ? logTable_->item(row, 1)->text() : QString();
    const QString object=logTable_->item(row, 3) ? logTable_->item(row, 3)->text() : QString();
    logDetail_->setText(showLogText(item->data(Qt::UserRole+3).toString()));
    if (meta) meta->setText(item->text()+" · "+level+" · "+object);
    if (tech) tech->setText(item->data(Qt::UserRole).toString());
    const auto fitTop=[&](QLabel* label) {
        if (!label || !label->isVisible()) return;
        const int width=std::max(180, label->width()>40 ? label->width() : 280);
        label->setMinimumHeight(std::max(label->fontMetrics().lineSpacing(), label->heightForWidth(width)));
    };
    fitTop(logDetail_);
    fitTop(meta);
    fitTop(tech);
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
    if (peerTable_) peerTable_->clearSelection();
    applyHistoryFilter();
    applyPointFilter();
    if (recentTable_) {
        recentTable_->hide();
        if (auto* note=findChild<QLabel*>("recentScope")) note->hide();
    }
    auto setFact=[&](const char* name, const QString& text){ if(auto* label=findChild<QLabel*>(name)) label->setText(text); };
    if(collectOwner_) collectOwner_->setText(view_.runtime.liveness=="recent" ? "心跳正常 · 不是 PLC 在线" : "心跳不可用 · 不是 PLC 在线");
    const bool bound=liveScope();
    const auto* owned=catalog_.plc_of_station(catalog_.live_station_id());
    if(collectLive_) collectLive_->setText(owned ? QString::fromStdString(owned->name) : "未绑定");
    const bool frame=bound && !view_.rows.empty();
    setFact("collectSeen", frame ? "快照 "+beijingTime(view_.rows.front().received_ms,"HH:mm:ss")+" 北京时间" : "无模拟帧");
    setFact("collectFrame", frame ? QString::number(view_.rows.front().seq) : "无");
    const QString qualityText=frame ? QString(view_.rows.front().quality=="good"?"正常":"不可用") : "无";
    const QString linkText=!frame ? "无" : view_.rows.front().link=="down" ? "模拟断开" : "正常";
    setFact("collectQuality", qualityText);
    setFact("collectLink", linkText);
    setFact("collectMode", bound ? QString(view_.runtime.mode=="manual" ? "手动" : "自动")+" · 模拟" : "不作用于本 PLC");
    setFact("collectPlc", "未探测");
    if (auto* fields=findChild<QTableWidget*>("collectFields")) {
        fields->setVisible(bound);
        if (bound && frame) {
            const bool good=view_.rows.front().quality=="good";
            fields->setRowCount(3);
            for (int i=0; i<3; ++i) {
                const auto raw=good ? view_.rows.front().carts[i] : "UNKNOWN";
                QStringList columns={QString("小车 %1 阶段").arg(i+1), stateText(raw), "140 · 本轮"};
                for (int column=0; column<columns.size(); ++column) fields->setItem(i, column, new QTableWidgetItem(columns[column]));
            }
            fitRows(fields, 28);
            fields->setFixedHeight(fields->height()+6);
        } else fields->setRowCount(0);
    }
    if (auto* roster=findChild<QWidget*>("collectRoster")) {
        if (auto* layout=qobject_cast<QVBoxLayout*>(roster->layout())) {
            QStringList signature;
            for (auto* station : catalog_.stations_of(selection_.plc_id)) {
                const bool own=catalog_.is_live(station->id);
                const QString mark=own ? "live" : catalog::candidate_workbook(station->id) ? "doc" : catalog::pack_workbook(station->id) ? "pack" : catalog::drawing_without_points(station->id) ? "drawn" : "idle";
                signature<<QString::fromStdString(station->code)+":"+mark;
            }
            const QString sig=signature.join("|");
            if (roster->property("rosterSig").toString()!=sig) {
                roster->setProperty("rosterSig", sig);
                while (QLayoutItem* child=layout->takeAt(0)) {
                    if (auto* widget=child->widget()) {
                        widget->setParent(nullptr);
                        delete widget;
                    }
                    delete child;
                }
                for (auto* station : catalog_.stations_of(selection_.plc_id)) {
                    auto* row=new QWidget(roster);
                    row->setObjectName("rosterRow");
                    auto* line=new QHBoxLayout(row);
                    line->setContentsMargins(0, 1, 0, 1);
                    auto* code=new QLabel(QString::fromStdString(station->code), row);
                    code->setObjectName("rosterCode");
                    code->setStyleSheet("color:#1b1f23; font-size:14px; font-weight:600; background:transparent;");
                    const bool own=catalog_.is_live(station->id);
                    const QString stateText=own ? "模拟源"
                        : catalog::candidate_workbook(station->id) ? "已登记点表 · 未接入"
                        : catalog::pack_workbook(station->id) ? "已有点表·未接入"
                        : catalog::drawing_without_points(station->id) ? "已有布置图·点表仍缺·未接入"
                        : "未接入";
                    auto* state=new QLabel(stateText, row);
                    state->setWordWrap(true);
                    state->setAlignment(Qt::AlignRight | Qt::AlignVCenter);
                    state->setObjectName("sourceChip");
                    state->setProperty("state", own ? "live" : "idle");
                    state->style()->unpolish(state);
                    state->style()->polish(state);
                    line->addWidget(code);
                    line->addStretch(1);
                    line->addWidget(state);
                    layout->addWidget(row);
                }
            }
        }
    }
    markState(findChild<QLabel*>("collectQuality"), !frame ? "unknown" : view_.rows.front().quality=="good" ? "ok" : "warn");
    markState(findChild<QLabel*>("collectLink"), !frame ? "unknown" : view_.rows.front().link=="down" ? "warn" : "ok");
    markState(findChild<QLabel*>("collectPlc"), "unknown");
    if(auto* note=findChild<QLabel*>("collectUnbound")) note->setVisible(!bound);
    if (auto* workspace=findChild<QWidget*>("collectWorkspace"))
        workspace->setSizePolicy(QSizePolicy::Expanding, bound ? QSizePolicy::Expanding : QSizePolicy::Maximum);
    if(auto* chip=findChild<QLabel*>("collectSnapChip")) chip->setVisible(bound);
    if(auto* heading=findChild<QLabel*>("collectChangeLabel")) heading->setVisible(bound);
    if(collectChanges_) {
        collectChanges_->setVisible(bound);
        if (bound) {
            const int sample=std::max(28, collectChanges_->verticalHeader()->defaultSectionSize());
            int header=collectChanges_->horizontalHeader()->isHidden() ? 0 : collectChanges_->horizontalHeader()->height();
            if (header<8) header=collectChanges_->horizontalHeader()->sizeHint().height();
            const int minHeight=header+sample*4+4;
            if (collectChanges_->minimumHeight()!=minHeight) collectChanges_->setMinimumHeight(minHeight);
        }
    }
    refillLogs(view_.logs);
}

void Workbench::refill() {
    std::vector<std::pair<QString, QString>> factories, areas;
    for (auto& row : catalog_.factories) factories.emplace_back(QString::fromStdString(row.id), QString::fromStdString(row.name));
    for (auto* row : catalog_.areas_of(selection_.factory_id)) areas.emplace_back(QString::fromStdString(row->id), QString::fromStdString(row->name));
    fillCombo(factoryCombo_, factories, QString::fromStdString(selection_.factory_id));
    fillCombo(areaCombo_, areas, QString::fromStdString(selection_.area_id));
    QSignalBlocker block(plcCombo_);
    plcCombo_->clear();
    auto* model=qobject_cast<QStandardItemModel*>(plcCombo_->model());
    for (auto* area : catalog_.areas_of(selection_.factory_id)) {
        plcCombo_->addItem(QString::fromStdString(area->name));
        const int header=plcCombo_->count()-1;
        plcCombo_->setItemData(header, QString());
        if (model) {
            if (auto* item=model->item(header)) item->setFlags(item->flags() & ~(Qt::ItemIsSelectable | Qt::ItemIsEnabled));
        }
        for (auto* plc : catalog_.plcs_of(area->id))
            plcCombo_->addItem(QString::fromStdString(plc->name), QString::fromStdString(plc->id));
    }
    const int selected=plcCombo_->findData(QString::fromStdString(selection_.plc_id));
    if (selected>=0) plcCombo_->setCurrentIndex(selected);
}

void Workbench::rebuildCards() {}

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
    if (kind=="roster") { activateSchematicStation(QString::fromUtf8(catalog::kStation40)); open("运行"); return; }
    if (kind=="sort") { activateSchematicStation(QString::fromUtf8(catalog::kStation210)); open("运行"); return; }
    if (kind=="seq4") { activateSchematicStation(QString::fromUtf8(catalog::kStation230)); open("运行"); return; }
    if (kind=="collect-sort") { activateSchematicStation(QString::fromUtf8(catalog::kStation210)); open("采集"); return; }
    if (kind=="collect-seq4") { activateSchematicStation(QString::fromUtf8(catalog::kStation230)); open("采集"); return; }
    if (kind=="peers") { activateSchematicStation(QString::fromUtf8(catalog::kStation150)); open("运行"); return; }
    if (!catalog_.live_station_id().empty()) activateSchematicStation(QString::fromStdString(catalog_.live_station_id()));
    if (kind=="stations") open("运行");
    else if (kind=="events") open("运行");
    else if (kind=="eventbad") { if (historyFilter_) historyFilter_->setCurrentIndex(historyFilter_->findData("bad")); open("运行"); }
    else if (kind=="points") open("点表");
    else if (kind=="pointfind") {
        if (pointSearch_) pointSearch_->setText("DB1900.DBX0.0");
        if (pointTable_ && pointTable_->rowCount()>0) pointTable_->selectRow(0);
        open("点表");
    }
    else if (kind=="boundary") open("接口");
    else if (kind=="logs" || kind=="logs-open") open("日志");
    else if (kind=="collect" || kind=="collectdown") open("采集");
    else if (kind=="emptystations") {
        if (auto selection=catalog_.selection_for_plc(catalog_.empty_plc_id())) setSelection(*selection);
        open("运行");
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
    if (catalog_.stations_of(selection_.plc_id).empty()) reason="未配置工位，不能控制";
    else if (!live) reason="当前 PLC 未绑定模拟采集，不能推进";
    else if (!usable) reason="采集心跳不可用";
    else if (pending) reason="等待采集端确认";
    else if (manual) reason="本 PLC 的模拟只作用于工位 140，手动待步";
    else reason="本 PLC 的模拟只作用于工位 140，自动中不可单步";
    controlReason_->setText(reason);
    modeButton_->setToolTip(reason);
    step_->setToolTip(reason);
    disconnect_->setToolTip(reason);
    if (auto* pane=findChild<QWidget*>("collectControls")) pane->setVisible(live);
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

void Workbench::presentStations(bool live, bool good, const std::vector<const catalog::Station*>& rows) {
    const QString plcId=QString::fromStdString(selection_.plc_id);
    if (plcId!=expandedPlc_) {
        expandedPlc_=plcId;
        expandedId_.clear();
        boardSignature_.clear();
        if (live) {
            for (auto* station : rows) if (catalog_.is_live(station->id)) expandedId_=QString::fromStdString(station->id);
        }
    }
    QStringList ids;
    for (auto* station : rows) ids<<QString::fromStdString(station->id);
    const QString signature=ids.join("|")+"#"+expandedId_;
    auto workText=[&](bool own){ return !own ? QString("—") : good ? QString("三车分拣 · 本轮") : QString("本轮 · 未知"); };
    auto sourceText=[&](const catalog::Station* station){
        if (catalog_.is_live(station->id)) return QString("模拟预览");
        if (catalog::candidate_workbook(station->id)) return QString("已登记点表 · 未接入");
        if (catalog::pack_workbook(station->id)) return QString("已有点表·未接入");
        if (catalog::drawing_without_points(station->id)) return QString("已有布置图·点表仍缺·未接入");
        return QString("未接入");
    };
    auto displayName=[&](const catalog::Station* station){
        const auto drawn=catalog::drawing_names(station->id);
        if (!drawn.empty()) {
            QStringList lines;
            for (const auto& name : drawn) lines<<QString::fromStdString(name);
            return lines.join("\n");
        }
        return station->name==station->code ? QString("名称未登记") : QString::fromStdString(station->name);
    };
    auto paintSummary=[&](int row, const catalog::Station* station, bool grouped){
        const bool own=catalog_.is_live(station->id);
        const QString name=displayName(station);
        const QStringList columns={QString::fromStdString(station->code), name, workText(own), sourceText(station)};
        for (int column=0; column<columns.size(); ++column) {
            auto* item=stationBoard_->item(row, column);
            if (!item) {
                item=new QTableWidgetItem;
                item->setFlags(Qt::ItemIsEnabled);
                stationBoard_->setItem(row, column, item);
            }
            item->setText(columns[column]);
            item->setData(Qt::UserRole, QString::fromStdString(station->id));
            item->setData(Qt::UserRole+2, own);
            item->setData(Qt::UserRole+3, grouped && own);
            item->setTextAlignment(Qt::AlignLeft | Qt::AlignVCenter);
        }
    };
    if (signature!=boardSignature_) {
        if (stageTable_) stageTable_->setParent(this);
        if (simOrder_) simOrder_->setParent(this);
        if (roundRack_) roundRack_->setParent(this);
        if (rackNote_) rackNote_->setParent(this);
        if (progressLabel_) progressLabel_->setParent(this);
        QWidget* previousFace=liveFace_;
        liveFace_=nullptr;
        if (previousFace && previousFace->parentWidget()==this) previousFace->deleteLater();
        stationBoard_->clearSpans();
        stationBoard_->setRowCount(0);
        liveFace_=new QWidget(this);
        liveFace_->setObjectName("liveFace");
        auto* faceLayout=new QHBoxLayout(liveFace_);
        faceLayout->setContentsMargins(20, 2, 12, 4);
        faceLayout->setSpacing(16);
        auto* doc=new QWidget(liveFace_);
        auto* docLayout=new QVBoxLayout(doc);
        docLayout->setContentsMargins(0, 0, 0, 0);
        docLayout->setSpacing(6);
        auto* docTitle=new QHBoxLayout;
        auto* roundName=new QLabel("本轮", doc);
        roundName->setObjectName("section");
        auto* simChip=new QLabel("模拟", doc);
        simChip->setObjectName("sourceChip");
        docTitle->addWidget(roundName);
        docTitle->addWidget(simChip);
        docTitle->addStretch(1);
        docLayout->addLayout(docTitle);
        docLayout->addWidget(simOrder_);
        auto* openLogs=new QPushButton("查看模拟记录", doc);
        openLogs->setObjectName("textLink");
        connect(openLogs, &QPushButton::clicked, this, [this]{ openDiagnostic("日志"); });
        docLayout->addWidget(openLogs, 0, Qt::AlignLeft);
        docLayout->addStretch(1);
        auto* track=new QWidget(liveFace_);
        track->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Preferred);
        auto* trackLayout=new QVBoxLayout(track);
        trackLayout->setContentsMargins(0, 0, 0, 0);
        trackLayout->setSpacing(4);
        auto* rackRow=new QHBoxLayout;
        rackRow->setContentsMargins(0, 0, 0, 0);
        rackRow->addWidget(progressLabel_);
        rackRow->addWidget(rackNote_);
        rackRow->addWidget(roundRack_, 1);
        trackLayout->addLayout(rackRow);
        trackLayout->addWidget(stageTable_);
        faceLayout->addWidget(doc);
        faceLayout->addWidget(track, 1);
        liveFace_->hide();
        int row=0;
        for (auto* station : rows) {
            stationBoard_->insertRow(row);
            const QString id=QString::fromStdString(station->id);
            const bool own=catalog_.is_live(station->id);
            const bool opened=id==expandedId_;
            paintSummary(row, station, opened);
            auto* link=new QPushButton(opened ? "收起" : "详情");
            link->setObjectName("textLink");
            auto* cell=new QWidget;
            cell->setObjectName("sourceCell");
            cell->setAutoFillBackground(true);
            auto* layout=new QHBoxLayout(cell);
            cell->setAttribute(Qt::WA_TranslucentBackground);
            cell->setStyleSheet(opened && own ? "background:#f7f8fa;" : "background:transparent;");
            layout->setContentsMargins(0, 0, 4, 0);
            layout->setSpacing(0);
            layout->addStretch(1);
            layout->addWidget(link);
            stationBoard_->setCellWidget(row, 4, cell);
            stationBoard_->setRowHeight(row, displayName(station).count('\n')>0 ? 52 : 40);
            connect(link, &QPushButton::clicked, this, [this, id]{
                expandedId_=expandedId_==id ? QString() : id;
                boardSignature_.clear();
                QTimer::singleShot(0, this, [this]{ render(); });
            });
            ++row;
            if (!opened) continue;
            stationBoard_->insertRow(row);
            stationBoard_->setSpan(row, 0, 1, stationBoard_->columnCount());
            if (own && liveFace_) {
                stationBoard_->setCellWidget(row, 0, liveFace_);
                liveFace_->show();
                liveFace_->adjustSize();
                stationBoard_->setRowHeight(row, std::max(148, liveFace_->sizeHint().height()+4));
            }             else {
                const bool documented=catalog::candidate_workbook(station->id);
                const bool workbook=catalog::pack_workbook(station->id);
                const bool drawn=catalog::drawing_without_points(station->id);
                auto* note=new QLabel(documented
                    ? "已登记点表，尚未接入。不借用其他工位的模拟。"
                    : workbook
                    ? "已有点表，尚未接入。没有模拟源，不借用其他工位的作业。"
                    : drawn
                    ? "已有布置图，点表仍缺。没有模拟源，不借用其他工位的作业。"
                    : "已登记。此工位没有模拟源，不借用其他工位的作业。");
                note->setObjectName("muted");
                note->setContentsMargins(28, 8, 16, 8);
                stationBoard_->setCellWidget(row, 0, note);
                stationBoard_->setRowHeight(row, 44);
            }
            ++row;
        }
        stationBoard_->horizontalHeader()->setDefaultAlignment(Qt::AlignLeft | Qt::AlignVCenter);
        if (auto* model=stationBoard_->horizontalHeader()->model()) {
            for (int column=0; column<stationBoard_->columnCount(); ++column)
                model->setHeaderData(column, Qt::Horizontal, int(Qt::AlignLeft | Qt::AlignVCenter), Qt::TextAlignmentRole);
        }
        boardSignature_=signature;
    } else {
        int summary=0;
        for (int row=0; row<stationBoard_->rowCount() && summary<static_cast<int>(rows.size()); ++row) {
            auto* key=stationBoard_->item(row, 0);
            if (!key || key->text().isEmpty()) continue;
            const bool grouped=stationBoard_->item(row, 0) && stationBoard_->item(row, 0)->data(Qt::UserRole+3).toBool();
            paintSummary(row, rows[static_cast<std::size_t>(summary)], grouped);
            ++summary;
        }
    }
    int height=std::max(28, stationBoard_->horizontalHeader()->sizeHint().height());
    for (int row=0; row<stationBoard_->rowCount(); ++row) height+=stationBoard_->rowHeight(row);
    stationBoard_->setFixedHeight(height+2);
    stationBoard_->setMinimumHeight(height+2);
    stationBoard_->setVisible(!rows.empty());
    if (auto* ready=findChild<QWidget*>("readyBox")) ready->setVisible(!live && !rows.empty());
}

void Workbench::render() {
    const bool live=liveScope();
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

    const catalog::Area* area=catalog_.find_area(selection_.area_id);
    const catalog::Plc* plc=catalog_.find_plc(selection_.plc_id);
    QString runtimeValue="未知";
    if (available_ && view_.runtime.mode=="manual") runtimeValue="手动";
    else if (available_ && view_.runtime.mode=="automatic") runtimeValue="自动";
    std::vector<const catalog::Station*> peer_rows;
    if (plc) peer_rows=catalog_.stations_of(plc->id);
    const QString peers=joinedIds(peer_rows);
    const bool good=live && available_ && !view_.rows.empty() && view_.rows.front().quality=="good" && view_.freshness!="clock_uncertain" && (view_.runtime.mode=="manual" || view_.freshness=="recent");
    QString sim_quality="未配置";
    QString scope="未配置";
    if (!peer_rows.empty() && !live) { sim_quality="未接入"; scope="未接入"; }
    else if (live) {
        scope="工位 140 · 模拟流 "+QString::fromStdString(catalog::kStream);
        sim_quality=view_.rows.empty() || !available_ ? "未知" : good ? "正常" : "不可用";
    }
    const auto* line=catalog_.find_line(selection_.line_id);
    const QString storage=line ? QString::fromStdString(line->storage) : "未配置";
    const QString zone=line ? QString::fromStdString(line->zone) : "未配置";
    stationMeta_->setText("仓库 "+storage+" · 库区 "+zone);
    if (acquisitionOwner_) {
        const auto* owned=catalog_.plc_of_station(catalog_.live_station_id());
        acquisitionOwner_->setText(owned ? "采集归属 "+QString::fromStdString(owned->name) : "采集归属 未绑定");
        acquisitionOwner_->setMinimumWidth(acquisitionOwner_->fontMetrics().horizontalAdvance(acquisitionOwner_->text())+8);
    }
    if (previewHint_) {
        const bool previewing=!live && plc;
        previewHint_->setVisible(previewing);
        previewHint_->setText(previewing ? "仅预览 · 采集归属仍为外装" : "");
        if (previewing) previewHint_->setMinimumWidth(previewHint_->fontMetrics().horizontalAdvance(previewHint_->text())+8);
    }
    if (readinessNote_) {
        readinessNote_->setVisible(!live && !peer_rows.empty());
        readinessNote_->setText("等待工位信号");
    }
    if (auto* more=findChild<QLabel*>("readyMore")) {
        more->setVisible(!live && !peer_rows.empty());
        more->setText("设备许可、读码与补料信息将在接入后呈现。");
    }
    stationMeta_->setToolTip(plc ? QString::fromStdString(plc->name)+" · PLC 自动未读取" : "PLC 未配置");
    interfaceLine_->setVisible(live);
    interfaceLine_->setText(live ? "库卡未授权 · LWMS 未连接 · 库位待补" : "");
    // One projection of the selected station's current frame feeds the order and the shared lane.
    CartLane lane;
    lane.visible=false;
    int completed=0;
    for (int index=0;index<3;++index) {
        const auto raw=good ? view_.rows.front().carts[index] : "UNKNOWN";
        lane.states[index]=stateText(raw);
        lane.marks[index]=good ? cartMark(raw) : CartMark::none;
        if (raw=="scanning" || raw=="sorting" || raw=="blocked") lane.target=index;
        if (raw=="station_complete") ++completed;
    }
    simOrder_->setVisible(live && !expandedId_.isEmpty());
    const QString progress=good ? QString("%1 / 3 台完成").arg(completed) : "进度未知";
    if (!live) simOrder_->setText(QString());
    else if (!good) simOrder_->setText("目标未知 · 进度未知");
    else if (lane.target>=0) simOrder_->setText(QString("取料工位 140 → 料车\u00A0%1").arg(lane.target+1));
    else simOrder_->setText("当前无活动料车");
    simOrder_->setWordWrap(true);
    simOrder_->setMinimumWidth(simOrder_->fontMetrics().horizontalAdvance(simOrder_->text())+12);
    if (progressLabel_) progressLabel_->setText(progress);
    std::vector<StationGlyph> glyphs;
    QStringList stationCodes;
    for (auto* item : peer_rows) {
        glyphs.push_back({item->id,QString::fromStdString(item->code)});
        stationCodes.push_back(QString::fromStdString(item->code));
    }
    schematic_->setScene(plc ? QString::fromStdString(plc->name) : QString(), glyphs, "", lane);
    if (auto* plan=static_cast<ShopPlan*>(shopPlan_)) plan->apply(plc, peer_rows);
    if (pageScope_) {
        pageScope_->setText(plc ? QString::fromStdString(plc->name)+" · 全部 "+QString::number(peer_rows.size())+" 个工位" : "未选择 PLC");
        pageScope_->setMinimumWidth(pageScope_->fontMetrics().horizontalAdvance(pageScope_->text())+4);
    }
    if (stationBoard_) presentStations(live, good, peer_rows);
    if (auto* rack=static_cast<RoundRack*>(roundRack_)) {
        int active=-1;
        QString rackStates[3];
        for (int index=0; index<3; ++index) {
            const auto raw=live && !view_.rows.empty() && good ? view_.rows.front().carts[index] : "UNKNOWN";
            rackStates[index]=live ? stateText(raw) : QString("—");
            if (raw=="scanning" || raw=="sorting") active=index;
        }
        rack->setFrame(rackStates[0], rackStates[1], rackStates[2], active);
        const bool openedLive=live && !expandedId_.isEmpty() && catalog_.is_live(expandedId_.toStdString());
        rack->setVisible(openedLive);
    }
    setRows(detailTable_, {
        {"工位", stationCodes.isEmpty() ? "未配置" : stationCodes.join("、")},
        {"业务模式", area ? QString::fromStdString(area->name) : "未配置"},
        {"运行模式", runtimeValue},
        {"实时范围", scope},
        {"模拟质量", sim_quality},
        {"设备连接", "未探测"},
        {"库存", "未知"},
        {"控制器", plc ? QString::fromStdString(plc->id+" "+plc->name) : "未配置"},
        {"关联工位", peers},
        {"仓库", storage},
        {"库区", zone},
        {"库位", "待补映射"},
    });
    peerTable_->setColumnCount(3);
    peerTable_->setRowCount(static_cast<int>(peer_rows.size()));
    peerTable_->setSelectionMode(QAbstractItemView::NoSelection);
    for (int i=0; i<static_cast<int>(peer_rows.size()); ++i) {
        const bool own=catalog_.is_live(peer_rows[i]->id);
        const auto drawn=catalog::drawing_names(peer_rows[i]->id);
        const QString name=drawn.empty()
            ? (peer_rows[i]->name==peer_rows[i]->code ? "名称未登记" : QString::fromStdString(peer_rows[i]->name))
            : QString::fromStdString(drawn.front());
        QStringList columns={QString::fromStdString(peer_rows[i]->code), name, own ? "模拟源" : "无模拟源"};
        for (int column=0; column<columns.size(); ++column) {
            auto* item=new QTableWidgetItem(columns[column]);
            item->setFlags(Qt::ItemIsEnabled | Qt::ItemIsSelectable);
            item->setToolTip(columns[column]);
            peerTable_->setItem(i, column, item);
        }
    }
    const auto audit_rows=audit::for_station(live ? catalog_.live_station_id() : std::string());
    bool documentedWorkbook=false;
    for (auto* station : peer_rows) if (catalog::candidate_workbook(station->id)) documentedWorkbook=true;
    const bool drawnOnly=std::any_of(peer_rows.begin(), peer_rows.end(), [](const catalog::Station* station){ return catalog::drawing_without_points(station->id); });
    if (!audit_rows.empty()) pointScope_->setText("只读。点表只属于工位 140，本批没有新的电气交接。150 与 160 的名称来自布置图标注，点表未接入。布置图不是写配置。");
    else if (documentedWorkbook) pointScope_->setText("候选点表已登记。工位对应待确认。尚未装入点配置。");
    else if (drawnOnly) pointScope_->setText("布置图名称只作显示。点表仍缺，尚未接入。不是电气交接。");
    else if (plc && std::string(catalog::shop_layout(plc->id))=="seq3-sides") pointScope_->setText("当前 PLC 没有点表。料架名称未接入，没有占位。");
    else pointScope_->setText("当前 PLC 没有点表。");
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
            if (column==1) setFigure(item);
            if (!codes.isEmpty() && column==2) item->setForeground(QColor("#8a5a12"));
            pointTable_->setItem(i, column, item);
        }
    }
    if (eventSource_) eventSource_->setText(live ? "来源：本 PLC 工位 140 · 模拟流 sim.three-cart.v1 · 北京时间 UTC+8" : "来源：当前 PLC 未绑定模拟采集。");
    const bool unconfigured=peer_rows.empty();
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
    if (auto* body=findChild<QWidget*>("runBody")) body->setVisible(live);
    if (auto* surface=findChild<QWidget*>("workSurface")) surface->setMaximumHeight(QWIDGETSIZE_MAX);

    auto showLiveDetail=[&](bool on) {
        stageTable_->setVisible(on);
        simOrder_->setVisible(on);
        if (stageGuide_) stageGuide_->hide();
        if (resultTable_) resultTable_->hide();
        if (recentTable_) recentTable_->hide();
    };
    if (!live) {
        showStageResult();
        stageTable_->setRowCount(0);
        showLiveDetail(false);
        historyTable_->setRowCount(0);
        eventNote_->setText(unconfigured ? "未配置" : "无实时历史");
        scopeBanner_->setText(unconfigured
            ? "当前范围没有工位，不能控制。"
            : "未绑定模拟源。采集仍在外装。");
        scopeBanner_->setVisible(false);
        syncModuleChrome();
        return;
    }
    scopeBanner_->setVisible(true);
    if (view_.rows.empty()) {
        showLiveDetail(true);
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
    stageTable_->setColumnCount(5);
    stageTable_->setRowCount(3);
    for (int i=0; i<3; ++i) {
        const auto value=good ? latest.carts[i] : "UNKNOWN";
        int current=0;
        QString note="未知";
        if (!good) note="无可信值";
        else if (value=="scanning") { current=1; note="扫码中"; }
        else if (value=="sorting") { current=2; note="分拣中"; }
        else if (value=="station_complete") { current=3; note="本轮步骤结束"; }
        else if (value=="waiting") note="等待扫码";
        else if (value=="blocked") note="模拟阻塞";
        const QStringList columns={
            QString("小车 %1").arg(i+1),
            current==1 ? "读码" : "—",
            current==2 ? "分拣" : "—",
            current==3 ? "本工位完成" : "—",
            note,
        };
        for (int column=0; column<columns.size(); ++column) {
            auto* item=new QTableWidgetItem(columns[column]);
            item->setFlags(Qt::ItemIsEnabled | Qt::ItemIsSelectable);
            const bool active=(current==1 && column==1) || (current==2 && column==2);
            if (active) item->setForeground(QColor("#3b5bdb"));
            else if (column>0) item->setForeground(current==3 && column==3 ? QColor("#1b1f23") : QColor("#5c6370"));
            stageTable_->setItem(i, column, item);
        }
    }
    if (stageTable_->currentRow()<0) stageTable_->setCurrentCell(std::max(0,selectedStage),0);
    fitRows(stageTable_, 28);
    if (liveFace_ && stationBoard_) {
        liveFace_->updateGeometry();
        for (int row=0; row<stationBoard_->rowCount(); ++row) {
            if (stationBoard_->cellWidget(row, 0)!=liveFace_) continue;
            liveFace_->adjustSize();
            stationBoard_->setRowHeight(row, std::max(148, liveFace_->sizeHint().height()+4));
        }
        int height=std::max(28, stationBoard_->horizontalHeader()->sizeHint().height());
        for (int row=0; row<stationBoard_->rowCount(); ++row) height+=stationBoard_->rowHeight(row);
        stationBoard_->setFixedHeight(height+2);
        stationBoard_->setMinimumHeight(height+2);
        auto* header=stationBoard_->horizontalHeader();
        const int span=header->count()>0 ? header->sectionPosition(header->count()-1)+header->sectionSize(header->count()-1)-header->sectionPosition(0) : 0;
        if (span>240) liveFace_->setFixedWidth(span);
    }
    if (stationBoard_) {
        auto* accent=stationBoard_->viewport()->findChild<QWidget*>("rowAccent");
        if (!accent) {
            accent=new QWidget(stationBoard_->viewport());
            accent->setObjectName("rowAccent");
            accent->setAttribute(Qt::WA_TransparentForMouseEvents);
            accent->setStyleSheet("background:#3b5bdb;");
        }
        int top=-1, bottom=-1;
        for (int row=0; row<stationBoard_->rowCount(); ++row) {
            auto* key=stationBoard_->item(row, 0);
            const bool grouped=key && key->data(Qt::UserRole+3).toBool();
            const bool face=stationBoard_->cellWidget(row, 0)==liveFace_;
            if (!grouped && !face) continue;
            const QRect rect=stationBoard_->visualRect(stationBoard_->model()->index(row, 0));
            if (!rect.isValid()) continue;
            if (top<0) top=rect.top();
            bottom=rect.bottom();
        }
        if (top>=0 && bottom>top) {
            accent->setGeometry(0, top, 3, bottom-top+1);
            accent->show();
            accent->raise();
        } else accent->hide();
    }
    showLiveDetail(true);
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
    scopeBanner_->setVisible(!scopeBanner_->text().startsWith("模拟帧"));
    if (recentTable_) {
        const int row=24;
        const int visible=height()>=760 ? 2 : 1;
        recentTable_->setFixedHeight(visible*row+2);
    }
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
    fitRows(resultTable_, 24);
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
    if (kind=="stations") return onTab("运行") && liveScope();
    if (kind=="events") return onTab("运行") && liveScope() && historyTable_ && historyTable_->rowCount()>0;
    if (kind=="eventbad") return onTab("运行") && liveScope() && historyFilter_ && historyFilter_->currentData().toString()=="bad" && historyTable_ && historyTable_->rowCount()>0;
    if (kind=="points") return onTab("点表") && liveScope() && pointTable_ && pointTable_->rowCount()>0;
    if (kind=="pointfind") return onTab("点表") && pointSearch_ && pointSearch_->text()=="DB1900.DBX0.0" && pointDetail_ && pointDetail_->text().contains("DB1900.DBX0.0");
    if (kind=="emptystations") return onTab("运行") && selection_.station_id.empty();
    if (kind=="boundary") return onTab("接口") && liveScope();
    if (kind=="logs" || kind=="logs-open") return onTab("日志") && logTable_ && logTable_->rowCount()>=12 && logTable_->viewport() && logTable_->viewport()->height()>=logTable_->rowHeight(0)*4;
    if (kind=="collect") return onTab("采集") && liveScope();
    if (kind=="collect-sort") return onTab("采集") && !liveScope() && selection_.plc_id==catalog::kPlcSeq3;
    if (kind=="seq4") return onTab("运行") && !liveScope() && selection_.plc_id==catalog::kPlcSeq4;
    if (kind=="collect-seq4") return onTab("采集") && !liveScope() && selection_.plc_id==catalog::kPlcSeq4;
    if (kind=="collectdown") return onTab("采集") && liveScope() && !view_.rows.empty() && view_.rows.front().link=="down";
    if (kind=="roster") return onTab("运行") && selection_.station_id==catalog::kStation40 && controllerPeerIds().size()==4 && !liveScope();
    if (kind=="sort") {
        auto* area=catalog_.area_of_station(selection_.station_id);
        return area && area->code=="seq" && !liveScope() && !selection_.station_id.empty();
    }
    if (kind=="peers") return controllerPeerIds().size()>=3 && controllerPeerIds().contains(QString::fromUtf8(catalog::kStation140)) && controllerPeerIds().contains(QString::fromUtf8(catalog::kStation150)) && liveScope();
    if (kind=="south") return selection_.station_id==catalog::kStation20 && !liveScope();
    if (kind=="meter2") return selection_.station_id==catalog::kStation40 && controllerPeerIds().size()==4 && !liveScope();
    if (kind=="empty") return selection_.station_id.empty() && !selection_.plc_id.empty();
    if (kind=="dropdown") return liveScope() && plcCombo_ && plcCombo_->count()>=4;
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
bool Workbench::liveScope() const {
    for (auto* station : catalog_.stations_of(selection_.plc_id)) if (catalog_.is_live(station->id)) return true;
    return false;
}
