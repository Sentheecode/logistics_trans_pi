#pragma once
#include <QColor>
#include <QPointF>
#include <QStringList>
#include <QSvgRenderer>
#include <QWidget>
#include <memory>
#include <string>
#include <vector>
class QMouseEvent;
class QPaintEvent;

struct SvgRect { std::string id; double x=0, y=0, w=0, h=0; };

// Native Qt view of the synthetic SVG. Hit targets are the file's own rects.
class SchematicView final : public QWidget {
    Q_OBJECT
public:
    explicit SchematicView(QWidget* parent=nullptr);
    explicit SchematicView(const QString& resource, QWidget* parent=nullptr);
    bool loaded() const;
    QString loadError() const { return error_; }
    void setHighlight(const std::string& selected, const std::vector<std::string>& peers, const std::string& zone);
    QString selectedId() const { return QString::fromStdString(selected_); }
    QStringList peerIds() const;
    QColor markerColor(const QString& id) const;
    QString stationAt(const QPointF& widgetPoint) const;
    QRectF stationRect(const QString& id) const;
    QStringList shapeIds() const;
    QSize sizeHint() const override { return {640, 420}; }
signals:
    void stationActivated(const QString& stationId);
protected:
    void paintEvent(QPaintEvent* event) override;
    void mousePressEvent(QMouseEvent* event) override;
private:
    void load();
    void parse(const std::string& xml);
    QRectF content() const;
    QRectF mapSvg(const SvgRect& shape) const;
    QString resource_;
    std::unique_ptr<QSvgRenderer> renderer_;
    std::vector<SvgRect> rects_;
    QString error_;
    std::string selected_, zone_;
    std::vector<std::string> peers_;
    double vbX_=0, vbY_=0, vbW_=1000, vbH_=640;
};
