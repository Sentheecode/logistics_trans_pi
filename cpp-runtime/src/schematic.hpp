#pragma once
#include <QColor>
#include <QPointF>
#include <QStringList>
#include <QWidget>
#include <string>
#include <vector>
class QMouseEvent;
class QPaintEvent;

struct StationGlyph {
    std::string id;
    QString number;
    QStringList labels;
    QString status;
};

class SchematicView final : public QWidget {
    Q_OBJECT
public:
    explicit SchematicView(QWidget* parent=nullptr);
    bool loaded() const { return scene_set_; }
    QString loadError() const { return error_; }
    void setScene(const std::vector<StationGlyph>& stations, const std::string& focusId, bool exterior, const QString& placementNote);
    QString selectedId() const { return QString::fromStdString(focus_); }
    QStringList peerIds() const;
    QColor markerColor(const QString& id) const;
    QString stationAt(const QPointF& widgetPoint) const;
    QRectF stationRect(const QString& id) const;
    QStringList shapeIds() const;
    QSize sizeHint() const override { return {480, 175}; }
signals:
    void stationActivated(const QString& stationId);
protected:
    void paintEvent(QPaintEvent* event) override;
    void mousePressEvent(QMouseEvent* event) override;
private:
    struct Bay { std::string id; QRectF rect; };
    std::vector<Bay> bays() const;
    void drawBay(QPainter& painter, const Bay& bay, const StationGlyph& glyph) const;
    bool exterior_=false;
    bool scene_set_=false;
    QString error_;
    QString placementNote_;
    std::vector<StationGlyph> stations_;
    std::string focus_;
};
