#include "CardList.h"
#include "model/JsonDocument.h"
#include "model/Project.h"
#include "model/Validator.h"

#include <QApplication>
#include <QFileInfo>
#include <QPainter>
#include <QPainterPath>

namespace {

bool isImagePath(const QString &s)
{
    static const QStringList exts = {QStringLiteral("png"), QStringLiteral("jpg"), QStringLiteral("jpeg"),
                                     QStringLiteral("webp"), QStringLiteral("svg"), QStringLiteral("bmp"),
                                     QStringLiteral("tga")};
    return s.startsWith(QLatin1String("res://")) && exts.contains(QFileInfo(s).suffix().toLower());
}

const QStringList &titleFields()
{
    static const QStringList f = {QStringLiteral("id"), QStringLiteral("name"), QStringLiteral("display_name"),
                                  QStringLiteral("title"), QStringLiteral("key"), QStringLiteral("label")};
    return f;
}

} // namespace

CardModel::CardModel(Project *project, QObject *parent) : QAbstractListModel(parent), m_project(project)
{
}

void CardModel::setDocument(JsonDocument *doc)
{
    beginResetModel();
    if (m_doc)
        disconnect(m_doc, nullptr, this, nullptr);
    m_doc = doc;
    m_issueCounts.clear();
    if (doc) {
        connect(doc, &JsonDocument::itemChanged, this, [this](int row) {
            recountIssues(row);
            const QModelIndex i = index(row);
            emit dataChanged(i, i);
        });
        connect(doc, &JsonDocument::itemsReset, this, [this] {
            beginResetModel();
            refreshIssues();
            endResetModel();
        });
        connect(doc, &JsonDocument::modifiedChanged, this, [this] {
            emit dataChanged(index(0), index(rowCount() - 1), {ModifiedRole});
        });
    }
    refreshIssues();
    endResetModel();
}

void CardModel::refreshIssues()
{
    m_issueCounts.clear();
    if (!m_doc)
        return;
    m_issueCounts.resize(m_doc->count());
    for (int i = 0; i < m_doc->count(); ++i)
        recountIssues(i);
    if (m_doc->count() > 0)
        emit dataChanged(index(0), index(m_doc->count() - 1), {ErrorsRole, WarningsRole});
}

void CardModel::recountIssues(int row)
{
    if (!m_doc || row < 0 || row >= m_doc->count())
        return;
    if (m_issueCounts.size() < m_doc->count())
        m_issueCounts.resize(m_doc->count());
    int e = 0, w = 0;
    const QString cls = m_doc->link().classKey;
    if (!cls.isEmpty() && m_project->registry().get(cls)) {
        Validator val(m_project->schema());
        for (const Issue &i : val.validateItem(m_doc->item(row).value, cls, row, m_doc->link().keyField))
            (i.error ? e : w)++;
    }
    m_issueCounts[row] = qMakePair(e, w);
}

int CardModel::rowCount(const QModelIndex &parent) const
{
    return parent.isValid() || !m_doc ? 0 : m_doc->count();
}

QPixmap CardModel::thumbnail(int row) const
{
    const JValue &v = m_doc->item(row).value;
    QString path;
    for (const JMember &m : v.members())
        if (m.value.isString() && isImagePath(m.value.toString())) {
            path = m.value.toString();
            break;
        }
    if (path.isEmpty())
        return QPixmap();
    auto it = m_thumbs.constFind(path);
    if (it != m_thumbs.cend())
        return it.value();
    QPixmap pm;
    const QString abs = m_project->toAbs(path);
    if (QFileInfo::exists(abs)) {
        QPixmap src(abs);
        if (!src.isNull())
            pm = src.scaled(44, 44, Qt::KeepAspectRatio, src.width() <= 44 ? Qt::FastTransformation : Qt::SmoothTransformation);
    }
    m_thumbs.insert(path, pm);
    return pm;
}

QVariant CardModel::data(const QModelIndex &index, int role) const
{
    if (!m_doc || !index.isValid() || index.row() >= m_doc->count())
        return QVariant();
    const int row = index.row();
    const DocItem &it = m_doc->item(row);
    switch (role) {
    case Qt::DisplayRole:
    case TitleRole:
        return m_doc->itemTitle(row);
    case SubtitleRole: {
        const QString declared = m_doc->link().classKey;
        if (declared.isEmpty())
            return tr("not linked");
        const QString actual = m_project->schema().actualClass(it.value, declared);
        QString s = m_project->schema().displayName(actual);
        if (m_doc->shape() == JsonDocument::Shape::Dictionary) {
            // For dictionary files the title is the key; show a display name when there is one.
            for (const char *f : {"display_name", "name", "title"}) {
                const JValue v = it.value.value(QLatin1String(f));
                if (v.isString() && !v.toString().isEmpty())
                    return v.toString() + QStringLiteral(" · ") + s;
            }
        }
        return s;
    }
    case SummaryRole: {
        QStringList out;
        if (!it.value.isObject())
            return QStringList{tr("not an object: %1").arg(it.value.preview(30))};
        for (const JMember &m : it.value.members()) {
            if (titleFields().contains(m.key) || (m.value.isString() && isImagePath(m.value.toString())))
                continue;
            if (m.value.isContainer() && m.value.size() == 0)
                continue;
            out << QStringLiteral("%1: %2").arg(m.key, m.value.isObject() ? QStringLiteral("{…}") : m.value.preview(18));
            if (out.size() == 3)
                break;
        }
        return out;
    }
    case ThumbRole:
        return thumbnail(row);
    case ErrorsRole:
        return row < m_issueCounts.size() ? m_issueCounts[row].first : 0;
    case WarningsRole:
        return row < m_issueCounts.size() ? m_issueCounts[row].second : 0;
    case ModifiedRole:
        return m_doc->isItemModified(row);
    case SearchRole: {
        QString s = m_doc->itemTitle(row) + QLatin1Char(' ') + it.key;
        for (const JMember &m : it.value.members())
            if (!m.value.isContainer())
                s += QLatin1Char(' ') + m.value.preview(0);
        return s;
    }
    case Qt::ToolTipRole: {
        const int e = data(index, ErrorsRole).toInt();
        const int w = data(index, WarningsRole).toInt();
        if (e || w)
            return tr("%1 error(s), %2 warning(s)").arg(e).arg(w);
        return QVariant();
    }
    default:
        return QVariant();
    }
}

void CardFilter::setOnlyProblems(bool v)
{
#if QT_VERSION >= QT_VERSION_CHECK(6, 10, 0)
    beginFilterChange();
    m_onlyProblems = v;
    endFilterChange();
#else
    m_onlyProblems = v;
    invalidateFilter();
#endif
}

bool CardFilter::filterAcceptsRow(int row, const QModelIndex &parent) const
{
    const QModelIndex i = sourceModel()->index(row, 0, parent);
    if (m_onlyProblems && i.data(CardModel::ErrorsRole).toInt() == 0 && i.data(CardModel::WarningsRole).toInt() == 0)
        return false;
    const QString needle = filterRegularExpression().pattern();
    if (needle.isEmpty())
        return true;
    return i.data(CardModel::SearchRole).toString().contains(needle, Qt::CaseInsensitive);
}

QSize CardDelegate::sizeHint(const QStyleOptionViewItem &opt, const QModelIndex &) const
{
    const int lh = opt.fontMetrics.height();
    return QSize(220, qMax(64, lh * 4 + 14));
}

void CardDelegate::paint(QPainter *p, const QStyleOptionViewItem &opt, const QModelIndex &index) const
{
    p->save();
    p->setRenderHint(QPainter::Antialiasing);
    const QPalette &pal = opt.palette;
    const bool selected = opt.state & QStyle::State_Selected;
    const bool hover = opt.state & QStyle::State_MouseOver;
    const QRectF card = QRectF(opt.rect).adjusted(4, 3, -4, -3);

    QColor bg = pal.color(QPalette::Base);
    if (hover)
        bg = bg.lightness() > 128 ? bg.darker(104) : bg.lighter(115);
    QColor border = selected ? pal.color(QPalette::Highlight) : pal.color(QPalette::Mid);
    QPainterPath path;
    path.addRoundedRect(card, 7, 7);
    p->fillPath(path, bg);
    p->setPen(QPen(border, selected ? 2.0 : 1.0));
    p->drawPath(path);

    const int errors = index.data(CardModel::ErrorsRole).toInt();
    const int warnings = index.data(CardModel::WarningsRole).toInt();
    if (errors || warnings) {
        // Left accent stripe
        QPainterPath stripe;
        stripe.addRoundedRect(QRectF(card.left(), card.top(), 5, card.height()), 2, 2);
        p->fillPath(stripe, errors ? QColor(0xe5, 0x53, 0x4b) : QColor(0xd2, 0x99, 0x22));
    }

    // Thumbnail or initial
    const QRect thumbRect(int(card.left()) + 10, int(card.top()) + (int(card.height()) - 44) / 2, 44, 44);
    const QPixmap pm = index.data(CardModel::ThumbRole).value<QPixmap>();
    const QString title = index.data(CardModel::TitleRole).toString();
    if (!pm.isNull()) {
        const QPoint c = thumbRect.center() - QPoint(pm.width() / 2, pm.height() / 2);
        p->drawPixmap(c, pm);
    } else {
        const QColor tint = QColor::fromHsv(int(qHash(title) % 360), 90, 200);
        QPainterPath circle;
        circle.addEllipse(QRectF(thumbRect).adjusted(4, 4, -4, -4));
        p->fillPath(circle, tint);
        QFont f = opt.font;
        f.setBold(true);
        f.setPointSizeF(f.pointSizeF() * 1.3);
        p->setFont(f);
        p->setPen(Qt::white);
        p->drawText(thumbRect, Qt::AlignCenter, title.left(1).toUpper());
    }

    // Text
    const int textLeft = thumbRect.right() + 10;
    const int right = int(card.right()) - 8;
    QFont tf = opt.font;
    tf.setBold(true);
    p->setFont(tf);
    p->setPen(pal.color(QPalette::Text));
    const QFontMetrics tfm(tf);
    int y = int(card.top()) + 6;
    QString badge;
    if (errors)
        badge = QString::number(errors);
    const int badgeW = badge.isEmpty() ? 0 : tfm.horizontalAdvance(badge) + 12;
    const bool modified = index.data(CardModel::ModifiedRole).toBool();
    QString shownTitle = tfm.elidedText(title, Qt::ElideRight, right - textLeft - badgeW - (modified ? 14 : 0));
    p->drawText(QRect(textLeft, y, right - textLeft, tfm.height()), Qt::AlignLeft | Qt::AlignVCenter, shownTitle);
    if (modified) {
        p->setBrush(pal.color(QPalette::Highlight));
        p->setPen(Qt::NoPen);
        const int dx = textLeft + tfm.horizontalAdvance(shownTitle) + 6;
        p->drawEllipse(QPointF(dx + 3, y + tfm.height() / 2.0), 3, 3);
    }
    if (!badge.isEmpty()) {
        const QRectF br(right - badgeW, y + 1, badgeW, tfm.height() - 2);
        QPainterPath bp;
        bp.addRoundedRect(br, br.height() / 2, br.height() / 2);
        p->fillPath(bp, QColor(0xe5, 0x53, 0x4b));
        p->setPen(Qt::white);
        p->drawText(br, Qt::AlignCenter, badge);
    }
    y += tfm.height() + 1;

    QFont sf = opt.font;
    sf.setPointSizeF(sf.pointSizeF() * 0.9);
    const QFontMetrics sfm(sf);
    p->setFont(sf);
    QColor dim = pal.color(QPalette::Text);
    dim.setAlphaF(0.65f);
    p->setPen(dim);
    p->drawText(QRect(textLeft, y, right - textLeft, sfm.height()), Qt::AlignLeft | Qt::AlignVCenter,
                sfm.elidedText(index.data(CardModel::SubtitleRole).toString(), Qt::ElideRight, right - textLeft));
    y += sfm.height();
    const QStringList summary = index.data(CardModel::SummaryRole).toStringList();
    dim.setAlphaF(0.85f);
    p->setPen(dim);
    const QString line = summary.join(QStringLiteral("  ·  "));
    p->drawText(QRect(textLeft, y, right - textLeft, sfm.height() * 2), Qt::AlignLeft | Qt::AlignTop | Qt::TextWordWrap,
                sfm.elidedText(line, Qt::ElideRight, (right - textLeft) * 2 - 20));
    p->restore();
}
