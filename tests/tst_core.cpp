#include "core/JsonIO.h"
#include "model/ClassRegistry.h"
#include "model/CSharpParser.h"
#include "model/GdScriptParser.h"
#include "model/JsonDocument.h"
#include "model/Schema.h"
#include "model/Validator.h"

#include <QTemporaryDir>
#include <QtTest>

class TstCore : public QObject
{
    Q_OBJECT

private:
    ClassRegistry reg;
    ProjectConfig cfg;

    QString demo(const QString &rel) const { return QStringLiteral(GIA_TEST_DATA "/") + rel; }

    JValue parse(const QString &s)
    {
        JsonParseError e;
        JValue v = JsonIO::parse(s, &e);
        if (!e.ok())
            qWarning() << e.message;
        return v;
    }

    QList<Issue> validate(const QString &json, const QString &cls)
    {
        Schema schema(&reg, &cfg);
        Validator val(schema);
        return val.validateItem(parse(json), cls, 0);
    }

private slots:
    void initTestCase()
    {
        reg.setProjectRoot(QStringLiteral(GIA_TEST_DATA));
        reg.scanProject();
        QVERIFY(cfg.load(QStringLiteral(GIA_TEST_DATA)));
    }

    void jsonRoundTripKeepsLayout()
    {
        const QString src = QStringLiteral(
            "{\n\t\"b\": {\"x\": 1, \"y\": 2.0},\n\t\"a\": [\n\t\t{ \"type\": \"k\", \"force\": 150 }\n\t],\n"
            "\t\"s\": \"q\\\"\\n\"\n}\n");
        const JValue v = parse(src);
        QCOMPARE(v.keys(), QStringList({QStringLiteral("b"), QStringLiteral("a"), QStringLiteral("s")}));
        JsonIO::WriteOptions o;
        QCOMPARE(JsonIO::write(v, o), src);
    }

    void jsonNumbers()
    {
        QCOMPARE(JsonIO::formatNumber(JValue(2.0, false)), QStringLiteral("2.0"));
        QCOMPARE(JsonIO::formatNumber(JValue(qint64(2))), QStringLiteral("2"));
        QCOMPARE(JsonIO::formatNumber(JValue(0.15, false)), QStringLiteral("0.15"));
        JsonParseError e;
        JsonIO::parse(QStringLiteral("{\"a\": 1,}"), &e);
        QVERIFY(!e.ok());
        QCOMPARE(e.line, 1);
    }

    void gdscriptParsing()
    {
        const ClassInfo *item = reg.get(QStringLiteral("ItemData"));
        QVERIFY(item);
        QCOMPARE(item->baseKey, QStringLiteral("Resource"));
        QVERIFY(item->enums.contains(QStringLiteral("Rarity")));
        QCOMPARE(item->enums.value(QStringLiteral("Rarity")).values.last().second, qint64(10));

        const ResolvedProperty *price = reg.property(QStringLiteral("ItemData"), QStringLiteral("price"));
        QVERIFY(price);
        QCOMPARE(price->type.kind, VKind::Int);
        QCOMPARE(price->defaultValue.toInt(), qint64(15)); // resolved through DEFAULT_PRICE
        QVERIFY(price->info.hint.orGreater);
        QCOMPARE(price->info.hint.max, 999.0);
        QCOMPARE(price->info.doc, QStringLiteral("What it costs in a shop."));

        const ResolvedProperty *rarity = reg.property(QStringLiteral("ItemData"), QStringLiteral("rarity"));
        QVERIFY(rarity && !rarity->type.enumKey.isEmpty());

        const ResolvedProperty *effects = reg.property(QStringLiteral("ItemData"), QStringLiteral("effects"));
        QVERIFY(effects && effects->type.kind == VKind::Array && effects->type.elem);
        QCOMPARE(effects->type.elem->className, QStringLiteral("ItemEffect"));
        QCOMPARE(effects->info.group, QStringLiteral("Stats"));

        const ResolvedProperty *parts = reg.property(QStringLiteral("ItemData"), QStringLiteral("sprite_parts"));
        QVERIFY(parts && parts->type.elem);
        QCOMPARE(parts->type.elem->className, QStringLiteral("ItemData.SpritePart"));
        QVERIFY(reg.property(QStringLiteral("ItemData.SpritePart"), QStringLiteral("behind")));

        // Only exported vars by default (plus the builtin inherited ones).
        QVERIFY(!reg.property(QStringLiteral("ItemData"), QStringLiteral("_cached_owner")));
        QVERIFY(!reg.property(QStringLiteral("ItemData"), QStringLiteral("id")));
        QVERIFY(reg.property(QStringLiteral("ItemData"), QStringLiteral("resource_name")));

        const ResolvedProperty *offset = reg.property(QStringLiteral("ItemData"), QStringLiteral("pickup_offset"));
        QVERIFY(offset && offset->type.kind == VKind::Vector2);
        QCOMPARE(JsonIO::writeCompact(offset->defaultValue), QStringLiteral("[0, -8]"));

        QVERIFY(reg.inherits(QStringLiteral("GrantAttackEffect"), QStringLiteral("ItemEffect")));
        QVERIFY(reg.descendants(QStringLiteral("ItemEffect")).contains(QStringLiteral("KnockbackEffect")));
    }

    void gdscriptEdgeCases()
    {
        const QString src = QStringLiteral(
            "extends \"res://base.gd\"\n"
            "@export_range(0, 10,\n    0.5) var a: float = 2.5:\n\tset(v):\n\t\ta = v\n"
            "@export var d: Dictionary = {\"k\": 1, \"j\": [1, 2]}\n"
            "@onready var node := $Sprite\n"
            "static var counter := 0\n"
            "const X = 3\n"
            "var arr: Array[int] = [1, X]\n"
            "func f():\n\tvar local = 1\n");
        GdScriptParser::Options o;
        o.scope = VarScope::Public;
        const QList<ClassPtr> classes = GdScriptParser::parse(src, QStringLiteral("res://x.gd"), o);
        QCOMPARE(classes.size(), 1);
        const ClassInfo &c = *classes.first();
        QCOMPARE(c.baseRaw, QStringLiteral("\"res://base.gd\""));
        QStringList names;
        for (const PropertyInfo &p : c.properties)
            names << p.name;
        QCOMPARE(names, QStringList({QStringLiteral("a"), QStringLiteral("d"), QStringLiteral("arr")}));
        QCOMPARE(c.properties[0].hint.step, 0.5);
        QCOMPARE(c.properties[0].defaultExpr, QStringLiteral("2.5"));
        QCOMPARE(c.properties[1].defaultExpr, QStringLiteral("{\"k\": 1, \"j\": [1, 2]}"));
    }

    void variableScopes()
    {
        const QString gd = QStringLiteral("@export var a := 1\nvar b := 2\nvar _c := 3\n@export var _d := 4\n");
        auto gdNames = [&](VarScope s) {
            GdScriptParser::Options o;
            o.scope = s;
            const QList<ClassPtr> classes = GdScriptParser::parse(gd, QStringLiteral("res://s.gd"), o);
            QStringList names;
            for (const PropertyInfo &p : classes.first()->properties)
                names << p.name;
            return names;
        };
        QCOMPARE(gdNames(VarScope::Exported), QStringList({QStringLiteral("a"), QStringLiteral("_d")}));
        QCOMPARE(gdNames(VarScope::Public), QStringList({QStringLiteral("a"), QStringLiteral("b"), QStringLiteral("_d")}));
        QCOMPARE(gdNames(VarScope::All), QStringList({QStringLiteral("a"), QStringLiteral("b"), QStringLiteral("_c"),
                                                      QStringLiteral("_d")}));

        const QString cs = QStringLiteral("public partial class S : Resource {\n"
                                          "  [Export] public int A { get; set; }\n"
                                          "  public int B;\n"
                                          "  private int _c;\n"
                                          "}\n");
        auto csNames = [&](VarScope s) {
            CSharpParser::Options o;
            o.scope = s;
            QStringList names;
            const QList<ClassPtr> classes = CSharpParser::parse(cs, QStringLiteral("res://S.cs"), o);
            if (!classes.isEmpty())
                for (const PropertyInfo &p : classes.first()->properties)
                    names << p.name;
            return names;
        };
        QCOMPARE(csNames(VarScope::Exported), QStringList({QStringLiteral("A")}));
        QCOMPARE(csNames(VarScope::Public), QStringList({QStringLiteral("A"), QStringLiteral("B")}));
        QCOMPARE(csNames(VarScope::All), QStringList({QStringLiteral("A"), QStringLiteral("B"), QStringLiteral("_c")}));

        QVERIFY(varScopeFromName(varScopeName(VarScope::Public)) == VarScope::Public);
        QVERIFY(varScopeFromName(QStringLiteral("bogus"), VarScope::All) == VarScope::All);
    }

    void csharpParsing()
    {
        const ClassInfo *w = reg.get(QStringLiteral("WeaponData"));
        QVERIFY(w);
        QCOMPARE(w->baseKey, QStringLiteral("Resource"));
        QStringList names;
        for (const PropertyInfo &p : w->properties)
            names << p.name;
        QCOMPARE(names, QStringList({QStringLiteral("DisplayName"), QStringLiteral("Damage"), QStringLiteral("Cooldown"),
                                     QStringLiteral("Type"), QStringLiteral("Icon"), QStringLiteral("Trail"),
                                     QStringLiteral("Tags")}));
        const ResolvedProperty *dmg = reg.property(QStringLiteral("WeaponData"), QStringLiteral("Damage"));
        QVERIFY(dmg && dmg->info.hint.kind == PropertyHint::Range && dmg->info.hint.max == 200.0);
        QCOMPARE(dmg->defaultValue.toInt(), qint64(10));
        const ResolvedProperty *type = reg.property(QStringLiteral("WeaponData"), QStringLiteral("Type"));
        QVERIFY(type && !type->type.enumKey.isEmpty());
        const ResolvedProperty *tags = reg.property(QStringLiteral("WeaponData"), QStringLiteral("Tags"));
        QVERIFY(tags && tags->type.kind == VKind::Array && tags->type.elem->kind == VKind::String);
        QCOMPARE(reg.property(QStringLiteral("WeaponData"), QStringLiteral("Icon"))->info.group, QStringLiteral("Visuals"));
    }

    void autoTypeNames()
    {
        QCOMPARE(Schema::autoTypeName(QStringLiteral("AlterStatEffect"), QStringLiteral("ItemEffect")), QStringLiteral("alter_stat"));
        QCOMPARE(Schema::autoTypeName(QStringLiteral("GrantAttackEffect"), QStringLiteral("ItemEffect")), QStringLiteral("grant_attack"));
        QCOMPARE(Schema::autoTypeName(QStringLiteral("HTTPRequestData"), QString()), QStringLiteral("http_request_data"));
    }

    void validItemHasNoErrors()
    {
        const QList<Issue> issues = validate(QStringLiteral(
            "{\"display_name\": \"A\", \"rarity\": 10, \"price\": 5000, \"stats\": {\"damage\": 3},"
            " \"effects\": [{\"type\": \"grant_attack\", \"effect\": {\"type\": \"knockback\", \"force\": 300}}],"
            " \"sprite_parts\": [{\"offset\": [1, 2]}], \"tint\": \"#ff0000\", \"elements\": 3}"),
            QStringLiteral("ItemData"));
        for (const Issue &i : issues)
            QVERIFY2(!i.error, qPrintable(pathToString(i.path) + QStringLiteral(": ") + i.message));
    }

    void invalidItemIsReported()
    {
        const QList<Issue> issues = validate(QStringLiteral(
            "{\"rarity\": 7, \"drop_chance\": 3.5, \"category\": \"Shoes\", \"weight\": 4, \"stack_size\": 1.5,"
            " \"stats\": {\"damage\": \"lots\"}, \"effects\": [{\"type\": \"teleport\"}], \"elements\": 64}"),
            QStringLiteral("ItemData"));
        QStringList paths;
        for (const Issue &i : issues)
            if (i.error)
                paths << pathToString(i.path);
        QVERIFY(paths.contains(QStringLiteral("rarity")));
        QVERIFY(paths.contains(QStringLiteral("drop_chance")));
        QVERIFY(paths.contains(QStringLiteral("category")));
        QVERIFY(paths.contains(QStringLiteral("weight")));
        QVERIFY(paths.contains(QStringLiteral("stack_size")));
        QVERIFY(paths.contains(QStringLiteral("stats.damage")));
        QVERIFY(paths.contains(QStringLiteral("effects[0].type")));
        QVERIFY(paths.contains(QStringLiteral("elements")));
    }

    void sanitizeFixesEverything()
    {
        Schema schema(&reg, &cfg);
        Validator val(schema);
        const JValue bad = parse(QStringLiteral(
            "{\"price\": -10, \"drop_chance\": 3.5, \"weight\": 4, \"stats\": {\"damage\": \"lots\", \"armor\": 500}}"));
        const JValue fixed = val.sanitizeObject(bad, QStringLiteral("ItemData"));
        QCOMPARE(JsonIO::writeCompact(fixed),
                 QStringLiteral("{\"price\": 0, \"drop_chance\": 1.0, \"stats\": {\"armor\": 100}}"));
        for (const Issue &i : val.validateItem(fixed, QStringLiteral("ItemData"), 0))
            QVERIFY(!i.error);
    }

    void documentShapes()
    {
        QStringList fields;
        for (const ResolvedProperty &p : reg.properties(QStringLiteral("ItemData")))
            fields << p.info.name;
        QCOMPARE(JsonDocument::detectShape(parse(QStringLiteral("{\"a\": {}, \"b\": {}}")), fields),
                 JsonDocument::Shape::Dictionary);
        QCOMPARE(JsonDocument::detectShape(parse(QStringLiteral("{\"price\": 1, \"stats\": {}}")), fields),
                 JsonDocument::Shape::Single);
        QCOMPARE(JsonDocument::detectShape(parse(QStringLiteral("[]")), fields), JsonDocument::Shape::Array);
    }

    void documentSaveRoundTrip()
    {
        QTemporaryDir dir;
        const QString path = dir.filePath(QStringLiteral("items.json"));
        QVERIFY(QFile::copy(demo(QStringLiteral("data/items.json")), path));
        QFile::setPermissions(path, QFile::ReadOwner | QFile::WriteOwner);
        QFile orig(path);
        QVERIFY(orig.open(QIODevice::ReadOnly));
        const QByteArray before = orig.readAll();
        orig.close();

        JsonDocument doc(path, QStringLiteral("res://data/items.json"));
        QString err;
        QVERIFY(doc.load(&err));
        LinkInfo l;
        l.classKey = QStringLiteral("ItemData");
        doc.applyLink(l, {});
        QCOMPARE(doc.count(), 4);
        QVERIFY(doc.save(true, &err));
        QFile after(path);
        QVERIFY(after.open(QIODevice::ReadOnly));
        QCOMPARE(after.readAll(), before);
        QVERIFY(QFile::exists(path + QStringLiteral(".bak")));
    }

    void plainDocuments()
    {
        QTemporaryDir dir;
        auto write = [&](const QString &name, const QByteArray &text) {
            QFile f(dir.filePath(name));
            QVERIFY(f.open(QIODevice::WriteOnly));
            f.write(text);
        };
        auto open = [&](const QString &name, JsonDocument &doc) {
            QString err;
            QVERIFY(doc.load(&err));
            LinkInfo l;
            l.plain = true;
            doc.applyLink(l, {});
            Q_UNUSED(name);
        };

        // A bare value at the root is one item and is written back unchanged.
        write(QStringLiteral("n.json"), "42\n");
        JsonDocument num(dir.filePath(QStringLiteral("n.json")), QStringLiteral("res://n.json"));
        open(QStringLiteral("n.json"), num);
        QCOMPARE(num.shape(), JsonDocument::Shape::Single);
        QCOMPARE(num.count(), 1);
        QCOMPARE(num.item(0).value.toInt(), qint64(42));
        QString err;
        QVERIFY(num.save(false, &err));
        QFile nf(dir.filePath(QStringLiteral("n.json")));
        QVERIFY(nf.open(QIODevice::ReadOnly));
        QCOMPARE(nf.readAll().trimmed(), QByteArray("42"));

        // Arrays of anything are split into items.
        write(QStringLiteral("a.json"), "[1, \"two\", {\"x\": 3}]\n");
        JsonDocument arr(dir.filePath(QStringLiteral("a.json")), QStringLiteral("res://a.json"));
        open(QStringLiteral("a.json"), arr);
        QCOMPARE(arr.shape(), JsonDocument::Shape::Array);
        QCOMPARE(arr.count(), 3);

        // An object of mixed values is a single item.
        write(QStringLiteral("o.json"), "{\"volume\": 0.5, \"name\": \"x\"}\n");
        JsonDocument obj(dir.filePath(QStringLiteral("o.json")), QStringLiteral("res://o.json"));
        open(QStringLiteral("o.json"), obj);
        QCOMPARE(obj.shape(), JsonDocument::Shape::Single);

        // The plain flag survives ginspector.json.
        ProjectConfig cfg;
        QVERIFY(cfg.load(dir.path()));
        LinkInfo l;
        l.json = QStringLiteral("res://o.json");
        l.plain = true;
        l.shape = QStringLiteral("single");
        cfg.links.insert(l.json, l);
        cfg.varScope = VarScope::Public;
        QVERIFY(cfg.save());
        ProjectConfig again;
        QVERIFY(again.load(dir.path()));
        QVERIFY(again.links.contains(l.json));
        QVERIFY(again.links.value(l.json).plain);
        QVERIFY(again.links.value(l.json).classKey.isEmpty());
        QVERIFY(again.varScope == VarScope::Public);
    }
};

QTEST_GUILESS_MAIN(TstCore)
#include "tst_core.moc"
