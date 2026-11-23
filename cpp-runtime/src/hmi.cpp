#include "workbench.hpp"
#include <QApplication>
#include <QComboBox>
#include <QFont>
#include <QTimer>
#include <QWidget>
#include <iostream>
int main(int argc,char** argv){
    QApplication app(argc,argv);
    QFont font;font.setFamilies({"Noto Sans CJK SC","Microsoft YaHei UI","Microsoft YaHei","sans-serif"});font.setPixelSize(13);app.setFont(font);
    if(argc<2||argc>6){std::cerr<<"Usage: hmi DATA_DIR [smoke_ms [capture.png [normal|manual|alarm|unavailable|sort|peers|south|meter2|roster|stations|events|points|boundary|empty|dropdown [compact]]]]\n";return 2;}
    try{
        int smoke=argc>=3?sim::number(argv[2],500,60000):0;
        QString capture=argc>=4?QString::fromUtf8(argv[3]):QString(),kind=argc>=5?argv[4]:"normal";
        if(kind!="normal"&&kind!="manual"&&kind!="alarm"&&kind!="unavailable"&&kind!="sort"&&kind!="peers"&&kind!="south"&&kind!="meter2"&&kind!="roster"&&kind!="stations"&&kind!="events"&&kind!="eventbad"&&kind!="points"&&kind!="pointfind"&&kind!="boundary"&&kind!="empty"&&kind!="emptystations"&&kind!="dropdown")throw std::runtime_error("invalid capture kind");
        Workbench window(std::filesystem::u8path(argv[1]));if(argc==6){if(std::string(argv[5])!="compact")throw std::runtime_error("invalid size");window.resize(1100,650);}
        window.prepareCapture(kind);
        bool captured=false;QTimer timer;
        QObject::connect(&timer,&QTimer::timeout,&window,[&]{
            if(capture.isEmpty()||captured||!window.captureReady(kind))return;
            if(kind=="dropdown"){
                auto* combo=window.findChild<QComboBox*>("areaCombo");
                if(!combo)return;
                combo->showPopup();
                QCoreApplication::processEvents();
                captured=window.grab().save(capture);
                QString popup=capture;
                if(popup.endsWith(".png"))popup.chop(4);
                popup+="-popup.png";
                bool popup_saved=false;
                for(auto* widget:QApplication::topLevelWidgets()){
                    if(widget==&window||!widget->isVisible()||widget->width()<40||widget->height()<40)continue;
                    popup_saved=widget->grab().save(popup)||popup_saved;
                }
                if(!popup_saved)captured=false;
                return;
            }
            captured=window.grab().save(capture);
        });timer.start(100);
        window.show();
        if(smoke)QTimer::singleShot(smoke,&window,[&]{std::cout<<"rendered_observations="<<window.observations()<<'\n';app.exit(window.observations()>=2&&(capture.isEmpty()||captured)?0:1);});
        return app.exec();
    }catch(const std::exception& e){std::cerr<<"hmi error: "<<e.what()<<'\n';return 1;}
}
