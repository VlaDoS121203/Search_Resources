#include <QApplication>
#include <QWidget>
#include <QLineEdit>
#include <QPushButton>
#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QListWidget>
#include <QListWidgetItem>
#include <QLabel>
#include <QDesktopServices>
#include <QUrl>
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QNetworkRequest>
#include <QRegularExpression>
#include <QTextStream>
#include <QMap>
#include <QSet>
#include <QStringList>
#include <QDebug>
#include <algorithm>
#include <cmath> // Для log

// Структура результата поиска
struct SearchResult {
    QString url;
    QString title;
    double score; // Используем double для точности TF-IDF
};

class SearchEngine : public QObject {
    Q_OBJECT
public:
    explicit SearchEngine(QObject *parent = nullptr) : QObject(parent) {
        m_stopWords << "и" << "в" << "на" << "с" << "по" << "для" << "что" << "это"
                    << "the" << "a" << "an" << "is" << "in" << "on" << "for" << "of" << "to";
        connect(&m_manager, &QNetworkAccessManager::finished, this, &SearchEngine::onFetchFinished);
    }

    void addUrlsToIndex(const QStringList &urls) {
        m_pendingUrls = urls;
        m_processedCount = 0;
        m_totalDocuments = urls.size(); // Запоминаем общее кол-во документов для IDF
        fetchNext();
    }

    // ГЛАВНЫЙ МЕТОД ПОИСКА С TF-IDF
    QList<SearchResult> search(const QString &query) const {
        QStringList queryTokens = tokenize(query);
        if (queryTokens.isEmpty()) return {};

        // Ключ: URL документа, Значение: Итоговый балл релевантности
        QMap<QString, double> docScores;

        for (const QString &term : queryTokens) {
            if (!m_index.contains(term)) continue;

            // TF(term) для текущего документа не нужен, если мы ищем по запросу
            // Но нам нужно знать IDF(term)
            int docsWithTerm = m_index.value(term).size();
            // Формула IDF: log(TotalDocs / DocsWithTerm)
            // Используем qLn (натуральный логарифм Qt) или std::log
            double idf = std::log((double)m_totalDocuments / (double)docsWithTerm);

            // Проходим по всем документам, где есть этот термин
            const auto& docMap = m_index.value(term);
            for (auto it = docMap.begin(); it != docMap.end(); ++it) {
                const QString& docUrl = it.key();
                int termCountInDoc = it.value();

                // Формула TF: (Кол-во вхождений термина в документе) / (Общее кол-во слов в документе)
                double tf = 0.0;
                if (m_docLengths.contains(docUrl)) {
                    tf = (double)termCountInDoc / (double)m_docLengths.value(docUrl);
                }

                docScores[docUrl] += tf * idf;
            }
        }

        // Преобразуем карту результатов в список для сортировки
        QList<SearchResult> results;
        results.reserve(docScores.size());
        for (auto it = docScores.begin(); it != docScores.end(); ++it) {
            results.append({it.key(), m_titles.value(it.key(), it.key()), it.value()});
        }

        // Сортируем по убыванию релевантности
        std::sort(results.begin(), results.end(), [](const SearchResult &a, const SearchResult &b) {
            return a.score > b.score;
        });

        return results;
    }

signals:
    void indexingProgress(int current, int total);
    void indexingFinished();

private slots:
    void onFetchFinished(QNetworkReply *reply) {
        if (reply->error() == QNetworkReply::NoError) {
            QString html = QTextStream(reply).readAll();
            processPage(reply->url().toString(), html);
        } else {
            qWarning() << "Ошибка загрузки" << reply->url() << ":" << reply->errorString();
        }
        m_processedCount++;
        emit indexingProgress(m_processedCount, m_processedCount + m_pendingUrls.size());
        reply->deleteLater();
        fetchNext();
    }

private:
    QNetworkAccessManager m_manager;

    // Инвертированный индекс: Термин -> (URL -> Частота вхождений)
    // Теперь мы храним не просто факт наличия, а количество раз
    QMap<QString, QMap<QString, int>> m_index;

    // Длина документа (общее кол-во слов) для расчёта TF
    QMap<QString, int> m_docLengths;

    // Заголовки страниц
    QMap<QString, QString> m_titles;

    // Стоп-слова
    QStringList m_stopWords;

    // Данные для индексации
    QStringList m_pendingUrls;
    int m_processedCount = 0;
    int m_totalDocuments = 0; // Нужно для формулы IDF

    void fetchNext() {
        if (m_pendingUrls.isEmpty()) {
            emit indexingFinished();
            return;
        }
        QString url = m_pendingUrls.takeFirst();
        QNetworkRequest request{QUrl{url}};
        request.setRawHeader("User-Agent", "QtSearchBot/1.0");
        m_manager.get(request);
    }

    void processPage(const QString &url, const QString &html) {
        // 1. Извлечение заголовка
        QRegularExpression titleRe("<title[^>]*>(.*?)</title>",
                                   QRegularExpression::CaseInsensitiveOption |
                                       QRegularExpression::DotMatchesEverythingOption);
        QRegularExpressionMatch titleMatch = titleRe.match(html);
        QString title = titleMatch.hasMatch() ? titleMatch.captured(1).trimmed() : url;
        m_titles[url] = title;

        // 2. Очистка текста
        QString text = html;
        text.remove(QRegularExpression("<[^>]*>"));
        text.replace(QRegularExpression("&nbsp;|&#160;"), " ");
        text.replace(QRegularExpression("\\s+"), " ");

        // 3. Токенизация
        QStringList tokens = tokenize(text);

        // Сохраняем длину документа
        m_docLengths[url] = tokens.size();

        // 4. Заполнение индекса частотности
        for (const QString &token : tokens) {
            m_index[token][url]++; // Увеличиваем счётчик вхождений
        }
    }

    QStringList tokenize(const QString &text) const {
        QStringList tokens;
        QStringList raw = text.split(QRegularExpression("\\W+"), Qt::SkipEmptyParts);
        for (const QString &word : raw) {
            QString lower = word.toLower();
            if (!m_stopWords.contains(lower) && lower.length() > 2) {
                tokens.append(lower);
            }
        }
        return tokens;
    }
};

class SearchAppWidget : public QWidget {
    Q_OBJECT
public:
    explicit SearchAppWidget(QWidget *parent = nullptr) : QWidget(parent) {
        auto *layout = new QVBoxLayout(this);
        layout->setContentsMargins(16, 16, 16, 16);
        layout->setSpacing(12);

        auto *searchLayout = new QHBoxLayout();
        searchEdit = new QLineEdit(this);
        searchEdit->setPlaceholderText("Поиск по TF-IDF...");
        searchEdit->setClearButtonEnabled(true);
        searchEdit->setStyleSheet(
            "QLineEdit { padding: 10px 15px; border: 2px solid #dfe1e5; border-radius: 24px; font-size: 15px; }"
            "QLineEdit:focus { border-color: #4285f4; background: #fff; box-shadow: 0 2px 4px rgba(66,133,244,0.2); }"
            );

        searchBtn = new QPushButton("🔍", this);
        searchBtn->setFixedSize(44, 44);
        searchBtn->setCursor(Qt::PointingHandCursor);
        searchBtn->setStyleSheet(
            "QPushButton { background: #4285f4; border: none; border-radius: 22px; color: white; font-size: 18px; }"
            "QPushButton:hover { background: #3367d6; }"
            );

        searchLayout->addWidget(searchEdit, 1);
        searchLayout->addWidget(searchBtn);
        layout->addLayout(searchLayout);

        statusLabel = new QLabel("Индексация...", this);
        statusLabel->setStyleSheet("color: #666; font-size: 13px;");
        layout->addWidget(statusLabel);

        resultsList = new QListWidget(this);
        resultsList->setStyleSheet(
            "QListWidget { border: 1px solid #ddd; border-radius: 8px; font-size: 14px; }"
            "QListWidget::item { padding: 10px; border-bottom: 1px solid #f0f0f0; }"
            "QListWidget::item:hover { background: #f8f9fa; }"
            );
        layout->addWidget(resultsList, 1);

        connect(searchBtn, &QPushButton::clicked, this, &SearchAppWidget::performSearch);
        connect(searchEdit, &QLineEdit::returnPressed, this, &SearchAppWidget::performSearch);
        connect(resultsList, &QListWidget::itemDoubleClicked, this, [](QListWidgetItem *item){
            QDesktopServices::openUrl(QUrl(item->data(Qt::UserRole).toString()));
        });

        setMinimumSize(520, 450);
    }

    void setEngine(SearchEngine *engine) {
        m_engine = engine;
        connect(m_engine, &SearchEngine::indexingProgress, this, [this](int cur, int total){
            statusLabel->setText(QString("Индексация: %1 / %2").arg(cur).arg(total));
        });
        connect(m_engine, &SearchEngine::indexingFinished, this, [this](){
            statusLabel->setText("✅ Готово. Введите запрос (TF-IDF).");
        });
    }

private:
    void performSearch() {
        if (!m_engine) return;
        QString query = searchEdit->text().trimmed();
        resultsList->clear();

        if (query.isEmpty()) {
            statusLabel->setText("Введите поисковый запрос");
            return;
        }

        statusLabel->setText("Поиск...");
        auto results = m_engine->search(query);

        if (results.isEmpty()) {
            resultsList->addItem("🔍 Ничего не найдено");
            statusLabel->setText("Готово к поиску");
            return;
        }

        for (const auto &res : results) {
            auto *item = new QListWidgetItem(resultsList);
            // Показываем балл релевантности с 2 знаками после запятой
            item->setText(QString("%1 [Score: %2]").arg(res.title).arg(res.score, 0, 'f', 4));
            item->setToolTip(res.url);
            item->setData(Qt::UserRole, res.url);
            item->setForeground(QBrush(QColor("#1a0dab")));
        }
        statusLabel->setText(QString("Найдено: %1").arg(results.size()));
    }

    QLineEdit *searchEdit = nullptr;
    QPushButton *searchBtn = nullptr;
    QLabel *statusLabel = nullptr;
    QListWidget *resultsList = nullptr;
    SearchEngine *m_engine = nullptr;
};

int main(int argc, char *argv[]) {
    QApplication app(argc, argv);
    app.setApplicationName("QtLocalSearch");

    SearchAppWidget ui;
    ui.setWindowTitle("Поисковик TF-IDF");
    ui.show();

    SearchEngine engine;
    ui.setEngine(&engine);

    // Для теста лучше взять страницы с разным контентом
    QStringList demoUrls = {
        "https://ru.wikipedia.org/wiki/Qt",
        "https://ru.wikipedia.org/wiki/C%2B%2B",
        "https://doc.qt.io/qt-6/index.html"
    };

    engine.addUrlsToIndex(demoUrls);

    return app.exec();
}

#include "main.moc"