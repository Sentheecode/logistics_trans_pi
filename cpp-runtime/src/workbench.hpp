#pragma once
#include "catalog.hpp"
#include "core.hpp"
#include <QMainWindow>
#include <QStringList>
#include <future>
#include <utility>
#include <vector>
class QComboBox;
class QLabel;
class QPushButton;
class QTableWidget;
class SchematicView;

class Workbench final : public QMainWindow {
public:
    explicit Workbench(std::filesystem::path directory);
    bool captureReady(const QString& kind) const;
    int observations() const { return observations_; }
    QString confirmedMode() const;
    void prepareCapture(const QString& kind);
    QString selectedStationId() const;
    QStringList controllerPeerIds() const;
    bool liveScope() const;
    void activateSchematicStation(const QString& id);
private:
    void refresh();
    void display(const sim::View& view);
    void render();
    void unavailable(const QString& reason);
    void command(const std::string& name);
    void buttons();
    void setSelection(catalog::Selection selection);
    void refill();
    void fillCombo(QComboBox* box, const std::vector<std::pair<QString, QString>>& items, const QString& selected);
    static QString comboId(const QComboBox* box);
    std::filesystem::path directory_;
    catalog::Directory catalog_;
    catalog::Selection selection_;
    sim::View view_;
    std::future<sim::View> read_;
    std::future<std::string> command_;
    bool waiting_confirmation_=false, available_=false, updating_=false;
    std::int64_t command_revision_=0, last_seq_=0;
    std::string last_mode_;
    int observations_=0;
    QComboBox *factoryCombo_=nullptr, *businessCombo_=nullptr, *lineCombo_=nullptr, *stationCombo_=nullptr;
    QLabel *processLabel_=nullptr, *runtimeLabel_=nullptr, *heartbeatLabel_=nullptr;
    QLabel *scopeBanner_=nullptr, *feedback_=nullptr, *controlReason_=nullptr, *eventNote_=nullptr;
    QPushButton *modeButton_=nullptr, *step_=nullptr, *disconnect_=nullptr;
    QTableWidget *detailTable_=nullptr, *peerTable_=nullptr, *stageTable_=nullptr, *historyTable_=nullptr;
    SchematicView* schematic_=nullptr;
};
