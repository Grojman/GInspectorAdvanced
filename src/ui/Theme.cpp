#include "Theme.h"

#include <QApplication>
#include <QFont>
#include <QPalette>
#include <QStyle>
#include <QStyleFactory>

namespace Theme {

void apply(QApplication &app)
{
    // Keep the platform palette (light or dark) but use Fusion so stylesheets
    // render the same everywhere.
    const QPalette pal = app.palette();
    if (QStyle *fusion = QStyleFactory::create(QStringLiteral("Fusion")))
        app.setStyle(fusion);
    app.setPalette(pal);

    QFont f = app.font();
    const qreal base = f.pointSizeF() > 0 ? f.pointSizeF() : 10.0;
    f.setPointSizeF(qMax<qreal>(base + 1.5, 11.0));
    app.setFont(f);

    app.setStyleSheet(QStringLiteral(R"(
QLineEdit, QPlainTextEdit, QSpinBox, QDoubleSpinBox, QComboBox {
    padding: 5px 8px;
    border: 1px solid palette(mid);
    border-radius: 6px;
    background: palette(base);
    min-height: 24px;
    selection-background-color: palette(highlight);
}
QLineEdit:focus, QPlainTextEdit:focus, QSpinBox:focus, QDoubleSpinBox:focus, QComboBox:focus {
    border: 2px solid palette(highlight);
    padding: 4px 7px;
}
QSpinBox, QDoubleSpinBox { padding-right: 28px; }
QSpinBox:focus, QDoubleSpinBox:focus { padding-right: 27px; }
QSpinBox::up-button, QDoubleSpinBox::up-button {
    subcontrol-origin: border; subcontrol-position: top right;
    width: 24px; border: none; border-left: 1px solid palette(mid); border-top-right-radius: 6px;
}
QSpinBox::down-button, QDoubleSpinBox::down-button {
    subcontrol-origin: border; subcontrol-position: bottom right;
    width: 24px; border: none; border-left: 1px solid palette(mid); border-bottom-right-radius: 6px;
}
QSpinBox::up-button:hover, QDoubleSpinBox::up-button:hover,
QSpinBox::down-button:hover, QDoubleSpinBox::down-button:hover { background: palette(midlight); }
QSpinBox::up-arrow, QDoubleSpinBox::up-arrow { image: url(:/icons/chevron-up.svg); width: 10px; height: 10px; }
QSpinBox::down-arrow, QDoubleSpinBox::down-arrow { image: url(:/icons/chevron-down.svg); width: 10px; height: 10px; }
QSpinBox::up-arrow:disabled, QSpinBox::up-arrow:off, QDoubleSpinBox::up-arrow:disabled, QDoubleSpinBox::up-arrow:off,
QSpinBox::down-arrow:disabled, QSpinBox::down-arrow:off, QDoubleSpinBox::down-arrow:disabled, QDoubleSpinBox::down-arrow:off { image: none; }
QComboBox { padding-right: 30px; }
QComboBox::drop-down {
    subcontrol-origin: padding; subcontrol-position: center right;
    width: 28px; border: none;
}
QComboBox::down-arrow { image: url(:/icons/chevron-down.svg); width: 12px; height: 12px; }
QComboBox QAbstractItemView { padding: 4px; border: 1px solid palette(mid); selection-background-color: palette(highlight); }
QPushButton {
    padding: 7px 16px;
    border: 1px solid palette(mid);
    border-radius: 6px;
    background: palette(button);
    min-height: 22px;
}
QPushButton:hover { border-color: palette(highlight); }
QPushButton:pressed { background: palette(midlight); }
QPushButton#accentButton {
    background: palette(highlight);
    color: palette(highlighted-text);
    border: none;
    font-weight: bold;
}
QToolButton { padding: 5px; border-radius: 5px; }
QToolButton:hover { background: palette(midlight); }
QToolBar { spacing: 8px; padding: 6px; border: none; }
QDockWidget::title { padding: 8px 10px; font-weight: bold; background: palette(window); }
QTreeView::item, QListWidget::item { padding: 5px 4px; }
QHeaderView::section { padding: 6px 10px; border: none; border-bottom: 1px solid palette(mid); background: palette(window); }
QCheckBox { spacing: 8px; }
QCheckBox::indicator { width: 18px; height: 18px; }
QSlider::groove:horizontal { height: 6px; border-radius: 3px; background: palette(mid); }
QSlider::sub-page:horizontal { height: 6px; border-radius: 3px; background: palette(highlight); }
QSlider::handle:horizontal {
    width: 16px; height: 16px; margin: -6px 0; border-radius: 8px;
    background: palette(button); border: 2px solid palette(highlight);
}
QStatusBar { padding: 2px 8px; }
QStatusBar QLabel { padding: 0 8px; }
QTabBar::tab { padding: 8px 18px; }
QGroupBox { font-weight: bold; margin-top: 14px; padding-top: 8px; border: 1px solid palette(mid); border-radius: 6px; }
QGroupBox::title { subcontrol-origin: margin; left: 10px; padding: 0 4px; }
QMenu::item { padding: 6px 24px 6px 20px; }
)"));
}

} // namespace Theme
