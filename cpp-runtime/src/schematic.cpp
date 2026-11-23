#include "schematic.hpp"
#include <QFile>
#include <QMouseEvent>
#include <QPainter>
#include <QPaintEvent>
#include <QSvgRenderer>
#include <algorithm>
#include <cctype>
#include <cmath>
#include <optional>
#include <sstream>
#include <stdexcept>

static void init_resources() {
    Q_INIT_RESOURCE(schematics);
}
namespace {
std::optional<std::string> attr(const std::string& tag, const std::string& key) {
    const auto token=key+"=";
    std::size_t pos=0;
    while ((pos=tag.find(token, pos))!=std::string::npos) {
        bool boundary=pos==0 || std::isspace(static_cast<unsigned char>(tag[pos-1]));
        if (!boundary) { pos+=token.size(); continue; }
        pos+=token.size();
        if (pos>=tag.size() || tag[pos]!='"') return std::nullopt;
        auto end=tag.find('"', pos+1);
        if (end==std::string::npos) return std::nullopt;
        return tag.substr(pos+1, end-pos-1);
    }
    return std::nullopt;
}
double number(const std::string& text) {
    std::size_t used=0;
    double value=std::stod(text, &used);
    if (used!=text.size()) throw std::runtime_error("bad number");
    return value;
}
}

SchematicView::SchematicView(QWidget* parent): SchematicView(QStringLiteral(":/catalog/schematic.svg"), parent) {}
SchematicView::SchematicView(const QString& resource, QWidget* parent): QWidget(parent), resource_(resource) {
    setObjectName("schematicView");
    setMinimumSize(400, 240);
    setMouseTracking(false);
    load();
}
void SchematicView::load() {
    init_resources();
    error_.clear();
    rects_.clear();
    renderer_=std::make_unique<QSvgRenderer>();
    QFile file(resource_);
    if (!file.open(QIODevice::ReadOnly)) { error_="示意图资源缺失"; renderer_.reset(); return; }
    const auto bytes=file.readAll();
    if (!renderer_->load(bytes) || !renderer_->isValid()) { error_="示意图无法解析"; renderer_.reset(); return; }
    renderer_->setAspectRatioMode(Qt::IgnoreAspectRatio);
    try { parse(bytes.toStdString()); }
    catch (const std::exception&) { rects_.clear(); error_="示意图无法解析"; return; }
    bool station=false;
    for (auto& shape : rects_) if (shape.id.rfind("stn.", 0)==0 && shape.w>0 && shape.h>0) station=true;
    if (!station) { rects_.clear(); error_="示意图没有工位"; }
}
void SchematicView::parse(const std::string& xml) {
    auto box_at=xml.find("viewBox=\"");
    if (box_at==std::string::npos) throw std::runtime_error("viewBox");
    box_at+=9;
    auto box_end=xml.find('"', box_at);
    if (box_end==std::string::npos) throw std::runtime_error("viewBox");
    std::istringstream box(xml.substr(box_at, box_end-box_at));
    if (!(box>>vbX_>>vbY_>>vbW_>>vbH_) || !(vbW_>0) || !(vbH_>0)) throw std::runtime_error("viewBox");
    std::size_t cursor=0;
    while ((cursor=xml.find("<rect", cursor))!=std::string::npos) {
        auto end=xml.find('>', cursor);
        if (end==std::string::npos) throw std::runtime_error("rect");
        auto tag=xml.substr(cursor, end-cursor);
        cursor=end+1;
        auto id=attr(tag, "id");
        if (!id) continue;
        auto x=attr(tag, "x"), y=attr(tag, "y"), w=attr(tag, "width"), h=attr(tag, "height");
        if (!x || !y || !w || !h) throw std::runtime_error("rect");
        SvgRect shape{*id, number(*x), number(*y), number(*w), number(*h)};
        if (!(shape.w>0) || !(shape.h>0)) throw std::runtime_error("rect");
        rects_.push_back(shape);
    }
}
bool SchematicView::loaded() const { return error_.isEmpty() && renderer_ && renderer_->isValid() && !rects_.empty(); }
void SchematicView::setHighlight(const std::string& selected, const std::vector<std::string>& peers, const std::string& zone) {
    selected_=selected; peers_=peers; zone_=zone; update();
}
QStringList SchematicView::peerIds() const {
    QStringList ids;
    for (auto& id : peers_) ids.push_back(QString::fromStdString(id));
    return ids;
}
QColor SchematicView::markerColor(const QString& id) const {
    if (!id.isEmpty() && id.toStdString()==selected_) return QColor(29, 78, 216);
    if (std::find(peers_.begin(), peers_.end(), id.toStdString())!=peers_.end()) return QColor(180, 83, 9);
    return {};
}
QRectF SchematicView::content() const {
    return QRectF(rect()).adjusted(2, 2, -2, -2);
}
QRectF SchematicView::mapSvg(const SvgRect& shape) const {
    auto bounds=content();
    double sx=bounds.width()/vbW_, sy=bounds.height()/vbH_;
    return {bounds.x()+(shape.x-vbX_)*sx, bounds.y()+(shape.y-vbY_)*sy, shape.w*sx, shape.h*sy};
}
QString SchematicView::stationAt(const QPointF& widgetPoint) const {
    QString station;
    QString group;
    for (auto& shape : rects_) {
        if (!mapSvg(shape).contains(widgetPoint)) continue;
        const auto id=QString::fromStdString(shape.id);
        if (id.startsWith("stn.")) station=id;
        else if (id.startsWith("plc.cfg.")) group=id;
    }
    return station.isEmpty() ? group : station;
}
QRectF SchematicView::stationRect(const QString& id) const {
    for (auto& shape : rects_) if (QString::fromStdString(shape.id)==id) return mapSvg(shape);
    return {};
}
QStringList SchematicView::shapeIds() const {
    QStringList ids;
    for (auto& shape : rects_) ids.push_back(QString::fromStdString(shape.id));
    return ids;
}
void SchematicView::paintEvent(QPaintEvent*) {
    QPainter painter(this);
    painter.setRenderHint(QPainter::Antialiasing, true);
    painter.fillRect(rect(), QColor("#e8eaed"));
    if (!loaded()) {
        painter.setPen(QColor("#8a5a00"));
        painter.drawText(rect().adjusted(12, 12, -12, -12), Qt::AlignCenter|Qt::TextWordWrap, error_.isEmpty()?QString("示意图资源缺失"):error_);
        return;
    }
    auto bounds=content();
    renderer_->render(&painter, bounds);
    for (auto& shape : rects_) if (!zone_.empty() && shape.id==zone_) {
        painter.setPen(QPen(QColor("#526070"), 1.5, Qt::DashLine));
        painter.setBrush(Qt::NoBrush);
        painter.drawRect(mapSvg(shape));
    }
    for (auto& shape : rects_) {
        if (shape.id.rfind("stn.", 0)!=0) continue;
        auto color=markerColor(QString::fromStdString(shape.id));
        if (!color.isValid()) continue;
        bool selected=shape.id==selected_;
        painter.setPen(QPen(color, selected?3.0:2.0));
        painter.setBrush(QColor(color.red(), color.green(), color.blue(), selected?72:46));
        painter.drawRect(mapSvg(shape).adjusted(1, 1, -1, -1));
    }
}
void SchematicView::mousePressEvent(QMouseEvent* event) {
    if (event->button()!=Qt::LeftButton) { QWidget::mousePressEvent(event); return; }
    auto id=stationAt(event->position());
    if (id.isEmpty()) return;
    emit stationActivated(id);
}
