#include "GdScriptParser.h"
#include "ScriptText.h"

#include <QFile>
#include <QFileInfo>

using namespace ScriptText;

namespace {

struct LogicalLine
{
    int indent = 0;
    QString text;      // code without comments, continuation lines joined
    QString doc;       // "##" comment lines directly above (or trailing)
    int line = 0;      // 1-based line number of the first physical line
};

// Splits a GDScript source into logical lines: comments removed, bracketed or
// backslash-continued lines joined, "##" doc comments attached to the next line.
QList<LogicalLine> logicalLines(const QString &src)
{
    QList<LogicalLine> out;
    QStringList pendingDoc;
    LogicalLine cur;
    bool inLine = false;
    int depth = 0;
    QChar quote;
    bool triple = false;
    int lineNo = 1;
    int i = 0;
    const int n = src.size();
    QString trailingDoc;

    auto finishLine = [&]() {
        const QString t = cur.text.trimmed();
        if (!t.isEmpty()) {
            cur.text = t;
            QStringList docLines = pendingDoc;
            if (!trailingDoc.isEmpty())
                docLines << trailingDoc;
            cur.doc = docLines.join(QLatin1Char('\n')).trimmed();
            out << cur;
            pendingDoc.clear();
        }
        trailingDoc.clear();
        cur = LogicalLine();
        inLine = false;
    };

    while (i < n) {
        const QChar c = src[i];
        if (!quote.isNull()) {
            cur.text += c;
            if (c == QLatin1Char('\\') && i + 1 < n) {
                cur.text += src[i + 1];
                if (src[i + 1] == QLatin1Char('\n'))
                    ++lineNo;
                i += 2;
                continue;
            }
            if (c == QLatin1Char('\n'))
                ++lineNo;
            if (triple) {
                if (src.mid(i, 3) == QString(3, quote)) {
                    cur.text += QString(2, quote);
                    i += 3;
                    quote = QChar();
                    continue;
                }
            } else if (c == quote) {
                quote = QChar();
            }
            ++i;
            continue;
        }

        if (!inLine) {
            // Start of a physical line: measure indentation.
            int ind = 0;
            int j = i;
            while (j < n && (src[j] == QLatin1Char(' ') || src[j] == QLatin1Char('\t'))) {
                ind += src[j] == QLatin1Char('\t') ? 4 : 1;
                ++j;
            }
            if (j >= n)
                break;
            if (src[j] == QLatin1Char('\n') || src[j] == QLatin1Char('\r')) {
                // Blank line: breaks a doc comment block from the next declaration.
                if (src[j] == QLatin1Char('\n'))
                    ++lineNo;
                i = j + 1;
                pendingDoc.clear();
                continue;
            }
            if (src[j] == QLatin1Char('#')) {
                // Whole-line comment.
                int e = src.indexOf(QLatin1Char('\n'), j);
                if (e < 0)
                    e = n;
                const QString comment = src.mid(j, e - j);
                if (comment.startsWith(QLatin1String("##")))
                    pendingDoc << comment.mid(2).trimmed();
                else
                    pendingDoc.clear();
                i = e + 1;
                ++lineNo;
                continue;
            }
            cur.indent = ind;
            cur.line = lineNo;
            inLine = true;
            i = j;
            continue;
        }

        if (c == QLatin1Char('"') || c == QLatin1Char('\'')) {
            quote = c;
            triple = src.mid(i, 3) == QString(3, c);
            cur.text += triple ? QString(3, c) : QString(c);
            i += triple ? 3 : 1;
            continue;
        }
        if (c == QLatin1Char('#')) {
            int e = src.indexOf(QLatin1Char('\n'), i);
            if (e < 0)
                e = n;
            const QString comment = src.mid(i, e - i);
            if (comment.startsWith(QLatin1String("##")))
                trailingDoc = comment.mid(2).trimmed();
            i = e; // the newline is handled below
            continue;
        }
        if (c == QLatin1Char('(') || c == QLatin1Char('[') || c == QLatin1Char('{'))
            ++depth;
        else if (c == QLatin1Char(')') || c == QLatin1Char(']') || c == QLatin1Char('}'))
            depth = qMax(0, depth - 1);

        if (c == QLatin1Char('\\') && i + 1 < n && (src[i + 1] == QLatin1Char('\n') || src[i + 1] == QLatin1Char('\r'))) {
            // Explicit line continuation.
            cur.text += QLatin1Char(' ');
            i += src[i + 1] == QLatin1Char('\r') ? 3 : 2;
            ++lineNo;
            continue;
        }
        if (c == QLatin1Char('\n')) {
            ++lineNo;
            if (depth > 0) {
                cur.text += QLatin1Char(' ');
                ++i;
                // Skip the next line's indentation.
                while (i < n && (src[i] == QLatin1Char(' ') || src[i] == QLatin1Char('\t')))
                    ++i;
                continue;
            }
            finishLine();
            ++i;
            continue;
        }
        if (c == QLatin1Char('\r')) {
            ++i;
            continue;
        }
        cur.text += c;
        ++i;
    }
    if (inLine)
        finishLine();
    return out;
}

struct Annotation
{
    QString name;
    QStringList args; // raw argument expressions
};

// Consumes leading "@name(args)" annotations of a line.
QList<Annotation> takeAnnotations(QString &text)
{
    QList<Annotation> out;
    int pos = 0;
    while (true) {
        skipSpaces(text, pos);
        if (pos >= text.size() || text[pos] != QLatin1Char('@'))
            break;
        ++pos;
        Annotation a;
        a.name = readIdent(text, pos);
        if (pos < text.size() && text[pos] == QLatin1Char('(')) {
            const int end = matchBracket(text, pos);
            if (end < 0)
                break;
            a.args = splitTopLevel(text.mid(pos + 1, end - pos - 2));
            pos = end;
        }
        out << a;
    }
    text = text.mid(pos).trimmed();
    return out;
}

QString argString(const Annotation &a, int i)
{
    if (i >= a.args.size())
        return QString();
    QString s;
    if (unquote(a.args[i], &s))
        return s;
    return a.args[i].trimmed();
}

bool argNumber(const Annotation &a, int i, double *out)
{
    if (i >= a.args.size())
        return false;
    QString t = a.args[i].trimmed();
    bool ok = false;
    double d = t.toDouble(&ok);
    if (!ok) {
        // Common constants used in ranges.
        const bool neg = t.startsWith(QLatin1Char('-'));
        const QString body = neg ? t.mid(1).trimmed() : t;
        if (body == QLatin1String("PI")) { d = 3.14159265358979323846; ok = true; }
        else if (body == QLatin1String("TAU")) { d = 6.28318530717958647692; ok = true; }
        else if (body == QLatin1String("INF")) { d = 1e300; ok = true; }
        if (ok && neg)
            d = -d;
    }
    if (ok && out)
        *out = d;
    return ok;
}

QList<QPair<QString, qint64>> parseOptions(const Annotation &a, bool flags)
{
    QList<QPair<QString, qint64>> out;
    qint64 next = flags ? 1 : 0;
    for (int i = 0; i < a.args.size(); ++i) {
        const QString full = argString(a, i);
        // A single argument may hold several comma separated entries ("A,B,C").
        for (const QString &entry : full.split(QLatin1Char(','), Qt::SkipEmptyParts)) {
            QString name = entry.trimmed();
            qint64 value = next;
            const int colon = name.lastIndexOf(QLatin1Char(':'));
            if (colon > 0) {
                bool ok = false;
                const qint64 v = name.mid(colon + 1).trimmed().toLongLong(&ok);
                if (ok) {
                    value = v;
                    name = name.left(colon).trimmed();
                }
            }
            out << qMakePair(name, value);
            next = flags ? (value << 1) : value + 1;
        }
    }
    return out;
}

void applyAnnotation(const Annotation &a, PropertyInfo &p)
{
    const QString &n = a.name;
    if (!n.startsWith(QLatin1String("export")))
        return;
    p.exported = true;
    PropertyHint &h = p.hint;
    if (n == QLatin1String("export_range")) {
        h.kind = PropertyHint::Range;
        h.hasMin = argNumber(a, 0, &h.min);
        h.hasMax = argNumber(a, 1, &h.max);
        int extraStart = 2;
        if (argNumber(a, 2, &h.step))
            extraStart = 3;
        for (int i = extraStart; i < a.args.size(); ++i) {
            const QString e = argString(a, i);
            if (e == QLatin1String("or_greater")) h.orGreater = true;
            else if (e == QLatin1String("or_less")) h.orLess = true;
            else if (e == QLatin1String("hide_slider")) h.hideSlider = true;
            else if (e == QLatin1String("radians_as_degrees") || e == QLatin1String("degrees")) h.degrees = true;
            else if (e.startsWith(QLatin1String("suffix:"))) h.suffix = e.mid(7);
        }
    } else if (n == QLatin1String("export_enum")) {
        h.kind = PropertyHint::Enum;
        h.options = parseOptions(a, false);
    } else if (n == QLatin1String("export_flags")) {
        h.kind = PropertyHint::Flags;
        h.options = parseOptions(a, true);
    } else if (n.startsWith(QLatin1String("export_flags_"))) {
        h.kind = PropertyHint::Flags;
        for (int i = 0; i < 32; ++i)
            h.options << qMakePair(QStringLiteral("Layer %1").arg(i + 1), qint64(1) << i);
    } else if (n == QLatin1String("export_file") || n == QLatin1String("export_global_file")
               || n == QLatin1String("export_file_path")) {
        h.kind = PropertyHint::File;
        h.globalPath = n == QLatin1String("export_global_file");
        for (int i = 0; i < a.args.size(); ++i)
            for (const QString &f : argString(a, i).split(QLatin1Char(','), Qt::SkipEmptyParts))
                h.filters << f.trimmed().section(QLatin1Char(';'), 0, 0).trimmed();
    } else if (n == QLatin1String("export_dir") || n == QLatin1String("export_global_dir")) {
        h.kind = PropertyHint::Dir;
        h.globalPath = n == QLatin1String("export_global_dir");
    } else if (n == QLatin1String("export_multiline")) {
        h.kind = PropertyHint::Multiline;
    } else if (n == QLatin1String("export_placeholder")) {
        h.kind = PropertyHint::Placeholder;
        h.placeholder = argString(a, 0);
    } else if (n == QLatin1String("export_color_no_alpha")) {
        h.kind = PropertyHint::ColorNoAlpha;
    } else if (n == QLatin1String("export_exp_easing")) {
        h.kind = PropertyHint::ExpEasing;
    } else if (n == QLatin1String("export_node_path")) {
        h.kind = PropertyHint::NodePathTypes;
        for (int i = 0; i < a.args.size(); ++i)
            h.filters << argString(a, i);
    }
}

void parseEnum(const QString &text, ClassInfo &cls)
{
    // text: "enum Name { A, B = 2 }" or "enum { A, B }"
    int pos = 4;
    skipSpaces(text, pos);
    const QString name = readIdent(text, pos);
    const int open = text.indexOf(QLatin1Char('{'), pos);
    if (open < 0)
        return;
    const int end = matchBracket(text, open);
    const QString body = text.mid(open + 1, (end < 0 ? text.size() : end - 1) - open - 1);
    EnumInfo info;
    info.name = name;
    qint64 next = 0;
    for (const QString &entry : splitTopLevel(body)) {
        if (entry.isEmpty())
            continue;
        QString key = entry;
        qint64 value = next;
        const int eq = entry.indexOf(QLatin1Char('='));
        if (eq > 0) {
            key = entry.left(eq).trimmed();
            const QString expr = entry.mid(eq + 1).trimmed();
            bool ok = false;
            qint64 v = expr.toLongLong(&ok);
            if (!ok && expr.startsWith(QLatin1String("0x")))
                v = expr.mid(2).toLongLong(&ok, 16);
            if (!ok) {
                // Reference to an earlier member of the same enum.
                qint64 ref = 0;
                if (info.valueOf(expr, &ref)) {
                    v = ref;
                    ok = true;
                }
            }
            if (ok)
                value = v;
        }
        info.values << qMakePair(key, value);
        next = value + 1;
        if (name.isEmpty())
            cls.consts.insert(key, QString::number(value));
    }
    if (!name.isEmpty())
        cls.enums.insert(name, info);
}

void parseVar(const LogicalLine &ln, const QString &rest, const QList<Annotation> &annotations,
              const QString &group, const QString &subgroup, ClassInfo &cls,
              const GdScriptParser::Options &opts)
{
    // rest: after "var "
    int pos = 0;
    skipSpaces(rest, pos);
    PropertyInfo p;
    p.name = readIdent(rest, pos);
    if (p.name.isEmpty())
        return;
    p.line = ln.line;
    p.doc = ln.doc;
    p.group = group;
    p.subgroup = subgroup;
    skipSpaces(rest, pos);
    const QString tail = rest.mid(pos);
    if (tail.startsWith(QLatin1String(":="))) {
        p.inferred = true;
        QString expr = tail.mid(2);
        const int setget = findTopLevel(expr, QStringLiteral(":"));
        if (setget >= 0)
            expr = expr.left(setget);
        p.defaultExpr = expr.trimmed();
    } else if (tail.startsWith(QLatin1Char(':'))) {
        QString t = tail.mid(1);
        const int stop = findTopLevel(t, QStringLiteral("=:"));
        if (stop < 0) {
            p.typeRaw = t.trimmed();
        } else {
            p.typeRaw = t.left(stop).trimmed();
            if (t[stop] == QLatin1Char('=')) {
                QString expr = t.mid(stop + 1);
                const int setget = findTopLevel(expr, QStringLiteral(":"));
                if (setget >= 0)
                    expr = expr.left(setget);
                p.defaultExpr = expr.trimmed();
            }
        }
    } else if (tail.startsWith(QLatin1Char('='))) {
        QString expr = tail.mid(1);
        const int setget = findTopLevel(expr, QStringLiteral(":"));
        if (setget >= 0)
            expr = expr.left(setget);
        p.defaultExpr = expr.trimmed();
    }

    bool onready = false;
    for (const Annotation &a : annotations) {
        if (a.name == QLatin1String("onready"))
            onready = true;
        else
            applyAnnotation(a, p);
    }
    if (onready)
        return;
    if (!opts.includePrivate && p.name.startsWith(QLatin1Char('_')) && !p.exported)
        return;
    // Typed enum for @export_enum on String keeps the String type.
    cls.properties << p;
}

void parseBlock(const QList<LogicalLine> &lines, int begin, int end, int indent, ClassInfo &cls,
                QList<ClassPtr> &all, const GdScriptParser::Options &opts)
{
    QList<Annotation> pending;
    QString group, subgroup;
    int i = begin;
    while (i < end) {
        const LogicalLine &ln = lines[i];
        if (ln.indent > indent) {
            ++i; // body of a function / setter / property: skipped
            continue;
        }
        QString text = ln.text;
        QList<Annotation> anns = takeAnnotations(text);
        for (const Annotation &a : anns) {
            if (a.name == QLatin1String("export_group")) {
                group = argString(a, 0);
                subgroup.clear();
            } else if (a.name == QLatin1String("export_subgroup")) {
                subgroup = argString(a, 0);
            } else if (a.name == QLatin1String("export_category")) {
                group = argString(a, 0);
                subgroup.clear();
            } else if (a.name == QLatin1String("icon")) {
                cls.iconPath = argString(a, 0);
            } else if (a.name == QLatin1String("tool") || a.name == QLatin1String("static_unload")
                       || a.name == QLatin1String("abstract")) {
                // class-level flags, nothing to do
            } else {
                pending << a;
            }
        }
        if (text.isEmpty()) {
            ++i; // annotation-only line: applies to the next declaration
            continue;
        }

        int pos = 0;
        const QString kw = readIdent(text, pos);
        if (kw == QLatin1String("class_name")) {
            skipSpaces(text, pos);
            cls.name = readIdent(text, pos);
            cls.global = true;
            skipSpaces(text, pos);
            int p2 = pos;
            if (readIdent(text, p2) == QLatin1String("extends")) {
                cls.baseRaw = text.mid(p2).trimmed();
            }
            if (cls.doc.isEmpty())
                cls.doc = ln.doc;
        } else if (kw == QLatin1String("extends")) {
            cls.baseRaw = text.mid(pos).trimmed();
            if (cls.doc.isEmpty())
                cls.doc = ln.doc;
        } else if (kw == QLatin1String("const")) {
            skipSpaces(text, pos);
            const QString name = readIdent(text, pos);
            const int eq = text.indexOf(QLatin1Char('='), pos);
            if (!name.isEmpty() && eq >= 0)
                cls.consts.insert(name, text.mid(eq + 1).trimmed());
        } else if (kw == QLatin1String("enum")) {
            parseEnum(text, cls);
        } else if (kw == QLatin1String("var")) {
            parseVar(ln, text.mid(pos), pending, group, subgroup, cls, opts);
        } else if (kw == QLatin1String("class")) {
            // Inner class: "class Name extends Base:" followed by an indented body.
            skipSpaces(text, pos);
            const QString innerName = readIdent(text, pos);
            QString rest = text.mid(pos).trimmed();
            if (rest.endsWith(QLatin1Char(':')))
                rest.chop(1);
            auto inner = std::make_shared<ClassInfo>();
            inner->name = innerName;
            inner->language = QStringLiteral("gdscript");
            inner->scriptPath = cls.scriptPath;
            inner->doc = ln.doc;
            inner->outerKey = cls.key;
            inner->key = cls.key + QLatin1Char('.') + innerName;
            int rp = 0;
            if (readIdent(rest, rp) == QLatin1String("extends"))
                inner->baseRaw = rest.mid(rp).trimmed();
            int j = i + 1;
            while (j < end && lines[j].indent > indent)
                ++j;
            if (j > i + 1)
                parseBlock(lines, i + 1, j, lines[i + 1].indent, *inner, all, opts);
            cls.innerKeys << inner->key;
            all << inner;
            pending.clear();
            i = j;
            continue;
        }
        // static var / func / signal / anything else: ignored
        pending.clear();
        ++i;
    }
}

} // namespace

QList<ClassPtr> GdScriptParser::parse(const QString &source, const QString &resPath, const Options &opts)
{
    const QList<LogicalLine> lines = logicalLines(source);
    auto main = std::make_shared<ClassInfo>();
    main->language = QStringLiteral("gdscript");
    main->scriptPath = resPath;

    // First pass for class_name so inner class keys use it.
    for (const LogicalLine &ln : lines) {
        if (ln.indent != 0)
            continue;
        QString t = ln.text;
        takeAnnotations(t);
        if (t.startsWith(QLatin1String("class_name "))) {
            int pos = 10;
            skipSpaces(t, pos);
            main->name = readIdent(t, pos);
            break;
        }
    }
    main->key = main->name.isEmpty() ? resPath : main->name;

    QList<ClassPtr> inner;
    parseBlock(lines, 0, lines.size(), lines.isEmpty() ? 0 : lines.first().indent, *main, inner, opts);
    if (main->name.isEmpty()) {
        main->name = QFileInfo(resPath).fileName();
        main->global = false;
    }
    if (main->baseRaw.isEmpty())
        main->baseRaw = QStringLiteral("RefCounted");
    for (const ClassPtr &c : inner)
        if (c->baseRaw.isEmpty())
            c->baseRaw = QStringLiteral("RefCounted");

    QList<ClassPtr> out;
    out << main << inner;
    return out;
}

QList<ClassPtr> GdScriptParser::parseFile(const QString &absPath, const QString &resPath, const Options &opts)
{
    QFile f(absPath);
    if (!f.open(QIODevice::ReadOnly))
        return {};
    return parse(QString::fromUtf8(f.readAll()), resPath, opts);
}
