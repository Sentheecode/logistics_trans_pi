#include "workbench.hpp"
#include "control.hpp"
#include <QDateTime>
#include <QFrame>
#include <QHeaderView>
#include <QLabel>
#include <QMessageBox>
#include <QProgressBar>
#include <QPushButton>
#include <QTableWidget>
#include <QTimer>
#include <QVBoxLayout>
#include <iostream>
namespace {
QLabel* label(const QString& text,const char* name,QWidget* parent=nullptr){auto* l=new QLabel(text,parent);l->setObjectName(name);return l;}
QString state(const std::string& value){
    if(value=="waiting")return "等待扫码";
    if(value=="scanning")return "扫码中";
    if(value=="sorting")return "分拣中";
    if(value=="station_complete")return "工位完成";
    if(value=="blocked")return "模拟阻塞";
    return "状态未知";
}
QString eventText(const std::string& code){
    if(code=="SIM_TASK_BLOCKED")return "模拟异常：小车 02 阻塞";
    if(code=="SIM_BAD_QUALITY")return "源数据质量异常，值不可用";
    if(code=="SIM_LINK_DOWN"||code=="SIM_MANUAL_DISCONNECT")return "模拟链路断开，值不可用";
    if(code=="SIM_LINK_RECOVERED")return "模拟链路恢复";
    if(code=="SIM_ALARM_CLEARED")return "模拟阻塞解除";
    if(code=="SIM_STATION_COMPLETE")return "本轮模拟工位完成";
    return "模拟任务阶段更新";
}
QString feedback(const std::string& result){
    if(result=="ok")return "采集端已执行，正在核对最新状态…";
    if(result=="stale_revision"||result=="session_changed")return "操作未执行：采集端状态已变化，请核对后重新操作。";
    if(result=="manual_required")return "操作未执行：请先切换到手动模式。";
    if(result=="expired")return "操作未执行：请求已过期。";
    return "采集端拒绝操作："+QString::fromStdString(result);
}
}
Workbench::Workbench(std::filesystem::path directory):directory_(std::move(directory)){
    setWindowTitle("物流工作台 · 模拟环境");resize(1280,800);setMinimumSize(1100,650);
    setStyleSheet(R"(
        QWidget {background:#f3f5f8;color:#182337;font-size:13px;}
        QLabel {background:transparent;}
        QLabel#title {font-size:25px;font-weight:700;}
        QLabel#muted,QLabel#updated,QLabel#eventNote {color:#64748b;font-size:12px;}
        QLabel#brand {background:#245eea;color:white;font-size:23px;font-weight:700;border-radius:10px;}
        QLabel#environment {background:#e8efff;color:#2459c7;border-radius:6px;padding:6px 10px;font-weight:600;}
        QLabel#mode {background:#e7ebf1;color:#34445d;border-radius:6px;padding:6px 10px;font-weight:600;}
        QFrame#panel,QFrame#cart {background:white;border:1px solid #dfe5ee;border-radius:10px;}
        QLabel#section {font-size:15px;font-weight:600;}
        QLabel#summary {font-size:23px;font-weight:600;}
        QLabel#cartState {font-size:23px;font-weight:600;}
        QLabel#number {color:#a5b4cb;font-size:18px;font-weight:600;}
        QLabel#badge {color:#365a8c;background:#edf3fc;border-radius:5px;padding:4px 8px;}
        QLabel#feedback {color:#735114;font-size:12px;}
        QPushButton {background:white;border:1px solid #cbd5e1;border-radius:7px;padding:9px 16px;font-weight:600;}
        QPushButton:hover {background:#f1f5ff;border-color:#8faeea;}
        QPushButton#primary {background:#245eea;color:white;border:1px solid #245eea;}
        QPushButton#primary:hover {background:#1c4dca;}
        QPushButton:disabled,QPushButton#primary:disabled {background:#eef1f5;color:#8b96a6;border:1px solid #e1e6ee;}
        QProgressBar {background:#e9eef5;border:none;border-radius:3px;height:6px;max-height:6px;}
        QProgressBar::chunk {background:#4678e9;border-radius:3px;}
        QTableWidget {background:white;border:none;gridline-color:#eef1f5;selection-background-color:#eaf1ff;selection-color:#182337;}
        QHeaderView::section {background:#f7f9fc;color:#64748b;border:none;border-bottom:1px solid #e6ebf2;padding:9px;font-size:12px;}
        QTableWidget::item {padding:7px;border-bottom:1px solid #eef1f5;}
        QScrollBar:vertical {background:#f6f8fb;width:9px;}
        QScrollBar::handle:vertical {background:#c6cfdd;border-radius:4px;min-height:20px;}
        QScrollBar::add-line:vertical,QScrollBar::sub-line:vertical {height:0px;}
    )");
    auto* root=new QVBoxLayout(this);root->setContentsMargins(28,22,28,16);root->setSpacing(16);
    auto* top=new QHBoxLayout;top->setSpacing(14);
    auto* brand=label("L","brand");brand->setFixedSize(44,44);brand->setAlignment(Qt::AlignCenter);top->addWidget(brand);
    auto* titles=new QVBoxLayout;titles->setSpacing(1);titles->addWidget(label("物流工作台","title"));titles->addWidget(label("运行总览  /  本地模拟调试","muted"));top->addLayout(titles);
    top->addSpacing(10);auto* environment=label("模拟环境","environment");environment->setMaximumHeight(32);top->addWidget(environment,0,Qt::AlignVCenter);mode_=label("模式待确认","mode");mode_->setMaximumHeight(32);top->addWidget(mode_,0,Qt::AlignVCenter);top->addStretch();
    auto* health=new QVBoxLayout;health->setSpacing(3);connection_=label("等待采集进程","connection");updated_=label("尚无心跳证据","updated");health->addWidget(connection_,0,Qt::AlignRight);health->addWidget(updated_,0,Qt::AlignRight);top->addLayout(health);root->addLayout(top);
    auto* task=new QFrame;task->setObjectName("panel");auto* taskLayout=new QVBoxLayout(task);taskLayout->setContentsMargins(20,15,20,14);taskLayout->setSpacing(8);
    auto* overview=new QHBoxLayout;auto* texts=new QVBoxLayout;texts->setSpacing(5);
    texts->addWidget(label("当前任务 · 合成三车场景","section"));summary_=label("正在获取采集证据","summary");texts->addWidget(summary_);
    summaryDetail_=label("仅连接本地模拟采集器，不连接现场设备。","muted");texts->addWidget(summaryDetail_);overview->addLayout(texts);overview->addStretch();
    modeButton_=new QPushButton("模式待确认");modeButton_->setObjectName("modeButton");
    step_=new QPushButton("单步推进");step_->setObjectName("primary");step_->setProperty("testRole","step");
    disconnect_=new QPushButton("模拟断链");disconnect_->setObjectName("disconnectButton");
    overview->addWidget(modeButton_);overview->addWidget(step_);overview->addWidget(disconnect_);taskLayout->addLayout(overview);
    auto* explanations=new QHBoxLayout;feedback_=label("","feedback");feedback_->setWordWrap(true);explanations->addWidget(feedback_,1);controlReason_=label("控制端状态待确认","muted");explanations->addWidget(controlReason_,0,Qt::AlignRight);taskLayout->addLayout(explanations);root->addWidget(task);
    auto* cards=new QHBoxLayout;cards->setSpacing(14);
    for(int i=0;i<3;++i){
        cards_[i]=new QFrame;cards_[i]->setObjectName("cart");auto* c=new QVBoxLayout(cards_[i]);c->setContentsMargins(18,14,18,15);c->setSpacing(8);
        auto* header=new QHBoxLayout;header->addWidget(label(QString("小车 %1").arg(i+1,2,10,QChar('0')),"section"));header->addStretch();cartBadge_[i]=label("等待证据","badge");header->addWidget(cartBadge_[i]);c->addLayout(header);
        cartState_[i]=label("状态未知","cartState");c->addWidget(cartState_[i]);
        cartDetail_[i]=label("等待有效观测","muted");c->addWidget(cartDetail_[i]);
        progress_[i]=new QProgressBar;progress_[i]->setRange(0,3);progress_[i]->setValue(0);progress_[i]->setTextVisible(false);c->addWidget(progress_[i]);
        c->addWidget(label("扫码   →   分拣   →   工位完成","muted"));cards->addWidget(cards_[i],1);
    }
    root->addLayout(cards);
    auto* history=new QFrame;history->setObjectName("panel");auto* historyLayout=new QVBoxLayout(history);historyLayout->setContentsMargins(16,12,16,8);historyLayout->setSpacing(8);
    auto* historyHeader=new QHBoxLayout;historyHeader->addWidget(label("最近事件与历史","section"));historyHeader->addStretch();eventNote_=label("最近 40 帧 · 时间为 UTC","eventNote");historyHeader->addWidget(eventNote_);historyLayout->addLayout(historyHeader);
    table_=new QTableWidget(0,5);table_->setObjectName("historyTable");table_->setHorizontalHeaderLabels({"序号","接收时间 / UTC","三车阶段（01 / 02 / 03）","数据质量","事件"});
    table_->setEditTriggers(QAbstractItemView::NoEditTriggers);table_->setSelectionBehavior(QAbstractItemView::SelectRows);table_->setShowGrid(false);table_->verticalHeader()->hide();table_->verticalHeader()->setDefaultSectionSize(33);
    table_->horizontalHeader()->setSectionResizeMode(QHeaderView::Stretch);table_->horizontalHeader()->setSectionResizeMode(0,QHeaderView::Fixed);table_->setColumnWidth(0,65);table_->horizontalHeader()->setSectionResizeMode(1,QHeaderView::Fixed);table_->setColumnWidth(1,135);table_->horizontalHeader()->setSectionResizeMode(3,QHeaderView::Fixed);table_->setColumnWidth(3,95);
    historyLayout->addWidget(table_,1);root->addWidget(history,1);
    auto* footer=new QHBoxLayout;footer->addWidget(label("仅模拟  ·  工位完成不代表整线完成  ·  诊断不推断根因","muted"));footer->addStretch();footer->addWidget(label("关闭界面不停止采集","muted"));root->addLayout(footer);
    connect(step_,&QPushButton::clicked,this,[this]{command("step");});
    connect(disconnect_,&QPushButton::clicked,this,[this]{command("disconnect");});
    connect(modeButton_,&QPushButton::clicked,this,[this]{
        if(view_.runtime.mode=="manual"){
            QMessageBox confirm(QMessageBox::Question,"恢复自动模拟","恢复后，模拟任务将按采集器节奏连续推进。\n请确认当前调试已结束。",QMessageBox::Yes|QMessageBox::No,this);
            confirm.setObjectName("confirmAutomatic");confirm.setDefaultButton(QMessageBox::No);confirm.button(QMessageBox::Yes)->setText("确认恢复自动");confirm.button(QMessageBox::No)->setText("继续手动调试");
            if(confirm.exec()!=QMessageBox::Yes)return;
            command("automatic_confirmed");
        }else command("manual");
    });
    auto* timer=new QTimer(this);connect(timer,&QTimer::timeout,this,[this]{refresh();});timer->start(100);buttons();
}
void Workbench::buttons(){
    bool pending=command_.valid()||waiting_confirmation_;
    bool usable=available_&&view_.runtime.liveness=="recent";
    bool manual=view_.runtime.mode=="manual";
    modeButton_->setEnabled(usable&&!pending);step_->setEnabled(usable&&manual&&!pending);disconnect_->setEnabled(usable&&manual&&!pending);
    modeButton_->setText(!usable?"模式不可用":manual?"恢复自动…":"切换手动");
    modeButton_->setStyleSheet(usable&&!manual&&!pending?"background:#245eea;color:white;border:1px solid #245eea;":"");
    const QString reason=!usable?"采集端不可达或心跳过期，控制已禁用":pending?"正在等待采集端确认，请勿重复操作":manual?"手动待步：只有点击才推进":"自动运行中：单步与断链操作仅限手动模式";
    controlReason_->setText(reason);modeButton_->setToolTip(reason);step_->setToolTip(reason);disconnect_->setToolTip(reason);
}
void Workbench::command(const std::string& name){
    if(command_.valid()||waiting_confirmation_||!available_||view_.runtime.liveness!="recent")return;
    const auto state=view_.runtime;command_revision_=state.revision;
    command_=std::async(std::launch::async,[dir=directory_,state,name]{return sim::send_control(dir,state.boot,state.revision,name);});
    feedback_->setText("请求已发送，模式以采集端确认结果为准…");buttons();
}
void Workbench::refresh(){
    if(command_.valid()&&command_.wait_for(std::chrono::milliseconds(0))==std::future_status::ready){
        try{auto result=command_.get();feedback_->setText(feedback(result));waiting_confirmation_=result=="ok";}
        catch(const std::exception&){feedback_->setText("控制端未确认：可能未执行或结果未知。请刷新状态后核对，不会自动重试。");}
    }
    if(read_.valid()&&read_.wait_for(std::chrono::milliseconds(0))==std::future_status::ready){
        try{display(read_.get());}catch(const std::exception&){unavailable("无法读取本地采集数据，请先独立启动模拟采集器。");}
    }
    if(!read_.valid())read_=std::async(std::launch::async,[dir=directory_]{return sim::read(dir,"recent",40);});
    buttons();
}
void Workbench::display(const sim::View& v){
    view_=v;available_=v.runtime.liveness=="recent";++observations_;
    if(waiting_confirmation_&&(v.runtime.revision>command_revision_||!available_)){waiting_confirmation_=false;feedback_->setText(available_?"操作完成 · 已核对采集端状态":"操作后心跳中断，请核对采集端状态。");}
    mode_->setText(!available_?"模式未确认":v.runtime.mode=="manual"?"手动调试":"自动运行");
    connection_->setText(available_?"●  采集进程在线":"○  采集进程不可达 / 心跳过期");
    updated_->setText("心跳 "+QDateTime::fromMSecsSinceEpoch(v.runtime.heartbeat_ms).toUTC().toString("HH:mm:ss.zzz")+" UTC");
    if(v.rows.empty()){
        summary_->setText(available_?"等待首个模拟任务事件":"等待本地模拟采集器");
        summaryDetail_->setText("尚无任务事件；手动模式请执行单步，心跳与任务事件独立。");return;
    }
    const auto& r=v.rows.front();bool good=available_&&r.quality=="good"&&v.freshness!="clock_uncertain"&&(v.runtime.mode=="manual"||v.freshness=="recent");
    summary_->setText(!available_?"采集端离线，当前状态待确认":v.runtime.mode=="manual"?"手动调试 · 等待下一步":"自动运行 · 模拟任务持续推进");
    const auto age=std::max<std::int64_t>(0,(sim::now_ms()-r.received_ms)/1000);
    summaryDetail_->setText(QString("当前记录 #%1  ·  最近任务事件 %2 秒前  ·  %3").arg(r.seq).arg(age).arg(v.runtime.mode=="manual"?"待步不等于断链":"阶段按合成场景依次推进"));
    if(!good)summaryDetail_->setText(v.freshness=="clock_uncertain"?"事件时间不确定，当前值需要重新核对。":!available_?"心跳已过期；以下历史保留原始质量，不能当作当前状态。":r.reason=="disconnected"?"模拟链路已断开，当前值未知；采集进程仍在线。":"源数据质量异常，当前值不可用；不会补零或推断完成。");
    for(int i=0;i<3;++i){
        auto value=good?r.carts[i]:"UNKNOWN";
        cartState_[i]->setText(state(value));
        bool exception=value=="blocked"||value=="UNKNOWN";
        cartBadge_[i]->setText(value=="station_complete"?"已完成":value=="waiting"?"待处理":exception?"需关注":"进行中");
        cartBadge_[i]->setStyleSheet(exception?"color:#94590a;background:#fff1d6;border-radius:5px;padding:4px 8px;":"");
        cartDetail_[i]->setText(value=="station_complete"?"当前工位的本轮步骤已结束":value=="waiting"?"等待前序小车完成当前步骤":value=="blocked"?"合成阻塞事件；当前条件仍活动":value=="UNKNOWN"?"无可信当前值，请查看质量记录":"合成数据 · 当前阶段已确认");
        progress_[i]->setValue(value=="station_complete"?3:value=="sorting"||value=="blocked"?2:value=="scanning"?1:0);
    }
    table_->setRowCount(static_cast<int>(v.rows.size()));
    for(int i=0;i<static_cast<int>(v.rows.size());++i){const auto& x=v.rows[i];
        QStringList cols={QString::number(x.seq),QDateTime::fromMSecsSinceEpoch(x.received_ms).toUTC().toString("HH:mm:ss.zzz"),state(x.carts[0])+" / "+state(x.carts[1])+" / "+state(x.carts[2]),x.quality=="good"?"正常":"不可用",eventText(x.event)};
        for(int j=0;j<cols.size();++j){auto* item=new QTableWidgetItem(cols[j]);item->setToolTip(cols[j]);if(x.quality!="good"&&j==3)item->setForeground(QColor("#94590a"));table_->setItem(i,j,item);}
    }
    eventNote_->setText(QString("最近 %1 帧 · 覆盖范围未知 · 时间为 UTC").arg(v.rows.size()));
    if(r.seq!=last_seq_||v.runtime.mode!=last_mode_){last_seq_=r.seq;last_mode_=v.runtime.mode;std::cout<<"render seq="<<r.seq<<" alarm="<<r.alarm<<" quality="<<r.quality<<" link="<<r.link<<" mode="<<v.runtime.mode<<" process="<<v.runtime.liveness<<'\n'<<std::flush;}
}
void Workbench::unavailable(const QString& reason){
    available_=false;mode_->setText("模式未确认");connection_->setText("○  采集数据不可用");summary_->setText("等待本地模拟采集器");summaryDetail_->setText(reason);
    for(int i=0;i<3;++i){cartState_[i]->setText("状态未知");cartDetail_[i]->setText("没有可用的当前观测");cartBadge_[i]->setText("等待证据");progress_[i]->setValue(0);}
}
bool Workbench::captureReady(const QString& kind) const{
    if(observations_<2||view_.rows.empty())return false;
    const auto& r=view_.rows.front();
    if(kind=="manual")return available_&&view_.runtime.mode=="manual";
    if(kind=="normal")return available_&&view_.runtime.mode=="automatic"&&r.quality=="good"&&r.alarm=="cleared";
    if(kind=="alarm")return available_&&r.alarm=="active";
    return !available_||r.quality!="good";
}
QString Workbench::confirmedMode()const{return QString::fromStdString(view_.runtime.mode);}
