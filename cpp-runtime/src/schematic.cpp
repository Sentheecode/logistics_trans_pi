#include "schematic.hpp"
#include <QMouseEvent>
#include <QPainter>
#include <QPaintEvent>
#include <algorithm>
#include <vector>

SchematicView::SchematicView(QWidget* parent): QWidget(parent) {
    setObjectName("schematicView");
    setMinimumHeight(120);
    setMouseTracking(false);
    error_="未选择 PLC";
}
void SchematicView::setScene(const QString& plcName, const std::vector<StationGlyph>& stations, const std::string& focusId, const CartLane& lane) {
    plc_name_=plcName;
    stations_=stations;
    focus_=focusId;
    lane_=lane;
    scene_set_=true;
    error_.clear();
    update();
}
QStringList SchematicView::peerIds() const {
    QStringList ids;
    for (auto& station : stations_) if (station.id!=focus_) ids.push_back(QString::fromStdString(station.id));
    return ids;
}
QColor SchematicView::markerColor(const QString& id) const {
    if (!id.isEmpty() && id.toStdString()==focus_) return QColor("#166660");
    for (auto& station : stations_) if (QString::fromStdString(station.id)==id) return QColor("#5c7279");
    return {};
}
QStringList SchematicView::shapeIds() const {
    QStringList ids;
    for (auto& station : stations_) ids.push_back(QString::fromStdString(station.id));
    return ids;
}
std::vector<SchematicView::Bay> SchematicView::bays() const {
    std::vector<Bay> out;
    if (stations_.empty() || width()<80 || height()<80) return out;
    QRectF area=QRectF(rect()).adjusted(8, 26, -8, lane_.visible ? -62 : -12);
    const int count=static_cast<int>(stations_.size());
    const int columns=std::min(count, std::max(1, static_cast<int>(area.width()/88)));
    const int rows=(count+columns-1)/columns;
    const double gap=6;
    const double bayWidth=(area.width()-gap*(columns-1))/columns;
    const double bayHeight=(area.height()-gap*(rows-1))/rows;
    for (int index=0; index<count; ++index) {
        const int row=index/columns;
        const int column=index%columns;
        out.push_back({stations_[static_cast<std::size_t>(index)].id,
            QRectF(area.left()+column*(bayWidth+gap), area.top()+row*(bayHeight+gap), bayWidth, bayHeight)});
    }
    return out;
}
QString SchematicView::stationAt(const QPointF& widgetPoint) const {
    for (auto& bay : bays()) if (bay.rect.contains(widgetPoint)) return QString::fromStdString(bay.id);
    return {};
}
QRectF SchematicView::stationRect(const QString& id) const {
    for (auto& bay : bays()) if (QString::fromStdString(bay.id)==id) return bay.rect;
    return {};
}
namespace {
void drawEquipment(QPainter& painter, const QRectF& space, const QColor& ink) {
    // One generic rack and arm for the simulation. Geometry is local to the icon.
    const double scale=std::min({space.width()/98, space.height()/42, 1.5});
    if (scale<=0) return;
    painter.save();
    painter.translate(space.center());
    painter.scale(scale,scale);
    painter.setPen(Qt::NoPen);
    painter.setBrush(QColor("#dce5e6"));
    painter.drawRect(QRectF(-44,17,87,2));

    painter.setPen(QPen(ink,1.4,Qt::SolidLine,Qt::SquareCap));
    painter.setBrush(QColor("#e5eded"));
    for (int row=0;row<2;++row) {
        const double shelfY=-2+row*15;
        for (int column=0;column<2;++column) {
            const double x=-38+column*14;
            painter.drawRect(QRectF(x,shelfY-8,11,8));
            painter.drawLine(QPointF(x+5.5,shelfY-8),QPointF(x+5.5,shelfY-5));
        }
        painter.drawLine(QPointF(-42,shelfY+1),QPointF(-9,shelfY+1));
    }
    painter.drawLine(QPointF(-42,-15),QPointF(-42,17));
    painter.drawLine(QPointF(-9,-15),QPointF(-9,17));
    painter.drawLine(QPointF(-42,-15),QPointF(-9,-15));

    // The arm is fixed to a base; the open fork is visible at icon size.
    const QPointF base(31,10), elbow(18,-9), wrist(3,-4);
    painter.setPen(QPen(ink,5,Qt::SolidLine,Qt::RoundCap,Qt::RoundJoin));
    painter.drawLine(base,elbow);
    painter.drawLine(elbow,wrist);
    painter.setPen(QPen(ink,1.6));
    painter.setBrush(QColor("#f7fafa"));
    painter.drawEllipse(elbow,3.1,3.1);
    painter.drawEllipse(base,3.4,3.4);
    painter.drawLine(wrist,QPointF(3,2));
    painter.drawLine(QPointF(0,2),QPointF(6,2));
    painter.drawLine(QPointF(0,2),QPointF(0,6));
    painter.drawLine(QPointF(6,2),QPointF(6,6));
    painter.setBrush(ink);
    painter.drawRect(QRectF(23,13,16,3));
    painter.restore();
}
}

void SchematicView::drawBay(QPainter& painter, const Bay& bay, const StationGlyph& glyph) const {
    painter.save();
    const QRectF box=bay.rect.adjusted(0.5,0.5,-0.5,-0.5);
    const bool selected=glyph.id==focus_;
    const QColor ink=markerColor(QString::fromStdString(glyph.id));
    painter.setPen(QPen(selected ? ink : QColor("#beced0"), 1));
    painter.setBrush(selected ? QColor("#e9f4f2") : QColor("#fafcfc"));
    painter.drawRoundedRect(box,3,3);
    painter.setClipRect(box);
    if (selected) {
        painter.fillRect(QRectF(box.left()+1,box.top()+1,3,box.height()-2),ink);
        const QPointF mark(box.right()-10,box.top()+9);
        painter.setPen(QPen(ink,1.8,Qt::SolidLine,Qt::RoundCap,Qt::RoundJoin));
        painter.drawLine(mark+QPointF(-3,0),mark+QPointF(-1,2));
        painter.drawLine(mark+QPointF(-1,2),mark+QPointF(3,-2));
    }
    const QString label=glyph.number.contains("工位") ? glyph.number : "工位 "+glyph.number;
    QFont labelFont=painter.font();
    labelFont.setBold(true);
    labelFont.setPixelSize(13);
    painter.setFont(labelFont);
    painter.setPen(selected ? QColor("#124e4c") : QColor("#354b52"));
    const auto name=box.adjusted(selected ? 14 : 7,2,selected ? -14 : -7,0);
    const auto heading=lane_.visible ? QRectF(name.x(),name.y(),name.width(),19) : name;
    painter.drawText(heading, Qt::AlignCenter, painter.fontMetrics().elidedText(label,Qt::ElideRight,heading.width()));
    if (lane_.visible) drawEquipment(painter,box.adjusted(4,21,-4,-3),ink);
    painter.restore();
}

void SchematicView::drawLane(QPainter& painter) const {
    // A single shared rail and a separate caption keep vehicles independent of station columns.
    const QRectF lane(8,height()-55,width()-16,48);
    painter.setPen(Qt::NoPen);
    painter.setBrush(QColor("#e7eeef"));
    painter.drawRoundedRect(lane,3,3);
    const QRectF caption(lane.left()+6,lane.top()+3,58,lane.height()-6);
    painter.setPen(QColor("#304e55"));
    painter.drawText(caption,Qt::AlignLeft|Qt::AlignTop,"共用通道");
    QFont small=painter.font();
    small.setBold(false);
    small.setPixelSize(11);
    painter.setFont(small);
    painter.setPen(QColor("#50696e"));
    painter.drawText(caption,Qt::AlignLeft|Qt::AlignBottom,"位置示意");
    const double start=caption.right()+5;
    const double end=lane.right()-5;
    const double gap=5;
    const double carWidth=(end-start-2*gap)/3;
    painter.setPen(QPen(QColor("#8fa5a9"),1.3));
    painter.drawLine(QPointF(start,lane.bottom()-3),QPointF(end,lane.bottom()-3));
    for (int i=0;i<3;++i) {
        const QRectF cart(start+i*(carWidth+gap),lane.top()+3,carWidth,34);
        const bool target=lane_.target==i;
        const bool alert=lane_.marks[i]==CartMark::alert;
        const QColor ink=alert ? QColor("#96501c") : target ? QColor("#135e5a") : QColor("#52676e");
        painter.setPen(QPen(ink,target ? 1.6 : 1));
        painter.setBrush(alert ? QColor("#fff0d7") : target ? QColor("#d4ece7") : QColor("#f8fafb"));
        painter.drawRoundedRect(cart,3,3);
        QFont carFont=small;
        carFont.setBold(true);
        painter.setFont(carFont);
        painter.setPen(ink);
        const QString name=QString(target ? "目标 · 料车 %1" : "料车 %1").arg(i+1);
        painter.drawText(cart.adjusted(2,1,-2,0),Qt::AlignTop|Qt::AlignHCenter,name);
        painter.setFont(small);
        painter.drawText(cart.adjusted(2,0,-2,-1),Qt::AlignBottom|Qt::AlignHCenter,lane_.states[i]);
        painter.setPen(Qt::NoPen);
        painter.setBrush(ink);
        for (double x : {cart.left()+10,cart.right()-10})
            painter.drawEllipse(QPointF(x,cart.bottom()+2.5),2.4,2.4);
    }
}

void SchematicView::paintEvent(QPaintEvent*) {
    QPainter painter(this);
    painter.setRenderHint(QPainter::Antialiasing,true);
    painter.fillRect(rect(),QColor("#f2f6f6"));
    QFont caption=font();
    caption.setPixelSize(12);
    caption.setBold(true);
    painter.setFont(caption);
    painter.setPen(QColor("#3c555b"));
    const auto left=lane_.visible ? QString("固定工位  /  货架与机械臂") : QString("关联工位  /  目录");
    const QRectF heading(8,2,width()-16,23);
    painter.drawText(heading,Qt::AlignVCenter|Qt::AlignLeft,left);
    caption.setBold(false);
    painter.setFont(caption);
    const auto right=plc_name_.isEmpty() ? QString("未选择 PLC") : "PLC · "+plc_name_;
    const auto remaining=heading.adjusted(painter.fontMetrics().horizontalAdvance(left)+14,0,0,0);
    painter.drawText(remaining,Qt::AlignVCenter|Qt::AlignRight,
                     painter.fontMetrics().elidedText(right,Qt::ElideRight,remaining.width()));
    if (!scene_set_) return;
    if (stations_.empty()) {
        painter.drawText(rect().adjusted(8,26,-8,-6),Qt::AlignCenter,"当前目录未配置工位");
        return;
    }
    const auto layout=bays();
    for (std::size_t index=0;index<layout.size();++index) drawBay(painter,layout[index],stations_[index]);
    if (lane_.visible) drawLane(painter);
}
void SchematicView::mousePressEvent(QMouseEvent* event) {
    if (event->button()!=Qt::LeftButton) { QWidget::mousePressEvent(event); return; }
    const auto id=stationAt(event->position());
    if (id.isEmpty()) return;
    emit stationActivated(id);
}
