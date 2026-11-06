#include "Widgets.h"
#include "Theme.h"

#include <QAbstractSpinBox>
#include <QApplication>
#include <QComboBox>
#include <QEvent>
#include <QHBoxLayout>
#include <QLabel>
#include <QMouseEvent>
#include <QSlider>
#include <QSpinBox>
#include <QStyle>
#include <QVBoxLayout>
#include <QWheelEvent>
#include <cmath>
#include <limits>

CollapsibleSection::CollapsibleSection(const QString &title, Style style, QWidget *parent) : QWidget(parent)
{
    auto *outer = new QVBoxLayout(this);
    outer->setContentsMargins(0, 0, 0, 0);
    outer->setSpacing(0);

    m_header = new QWidget(this);
    m_header->setObjectName(style == Category ? QStringLiteral("categoryHeader")
                            : style == Group  ? QStringLiteral("groupHeader")
                                              : QStringLiteral("valueHeader"));
    m_header->setAttribute(Qt::WA_StyledBackground, true);
    m_header->setCursor(Qt::PointingHandCursor);
    auto *hl = new QHBoxLayout(m_header);
    if (style == Category)
        hl->setContentsMargins(10, 8, 8, 8);
    else if (style == Group)
        hl->setContentsMargins(4, 8, 6, 6);
    else
        hl->setContentsMargins(2, 3, 4, 3);
    hl->setSpacing(Theme::FieldSpacing);
    m_arrow = new QToolButton(m_header);
    m_arrow->setAutoRaise(true);
    m_arrow->setArrowType(Qt::DownArrow);
    m_arrow->setFixedSize(22, 22);
    m_arrow->setFocusPolicy(Qt::NoFocus);
    hl->addWidget(m_arrow);
    m_title = new QLabel(title, m_header);
    if (style != Value) {
        QFont f = m_title->font();
        f.setBold(true);
        if (style == Category)
            f.setPointSizeF(f.pointSizeF() * 1.12);
        m_title->setFont(f);
    }
    m_title->setMinimumWidth(style == Value ? 120 : 0);
    hl->addWidget(m_title);
    m_titleLayout = hl;
    m_subtitle = new QLabel(m_header);
    m_subtitle->setObjectName(QStringLiteral("dimLabel"));
    hl->addWidget(m_subtitle);
    hl->addStretch(1);
    m_headerExtra = new QHBoxLayout();
    m_headerExtra->setSpacing(4);
    hl->addLayout(m_headerExtra);
    outer->addWidget(m_header);

    m_body = new QWidget(this);
    m_bodyLayout = new QVBoxLayout(m_body);
    m_bodyLayout->setContentsMargins(style == Category ? 10 : 22, Theme::RowSpacing, style == Category ? 6 : 0,
                                     style == Category ? Theme::SectionSpacing : Theme::RowSpacing);
    m_bodyLayout->setSpacing(Theme::RowSpacing);
    outer->addWidget(m_body);

    connect(m_arrow, &QToolButton::clicked, this, [this] { setExpanded(!m_expanded); emit toggled(m_expanded); });
    m_header->installEventFilter(this);
    m_title->installEventFilter(this);
}

bool CollapsibleSection::eventFilter(QObject *o, QEvent *e)
{
    if ((o == m_header || o == m_title) && e->type() == QEvent::MouseButtonRelease) {
        auto *me = static_cast<QMouseEvent *>(e);
        if (me->button() == Qt::LeftButton) {
            setExpanded(!m_expanded);
            emit toggled(m_expanded);
            return true;
        }
    }
    return QWidget::eventFilter(o, e);
}

void CollapsibleSection::setExpanded(bool e)
{
    m_expanded = e;
    m_arrow->setArrowType(e ? Qt::DownArrow : Qt::RightArrow);
    m_body->setVisible(e);
}

void CollapsibleSection::setTitle(const QString &t)
{
    m_title->setText(t);
}

void CollapsibleSection::setSubtitle(const QString &t)
{
    m_subtitle->setText(t);
}

WheelGuard *WheelGuard::instance()
{
    static WheelGuard *g = new WheelGuard();
    return g;
}

void WheelGuard::install(QWidget *w)
{
    w->setFocusPolicy(Qt::StrongFocus);
    w->installEventFilter(instance());
}

bool WheelGuard::eventFilter(QObject *o, QEvent *e)
{
    if (e->type() == QEvent::Wheel) {
        auto *w = qobject_cast<QWidget *>(o);
        if (w && !w->hasFocus()) {
            e->ignore();
            return true; // let the scroll area handle it
        }
    }
    return false;
}

QString SmartDoubleSpinBox::textFromValue(double value) const
{
    QString s = locale().toString(value, 'f', decimals());
    const QChar dp = locale().decimalPoint().at(0);
    if (s.contains(dp)) {
        while (s.endsWith(QLatin1Char('0')))
            s.chop(1);
        if (s.endsWith(dp))
            s.chop(1);
    }
    if (s == QLatin1String("-0"))
        s = QStringLiteral("0");
    return s;
}

void CommitPlainTextEdit::focusOutEvent(QFocusEvent *e)
{
    QPlainTextEdit::focusOutEvent(e);
    emit editingFinished();
}

static int decimalsForStep(double step)
{
    if (step <= 0)
        return 6;
    int d = 0;
    double s = step;
    while (d < 6 && std::fabs(s - std::round(s)) > 1e-9) {
        s *= 10;
        ++d;
    }
    return qMax(d, 1);
}

NumberEdit::NumberEdit(bool integer, double min, double max, double step, bool slider, const QString &suffix,
                       QWidget *parent)
    : QWidget(parent), m_integer(integer), m_min(min), m_max(max), m_step(step)
{
    auto *l = new QHBoxLayout(this);
    l->setContentsMargins(0, 0, 0, 0);
    l->setSpacing(Theme::FieldSpacing);
    if (integer) {
        auto *sb = new QSpinBox(this);
        sb->setRange(int(qMax(min, double(std::numeric_limits<int>::min()))),
                     int(qMin(max, double(std::numeric_limits<int>::max()))));
        sb->setSingleStep(step > 0 ? qMax(1, int(step)) : 1);
        if (!suffix.isEmpty())
            sb->setSuffix(QLatin1Char(' ') + suffix);
        sb->setKeyboardTracking(false);
        connect(sb, qOverload<int>(&QSpinBox::valueChanged), this, [this](int v) {
            if (m_updating)
                return;
            syncSlider();
            emit valueCommitted(v, false);
        });
        m_spin = sb;
    } else {
        auto *sb = new SmartDoubleSpinBox(this);
        sb->setLocale(QLocale::c());
        sb->setDecimals(decimalsForStep(step));
        sb->setRange(min, max);
        sb->setSingleStep(step > 0 ? step : 0.1);
        if (!suffix.isEmpty())
            sb->setSuffix(QLatin1Char(' ') + suffix);
        sb->setKeyboardTracking(false);
        connect(sb, qOverload<double>(&QDoubleSpinBox::valueChanged), this, [this](double v) {
            if (m_updating)
                return;
            syncSlider();
            emit valueCommitted(v, false);
        });
        m_spin = sb;
    }
    m_spin->setMinimumWidth(80);
    m_spin->setCorrectionMode(QAbstractSpinBox::CorrectToNearestValue);
    WheelGuard::install(m_spin);

    const bool bounded = min > -1e15 && max < 1e15 && max > min;
    if (slider && bounded) {
        m_slider = new QSlider(Qt::Horizontal, this);
        const double effStep = step > 0 ? step : (integer ? 1.0 : (max - min) / 1000.0);
        const int ticks = int(qMin(10000.0, std::round((max - min) / effStep)));
        m_slider->setRange(0, qMax(1, ticks));
        WheelGuard::install(m_slider);
        connect(m_slider, &QSlider::valueChanged, this, [this, effStep](int t) {
            if (m_updating)
                return;
            double v = m_min + t * effStep;
            if (m_integer)
                v = std::round(v);
            v = qBound(m_min, v, m_max);
            m_updating = true;
            if (auto *sb = qobject_cast<QSpinBox *>(m_spin)) sb->setValue(int(v));
            else if (auto *db = qobject_cast<QDoubleSpinBox *>(m_spin)) db->setValue(v);
            m_updating = false;
            emit valueCommitted(value(), m_slider->isSliderDown());
        });
        l->addWidget(m_slider, 3);
        l->addWidget(m_spin, 2);
    } else {
        l->addWidget(m_spin, 1);
    }
}

void NumberEdit::syncSlider()
{
    if (!m_slider)
        return;
    const double effStep = m_step > 0 ? m_step : (m_integer ? 1.0 : (m_max - m_min) / 1000.0);
    const bool was = m_updating;
    m_updating = true;
    m_slider->setValue(int(std::round((value() - m_min) / effStep)));
    m_updating = was;
}

void NumberEdit::setValue(double v)
{
    m_updating = true;
    if (auto *sb = qobject_cast<QSpinBox *>(m_spin))
        sb->setValue(int(std::llround(v)));
    else if (auto *db = qobject_cast<QDoubleSpinBox *>(m_spin))
        db->setValue(v);
    m_updating = false;
    syncSlider();
}

double NumberEdit::value() const
{
    if (auto *sb = qobject_cast<QSpinBox *>(m_spin))
        return sb->value();
    if (auto *db = qobject_cast<QDoubleSpinBox *>(m_spin))
        return db->value();
    return 0;
}

QToolButton *makeToolButton(const QString &text, const QString &tooltip, QWidget *parent)
{
    auto *b = new QToolButton(parent);
    b->setText(text);
    b->setToolTip(tooltip);
    b->setAutoRaise(true);
    b->setFocusPolicy(Qt::NoFocus);
    b->setFixedSize(30, 30);
    QFont f = b->font();
    f.setPointSizeF(f.pointSizeF() * 1.1);
    b->setFont(f);
    return b;
}

QIcon themeIcon(const QString &name)
{
    return QIcon::fromTheme(name);
}

void CollapsibleSection::setTitleWidget(QWidget *w)
{
    m_title->hide();
    w->setParent(m_header);
    m_titleLayout->insertWidget(m_titleLayout->indexOf(m_title) + 1, w);
    w->show();
}
