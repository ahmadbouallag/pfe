#ifndef MAINWINDOW_H
#define MAINWINDOW_H

#include <QMainWindow>
#include "document_view.h"

using namespace UDoc;

class MainWindow : public QMainWindow {
    Q_OBJECT
public:
    explicit MainWindow(QWidget* parent = nullptr);
    
private:
    DocumentView* m_documentView;
};

#endif // MAINWINDOW_H
