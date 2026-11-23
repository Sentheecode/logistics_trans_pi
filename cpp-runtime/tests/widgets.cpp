#include "workbench.hpp"
#include <QApplication>
#include <QAbstractButton>
#include <QElapsedTimer>
#include <QFont>
#include <QFontMetrics>
#include <QLabel>
#include <QMessageBox>
#include <QPushButton>
#include <QThread>
#include <QTimer>
#include <functional>
#include <iostream>
#include <stdexcept>
void wait(const std::function<bool()>& condition,int timeout=6000){
    QElapsedTimer clock;clock.start();
    while(clock.elapsed()<timeout){QCoreApplication::processEvents();if(condition())return;QThread::msleep(10);}
    throw std::runtime_error("widget condition timed out");
}
void pump(int ms){QElapsedTimer t;t.start();wait([&]{return t.elapsed()>ms;},ms+1000);}
int main(int argc,char** argv){
    QApplication app(argc,argv);
    QFont font;font.setFamilies({"Noto Sans CJK SC","Microsoft YaHei UI","Microsoft YaHei","sans-serif"});font.setPixelSize(13);app.setFont(font);
    if(argc!=3)return 2;
    try{
        Workbench window(std::filesystem::u8path(argv[1]));window.resize(1100,650);window.show();
        auto* mode=window.findChild<QPushButton*>("modeButton");auto* step=window.findChild<QPushButton*>("primary");
        if(std::string(argv[2])=="offline"){
            pump(700);if(mode->isEnabled()||step->isEnabled())throw std::runtime_error("offline controls enabled");
            std::cout<<"PASS offline controls disabled\n";return 0;
        }
        wait([&]{return mode->isEnabled();});
        if(window.confirmedMode()!="automatic"||step->isEnabled())throw std::runtime_error("auto mode controls incorrect");
        mode->click();wait([&]{return window.confirmedMode()=="manual"&&step->isEnabled();});
        auto before=sim::read(std::filesystem::u8path(argv[1]),"status");
        step->click();step->click();step->click();
        wait([&]{return step->isEnabled();});
        auto after=sim::read(std::filesystem::u8path(argv[1]),"status");
        if(after.rows.front().seq!=before.rows.front().seq+1)throw std::runtime_error("rapid clicks advanced more than once");
        pump(1700);
        if(!step->isEnabled()||sim::read(std::filesystem::u8path(argv[1]),"status").rows.front().seq!=after.rows.front().seq)throw std::runtime_error("manual wait treated as disconnected or advanced");
        // Cancellation is the default and sends no automatic-mode command.
        QTimer::singleShot(50,[&]{auto* box=window.findChild<QMessageBox*>("confirmAutomatic");if(box)box->button(QMessageBox::No)->click();});
        mode->click();pump(300);
        if(window.confirmedMode()!="manual")throw std::runtime_error("cancel resumed automatic");
        QTimer::singleShot(50,[&]{auto* box=window.findChild<QMessageBox*>("confirmAutomatic");if(box)box->button(QMessageBox::Yes)->click();});
        mode->click();wait([&]{return window.confirmedMode()=="automatic"&&mode->isEnabled();});
        if(step->isEnabled())throw std::runtime_error("step enabled in automatic");
        // No leaf label may silently clip in either documented viewport.
        for(auto size:{QSize(1100,650),QSize(1280,800)}){
            window.resize(size);pump(250);
            for(auto* l:window.findChildren<QLabel*>()){
                if(!l->isVisible()||l->wordWrap())continue;
                if(l->fontMetrics().horizontalAdvance(l->text())>l->contentsRect().width()+2)throw std::runtime_error(("clipped label: "+l->text()).toStdString());
                if(!l->fontMetrics().inFont(QChar(0x6a21)))throw std::runtime_error("missing Chinese glyph");
            }
        }
        std::cout<<"PASS widgets: actual mode buttons, rapid-click suppression, explicit confirmation/cancel, two viewport sizes, Chinese glyphs\n";
        return 0;
    }catch(const std::exception& e){std::cerr<<e.what()<<'\n';return 1;}
}
