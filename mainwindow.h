#ifndef MAINWINDOW_H
#define MAINWINDOW_H

#include <QMainWindow>
#include <QLabel>
#include <QToolBar>
#include <QFontComboBox>
#include <QSpinBox>
#include <QAction>
#include "document_view.h"
#include "properties.h"

using namespace UDoc;

class MainWindow : public QMainWindow {
    Q_OBJECT
public:
    explicit MainWindow(QWidget* parent = nullptr);

private slots:
    void openPdf();
    void onSelectionChanged(const UDoc::Selection& sel);
    void onZoomChanged(double zoom);
    void onTextFormatChanged(const UDoc::CharacterProperties& props);

    void onBoldToggled(bool on);
    void onItalicToggled(bool on);
    void onUnderlineToggled(bool on);
    void onFontFamilyChanged(const QFont& font);
    void onFontSizeChanged(int size);
    void onColorPicker();
    void onAlignLeft();
    void onAlignCenter();
    void onAlignRight();
    void onAlignJustify();

private:
    void buildMenuBar();
    void buildFormatToolBar();
    void buildViewToolBar();
    void buildStatusBar();
    void loadDocument(std::unique_ptr<UDoc::Document> doc);
    void blockToolbarSignals(bool block);
    CharacterProperties currentCharProps() const;

    DocumentView*  m_documentView   = nullptr;
    QLabel*        m_statusLabel    = nullptr;
    QLabel*        m_zoomLabel      = nullptr;

    // Format toolbar
    QToolBar*      m_formatBar      = nullptr;
    QAction*       m_boldAct        = nullptr;
    QAction*       m_italicAct      = nullptr;
    QAction*       m_underlineAct   = nullptr;
    QFontComboBox* m_fontCombo      = nullptr;
    QSpinBox*      m_sizeSpin       = nullptr;
    QAction*       m_colorAct       = nullptr;
    QAction*       m_alignLeftAct   = nullptr;
    QAction*       m_alignCenterAct = nullptr;
    QAction*       m_alignRightAct  = nullptr;
    QAction*       m_alignJustAct   = nullptr;

    QColor m_textColor = Qt::black;

    std::unique_ptr<UDoc::Document> m_document;
};

#endif // MAINWINDOW_H
