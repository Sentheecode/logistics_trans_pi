#include "schematic.hpp"
#include <QMouseEvent>
#include <QPainter>
#include <QPaintEvent>
#include <algorithm>

SchematicView::SchematicView(QWidget* parent): QWidget(parent) {
    setObjectName("schematicView");
    setMinimumHeight(220);
    setMouseTracking(false);
    error_="未选择 PLC";
}
void SchematicView::setScene(const QString& plcName, const QString& areaCode, const std::vector<StationGlyph>& stations, const std::string& focusId) {
    plc_name_=plcName;
    area_code_=areaCode;
    stations_=stations;
    focus_=focusId;
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
    QRectF area=QRectF(rect()).adjusted(12, 34, -12, -8);
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
    const QRectF rect=bay.rect.adjusted(6, 6, -6, -6);
    const bool focus=glyph.id==focus_;
    const bool sequence=area_code_=="seq";
    painter.setPen(QPen(focus ? QColor("#1d4e89") : QColor("#8a5a12"), focus ? 2.2 : 1.2));
    painter.setBrush(Qt::NoBrush);
    painter.drawRoundedRect(rect, 2, 2);
    painter.setPen(QColor("#1c2430"));
    QFont number=font();
    number.setPixelSize(16);
    number.setBold(true);
    painter.setFont(number);
    painter.drawText(QRectF(rect.left()+8, rect.top()+4, rect.width()-16, 22), Qt::AlignLeft|Qt::AlignVCenter, glyph.number);
    QFont small=font();
    small.setPixelSize(11);
    painter.setFont(small);
    painter.setPen(QColor("#5c6b7c"));
    painter.drawText(QRectF(rect.right()-72, rect.top()+4, 64, 22), Qt::AlignRight|Qt::AlignVCenter, focus ? "当前" : "同PLC");

    const QRectF floor(rect.left()+16, rect.bottom()-36, rect.width()-32, 10);
    painter.fillRect(floor, QColor("#d5dbe3"));
    const QRectF inbound(rect.left()+14, rect.center().y()-34, 42, 22);
    painter.setPen(QPen(QColor("#1c2430"), 1.4));
    painter.setBrush(QColor("#f7f8fa"));
    painter.drawRoundedRect(inbound, 3, 3);
    painter.drawLine(QPointF(inbound.center().x(), inbound.top()-12), QPointF(inbound.center().x(), inbound.top()));
    painter.drawText(inbound, Qt::AlignCenter, sequence ? "来料" : "进");
    QPen path(QColor("#5c6b7c"), 1.2, Qt::DashLine);
    painter.setPen(path);
    painter.drawLine(QPointF(inbound.right()+2, inbound.center().y()), QPointF(rect.center().x()-18, floor.top()-18));

    const QPointF shoulder(rect.center().x()-6, rect.top()+34);
    const QPointF elbow(rect.center().x()+26, rect.top()+62);
    const QPointF wrist(rect.center().x()-4, floor.top()-22);
    painter.setPen(QPen(QColor("#1c2430"), 3.0, Qt::SolidLine, Qt::RoundCap, Qt::RoundJoin));
    painter.drawLine(shoulder, elbow);
    painter.drawLine(elbow, wrist);
    painter.setBrush(QColor("#1c2430"));
    painter.drawEllipse(shoulder, 3.5, 3.5);
    painter.drawEllipse(elbow, 2.8, 2.8);
    painter.setPen(QColor("#5c6b7c"));
    painter.setFont(small);
    painter.drawText(QRectF(shoulder.x()-28, shoulder.y()-16, 56, 14), Qt::AlignCenter, "机械臂");

    if (sequence) {
        const QRectF rack(rect.right()-78, rect.top()+40, 62, rect.height()-92);
        painter.setPen(QPen(QColor("#1c2430"), 1.3));
        painter.setBrush(Qt::NoBrush);
        painter.drawLine(rack.topLeft(), rack.bottomLeft());
        painter.drawLine(rack.bottomLeft(), rack.bottomRight());
        for (int shelf=1; shelf<=3; ++shelf) {
            const double y=rack.top()+shelf*rack.height()/4.0;
            painter.drawLine(QPointF(rack.left(), y), QPointF(rack.right(), y));
        }
        painter.setPen(QColor("#5c6b7c"));
        painter.drawText(QRectF(rack.left()-4, rack.bottom()+2, rack.width()+8, 16), Qt::AlignCenter, "料架未定");
    } else {
        const QRectF bin(rect.right()-84, rect.top()+42, 68, 74);
        painter.setPen(QPen(QColor("#1c2430"), 1.3));
        painter.drawLine(bin.topLeft(), bin.bottomLeft());
        painter.drawLine(QPointF(bin.left(), bin.top()+24), QPointF(bin.right(), bin.top()+24));
        painter.drawLine(QPointF(bin.left(), bin.top()+48), QPointF(bin.right(), bin.top()+48));
        painter.setPen(QColor("#5c6b7c"));
        painter.drawText(QRectF(bin.left()-6, bin.bottom()+2, bin.width()+12, 16), Qt::AlignCenter, glyph.live ? "料仓见点表" : "层位未提供");
    }

    const QRectF outbound(rect.right()-54, rect.top()+28, 40, 20);
    painter.setPen(QPen(QColor("#1c2430"), 1.4));
    painter.setBrush(QColor("#f7f8fa"));
    painter.drawRoundedRect(outbound, 3, 3);
    painter.drawText(outbound, Qt::AlignCenter, sequence ? "回传" : "出");

    const double track=std::min(floor.width()*0.62, 210.0);
    if (glyph.live && glyph.carts_known) {
        for (int index=0; index<3; ++index) {
            const QRectF cart(floor.left()+6+index*(track/3.0), floor.top()-28, track/3.0-6, 22);
            const bool alert=glyph.cart[index].contains("阻塞") || glyph.cart[index].contains("未知");
            painter.setPen(QPen(alert ? QColor("#8a5a12") : QColor("#1c2430"), 1));
            painter.setBrush(alert ? QColor("#fbf6ee") : QColor("#ffffff"));
            painter.drawRoundedRect(cart, 2, 2);
            painter.setFont(small);
            painter.drawText(cart, Qt::AlignCenter, cart.width()>=52 ? glyph.cart[index] : QString::number(index+1));
        }
    } else {
        const QRectF cart(floor.left()+10, floor.top()-28, std::min(120.0, floor.width()-20), 22);
        painter.setPen(QPen(QColor("#8b96a6"), 1, Qt::DashLine));
        painter.setBrush(Qt::NoBrush);
        painter.drawRoundedRect(cart, 2, 2);
        painter.setFont(small);
        painter.drawText(cart, Qt::AlignCenter, "料车未挂接");
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
    const QString title=plc_name_.isEmpty() ? "未选择 PLC" : plc_name_+" · 示意，不是现场实测图 · 无平面坐标";
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
}
void SchematicView::mousePressEvent(QMouseEvent* event) {
    if (event->button()!=Qt::LeftButton) { QWidget::mousePressEvent(event); return; }
    const auto id=stationAt(event->position());
    if (id.isEmpty()) return;
    emit stationActivated(id);
}
