#pragma once
#include "catalog.hpp"
#include "core.hpp"
#include <QMainWindow>
#include <QStringList>
#include <future>
#include <utility>
#include <vector>
class QCheckBox;
class QComboBox;
class QFrame;
class QLabel;
class QLineEdit;
class QPushButton;
class QStackedWidget;
class QTableWidget;
class QTabBar;
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
    void openDiagnostic(const QString& title);
    void refresh();
    void display(const sim::View& view);
    void render();
    void unavailable(const QString& reason);
    void command(const std::string& name);
    void buttons();
    void setSelection(catalog::Selection selection);
    void refill();
    void fillCombo(QComboBox* box, const std::vector<std::pair<QString, QString>>& items, const QString& selected);
    void rebuildCards();
    void syncModuleChrome();
    void applyHistoryFilter();
    void applyPointFilter();
    void showPointDetail();
    void applyLogFilter();
    void showLogDetail();
    void refillLogs(const std::vector<sim::LogRecord>& logs);
    static QString comboId(const QComboBox* box);
    std::filesystem::path directory_;
    catalog::Directory catalog_;
    catalog::Selection selection_;
    sim::View view_;
    std::future<sim::View> read_;
    std::future<std::string> command_;
    bool waiting_confirmation_=false, available_=false, updating_=false, logFollow_=true, applyingLogSelection_=false;
    std::int64_t command_revision_=0, last_seq_=0;
    std::string last_mode_, cards_plc_;
    int observations_=0;
    QComboBox *factoryCombo_=nullptr, *areaCombo_=nullptr, *plcCombo_=nullptr;
    QLabel *processLabel_=nullptr, *runtimeLabel_=nullptr, *heartbeatLabel_=nullptr;
    QLabel *acquisitionOwner_=nullptr, *previewHint_=nullptr, *readinessNote_=nullptr;
    QLabel *stationMeta_=nullptr, *interfaceLine_=nullptr;
    QLabel *scopeBanner_=nullptr, *feedback_=nullptr, *controlReason_=nullptr, *eventNote_=nullptr, *pointScope_=nullptr;
    QPushButton *modeButton_=nullptr, *step_=nullptr, *disconnect_=nullptr, *logFollowButton_=nullptr;
    QTabBar* moduleTabs_=nullptr;
    QStackedWidget* moduleStack_=nullptr;
    QTableWidget *detailTable_=nullptr, *peerTable_=nullptr, *stageTable_=nullptr, *historyTable_=nullptr, *pointTable_=nullptr;
    QLabel *stationEmpty_=nullptr, *historyEmpty_=nullptr, *eventSource_=nullptr, *pointEmpty_=nullptr, *pointDetail_=nullptr;
    QLabel *logEmpty_=nullptr, *logDetail_=nullptr, *collectOwner_=nullptr, *collectLive_=nullptr;
    QTableWidget *collectChanges_=nullptr, *recentTable_=nullptr;
    QComboBox *historyFilter_=nullptr, *logCategory_=nullptr, *logLevel_=nullptr;
    QLineEdit *pointSearch_=nullptr, *logSearch_=nullptr;
    QCheckBox* pointDoubtsOnly_=nullptr;
    QTableWidget* logTable_=nullptr;
    std::int64_t shown_log_id_=-1;
    int shown_log_count_=-1;
    QTableWidget *stationBoard_=nullptr;
    QLabel *stageGuide_=nullptr, *simOrder_=nullptr, *progressLabel_=nullptr, *pageScope_=nullptr;
    QTableWidget *resultTable_=nullptr;
    QWidget *liveFace_=nullptr;
    QString boardSignature_, expandedId_, expandedPlc_;
    void showStageResult();
    void presentStations(bool live, bool good, const std::vector<const catalog::Station*>& rows);
    SchematicView* schematic_=nullptr;
};
