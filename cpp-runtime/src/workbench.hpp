#pragma once
#include "core.hpp"
#include <QWidget>
#include <future>
class QLabel;
class QPushButton;
class QProgressBar;
class QTableWidget;
class QFrame;
class Workbench final : public QWidget {
public:
    explicit Workbench(std::filesystem::path directory);
    bool captureReady(const QString& kind) const;
    int observations() const {return observations_;}
    QString confirmedMode() const;
private:
    void refresh();
    void display(const sim::View& view);
    void unavailable(const QString& reason);
    void command(const std::string& name);
    void buttons();
    std::filesystem::path directory_;
    sim::View view_;
    std::future<sim::View> read_;
    std::future<std::string> command_;
    bool waiting_confirmation_=false,available_=false;
    std::int64_t command_revision_=0,last_seq_=0;
    std::string last_mode_;
    int observations_=0;
    QLabel *mode_,*connection_,*updated_,*summary_,*summaryDetail_,*feedback_,*controlReason_,*eventNote_;
    QPushButton *modeButton_,*step_,*disconnect_;
    QLabel *cartState_[3],*cartDetail_[3],*cartBadge_[3];
    QProgressBar* progress_[3];
    QFrame* cards_[3];
    QTableWidget* table_;
};
