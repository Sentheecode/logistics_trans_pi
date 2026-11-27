#pragma once
#include "catalog.hpp"
#include "core.hpp"
#include <QMainWindow>
#include <QStringList>
#include <future>
#include <optional>
#include <utility>
#include <vector>
class QCheckBox;
class QProgressBar;
class QComboBox;
class QFrame;
class QTabBar;
class QLabel;
class QLineEdit;
class QPushButton;
class QTableWidget;
class QTabWidget;
class SchematicView;

class Workbench final : public QMainWindow {
public:
    void openDiagnostic(const QString& title);
    explicit Workbench(std::filesystem::path directory, bool trace=false);
    bool captureReady(const QString& kind) const;
    int observations() const { return observations_; }
    QString confirmedMode() const;
    void prepareCapture(const QString& kind);
    QString selectedStationId() const;
    QStringList controllerPeerIds() const;
    bool liveScope() const;
    void activateSchematicStation(const QString& id);
private:
    bool diagnosticLive() const;
    void refresh();
    void display(const sim::View& view);
    void render();
    void unavailable(const QString& reason);
    void command(const std::string& name);
    void buttons();
    void setSelection(catalog::Selection selection);
    void refill();
    void fillCombo(QComboBox* box, const std::vector<std::pair<QString, QString>>& items, const QString& selected);
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
    bool trace_=false;
    bool waiting_confirmation_=false, available_=false, updating_=false;
    std::int64_t command_revision_=0, last_seq_=0;
    std::string last_mode_;
    int observations_=0;
    QComboBox *areaCombo_=nullptr, *plcCombo_=nullptr;
    QLabel *processLabel_=nullptr, *runtimeLabel_=nullptr, *heartbeatLabel_=nullptr;
    QLabel* pageScope_=nullptr;
    QLabel *stationMeta_=nullptr;
    QLabel *scopeBanner_=nullptr, *feedback_=nullptr, *controlReason_=nullptr, *eventNote_=nullptr, *pointScope_=nullptr;
    QPushButton *modeButton_=nullptr, *step_=nullptr, *disconnect_=nullptr;
    QTabWidget* moduleTabs_=nullptr;
    QTabBar* navigation_=nullptr;
    std::string detailStation_;
    std::optional<std::string> displayedPointStation_;
    QTableWidget *detailTable_=nullptr, *peerTable_=nullptr, *stageTable_=nullptr, *historyTable_=nullptr, *pointTable_=nullptr;
    QLabel *stationEmpty_=nullptr, *historyEmpty_=nullptr, *eventSource_=nullptr, *pointEmpty_=nullptr, *pointDetail_=nullptr;
    QLabel *logEmpty_=nullptr, *logDetail_=nullptr, *collectOwner_=nullptr, *collectLive_=nullptr;
    QTableWidget *collectChanges_=nullptr;
    QComboBox *historyFilter_=nullptr, *logCategory_=nullptr, *logLevel_=nullptr;
    QLineEdit *pointSearch_=nullptr, *logSearch_=nullptr;
    QProgressBar* progress_=nullptr;
    QCheckBox* pointReserved_=nullptr;
    QCheckBox* pointDoubtsOnly_=nullptr;
    QTableWidget* logTable_=nullptr;
    std::int64_t shown_log_id_=-1;
    int shown_log_count_=-1;
    bool stagePinned_=false;
    QTableWidget* sourceTable_=nullptr;
    QLabel *mapHeading_=nullptr, *stageHeading_=nullptr;
    QLabel *simOrder_=nullptr;
    QTableWidget *resultTable_=nullptr;
    void showStageResult();
    const sim::Row* stationFrame() const;
    SchematicView* schematic_=nullptr;
};
