#pragma once

#include "ClassRegistry.h"
#include "ProjectConfig.h"

// Combines the class registry with the project configuration to answer the
// questions the validator and the inspector ask: which class does this JSON
// object belong to, and which fields may it contain?
class Schema
{
public:
    Schema(ClassRegistry *registry, ProjectConfig *config) : m_reg(registry), m_cfg(config) {}

    ClassRegistry *registry() const { return m_reg; }
    ProjectConfig *config() const { return m_cfg; }
    const ValueFormat &format() const { return m_reg->format(); }

    struct Poly
    {
        QString baseKey;  // class owning the configuration
        QString key;      // discriminator field, e.g. "type"
        QList<QPair<QString, QString>> types; // value -> class key
        bool automatic = false;
    };

    // Polymorphism rules for objects declared as `declaredKey`. False when the
    // class has no subclasses and no explicit configuration.
    bool polyFor(const QString &declaredKey, Poly *out) const;
    Poly autoPoly(const QString &baseKey) const;
    static QString autoTypeName(const QString &className, const QString &baseName);

    // Class of a JSON object declared as `declaredKey`, honouring the discriminator.
    // Sets `error` when the discriminator names an unknown or unrelated class.
    QString actualClass(const JValue &obj, const QString &declaredKey, QString *error = nullptr) const;
    // Discriminator value to write for `classKey` (empty if none needed).
    QString typeValueFor(const QString &classKey, const QString &declaredKey) const;
    // Classes that can be used where `declaredKey` is expected (itself first).
    QStringList assignableClasses(const QString &declaredKey) const;

    // A new JSON object for `classKey` used where `declaredKey` is expected.
    JValue newObject(const QString &classKey, const QString &declaredKey) const;

    QString displayName(const QString &classKey) const;

private:
    ClassRegistry *m_reg;
    ProjectConfig *m_cfg;
};
