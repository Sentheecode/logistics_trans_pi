#include "core.hpp"
#include <QApplication>
#include <QDateTime>
#include <QHeaderView>
#include <QLabel>
#include <QTableWidget>
#include <QTimer>
#include <QVBoxLayout>
#include <QWidget>
#include <future>
#include <iostream>
int main(int argc,char** argv){
    QApplication app(argc,argv);
    if(argc<2||argc>5){std::cerr<<"Usage: hmi DATA_DIR [smoke_ms:500..60000] [capture.png [normal|alarm|unavailable]]\n";return 2;}
    try {
        const auto dir=std::filesystem::u8path(argv[1]);
        const int smoke=argc>=3?sim::number(argv[2],500,60000):0;
        const QString capture=argc>=4?QString::fromUtf8(argv[3]):QString();
        const std::string capture_kind=argc==5?argv[4]:"alarm";
        if(capture_kind!="normal"&&capture_kind!="alarm"&&capture_kind!="unavailable")throw std::runtime_error("invalid capture kind");
        QWidget window;window.setWindowTitle("SIMULATION ONLY | Logistics C++ runtime");window.resize(1100,650);
        auto* layout=new QVBoxLayout(&window);
        auto* title=new QLabel("SIMULATION ONLY — three carts / one synthetic station");
        title->setStyleSheet("font-size:22px; font-weight:bold; color:#075985;");layout->addWidget(title);
        auto* health=new QLabel("Waiting for local evidence…");health->setWordWrap(true);layout->addWidget(health);
        auto* state=new QLabel("Current cart states unavailable");state->setStyleSheet("font-size:18px;");state->setWordWrap(true);layout->addWidget(state);
        auto* alarm=new QLabel("Alarm condition: unknown");layout->addWidget(alarm);
        auto* table=new QTableWidget(0,8);
        table->setHorizontalHeaderLabels({"Sequence","Received UTC","Cart 1","Cart 2","Cart 3","Quality","Link","Event"});
        table->setEditTriggers(QAbstractItemView::NoEditTriggers);table->horizontalHeader()->setSectionResizeMode(QHeaderView::ResizeToContents);layout->addWidget(table);
        auto* note=new QLabel("Read-only tail: 40 frames. Coverage unknown. Evidence does not establish root cause.\nStation completion is not whole-line completion. Closing this window does not stop collection.");
        note->setWordWrap(true);layout->addWidget(note);
        std::future<sim::View> pending;
        std::int64_t last_seq=0;int rendered=0;bool captured=false;
        QTimer timer;
        QObject::connect(&timer,&QTimer::timeout,&window,[&]{
            if(pending.valid()){
                if(pending.wait_for(std::chrono::milliseconds(0))!=std::future_status::ready)return;
                try{
                    const auto v=pending.get();
                    if(v.rows.empty())throw std::runtime_error("no samples committed");
                    const auto& r=v.rows.front();
                    const bool usable=v.freshness=="recent"&&r.quality=="good";
                    health->setText(QString("Freshness: %1 | quality: %2 (%3) | simulated link: %4 | sequence: %5")
                        .arg(QString::fromStdString(v.freshness),QString::fromStdString(r.quality),QString::fromStdString(r.reason),QString::fromStdString(r.link)).arg(r.seq));
                    state->setText(usable?QString("Cart 1: %1  →  Cart 2: %2  →  Cart 3: %3").arg(QString::fromStdString(r.carts[0]),QString::fromStdString(r.carts[1]),QString::fromStdString(r.carts[2])):"Current cart states: UNKNOWN — stale / bad quality / clock uncertain");
                    alarm->setText("Alarm condition: "+QString::fromStdString(usable?r.alarm:"unknown")+" | acknowledgement/suppression: not implemented");
                    alarm->setStyleSheet(!usable?"color:#92400e; font-weight:bold;":r.alarm=="active"?"color:#b91c1c; font-weight:bold;":"color:#166534;");
                    table->setRowCount(static_cast<int>(v.rows.size()));
                    for(int i=0;i<static_cast<int>(v.rows.size());++i){const auto& x=v.rows[i];
                        QStringList cols={QString::number(x.seq),QDateTime::fromMSecsSinceEpoch(x.received_ms).toUTC().toString(Qt::ISODateWithMs),QString::fromStdString(x.carts[0]),QString::fromStdString(x.carts[1]),QString::fromStdString(x.carts[2]),QString::fromStdString(x.quality),QString::fromStdString(x.link),QString::fromStdString(x.event)};
                        for(int j=0;j<cols.size();++j)table->setItem(i,j,new QTableWidgetItem(cols[j]));
                    }
                    if(r.seq!=last_seq){++rendered;last_seq=r.seq;if(smoke)std::cout<<"render seq="<<r.seq<<" alarm="<<r.alarm<<" quality="<<r.quality<<" link="<<r.link<<'\n'<<std::flush;}
                    const bool capture_ready=capture_kind=="normal"?(usable&&r.alarm=="cleared"):capture_kind=="alarm"?(usable&&r.alarm=="active"):!usable;
                    if(!capture.isEmpty()&&!captured&&capture_ready)captured=window.grab().save(capture);
                }catch(const std::exception& e){
                    health->setText("Evidence unavailable: "+QString::fromUtf8(e.what()));
                    state->setText("Current cart states: UNKNOWN");alarm->setText("Alarm condition: unknown (query unavailable)");table->setRowCount(0);
                }
            }
            pending=std::async(std::launch::async,[dir]{return sim::read(dir,"recent",40);});
        });
        timer.start(100);window.show();
        if(smoke)QTimer::singleShot(smoke,&window,[&]{std::cout<<"rendered_updates="<<rendered<<'\n';app.exit(rendered>=2&&(capture.isEmpty()||captured)?0:1);});
        return app.exec();
    }catch(const std::exception& e){std::cerr<<"hmi error: "<<e.what()<<'\n';return 1;}
}
