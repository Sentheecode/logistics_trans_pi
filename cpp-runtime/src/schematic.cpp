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
void SchematicView::setScene(const QString& plcName, const QString& areaCode, const std::vector<StationGlyph>& stations, const std::string& focusId, const CartLane& lane) {
    plc_name_=plcName;
    area_code_=areaCode;
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
    if (!id.isEmpty() && id.toStdString()==focus_) return QColor(29, 78, 137);
    for (auto& station : stations_) if (QString::fromStdString(station.id)==id) return QColor(138, 90, 18);
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
    QRectF area=QRectF(rect()).adjusted(10, 28, -10, lane_.visible ? -60 : -8);
    const int count=static_cast<int>(stations_.size());
    const int columns=count>3 ? 2 : count;
    const int rows=(count+columns-1)/columns;
    const double gap=14;
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
void SchematicView::drawBay(QPainter& painter, const Bay& bay, const StationGlyph& glyph) const {
    painter.save();
    const QRectF box=bay.rect.adjusted(1,1,-1,-1);
    const bool selected=glyph.id==focus_;
    painter.setClipRect(box);
    painter.setPen(QPen(selected ? QColor("#176363") : QColor("#bdcbd0"), selected ? 2 : 1));
    painter.setBrush(selected ? QColor("#e2f0ef") : QColor("#f9fbfc"));
    painter.drawRect(box);
    painter.setPen(QColor("#183439"));
    painter.drawText(box.adjusted(4,1,-4,0), Qt::AlignTop|Qt::AlignHCenter, glyph.number);

    const double y=box.bottom()-15;
    const double x=box.center().x();
    painter.setPen(QPen(QColor("#48666c"),2));
    painter.setBrush(QColor("#f5faf9"));
    painter.drawRect(QRectF(x-39,y-13,26,24));
    painter.drawLine(QPointF(x-39,y-5),QPointF(x-13,y-5));
    painter.drawLine(QPointF(x-39,y+3),QPointF(x-13,y+3));

    painter.setPen(QPen(QColor("#176363"),3,Qt::SolidLine,Qt::RoundCap,Qt::RoundJoin));
    painter.drawLine(QPointF(x+12,y+10),QPointF(x+12,y+2));
    painter.drawLine(QPointF(x+12,y+2),QPointF(x+22,y-11));
    painter.drawLine(QPointF(x+22,y-11),QPointF(x+35,y-2));
    painter.drawLine(QPointF(x+5,y+11),QPointF(x+21,y+11));
    painter.setBrush(QColor("#176363"));
    painter.drawEllipse(QPointF(x+22,y-11),2.5,2.5);
    painter.restore();
}
void SchematicView::drawLane(QPainter& painter) const {
    const QRectF lane(10,height()-55,width()-20,50);
    painter.setPen(QColor("#9fb4bb"));
    painter.setBrush(QColor("#dde9ec"));
    painter.drawRect(lane);
    painter.setPen(QColor("#28474b"));
    painter.drawText(lane.adjusted(6,3,-6,0), Qt::AlignTop|Qt::AlignLeft,
                     "共用通道 · 模拟料车足迹（位置仅示意）");
    const double widthPerCart=(lane.width()-12)/3;
    for (int i=0;i<3;++i) {
        const QRectF cart(lane.left()+6+i*widthPerCart+3,lane.top()+20,widthPerCart-6,23);
        const bool target=lane_.target==i;
        const bool alert=lane_.marks[i]==CartMark::alert;
        painter.setPen(QPen(alert ? QColor("#9c671c") : target ? QColor("#176363") : QColor("#789097"), target ? 2 : 1));
        painter.setBrush(alert ? QColor("#fff1d5") : target ? QColor("#cde9e5") : QColor("#f6fafb"));
        painter.drawRoundedRect(cart,3,3);
        const QColor wheel("#51666a");
        painter.setBrush(wheel);
        painter.setPen(Qt::NoPen);
        painter.drawEllipse(QPointF(cart.left()+10,cart.bottom()+2),3,3);
        painter.drawEllipse(QPointF(cart.right()-10,cart.bottom()+2),3,3);
        painter.setPen(QColor("#183439"));
        painter.drawText(cart,Qt::AlignCenter,
            QString("料车 %1 · %2").arg(i+1).arg(lane_.states[i]));
    }
}
void SchematicView::paintEvent(QPaintEvent*) {
    QPainter painter(this);
    painter.setRenderHint(QPainter::Antialiasing, true);
    painter.fillRect(rect(), QColor("#eef1f4"));
    painter.setPen(QColor("#8a5a12"));
    QFont caption=font();
    caption.setPixelSize(12);
    caption.setBold(true);
    painter.setFont(caption);
    const QString title=plc_name_.isEmpty() ? "未选择 PLC" : plc_name_+" · 固定工位（货架 / 机械臂） · 通用示意";
    painter.drawText(QRectF(12, 6, width()-24, 22), Qt::AlignLeft|Qt::AlignVCenter, title);
    if (!scene_set_) return;
    if (stations_.empty()) {
        painter.setPen(QColor("#1c2430"));
        QFont empty=font();
        empty.setPixelSize(18);
        painter.setFont(empty);
        painter.drawText(rect().adjusted(24, 36, -24, -24), Qt::AlignCenter, plc_name_+"\n尚未配置工位\n不使用其他 PLC 的布置");
        return;
    }
    const auto layout=bays();
    for (std::size_t index=0; index<layout.size() && index<stations_.size(); ++index) drawBay(painter, layout[index], stations_[index]);
    if (lane_.visible) drawLane(painter);
}
void SchematicView::mousePressEvent(QMouseEvent* event) {
    if (event->button()!=Qt::LeftButton) { QWidget::mousePressEvent(event); return; }
    const auto id=stationAt(event->position());
    if (id.isEmpty()) return;
    emit stationActivated(id);
}
