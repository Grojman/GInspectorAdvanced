#pragma once

#include <QAbstractListModel>
#include <QHash>
#include <QPixmap>
#include <QPointer>
#include <QSortFilterProxyModel>
#include <QStyledItemDelegate>

class JsonDocument;
class Project;

// One card per item of the current JSON document.
class CardModel : public QAbstractListModel
{
    Q_OBJECT
public:
    enum Roles {
        TitleRole = Qt::UserRole + 1,
        SubtitleRole,
        SummaryRole,   // QStringList "key: value"
        ThumbRole,     // QPixmap
        ErrorsRole,    // int
        WarningsRole,  // int
        ModifiedRole,  // bool
        SearchRole,    // text used by the filter
    };

    explicit CardModel(Project *project, QObject *parent = nullptr);
    void setDocument(JsonDocument *doc);
    JsonDocument *document() const { return m_doc; }
    void refreshIssues();

    int rowCount(const QModelIndex &parent = QModelIndex()) const override;
    QVariant data(const QModelIndex &index, int role) const override;

private:
    QPixmap thumbnail(int row) const;
    void recountIssues(int row);

    Project *m_project;
    QPointer<JsonDocument> m_doc;
    QList<QPair<int, int>> m_issueCounts; // errors, warnings per item
    mutable QHash<QString, QPixmap> m_thumbs;
};

class CardFilter : public QSortFilterProxyModel
{
public:
    using QSortFilterProxyModel::QSortFilterProxyModel;
    void setOnlyProblems(bool v);

protected:
    bool filterAcceptsRow(int row, const QModelIndex &parent) const override;

private:
    bool m_onlyProblems = false;
};

class CardDelegate : public QStyledItemDelegate
{
public:
    using QStyledItemDelegate::QStyledItemDelegate;
    void paint(QPainter *p, const QStyleOptionViewItem &opt, const QModelIndex &index) const override;
    QSize sizeHint(const QStyleOptionViewItem &opt, const QModelIndex &index) const override;
};
