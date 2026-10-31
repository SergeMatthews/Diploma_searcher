#include "mainwindow.h"
#include "ui_mainwindow.h"

#include <QPushButton>
#include <QVBoxLayout>
#include <QLabel>
#include <QMessageBox>
#include <QSettings>
#include <QSqlQuery>
#include <QSqlError>
#include <QTableWidgetItem>
#include <QHeaderView>
#include <QTextBrowser>
#include <QLineEdit>


MainWindow::MainWindow(QWidget *parent)
    : QMainWindow(parent)
    , ui(new Ui::MainWindow)
{
    ui->setupUi(this);


    ui->tableWidget->setColumnCount(2);
    ui->tableWidget->setHorizontalHeaderLabels(
        {QStringLiteral("Слово"), QStringLiteral("Частота")});
    ui->tableWidget->horizontalHeader()->setStretchLastSection(true);
    ui->tableWidget->setEditTriggers(QAbstractItemView::NoEditTriggers);


    m_showRankBtn = new QPushButton(QStringLiteral("Показать релевантность"), this);
    m_showRankBtn->setEnabled(false);

    if (auto *tab = ui->tab)
    {
        auto *layout = qobject_cast<QVBoxLayout *>(tab->layout());
        if (!layout)
        {

            if (auto *form = ui->formLayout) {
                form->addRow(m_showRankBtn);
            }
        } else {
            layout->addWidget(m_showRankBtn);
        }
    }

    // Сигналы
    connect(ui->pushButton, &QPushButton::clicked,
            this,           &MainWindow::onSearch);
    connect(ui->lineEdit, &QLineEdit::returnPressed,
            this,         &MainWindow::onSearch);
    connect(m_showRankBtn, &QPushButton::clicked,
            this,          &MainWindow::onShowRanking);

    // Подключение к базе данных
    if (!connectToDb())
    {
        QMessageBox::critical(this,
                              QStringLiteral("Ошибка"),
                              QStringLiteral("Не удалось подключиться к базе данных.\n"
                                             "Проверьте настройки в indexer.ini."));
        return;
    }

    loadWords();
}

MainWindow::~MainWindow()
{
    if (m_db.isOpen())
        m_db.close();
    delete ui;
}

// Подключение к базе данных
bool MainWindow::connectToDb()
{
    QSettings s(QStringLiteral("indexer.ini"), QSettings::IniFormat);

    m_db = QSqlDatabase::addDatabase(QStringLiteral("QPSQL"));
    m_db.setHostName    (s.value(QStringLiteral("Database/Host"),     QStringLiteral("localhost")).toString());
    m_db.setPort        (s.value(QStringLiteral("Database/Port"),     5432).toInt());
    m_db.setDatabaseName(s.value(QStringLiteral("Database/Name"),     QStringLiteral("indexer")).toString());
    m_db.setUserName    (s.value(QStringLiteral("Database/User"),     QStringLiteral("postgres")).toString());
    m_db.setPassword    (s.value(QStringLiteral("Database/Password"), QString()).toString());

    if (!m_db.open())
        return false;

    //Создание таблицы в базе данных
    const QStringList ddl = {
        QStringLiteral(
            "CREATE TABLE IF NOT EXISTS documents ("
            " id   SERIAL PRIMARY KEY,"
            " path TEXT NOT NULL UNIQUE)"),
        QStringLiteral(
            "CREATE TABLE IF NOT EXISTS words ("
            " id   SERIAL PRIMARY KEY,"
            " word TEXT NOT NULL UNIQUE)"),
        QStringLiteral(
            "CREATE TABLE IF NOT EXISTS word_frequencies ("
            " document_id INTEGER NOT NULL REFERENCES documents(id) ON DELETE CASCADE,"
            " word_id     INTEGER NOT NULL REFERENCES words(id)     ON DELETE CASCADE,"
            " frequency   INTEGER NOT NULL,"
            " PRIMARY KEY (document_id, word_id))")
    };

    QSqlQuery q(m_db);
    for (const QString &sql : ddl) {
        if (!q.exec(sql))
            return false;
    }
    return true;
}


// Заполнение таблицы слов
void MainWindow::loadWords()
{
    QSqlQuery q(m_db);
    q.prepare(QStringLiteral(
        "SELECT w.word, COALESCE(SUM(f.frequency), 0) AS total "
        "FROM words w "
        "LEFT JOIN word_frequencies f ON f.word_id = w.id "
        "GROUP BY w.word "
        "ORDER BY total DESC, w.word ASC"
    ));

    if (!q.exec()) {
        QMessageBox::warning(this,
                             QStringLiteral("Ошибка"),
                             QStringLiteral("Не удалось получить список слов:\n") +
                             q.lastError().text());
        return;
    }

    ui->tableWidget->setRowCount(0);
    int row = 0;
    while (q.next()) {
        ui->tableWidget->insertRow(row);
        ui->tableWidget->setItem(row, 0,
                                 new QTableWidgetItem(q.value(0).toString()));
        ui->tableWidget->setItem(row, 1,
                                 new QTableWidgetItem(QString::number(q.value(1).toInt())));
        ++row;
    }
    ui->tableWidget->resizeColumnsToContents();
}

// Парсер по условию "слов"
QStringList MainWindow::parseQuery(const QString &raw) const
{
    QStringList words;
    QString current;

    for (QChar ch : raw) {
        if (ch.isLetterOrNumber()) {
            current.append(ch.toLower());
        } else if (!current.isEmpty()) {
            words << current;
            current.clear();
        }
    }
    if (!current.isEmpty())
        words << current;

    while (words.size() > 4)
        words.removeLast();

    return words;
}

// Запуск поиска по документам
bool MainWindow::runSearch(const QStringList &words)
{
    ui->textBrowser->clear();

    if (words.isEmpty()) {
        ui->textBrowser->setPlainText(QStringLiteral("Введите хотя бы одно слово."));
        return false;
    }

    QStringList ph;
    for (int i = 0; i < words.size(); ++i)
        ph << QStringLiteral(":w%1").arg(i);

    const QString sql = QStringLiteral(
        "SELECT d.path, SUM(f.frequency) AS relevance "
        "FROM documents d "
        "JOIN word_frequencies f ON f.document_id = d.id "
        "JOIN words w ON w.id = f.word_id "
        "WHERE w.word IN (%1) "
        "GROUP BY d.id, d.path "
        "HAVING COUNT(DISTINCT w.word) = %2 "
        "ORDER BY relevance DESC "
        "LIMIT 10"
    ).arg(ph.join(QStringLiteral(", "))).arg(words.size());

    QSqlQuery q(m_db);
    q.prepare(sql);
    for (int i = 0; i < words.size(); ++i)
        q.bindValue(ph[i], words[i]);

    if (!q.exec()) {
        QMessageBox::critical(this,
                              QStringLiteral("Ошибка"),
                              QStringLiteral("Ошибка выполнения запроса:\n") +
                              q.lastError().text());
        return false;
    }

    QString out;
    int count = 0;
    while (q.next()) {
        out += QStringLiteral("%1  —  релевантность: %2\n")
                   .arg(q.value(0).toString())
                   .arg(q.value(1).toInt());
        ++count;
    }

    if (count == 0)
        ui->textBrowser->setPlainText(QStringLiteral("Ничего не найдено."));
    else
        ui->textBrowser->setPlainText(out);

    return count > 0;
}


void MainWindow::onSearch()
{
    const QStringList words = parseQuery(ui->lineEdit->text());

    m_lastQueryWords = words;
    m_hasSearch      = runSearch(words);


    m_showRankBtn->setEnabled(m_hasSearch);
}


void MainWindow::onShowRanking()
{
    if (!m_hasSearch || m_lastQueryWords.isEmpty())
        return;

    QStringList ph;
    for (int i = 0; i < m_lastQueryWords.size(); ++i)
        ph << QStringLiteral(":w%1").arg(i);

    const QString sql = QStringLiteral(
        "SELECT d.path, SUM(f.frequency) AS relevance "
        "FROM documents d "
        "JOIN word_frequencies f ON f.document_id = d.id "
        "JOIN words w ON w.id = f.word_id "
        "WHERE w.word IN (%1) "
        "GROUP BY d.id, d.path "
        "HAVING COUNT(DISTINCT w.word) = %2 "
        "ORDER BY relevance DESC "
        "LIMIT 10"
    ).arg(ph.join(QStringLiteral(", "))).arg(m_lastQueryWords.size());

    QSqlQuery q(m_db);
    q.prepare(sql);
    for (int i = 0; i < m_lastQueryWords.size(); ++i)
        q.bindValue(ph[i], m_lastQueryWords[i]);

    if (!q.exec()) {
        QMessageBox::critical(this,
                              QStringLiteral("Ошибка"),
                              QStringLiteral("Ошибка выполнения запроса:\n") +
                              q.lastError().text());
        return;
    }


    ui->tabWidget->setCurrentWidget(ui->tab_2);

    ui->tableWidget->setColumnCount(2);
    ui->tableWidget->setHorizontalHeaderLabels(
        {QStringLiteral("Документ"), QStringLiteral("Релевантность")});

    ui->tableWidget->setRowCount(0);
    int row = 0;
    while (q.next()) {
        ui->tableWidget->insertRow(row);
        ui->tableWidget->setItem(row, 0,
                                 new QTableWidgetItem(q.value(0).toString()));
        ui->tableWidget->setItem(row, 1,
                                 new QTableWidgetItem(QString::number(q.value(1).toInt())));
        ++row;
    }
    ui->tableWidget->resizeColumnsToContents();
}
