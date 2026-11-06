#include "JsonDocument.h"
#include "core/JsonIO.h"

#include <QFile>
#include <QFileInfo>
#include <QSaveFile>
#include <QUndoStack>

JsonDocument::JsonDocument(const QString &absPath, const QString &resPath, QObject *parent)
    : QObject(parent), m_absPath(absPath), m_resPath(resPath), m_undo(new QUndoStack(this))
{
    connect(m_undo, &QUndoStack::cleanChanged, this, [this](bool clean) {
        if (clean)
            m_modifiedItems.clear();
        emit modifiedChanged(!clean);
    });
}

QString JsonDocument::fileName() const
{
    return QFileInfo(m_absPath).fileName();
}

bool JsonDocument::load(QString *error)
{
    QFile f(m_absPath);
    if (!f.open(QIODevice::ReadOnly)) {
        if (error)
            *error = f.errorString();
        return false;
    }
    const QString text = QString::fromUtf8(f.readAll());
    m_fileTime = QFileInfo(m_absPath).lastModified();
    if (text.trimmed().isEmpty()) {
        m_root = JValue::object();
        m_root.setLayout(JValue::LayoutMultiline);
        return true;
    }
    JsonParseError perr;
    m_root = JsonIO::parse(text, &perr);
    if (!perr.ok()) {
        if (error)
            *error = QStringLiteral("Line %1, column %2: %3").arg(perr.line).arg(perr.column).arg(perr.message);
        return false;
    }
    m_indent = JsonIO::detectIndent(text);
    m_trailingNewline = text.endsWith(QLatin1Char('\n'));
    return true;
}

JsonDocument::Shape JsonDocument::detectShape(const JValue &root, const QStringList &classFields)
{
    if (root.isArray())
        return Shape::Array;
    if (!root.isObject() || root.size() == 0)
        return Shape::Dictionary;
    // An object whose keys are mostly class fields is a single item.
    int matches = 0;
    bool allObjects = true;
    for (const JMember &m : root.members()) {
        if (classFields.contains(m.key))
            ++matches;
        if (!m.value.isObject())
            allObjects = false;
    }
    if (!allObjects)
        return Shape::Single;
    return matches * 2 > root.size() ? Shape::Single : Shape::Dictionary;
}

QString JsonDocument::shapeName(Shape s)
{
    switch (s) {
    case Shape::Array: return QStringLiteral("array");
    case Shape::Single: return QStringLiteral("single");
    default: return QStringLiteral("dictionary");
    }
}

JsonDocument::Shape JsonDocument::shapeFromName(const QString &s, Shape def)
{
    if (s == QLatin1String("array")) return Shape::Array;
    if (s == QLatin1String("single")) return Shape::Single;
    if (s == QLatin1String("dictionary")) return Shape::Dictionary;
    return def;
}

void JsonDocument::applyLink(const LinkInfo &link, const QStringList &classFields)
{
    m_link = link;
    m_shape = shapeFromName(link.shape, detectShape(m_root, classFields));
    // The file content must fit the shape; fall back to what it really is.
    if (m_shape == Shape::Array && !m_root.isArray())
        m_shape = m_root.isObject() ? detectShape(m_root, classFields) : Shape::Array;
    if (m_shape != Shape::Array && m_root.isArray())
        m_shape = Shape::Array;
    if (link.plain && !m_root.isContainer())
        m_shape = Shape::Single; // a bare value: the whole file is the item
    m_link.shape = shapeName(m_shape);

    m_items.clear();
    switch (m_shape) {
    case Shape::Array:
        for (const JValue &v : m_root.elements())
            m_items << DocItem{QString(), v};
        break;
    case Shape::Dictionary:
        for (const JMember &m : m_root.members())
            m_items << DocItem{m.key, m.value};
        break;
    case Shape::Single: {
        // Plain JSON files may hold any value at the root (a number, a string…).
        JValue v = m_root.isObject() || link.plain ? m_root : JValue::object();
        m_items << DocItem{QString(), v};
        break;
    }
    }
    m_modifiedItems.clear();
    emit itemsReset();
}

JValue JsonDocument::buildRoot() const
{
    switch (m_shape) {
    case Shape::Array: {
        JValue a = JValue::array();
        a.setLayout(m_root.isArray() ? m_root.layout() : JValue::LayoutMultiline);
        for (const DocItem &i : m_items)
            a.append(i.value);
        return a;
    }
    case Shape::Dictionary: {
        JValue o = JValue::object();
        o.setLayout(m_root.isObject() && m_root.size() > 0 ? m_root.layout() : JValue::LayoutMultiline);
        for (const DocItem &i : m_items)
            o.set(i.key, i.value);
        return o;
    }
    case Shape::Single:
        return m_items.isEmpty() ? JValue::object() : m_items.first().value;
    }
    return JValue();
}

bool JsonDocument::save(bool backup, QString *error)
{
    const JValue root = buildRoot();
    JsonIO::WriteOptions o;
    o.indent = m_indent;
    o.expandDepth = m_shape == Shape::Single ? 1 : 2;
    o.trailingNewline = m_trailingNewline;
    const QByteArray data = JsonIO::write(root, o).toUtf8();

    if (backup && QFile::exists(m_absPath)) {
        const QString bak = m_absPath + QStringLiteral(".bak");
        QFile::remove(bak);
        if (!QFile::copy(m_absPath, bak)) {
            if (error)
                *error = QStringLiteral("Could not write backup %1").arg(bak);
            return false;
        }
    }
    QSaveFile f(m_absPath);
    if (!f.open(QIODevice::WriteOnly)) {
        if (error)
            *error = f.errorString();
        return false;
    }
    f.write(data);
    if (!f.commit()) {
        if (error)
            *error = f.errorString();
        return false;
    }
    m_root = root;
    m_fileTime = QFileInfo(m_absPath).lastModified();
    m_undo->setClean();
    m_modifiedItems.clear();
    return true;
}

bool JsonDocument::changedOnDisk() const
{
    const QFileInfo fi(m_absPath);
    return fi.exists() && fi.lastModified() != m_fileTime;
}

QString JsonDocument::itemTitle(int i) const
{
    if (i < 0 || i >= m_items.size())
        return QString();
    const DocItem &it = m_items[i];
    if (m_shape == Shape::Dictionary)
        return it.key;
    static const char *fields[] = {"id", "name", "display_name", "title", "key", "label"};
    for (const char *f : fields) {
        const JValue v = it.value.value(QLatin1String(f));
        if (v.isString() && !v.toString().isEmpty())
            return v.toString();
    }
    if (m_shape == Shape::Single)
        return QFileInfo(m_absPath).completeBaseName();
    return QStringLiteral("Item %1").arg(i);
}

QString JsonDocument::uniqueKey(const QString &base) const
{
    auto taken = [this](const QString &k) {
        for (const DocItem &i : m_items)
            if (i.key == k)
                return true;
        return false;
    };
    QString b = base.isEmpty() ? QStringLiteral("new_item") : base;
    if (!taken(b))
        return b;
    // strip a trailing _N
    const int us = b.lastIndexOf(QLatin1Char('_'));
    bool num = false;
    if (us > 0)
        b.mid(us + 1).toInt(&num);
    if (num)
        b = b.left(us);
    for (int n = 2;; ++n) {
        const QString k = QStringLiteral("%1_%2").arg(b).arg(n);
        if (!taken(k))
            return k;
    }
}

JValue JsonDocument::valueAt(int item, const JPath &path) const
{
    if (item < 0 || item >= m_items.size())
        return JValue();
    return getAt(m_items[item].value, path);
}

void JsonDocument::setValueAt(int item, const JPath &path, const JValue &v)
{
    if (item < 0 || item >= m_items.size())
        return;
    if (path.isEmpty())
        m_items[item].value = v.isUndefined() ? JValue::object() : v;
    else
        setAt(m_items[item].value, path, v);
    m_modifiedItems.insert(item);
    emit itemChanged(item);
}

void JsonDocument::setItemKey(int item, const QString &key)
{
    if (item < 0 || item >= m_items.size())
        return;
    m_items[item].key = key;
    m_modifiedItems.insert(item);
    emit itemChanged(item);
}

void JsonDocument::replaceItems(const QList<DocItem> &items)
{
    m_items = items;
    for (int i = 0; i < m_items.size(); ++i)
        m_modifiedItems.insert(i);
    emit itemsReset();
}

bool JsonDocument::isModified() const
{
    return !m_undo->isClean();
}
