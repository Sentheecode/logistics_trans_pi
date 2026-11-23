#include "schematic.hpp"
#include <QMouseEvent>
#include <QPainter>
#include <QPaintEvent>
#include <algorithm>
#include <vector>

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
namespace {
QPointF rawIso(double x, double y, double z) {
    return QPointF((x - y) * 0.86, (x + y) * 0.5 - z);
}
void solid(QPainter& painter, const std::initializer_list<QPointF>& points, const QColor& fill) {
    QPolygonF polygon;
    for (const auto& point : points) polygon << point;
    painter.setPen(QPen(QColor("#243044"), 1.1));
    painter.setBrush(fill);
    painter.drawPolygon(polygon);
}
void wheel(QPainter& painter, QPointF center, double radius) {
    painter.setPen(QPen(QColor("#1c2430"), 1.2));
    painter.setBrush(QColor("#3d4654"));
    painter.drawEllipse(center, radius, radius * 0.62);
    painter.setBrush(QColor("#d5dbe3"));
    painter.drawEllipse(center, radius * 0.34, radius * 0.22);
}
void limb(QPainter& painter, QPointF from, QPointF to, double thickness) {
    const QLineF line(from, to);
    painter.save();
    painter.translate(from);
    painter.rotate(-line.angle());
    painter.setPen(QPen(QColor("#243044"), 1.1));
    painter.setBrush(QColor("#4a596c"));
    painter.drawRoundedRect(QRectF(0, -thickness / 2.0, std::max(1.0, line.length()), thickness), thickness * 0.35, thickness * 0.35);
    painter.restore();
}
struct Block { double x, y, z, w, d, h; QColor top, left, right; };
struct Disc { double x, y, z, r; };
struct Node { double x, y, z; };
}

void SchematicView::drawBay(QPainter& painter, const Bay& bay, const StationGlyph& glyph) const {
    painter.save();
    painter.setClipRect(bay.rect.adjusted(1, 1, -1, -1));
    const QRectF rect=bay.rect.adjusted(2, 2, -2, -2);
    const bool focus=glyph.id==focus_;
    const QRectF plot=rect.adjusted(4, 26, -4, -4);
    std::vector<Block> blocks;
    std::vector<Disc> discs;
    const auto add=[&](double x, double y, double z, double w, double d, double h, const QColor& top, const QColor& left, const QColor& right) {
        blocks.push_back({x, y, z, w, d, h, top, left, right});
    };
    const auto roll=[&](double x, double y, double z, double r) { discs.push_back({x, y, z, r}); };
    add(0, 18, 10, 92, 48, 20, QColor("#f7f9fc"), QColor("#8ea0b5"), QColor("#5d6d82"));
    add(8, 26, 30, 38, 30, 18, QColor("#d7e4f5"), QColor("#62748b"), QColor("#314056"));
    roll(10, 16, 6, 11); roll(74, 16, 6, 11); roll(10, 68, 6, 11); roll(74, 68, 6, 11);
    add(118, 8, 0, 62, 58, 16, QColor("#eceff3"), QColor("#9aa6b5"), QColor("#667588"));
    add(132, 22, 16, 28, 28, 108, QColor("#f7f8fa"), QColor("#b7c0cc"), QColor("#6d7b8d"));
    add(248, 0, 0, 8, 8, 168, QColor("#e7edf4"), QColor("#8b97a8"), QColor("#5c6b7c"));
    add(248, 46, 0, 8, 8, 168, QColor("#e7edf4"), QColor("#8b97a8"), QColor("#5c6b7c"));
    add(292, 0, 0, 8, 8, 168, QColor("#e7edf4"), QColor("#8b97a8"), QColor("#5c6b7c"));
    add(292, 46, 0, 8, 8, 168, QColor("#e7edf4"), QColor("#8b97a8"), QColor("#5c6b7c"));
    add(244, -4, 52, 62, 62, 6, QColor("#f7f1e4"), QColor("#d2c4a8"), QColor("#a89474"));
    add(244, -4, 112, 62, 62, 6, QColor("#f4efe4"), QColor("#cbb892"), QColor("#8f7754"));
    add(258, 10, 118, 28, 30, 26, QColor("#efe6d4"), QColor("#c3b496"), QColor("#8d7b5e"));
    if (glyph.live && glyph.carts_known) {
        for (int index=0; index<3; ++index) {
            const bool alert=glyph.cart[index].contains("阻塞") || glyph.cart[index].contains("未知");
            add(118 + index * 44, 132, 4, 36, 24, 16,
                alert ? QColor("#f6e2c4") : QColor("#f7f8fa"),
                alert ? QColor("#c48a45") : QColor("#8ea0b5"),
                alert ? QColor("#8a5a12") : QColor("#5d6d82"));
            roll(124 + index * 44, 134, 0, 6);
            roll(146 + index * 44, 134, 0, 6);
            roll(124 + index * 44, 154, 0, 6);
            roll(146 + index * 44, 154, 0, 6);
        }
    } else {
        add(128, 132, 4, 46, 28, 16, QColor("#f7f8fa"), QColor("#c5ced8"), QColor("#8b96a6"));
        roll(136, 134, 0, 7); roll(164, 134, 0, 7); roll(136, 158, 0, 7); roll(164, 158, 0, 7);
    }
    const Node shoulderModel{146, 36, 132};
    const Node elbowModel{210, 4, 188};
    const Node wristModel{236, 24, 126};
    double minX=1e9, minY=1e9, maxX=-1e9, maxY=-1e9;
    const auto take=[&](double x, double y, double z, double pad) {
        const QPointF point=rawIso(x, y, z);
        minX=std::min(minX, point.x()-pad); minY=std::min(minY, point.y()-pad);
        maxX=std::max(maxX, point.x()+pad); maxY=std::max(maxY, point.y()+pad);
    };
    for (const auto& block : blocks) for (double sx : {0.0, block.w}) for (double sy : {0.0, block.d}) for (double sz : {0.0, block.h})
        take(block.x+sx, block.y+sy, block.z+sz, 0);
    for (const auto& disc : discs) take(disc.x, disc.y, disc.z, disc.r);
    for (const auto& point : {shoulderModel, elbowModel, wristModel}) take(point.x, point.y, point.z, 16);
    const double spanX=std::max(1.0, maxX-minX), spanY=std::max(1.0, maxY-minY);
    const double scale=std::min(plot.width()/spanX, plot.height()/spanY);
    const auto at=[&](double x, double y, double z) {
        const QPointF point=rawIso(x, y, z);
        return QPointF(plot.left()+(plot.width()-spanX*scale)/2.0+(point.x()-minX)*scale,
                        plot.top()+(plot.height()-spanY*scale)/2.0+(point.y()-minY)*scale);
    };
    const auto cube=[&](const Block& block) {
        const auto corner=[&](double x, double y, double z){ return at(x, y, z); };
        const QPointF a=corner(block.x, block.y, block.z), c=corner(block.x+block.w, block.y+block.d, block.z), e=corner(block.x, block.y+block.d, block.z);
        const QPointF atop=corner(block.x, block.y, block.z+block.h), btop=corner(block.x+block.w, block.y, block.z+block.h);
        const QPointF ctop=corner(block.x+block.w, block.y+block.d, block.z+block.h), etop=corner(block.x, block.y+block.d, block.z+block.h);
        solid(painter, {e, c, ctop, etop}, block.right);
        solid(painter, {a, e, etop, atop}, block.left);
        solid(painter, {atop, btop, ctop, etop}, block.top);
    };
    for (const auto& block : blocks) cube(block);
    for (const auto& disc : discs) wheel(painter, at(disc.x, disc.y, disc.z), std::max(3.0, disc.r*scale));
    const QPointF shoulder=at(shoulderModel.x, shoulderModel.y, shoulderModel.z);
    const QPointF elbow=at(elbowModel.x, elbowModel.y, elbowModel.z);
    const QPointF wrist=at(wristModel.x, wristModel.y, wristModel.z);
    const double thickness=std::max(7.0, 16.0*scale);
    limb(painter, shoulder, elbow, thickness);
    limb(painter, elbow, wrist, thickness*0.82);
    painter.setPen(QPen(QColor("#1c2430"), 1.2));
    painter.setBrush(QColor("#1d4e89"));
    painter.drawEllipse(shoulder, thickness*0.55, thickness*0.55);
    painter.setBrush(QColor("#243044"));
    painter.drawEllipse(elbow, thickness*0.42, thickness*0.42);
    const QPointF finger=wrist+QPointF(0, thickness*0.9);
    painter.setPen(QPen(QColor("#243044"), std::max(2.0, thickness*0.28), Qt::SolidLine, Qt::RoundCap));
    painter.drawLine(wrist, finger);
    painter.drawLine(finger+QPointF(-thickness*0.55, 0), finger+QPointF(-thickness*0.55, thickness*1.15));
    painter.drawLine(finger+QPointF(thickness*0.55, 0), finger+QPointF(thickness*0.55, thickness*1.15));
    painter.drawLine(finger+QPointF(-thickness*0.55, 0), finger+QPointF(thickness*0.55, 0));
    QFont number=font();
    number.setPixelSize(16);
    number.setBold(true);
    painter.setFont(number);
    painter.setPen(focus ? QColor("#1d4e89") : QColor("#1c2430"));
    painter.drawText(QRectF(rect.left(), rect.top(), rect.width(), 22), Qt::AlignHCenter|Qt::AlignTop, glyph.number);
    if (focus) {
        painter.setPen(QPen(QColor("#1d4e89"), 2));
        painter.drawLine(QPointF(rect.center().x()-18, rect.top()+22), QPointF(rect.center().x()+18, rect.top()+22));
    }
    painter.restore();
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
