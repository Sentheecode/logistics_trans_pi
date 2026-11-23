#include "workbench.hpp"
#include <QApplication>
#include <QFont>
#include <QTimer>
#include <iostream>
int main(int argc,char** argv){
    QApplication app(argc,argv);
    QFont font;font.setFamilies({"Noto Sans CJK SC","Microsoft YaHei UI","Microsoft YaHei","sans-serif"});font.setPixelSize(13);app.setFont(font);
    if(argc<2||argc>6){std::cerr<<"Usage: hmi DATA_DIR [smoke_ms [capture.png [normal|manual|alarm|unavailable [compact]]]]\n";return 2;}
    try{
        int smoke=argc>=3?sim::number(argv[2],500,60000):0;
        QString capture=argc>=4?QString::fromUtf8(argv[3]):QString(),kind=argc>=5?argv[4]:"normal";
        if(kind!="normal"&&kind!="manual"&&kind!="alarm"&&kind!="unavailable")throw std::runtime_error("invalid capture kind");
        Workbench window(std::filesystem::u8path(argv[1]));if(argc==6){if(std::string(argv[5])!="compact")throw std::runtime_error("invalid size");window.resize(1100,650);}
        bool captured=false;QTimer timer;
        QObject::connect(&timer,&QTimer::timeout,&window,[&]{if(!capture.isEmpty()&&!captured&&window.captureReady(kind))captured=window.grab().save(capture);});timer.start(100);
        window.show();
        if(smoke)QTimer::singleShot(smoke,&window,[&]{std::cout<<"rendered_observations="<<window.observations()<<'\n';app.exit(window.observations()>=2&&(capture.isEmpty()||captured)?0:1);});
        return app.exec();
    }catch(const std::exception& e){std::cerr<<"hmi error: "<<e.what()<<'\n';return 1;}
}
