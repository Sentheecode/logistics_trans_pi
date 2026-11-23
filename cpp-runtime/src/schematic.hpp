#pragma once
#include <QColor>
#include <QPointF>
#include <QStringList>
#include <QWidget>
#include <string>
#include <vector>
class QMouseEvent;
class QPaintEvent;

enum class CartMark { none, wait, run, done, alert };

struct StationGlyph {
    std::string id;
    QString number;
    bool live=false;
    bool carts_known=false;
    QString cart[3];
    CartMark cart_mark[3]{CartMark::none, CartMark::none, CartMark::none};
};

// Figurative layout of one PLC. Not a measured site plan and not a numbered box row.
class SchematicView final : public QWidget {
    Q_OBJECT
public:
    explicit SchematicView(QWidget* parent=nullptr);
    bool loaded() const { return scene_set_; }
    QString loadError() const { return error_; }
    void setScene(const QString& plcName, const QString& areaCode, const std::vector<StationGlyph>& stations, const std::string& focusId);
    QString selectedId() const { return QString::fromStdString(focus_); }
    QStringList peerIds() const;
    QColor markerColor(const QString& id) const;
    QString stationAt(const QPointF& widgetPoint) const;
    QRectF stationRect(const QString& id) const;
    QStringList shapeIds() const;
    QSize sizeHint() const override { return {860, 460}; }
signals:
    void stationActivated(const QString& stationId);
protected:
    void paintEvent(QPaintEvent* event) override;
    void mousePressEvent(QMouseEvent* event) override;
private:
    struct Bay { std::string id; QRectF rect; };
    std::vector<Bay> bays() const;
    void drawBay(QPainter& painter, const Bay& bay, const StationGlyph& glyph) const;
    bool scene_set_=false;
    QString error_;
    QString plc_name_;
    QString area_code_;
    std::vector<StationGlyph> stations_;
    std::string focus_;
};
