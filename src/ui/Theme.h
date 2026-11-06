#pragma once

class QApplication;

// Look and feel shared by every window: Fusion style (identical on Windows and
// Linux) with the system palette, a larger base font and roomier widgets.
namespace Theme {

void apply(QApplication &app);

// Spacing constants used by the editor layouts.
constexpr int RowSpacing = 8;      // between inspector rows
constexpr int SectionSpacing = 12; // between sections
constexpr int PageMargin = 18;     // around the inspector page
constexpr int FieldSpacing = 8;    // between widgets inside a row
constexpr int LabelWidth = 200;    // property label column at depth 0

} // namespace Theme
