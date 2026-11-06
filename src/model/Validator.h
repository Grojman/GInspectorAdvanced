#pragma once

#include "Schema.h"

struct Issue
{
    int item = -1;
    JPath path;
    bool error = true; // false: warning
    QString message;
};

// Checks JSON values against the classes they are linked to, and repairs them.
class Validator
{
public:
    explicit Validator(const Schema &schema) : m_schema(schema) {}

    // Validates one item object (declared as `classKey`). `keyField` is the
    // class field filled from the dictionary key, which must not appear in the item.
    QList<Issue> validateItem(const JValue &item, const QString &classKey, int itemIndex,
                              const QString &keyField = QString()) const;

    void validateObject(const JValue &obj, const QString &declaredKey, int item, const JPath &path,
                        QList<Issue> &out, const QString &keyField = QString()) const;
    void validateValue(const JValue &v, const TypeRef &t, const PropertyHint &h, int item, const JPath &path,
                       QList<Issue> &out) const;

    // Returns a copy of the object with unknown keys removed, numbers clamped,
    // and values of the wrong type dropped (so the class default applies).
    JValue sanitizeObject(const JValue &obj, const QString &declaredKey, const QString &keyField = QString()) const;
    // Undefined means "drop this value".
    JValue sanitizeValue(const JValue &v, const TypeRef &t, const PropertyHint &h) const;

    // Single-value checks used by editors before committing.
    static bool inRange(double v, const PropertyHint &h, double *clamped = nullptr);
    static bool matchesFileFilter(const QString &path, const QStringList &filters);

private:
    const Schema &m_schema;
};
