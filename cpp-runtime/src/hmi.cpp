#include "workbench.hpp"
#include <QApplication>
#include <QComboBox>
#include <QFont>
#include <QFontInfo>
#include <QFontMetrics>
#include <QPainter>
#include <QPixmap>
#include <QTimer>
#include <QWidget>
#include <iostream>
int hmi_main(int argc,char** argv){
    QApplication app(argc,argv);
    if(argc<2||argc>6){std::cerr<<"Usage: hmi DATA_DIR [smoke_ms [capture.png [normal|manual|alarm|unavailable|sort|peers|south|demo-north|meter2|roster|stations|events|points|boundary|empty|dropdown [compact]]]]\n";return 2;}
    try{
        int smoke=argc>=3?sim::number(argv[2],500,60000):0;
        QString capture=argc>=4?QString::fromUtf8(argv[3]):QString(),kind=argc>=5?argv[4]:"normal";
        if(kind!="normal"&&kind!="manual"&&kind!="alarm"&&kind!="unavailable"&&kind!="sort"&&kind!="peers"&&kind!="south"&&kind!="demo-north"&&kind!="meter2"&&kind!="roster"&&kind!="stations"&&kind!="events"&&kind!="eventbad"&&kind!="points"&&kind!="pointfind"&&kind!="boundary"&&kind!="logs"&&kind!="collect"&&kind!="collectdown"&&kind!="empty"&&kind!="emptystations"&&kind!="dropdown")throw std::runtime_error("invalid capture kind");
        Workbench window(std::filesystem::u8path(argv[1]));if(argc==6){if(std::string(argv[5])!="compact")throw std::runtime_error("invalid size");window.resize(1100,650);}
        const QFontInfo info(app.font());
        const bool cjk=QFontMetrics(app.font()).inFont(QChar(0x6a21));
        std::cout<<"ui_font="<<info.family().toStdString()<<" ui_style="<<info.styleName().toStdString()<<" cjk_glyph="<<(cjk?"yes":"no")<<'\n'<<std::flush;
        window.prepareCapture(kind);
        bool captured=false;QTimer timer;
        QObject::connect(&timer,&QTimer::timeout,&window,[&]{
            if(capture.isEmpty()||captured||!window.captureReady(kind))return;
            if(kind=="dropdown"){
                auto* combo=window.findChild<QComboBox*>("areaCombo");
                if(!combo)return;
                combo->showPopup();
                QCoreApplication::processEvents();
                QString popup=capture;
                if(popup.endsWith(".png"))popup.chop(4);
                popup+="-popup.png";
                QRect bounds=window.frameGeometry();
                QList<QWidget*> extras;
                for(auto* widget:QApplication::topLevelWidgets()){
                    if(widget==&window||!widget->isVisible()||widget->width()<40||widget->height()<40)continue;
                    extras.append(widget);
                    bounds=bounds.united(widget->frameGeometry());
                    widget->grab().save(popup);
                }
                if(extras.isEmpty()){captured=false;return;}
                QPixmap canvas(bounds.size());
                canvas.fill(QColor("#f3f4f6"));
                QPainter painter(&canvas);
                painter.drawPixmap(window.frameGeometry().topLeft()-bounds.topLeft(), window.grab());
                for(auto* widget:extras) painter.drawPixmap(widget->frameGeometry().topLeft()-bounds.topLeft(), widget->grab());
                painter.end();
                captured=canvas.save(capture);
                return;
            }
            captured=window.grab().save(capture);
        });timer.start(100);
        window.show();
        if(smoke)QTimer::singleShot(smoke,&window,[&]{std::cout<<"rendered_observations="<<window.observations()<<'\n';app.exit(window.observations()>=2&&(capture.isEmpty()||captured)?0:1);});
        return app.exec();
    }catch(const std::exception& e){std::cerr<<"hmi error: "<<e.what()<<'\n';return 1;}
}
