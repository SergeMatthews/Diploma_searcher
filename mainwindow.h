#pragma once

#include <QMainWindow>
#include <QSqlDatabase>
#include <QStringList>

QT_BEGIN_NAMESPACE
namespace Ui { class MainWindow; }
QT_END_NAMESPACE

class QPushButton;

class MainWindow : public QMainWindow
{
    Q_OBJECT

public:
    explicit MainWindow(QWidget *parent = nullptr);
    ~MainWindow() override;

private slots:
    void onSearch();        // Enter или кнопка «Найти»
    void onShowRanking();   // «Показать релевантность»

private:
    bool connectToDb();
    void loadWords();
    QStringList parseQuery(const QString &raw) const;
    bool runSearch(const QStringList &words);
    void fillRankingTable(const QStringList &words);

    Ui::MainWindow *ui;

    // Добавляем программно — в .ui этой кнопки нет
    QPushButton *m_showRankBtn = nullptr;

    QSqlDatabase m_db;
    QStringList  m_lastQueryWords;
    bool         m_hasSearch = false;
};
