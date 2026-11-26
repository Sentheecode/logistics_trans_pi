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
void SchematicView::setScene(const std::vector<StationGlyph>& stations, const std::string& focusId, bool exterior, const QString& placementNote) {
    stations_=stations;
    focus_=focusId;
    exterior_=exterior;
    placementNote_=placementNote;
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
    if (!id.isEmpty() && id.toStdString()==focus_) return QColor("#3B5BDB");
    for (auto& station : stations_) if (QString::fromStdString(station.id)==id) return QColor("#58637A");
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
    const QRectF field=QRectF(rect()).adjusted(10,31,-10,-8);
    const int count=static_cast<int>(stations_.size());
    const bool joinedExterior=exterior_ && count==3;
    const double reserved=joinedExterior ? 36.0 : (count-1)*8.0;
    const double span=(field.width()-reserved)/count;
    double x=field.left();
    for (int n=0;n<count;++n) {
        const double gap=joinedExterior ? (n==0 ? reserved : 0.0) : 8.0;
        out.push_back({stations_[n].id,QRectF(x,field.top(),span,field.height())});
        x+=span+gap;
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
    painter.setBrush(QColor("#E3E8F1"));
    painter.drawRect(QRectF(-44,17,87,2));

    painter.setPen(QPen(ink,1.4,Qt::SolidLine,Qt::SquareCap));
    painter.setBrush(QColor("#EEF1F6"));
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
    painter.setBrush(QColor("#FFFFFF"));
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
    painter.setPen(QPen(selected ? ink : QColor("#DDE2ED"),1));
    painter.setBrush(selected ? QColor("#F1F4FF") : QColor("#FFFFFF"));
    painter.drawRoundedRect(box,7,7);
    painter.setClipRect(box);
    auto type=font();
    type.setPixelSize(14);
    type.setBold(true);
    painter.setFont(type);
    painter.setPen(QColor("#202A41"));
    painter.drawText(box.adjusted(12,10,-12,0),Qt::AlignTop|Qt::AlignLeft,glyph.number);
    type.setBold(false);
    painter.setFont(type);
    painter.setPen(ink);
    painter.drawText(box.adjusted(12,10,-12,0),Qt::AlignTop|Qt::AlignRight,glyph.status);
    painter.setPen(QColor("#344058"));
    painter.drawText(box.adjusted(12,34,-12,0),Qt::AlignTop|Qt::AlignLeft|Qt::TextWordWrap,glyph.labels.join("\n"));
    if (exterior_) {
        const double iconWidth=std::min(82.0,box.width()*0.25);
        drawEquipment(painter,QRectF(box.right()-iconWidth-8,box.bottom()-29,iconWidth,25),ink);
    }
    painter.restore();
}

void SchematicView::paintEvent(QPaintEvent*) {
    QPainter painter(this);
    painter.setRenderHint(QPainter::Antialiasing,true);
    painter.fillRect(rect(),QColor("#F5F6F8"));
    painter.setPen(QColor("#58637A"));
    if (stations_.empty()) {
        painter.drawText(rect(),Qt::AlignCenter,"当前目录未配置工位");
        return;
    }
    const auto layout=bays();
    for (std::size_t index=0;index<layout.size();++index) drawBay(painter,layout[index],stations_[index]);
    if (exterior_ && layout.size()==3) {
        const auto left=layout[0].rect;
        const auto right=layout[1].rect.united(layout[2].rect);
        painter.setPen(QColor("#58637A"));
        painter.drawText(QRectF(left.left(),3,left.width(),23),Qt::AlignCenter,"左侧工位");
        painter.drawText(QRectF(right.left(),3,right.width(),23),Qt::AlignCenter,"右侧 · 150 与 160 端部相接");
        const double seam=layout[1].rect.right();
        painter.setPen(QPen(QColor("#8190AC"),1,Qt::DashLine));
        painter.drawLine(QPointF(seam,right.top()+8),QPointF(seam,right.bottom()-8));
    } else {
        painter.setPen(QColor("#58637A"));
        painter.drawText(QRectF(10,3,width()-20,23),Qt::AlignVCenter|Qt::AlignLeft,placementNote_);
    }
}

void SchematicView::mousePressEvent(QMouseEvent* event) {
    if (event->button()!=Qt::LeftButton) { QWidget::mousePressEvent(event); return; }
    const auto id=stationAt(event->position());
    if (id.isEmpty()) return;
    emit stationActivated(id);
}
