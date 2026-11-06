#include "ProjectConfig.h"
#include "core/JsonIO.h"

#include <QDir>
#include <QFile>
#include <QSaveFile>

QString ProjectConfig::filePath() const
{
    return QDir(root).filePath(fileName());
}

bool ProjectConfig::load(const QString &projectRoot, QString *error)
{
    root = projectRoot;
    links.clear();
    poly.clear();
    format = ValueFormat();
    varScope = VarScope::Exported;
    backups = true;

    QFile f(filePath());
    if (!f.exists())
        return true; // created on first save
    if (!f.open(QIODevice::ReadOnly)) {
        if (error)
            *error = f.errorString();
        return false;
    }
    JsonParseError perr;
    const JValue doc = JsonIO::parse(QString::fromUtf8(f.readAll()), &perr);
    if (!perr.ok()) {
        if (error)
            *error = QStringLiteral("%1 (line %2)").arg(perr.message).arg(perr.line);
        return false;
    }
    const JValue settings = doc.value(QStringLiteral("settings"));
    format.vector = ValueFormat::vectorFromName(settings.value(QStringLiteral("vector_format")).toString());
    format.color = ValueFormat::colorFromName(settings.value(QStringLiteral("color_format")).toString());
    // "include_private_vars" is the setting of older versions; "variables" replaces it.
    const VarScope legacy = settings.value(QStringLiteral("include_private_vars")).toBool(false) ? VarScope::All
                                                                                              : VarScope::Exported;
    varScope = varScopeFromName(settings.value(QStringLiteral("variables")).toString(), legacy);
    backups = settings.value(QStringLiteral("backups")).toBool(true);

    for (const JMember &m : doc.ref(QStringLiteral("links")).members()) {
        LinkInfo l;
        l.json = m.key;
        l.classKey = m.value.value(QStringLiteral("class")).toString();
        l.scriptPath = m.value.value(QStringLiteral("script")).toString();
        l.shape = m.value.value(QStringLiteral("shape")).toString();
        l.keyField = m.value.value(QStringLiteral("key_field")).toString();
        if (!l.classKey.isEmpty())
            links.insert(l.json, l);
    }
    for (const JMember &m : doc.ref(QStringLiteral("polymorphism")).members()) {
        PolyConfig p;
        p.key = m.value.value(QStringLiteral("key")).toString(QStringLiteral("type"));
        for (const JMember &t : m.value.ref(QStringLiteral("types")).members())
            p.types << qMakePair(t.key, t.value.toString());
        poly.insert(m.key, p);
    }
    return true;
}

bool ProjectConfig::save(QString *error) const
{
    JValue doc = JValue::object();
    doc.set(QStringLiteral("_comment"),
            JValue(QStringLiteral("Created by GInspectorAdvanced. Links JSON data files to the Godot classes that describe them.")));
    doc.set(QStringLiteral("version"), JValue(1));

    JValue settings = JValue::object();
    settings.setLayout(JValue::LayoutInline);
    settings.set(QStringLiteral("vector_format"), JValue(ValueFormat::vectorName(format.vector)));
    settings.set(QStringLiteral("color_format"), JValue(ValueFormat::colorName(format.color)));
    settings.set(QStringLiteral("variables"), JValue(varScopeName(varScope)));
    settings.set(QStringLiteral("backups"), JValue(backups));
    doc.set(QStringLiteral("settings"), settings);

    JValue jl = JValue::object();
    for (const LinkInfo &l : links) {
        JValue o = JValue::object();
        o.setLayout(JValue::LayoutMultiline);
        o.set(QStringLiteral("class"), JValue(l.classKey));
        if (!l.scriptPath.isEmpty())
            o.set(QStringLiteral("script"), JValue(l.scriptPath));
        o.set(QStringLiteral("shape"), JValue(l.shape));
        if (!l.keyField.isEmpty())
            o.set(QStringLiteral("key_field"), JValue(l.keyField));
        jl.set(l.json, o);
    }
    doc.set(QStringLiteral("links"), jl);

    JValue jp = JValue::object();
    for (auto it = poly.cbegin(); it != poly.cend(); ++it) {
        JValue o = JValue::object();
        o.set(QStringLiteral("key"), JValue(it->key));
        JValue types = JValue::object();
        types.setLayout(JValue::LayoutMultiline);
        for (const auto &t : it->types)
            types.set(t.first, JValue(t.second));
        o.set(QStringLiteral("types"), types);
        jp.set(it.key(), o);
    }
    doc.set(QStringLiteral("polymorphism"), jp);

    QSaveFile f(filePath());
    if (!f.open(QIODevice::WriteOnly)) {
        if (error)
            *error = f.errorString();
        return false;
    }
    JsonIO::WriteOptions o;
    o.expandDepth = 2;
    f.write(JsonIO::write(doc, o).toUtf8());
    if (!f.commit()) {
        if (error)
            *error = f.errorString();
        return false;
    }
    return true;
}
