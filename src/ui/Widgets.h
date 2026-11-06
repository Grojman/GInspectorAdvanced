#pragma once

#include <QDoubleSpinBox>
#include <QPlainTextEdit>
#include <QToolButton>
#include <QWidget>

class QHBoxLayout;
class QLabel;
class QSlider;
class QVBoxLayout;

// A header with an arrow that shows/hides a body widget, like the Godot inspector sections.
class CollapsibleSection : public QWidget
{
    Q_OBJECT
public:
    enum Style { Category, Group, Value };
    CollapsibleSection(const QString &title, Style style, QWidget *parent = nullptr);

    void setExpanded(bool e);
    bool isExpanded() const { return m_expanded; }
    void setTitle(const QString &t);
    void setSubtitle(const QString &t);
    QLabel *titleLabel() const { return m_title; }
    // Shows `w` (e.g. an editable key) in place of the title label.
    void setTitleWidget(QWidget *w);
    // Widgets added to the right side of the header.
    QHBoxLayout *headerLayout() const { return m_headerExtra; }
    QVBoxLayout *bodyLayout() const { return m_bodyLayout; }
    QWidget *body() const { return m_body; }

signals:
    void toggled(bool expanded);

protected:
    bool eventFilter(QObject *o, QEvent *e) override;

private:
    QWidget *m_header;
    QToolButton *m_arrow;
    QLabel *m_title;
    QLabel *m_subtitle;
    QHBoxLayout *m_titleLayout = nullptr;
    QHBoxLayout *m_headerExtra;
    QWidget *m_body;
    QVBoxLayout *m_bodyLayout;
    bool m_expanded = true;
};

// Ignores wheel events on unfocused spin boxes, combo boxes and sliders so
// scrolling the inspector never changes values by accident.
class WheelGuard : public QObject
{
public:
    static void install(QWidget *w);
    static WheelGuard *instance();

protected:
    bool eventFilter(QObject *o, QEvent *e) override;
};

// A double spin box that shows the shortest representation ("1.25", not "1.250000").
class SmartDoubleSpinBox : public QDoubleSpinBox
{
public:
    using QDoubleSpinBox::QDoubleSpinBox;
    QString textFromValue(double value) const override;
};

// Emits editingFinished when it loses focus.
class CommitPlainTextEdit : public QPlainTextEdit
{
    Q_OBJECT
public:
    using QPlainTextEdit::QPlainTextEdit;
signals:
    void editingFinished();

protected:
    void focusOutEvent(QFocusEvent *e) override;
};

// A numeric editor: spin box plus an optional slider when both bounds are known.
class NumberEdit : public QWidget
{
    Q_OBJECT
public:
    NumberEdit(bool integer, double min, double max, double step, bool slider, const QString &suffix,
               QWidget *parent = nullptr);
    void setValue(double v);
    double value() const;

signals:
    void valueCommitted(double v, bool dragging);

private:
    void syncSlider();
    bool m_integer;
    double m_min, m_max, m_step;
    QAbstractSpinBox *m_spin = nullptr;
    QSlider *m_slider = nullptr;
    bool m_updating = false;
};

// Small square tool button with an icon/text, used for row actions.
QToolButton *makeToolButton(const QString &text, const QString &tooltip, QWidget *parent = nullptr);
// Standard theme icon with a fallback text glyph.
QIcon themeIcon(const QString &name);
