#pragma once

// Visual theme for the main editor window: palette, fonts and custom drawing
// for buttons, sliders, list boxes, labels, input fields and the status bar.

#include <windows.h>
#include <commctrl.h>

namespace ui
{
// Palette
constexpr COLORREF kBgApp         = RGB(15, 16, 19);
constexpr COLORREF kSurface       = RGB(23, 24, 28);
constexpr COLORREF kSurfaceRaised = RGB(30, 32, 37);
constexpr COLORREF kField         = RGB(17, 18, 21);
constexpr COLORREF kBorder        = RGB(42, 44, 51);
constexpr COLORREF kBorderStrong  = RGB(62, 65, 74);
constexpr COLORREF kText          = RGB(234, 235, 239);
constexpr COLORREF kTextMuted     = RGB(146, 150, 161);
constexpr COLORREF kTextDisabled  = RGB(80, 83, 92);
constexpr COLORREF kAccent        = RGB(74, 136, 245);
constexpr COLORREF kAccentHover   = RGB(96, 153, 255);
constexpr COLORREF kAccentPressed = RGB(56, 114, 218);
constexpr COLORREF kAccentSoft    = RGB(31, 46, 74);
constexpr COLORREF kDanger        = RGB(240, 88, 84);
constexpr COLORREF kSuccess       = RGB(70, 200, 125);
constexpr COLORREF kWarning       = RGB(255, 168, 76);
constexpr COLORREF kButton        = RGB(38, 40, 46);
constexpr COLORREF kButtonHover   = RGB(49, 52, 59);
constexpr COLORREF kButtonPressed = RGB(31, 33, 38);

// Segoe Fluent Icons / Segoe MDL2 Assets code points
namespace glyph
{
constexpr wchar_t Open      = 0xE838;
constexpr wchar_t Play      = 0xE768;
constexpr wchar_t Pause     = 0xE769;
constexpr wchar_t Stop      = 0xE71A;
constexpr wchar_t Minus     = 0xE738;
constexpr wchar_t Plus      = 0xE710;
constexpr wchar_t Settings  = 0xE713;
constexpr wchar_t OpenPane  = 0xE8A0;
constexpr wchar_t ClosePane = 0xE89F;
constexpr wchar_t Volume    = 0xE767;
constexpr wchar_t Mute      = 0xE74F;
constexpr wchar_t Save      = 0xE78C;
constexpr wchar_t Cut       = 0xE8C6;
constexpr wchar_t Video     = 0xE714;
}

enum class FontRole { Body, Semibold, Small, Header, Time, Icon, IconSmall, IconLarge, Title };

enum class ButtonStyle
{
    Default,   // filled neutral button with border
    Primary,   // accent-filled call to action
    Ghost,     // no fill until hovered
    Segment,   // radio button drawn as one half of a segmented control
    Check      // checkbox
};

enum class LabelKind
{
    Header,     // small uppercase section title
    Body,
    Muted,      // secondary, may wrap
    ValueSplit, // "Name: value" -> name left (muted), value right (bright)
    Unit,       // small right-aligned suffix inside an input field
    Meta,       // small right-aligned single line; line breaks shown as " · "
    Caption     // like Meta, but left-aligned
};

enum class ListKind { AudioTracks, Clips };

void Init(HINSTANCE instance);
void Shutdown();

HBRUSH Brush(COLORREF color);
HFONT Font(FontRole role);
void SetFont(HWND hwnd, FontRole role);

// Buttons (painted through NM_CUSTOMDRAW sent to the parent)
void StyleButton(HWND button, ButtonStyle style, wchar_t glyph, COLORREF parentBg,
                 bool iconOnly = false);
void SetButtonGlyph(HWND button, wchar_t glyph);
// When the button text equals `text`, draw `glyph` instead and optionally tint it red.
void SetButtonAlternate(HWND button, const wchar_t* text, wchar_t glyph, bool danger);

// Labels: a lightweight custom control class
HWND CreateLabel(HWND parent, const wchar_t* text, LabelKind kind, COLORREF bg);
void SetLabelKind(HWND label, LabelKind kind);

// Owner-drawn list boxes (create with LBS_OWNERDRAWFIXED | LBS_HASSTRINGS)
void StyleListBox(HWND list, ListKind kind, const wchar_t* emptyText);

// Volume-style sliders (center detent, bipolar fill)
void StyleSlider(HWND slider, int centerPosition);

// Rounded input "field" boxes painted by the parent behind a child control.
void SetFieldRect(HWND key, const RECT& box);
void PaintFields(HDC hdc);
void InvalidateField(HWND key);

// Status bar control
HWND CreateStatusBar(HWND parent);

// Message handlers to call from the parent window procedure.
bool HandleNotify(LPARAM lParam, LRESULT& result);
bool HandleDrawItem(LPARAM lParam);
bool HandleMeasureItem(LPARAM lParam);
HBRUSH HandleCtlColor(UINT message, HDC hdc, HWND control);

// Drawing helpers (anti-aliased through GDI+)
void FillRound(HDC hdc, const RECT& rc, float radius, COLORREF fill);
void StrokeRound(HDC hdc, const RECT& rc, float radius, COLORREF stroke, float width = 1.0f);
void FillEllipse(HDC hdc, float cx, float cy, float radius, COLORREF fill, BYTE alpha = 255);
void DrawGlyph(HDC hdc, wchar_t glyph, const RECT& rc, COLORREF color, FontRole role = FontRole::Icon);
void FillSolid(HDC hdc, const RECT& rc, COLORREF color);
} // namespace ui
