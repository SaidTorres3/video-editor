#include "theme.h"

#include <algorithm>
#include <cwchar>
#include <cwctype>
#include <iterator>
#include <string>
#include <unordered_map>
#include <vector>

namespace Gdiplus
{
using std::max;
using std::min;
}
#include <gdiplus.h>
#pragma comment(lib, "gdiplus.lib")

namespace ui
{
namespace
{
struct ButtonInfo
{
    ButtonStyle style = ButtonStyle::Default;
    wchar_t glyph = 0;
    COLORREF parentBg = kSurface;
    bool iconOnly = false;
    std::wstring altText;
    wchar_t altGlyph = 0;
    bool altDanger = false;
};

struct LabelInfo
{
    LabelKind kind = LabelKind::Body;
    COLORREF bg = kSurface;
};

struct ListInfo
{
    ListKind kind = ListKind::Clips;
    std::wstring emptyText;
};

ULONG_PTR g_gdiplusToken = 0;
bool g_gdiplusReady = false;
std::unordered_map<COLORREF, HBRUSH> g_brushes;
HFONT g_fonts[static_cast<int>(FontRole::Title) + 1] = {};
std::unordered_map<HWND, ButtonInfo> g_buttons;
std::unordered_map<HWND, LabelInfo> g_labels;
std::unordered_map<HWND, ListInfo> g_lists;
std::unordered_map<HWND, int> g_sliders; // slider -> center (detent) position
std::vector<std::pair<HWND, RECT>> g_fields;
const wchar_t* kLabelClass = L"VELabel";
const wchar_t* kStatusClass = L"VEStatusBar";

Gdiplus::Color ToColor(COLORREF c, BYTE alpha = 255)
{
    return Gdiplus::Color(alpha, GetRValue(c), GetGValue(c), GetBValue(c));
}

COLORREF Mix(COLORREF a, COLORREF b, float t)
{
    auto lerp = [t](int x, int y) { return static_cast<int>(x + (y - x) * t + 0.5f); };
    return RGB(lerp(GetRValue(a), GetRValue(b)),
               lerp(GetGValue(a), GetGValue(b)),
               lerp(GetBValue(a), GetBValue(b)));
}

void AddRoundRect(Gdiplus::GraphicsPath& path, float x, float y, float w, float h, float r)
{
    r = (std::min)(r, (std::min)(w, h) / 2.0f);
    if (r <= 0.25f)
    {
        path.AddRectangle(Gdiplus::RectF(x, y, w, h));
        return;
    }
    const float d = r * 2.0f;
    path.AddArc(x, y, d, d, 180.0f, 90.0f);
    path.AddArc(x + w - d, y, d, d, 270.0f, 90.0f);
    path.AddArc(x + w - d, y + h - d, d, d, 0.0f, 90.0f);
    path.AddArc(x, y + h - d, d, d, 90.0f, 90.0f);
    path.CloseFigure();
}

void PrepareGraphics(Gdiplus::Graphics& g)
{
    g.SetSmoothingMode(Gdiplus::SmoothingModeAntiAlias);
    g.SetPixelOffsetMode(Gdiplus::PixelOffsetModeHalf);
}

int CALLBACK FontExistsProc(const LOGFONTW*, const TEXTMETRICW*, DWORD, LPARAM lParam)
{
    *reinterpret_cast<bool*>(lParam) = true;
    return 0;
}

bool FontExists(const wchar_t* face)
{
    LOGFONTW lf{};
    lf.lfCharSet = DEFAULT_CHARSET;
    wcscpy_s(lf.lfFaceName, face);
    bool found = false;
    HDC dc = GetDC(nullptr);
    EnumFontFamiliesExW(dc, &lf, FontExistsProc, reinterpret_cast<LPARAM>(&found), 0);
    ReleaseDC(nullptr, dc);
    return found;
}

HFONT MakeFont(int height, int weight, const wchar_t* face)
{
    return CreateFontW(-height, 0, 0, 0, weight, FALSE, FALSE, FALSE, DEFAULT_CHARSET,
                       OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS, CLEARTYPE_QUALITY,
                       DEFAULT_PITCH | FF_DONTCARE, face);
}

const wchar_t* IconFace()
{
    static const wchar_t* face = FontExists(L"Segoe Fluent Icons")
                                     ? L"Segoe Fluent Icons"
                                     : L"Segoe MDL2 Assets";
    return face;
}

// Draws into an off-screen bitmap and copies it to the target on destruction.
class PaintBuffer
{
public:
    PaintBuffer(HDC target, const RECT& rc)
        : m_target(target), m_rc(rc),
          m_w((std::max)(1L, rc.right - rc.left)), m_h((std::max)(1L, rc.bottom - rc.top))
    {
        m_dc = CreateCompatibleDC(target);
        m_bmp = CreateCompatibleBitmap(target, m_w, m_h);
        m_old = SelectObject(m_dc, m_bmp);
        SetBkMode(m_dc, TRANSPARENT);
    }
    ~PaintBuffer()
    {
        BitBlt(m_target, m_rc.left, m_rc.top, m_w, m_h, m_dc, 0, 0, SRCCOPY);
        SelectObject(m_dc, m_old);
        DeleteObject(m_bmp);
        DeleteDC(m_dc);
    }
    HDC dc() const { return m_dc; }
    RECT local() const { return RECT{ 0, 0, m_w, m_h }; }
    int width() const { return m_w; }
    int height() const { return m_h; }

private:
    HDC m_target;
    RECT m_rc;
    int m_w, m_h;
    HDC m_dc = nullptr;
    HBITMAP m_bmp = nullptr;
    HGDIOBJ m_old = nullptr;
};

std::wstring WindowText(HWND hwnd)
{
    const int length = GetWindowTextLengthW(hwnd);
    std::wstring text(static_cast<size_t>(length) + 1, L'\0');
    GetWindowTextW(hwnd, text.data(), length + 1);
    text.resize(static_cast<size_t>(length));
    return text;
}

void DrawTextIn(HDC hdc, const std::wstring& text, RECT rc, COLORREF color, FontRole role, UINT format)
{
    HGDIOBJ old = SelectObject(hdc, Font(role));
    SetTextColor(hdc, color);
    SetBkMode(hdc, TRANSPARENT);
    DrawTextW(hdc, text.c_str(), static_cast<int>(text.size()), &rc, format | DT_NOPREFIX);
    SelectObject(hdc, old);
}

SIZE MeasureText(HDC hdc, const std::wstring& text, FontRole role)
{
    HGDIOBJ old = SelectObject(hdc, Font(role));
    SIZE size{};
    GetTextExtentPoint32W(hdc, text.c_str(), static_cast<int>(text.size()), &size);
    SelectObject(hdc, old);
    return size;
}

// Small rounded tag such as "MUTED" in list rows. Returns its width.
int DrawChip(HDC hdc, const std::wstring& text, int right, int centerY, COLORREF color, COLORREF bg)
{
    const SIZE size = MeasureText(hdc, text, FontRole::Header);
    const int w = size.cx + 12;
    RECT chip{ right - w, centerY - 9, right, centerY + 9 };
    FillRound(hdc, chip, 4.0f, Mix(bg, color, 0.18f));
    DrawTextIn(hdc, text, chip, color, FontRole::Header, DT_CENTER | DT_VCENTER | DT_SINGLELINE);
    return w;
}

// ---- Buttons --------------------------------------------------------------

void DrawCheckmark(HDC hdc, const RECT& box, COLORREF color)
{
    if (!g_gdiplusReady)
        return;
    Gdiplus::Graphics g(hdc);
    PrepareGraphics(g);
    Gdiplus::Pen pen(ToColor(color), 2.0f);
    pen.SetLineJoin(Gdiplus::LineJoinRound);
    pen.SetStartCap(Gdiplus::LineCapRound);
    pen.SetEndCap(Gdiplus::LineCapRound);
    const float x = static_cast<float>(box.left);
    const float y = static_cast<float>(box.top);
    Gdiplus::PointF pts[3] = { { x + 4.0f, y + 8.5f }, { x + 7.0f, y + 11.5f }, { x + 12.5f, y + 5.0f } };
    g.DrawLines(&pen, pts, 3);
}

void PaintButton(HWND hwnd, const ButtonInfo& info, HDC target, const RECT& rcTarget, UINT itemState)
{
    PaintBuffer buffer(target, rcTarget);
    HDC hdc = buffer.dc();
    const RECT rc = buffer.local();
    const int w = buffer.width();
    const int h = buffer.height();

    const bool disabled = (itemState & CDIS_DISABLED) || !IsWindowEnabled(hwnd);
    const LRESULT state = SendMessageW(hwnd, BM_GETSTATE, 0, 0);
    const bool pressed = !disabled && ((itemState & CDIS_SELECTED) || (state & BST_PUSHED));
    const bool hot = !disabled && ((itemState & CDIS_HOT) || (state & BST_HOT));
    const bool checked = SendMessageW(hwnd, BM_GETCHECK, 0, 0) == BST_CHECKED;

    FillSolid(hdc, rc, info.parentBg);

    std::wstring text = WindowText(hwnd);
    wchar_t glyph = info.glyph;
    bool danger = false;
    if (!info.altText.empty() && text == info.altText)
    {
        glyph = info.altGlyph;
        danger = info.altDanger;
    }

    if (info.style == ButtonStyle::Check)
    {
        RECT box{ 0, (h - 16) / 2, 16, (h - 16) / 2 + 16 };
        if (checked)
        {
            FillRound(hdc, box, 4.0f, disabled ? kBorderStrong : (hot ? kAccentHover : kAccent));
            DrawCheckmark(hdc, box, disabled ? kTextMuted : RGB(255, 255, 255));
        }
        else
        {
            FillRound(hdc, box, 4.0f, kField);
            StrokeRound(hdc, box, 4.0f, disabled ? kBorder : (hot ? kTextMuted : kBorderStrong));
        }
        RECT textRc{ 26, 0, w, h };
        DrawTextIn(hdc, text, textRc, disabled ? kTextDisabled : kText, FontRole::Body,
                   DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_END_ELLIPSIS);
        return;
    }

    if (info.style == ButtonStyle::Segment)
    {
        RECT pill = rc;
        if (checked)
            FillRound(hdc, pill, 5.0f, disabled ? kButton : kAccent);
        else if (hot)
            FillRound(hdc, pill, 5.0f, kButtonHover);
        COLORREF color = disabled ? kTextDisabled
                                  : checked ? RGB(255, 255, 255)
                                            : hot ? kText : kTextMuted;
        if (disabled && checked)
            color = kTextMuted;
        DrawTextIn(hdc, text, rc, color, checked ? FontRole::Semibold : FontRole::Body,
                   DT_CENTER | DT_VCENTER | DT_SINGLELINE | DT_END_ELLIPSIS);
        return;
    }

    const float radius = 6.0f;
    COLORREF fg = disabled ? kTextDisabled : kText;
    switch (info.style)
    {
    case ButtonStyle::Primary:
        if (disabled)
        {
            FillRound(hdc, rc, radius, kButton);
        }
        else
        {
            FillRound(hdc, rc, radius, pressed ? kAccentPressed : hot ? kAccentHover : kAccent);
            fg = RGB(255, 255, 255);
        }
        break;
    case ButtonStyle::Ghost:
        if (pressed || hot)
            FillRound(hdc, rc, radius, pressed ? kButtonPressed : kButtonHover);
        if (!disabled && !hot && !pressed)
            fg = Mix(kTextMuted, kText, 0.45f);
        break;
    default:
    {
        COLORREF fill = disabled ? Mix(kButton, info.parentBg, 0.5f)
                                 : pressed ? kButtonPressed : hot ? kButtonHover : kButton;
        COLORREF border = disabled ? kBorder : hot ? kBorderStrong : kBorder;
        if (danger && !disabled)
        {
            fill = Mix(fill, kDanger, pressed ? 0.10f : hot ? 0.22f : 0.16f);
            border = Mix(kBorder, kDanger, 0.45f);
        }
        FillRound(hdc, rc, radius, fill);
        StrokeRound(hdc, rc, radius, border);
        break;
    }
    }
    if (danger && !disabled && info.style != ButtonStyle::Primary)
        fg = kDanger;

    const bool showText = !info.iconOnly && !text.empty();
    const bool showGlyph = glyph != 0;
    const FontRole iconRole = (h <= 26) ? FontRole::IconSmall : FontRole::Icon;
    const FontRole textRole = info.style == ButtonStyle::Primary ? FontRole::Semibold : FontRole::Body;
    const int glyphWidth = showGlyph ? (iconRole == FontRole::IconSmall ? 12 : 16) : 0;
    const int gap = (showGlyph && showText) ? 8 : 0;
    const int textWidth = showText ? MeasureText(hdc, text, textRole).cx : 0;
    int x = (w - (glyphWidth + gap + textWidth)) / 2;
    if (x < 6)
        x = 6;
    const int pressOffset = pressed ? 1 : 0;
    if (showGlyph)
    {
        RECT g{ x, pressOffset, x + glyphWidth, h + pressOffset };
        DrawGlyph(hdc, glyph, g, fg, iconRole);
    }
    if (showText)
    {
        RECT t{ x + glyphWidth + gap, pressOffset, w - 4, h + pressOffset };
        DrawTextIn(hdc, text, t, fg, textRole, DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_END_ELLIPSIS);
    }
}

// ---- Sliders ----------------------------------------------------------------

struct SliderGeometry
{
    int minimum, maximum, position;
    int thumbX, centerX, cy, left, right;
};

SliderGeometry Measure(HWND slider, int center)
{
    SliderGeometry g{};
    g.minimum = static_cast<int>(SendMessageW(slider, TBM_GETRANGEMIN, 0, 0));
    g.maximum = static_cast<int>(SendMessageW(slider, TBM_GETRANGEMAX, 0, 0));
    g.position = static_cast<int>(SendMessageW(slider, TBM_GETPOS, 0, 0));
    RECT channel{}, thumb{};
    SendMessageW(slider, TBM_GETCHANNELRECT, 0, reinterpret_cast<LPARAM>(&channel));
    SendMessageW(slider, TBM_GETTHUMBRECT, 0, reinterpret_cast<LPARAM>(&thumb));
    const int thumbWidth = thumb.right - thumb.left;
    g.left = channel.left + thumbWidth / 2;
    g.right = channel.right - thumbWidth / 2;
    g.thumbX = (thumb.left + thumb.right) / 2;
    g.cy = (thumb.top + thumb.bottom) / 2;
    const int range = (std::max)(1, g.maximum - g.minimum);
    g.centerX = g.left + MulDiv(center - g.minimum, g.right - g.left, range);
    return g;
}

void PaintSliderTrack(HWND slider, int center, HDC target)
{
    RECT client{};
    GetClientRect(slider, &client);
    PaintBuffer buffer(target, client);
    HDC hdc = buffer.dc();
    FillSolid(hdc, buffer.local(), kSurface);

    const bool enabled = IsWindowEnabled(slider) != FALSE;
    const SliderGeometry g = Measure(slider, center);

    RECT track{ g.left, g.cy - 2, g.right, g.cy + 2 };
    FillRound(hdc, track, 2.0f, kBorderStrong);

    RECT tick{ g.centerX - 1, g.cy - 6, g.centerX + 1, g.cy + 6 };
    FillSolid(hdc, tick, enabled ? kTextMuted : kBorderStrong);

    const int fillLeft = (std::min)(g.centerX, g.thumbX);
    const int fillRight = (std::max)(g.centerX, g.thumbX);
    if (fillRight - fillLeft > 1)
    {
        RECT fill{ fillLeft, g.cy - 2, fillRight, g.cy + 2 };
        FillRound(hdc, fill, 2.0f, enabled ? (g.position == 0 ? kDanger : kAccent) : kTextDisabled);
    }
}

void PaintSliderThumb(HWND slider, int center, HDC hdc, UINT itemState)
{
    const bool enabled = IsWindowEnabled(slider) != FALSE;
    const SliderGeometry g = Measure(slider, center);
    const bool active = enabled && (itemState & (CDIS_HOT | CDIS_SELECTED)) != 0;
    if (active)
        FillEllipse(hdc, static_cast<float>(g.thumbX), static_cast<float>(g.cy), 11.0f, kAccent, 70);
    FillEllipse(hdc, static_cast<float>(g.thumbX), static_cast<float>(g.cy), 7.0f,
                enabled ? RGB(245, 246, 250) : kTextDisabled);
}

LRESULT CALLBACK SliderSubclassProc(HWND hwnd, UINT message, WPARAM wParam, LPARAM lParam,
                                    UINT_PTR, DWORD_PTR)
{
    if (message == WM_NCDESTROY)
    {
        g_sliders.erase(hwnd);
        RemoveWindowSubclass(hwnd, SliderSubclassProc, 0);
        return DefSubclassProc(hwnd, message, wParam, lParam);
    }
    const LRESULT result = DefSubclassProc(hwnd, message, wParam, lParam);
    switch (message)
    {
    case TBM_SETPOS:
    case TBM_SETPOSNOTIFY:
    case TBM_SETRANGE:
    case WM_MOUSEMOVE:
    case WM_MOUSELEAVE:
    case WM_LBUTTONDOWN:
    case WM_LBUTTONUP:
    case WM_KEYDOWN:
    case WM_MOUSEWHEEL:
    case WM_ENABLE:
    case WM_CAPTURECHANGED:
        // The custom thumb and fill extend beyond the native thumb rectangle.
        InvalidateRect(hwnd, nullptr, FALSE);
        break;
    }
    return result;
}

// ---- List boxes -------------------------------------------------------------

LRESULT CALLBACK ListSubclassProc(HWND hwnd, UINT message, WPARAM wParam, LPARAM lParam,
                                  UINT_PTR, DWORD_PTR)
{
    switch (message)
    {
    case WM_ERASEBKGND:
        if (SendMessageW(hwnd, LB_GETCOUNT, 0, 0) == 0)
            return 1;
        break;
    case WM_PAINT:
        if (SendMessageW(hwnd, LB_GETCOUNT, 0, 0) == 0)
        {
            PAINTSTRUCT ps;
            HDC dc = BeginPaint(hwnd, &ps);
            RECT rc{};
            GetClientRect(hwnd, &rc);
            {
                PaintBuffer buffer(dc, rc);
                FillSolid(buffer.dc(), buffer.local(), kField);
                auto it = g_lists.find(hwnd);
                if (it != g_lists.end() && !it->second.emptyText.empty())
                {
                    RECT text = buffer.local();
                    InflateRect(&text, -10, 0);
                    DrawTextIn(buffer.dc(), it->second.emptyText, text, kTextDisabled, FontRole::Small,
                               DT_CENTER | DT_VCENTER | DT_SINGLELINE | DT_END_ELLIPSIS);
                }
            }
            EndPaint(hwnd, &ps);
            return 0;
        }
        break;
    case WM_ENABLE:
        InvalidateRect(hwnd, nullptr, TRUE);
        break;
    case WM_NCDESTROY:
        g_lists.erase(hwnd);
        RemoveWindowSubclass(hwnd, ListSubclassProc, 0);
        break;
    }
    return DefSubclassProc(hwnd, message, wParam, lParam);
}

void PaintListItem(const DRAWITEMSTRUCT* di)
{
    PaintBuffer buffer(di->hDC, di->rcItem);
    HDC hdc = buffer.dc();
    const RECT rc = buffer.local();
    const int w = buffer.width();
    const int h = buffer.height();
    FillSolid(hdc, rc, kField);
    if (di->itemID == static_cast<UINT>(-1))
        return;

    const bool enabled = IsWindowEnabled(di->hwndItem) != FALSE;
    const bool selected = (di->itemState & ODS_SELECTED) != 0;
    if (selected)
    {
        RECT row{ 2, 1, w - 2, h - 1 };
        FillRound(hdc, row, 5.0f, enabled ? kAccentSoft : kButton);
        if (enabled)
        {
            RECT bar{ 2, 6, 5, h - 6 };
            FillRound(hdc, bar, 1.5f, kAccent);
        }
    }

    const LRESULT length = SendMessageW(di->hwndItem, LB_GETTEXTLEN, di->itemID, 0);
    std::wstring text(length > 0 ? static_cast<size_t>(length) + 1 : 1, L'\0');
    if (length > 0)
    {
        SendMessageW(di->hwndItem, LB_GETTEXT, di->itemID, reinterpret_cast<LPARAM>(text.data()));
        text.resize(static_cast<size_t>(length));
    }
    else
    {
        text.clear();
    }

    const COLORREF fg = enabled ? kText : kTextDisabled;
    const COLORREF dim = enabled ? kTextMuted : kTextDisabled;
    const COLORREF rowBg = selected ? (enabled ? kAccentSoft : kButton) : kField;
    const int cy = h / 2;

    auto it = g_lists.find(di->hwndItem);
    const ListKind kind = it != g_lists.end() ? it->second.kind : ListKind::Clips;
    if (kind == ListKind::AudioTracks)
    {
        auto stripTag = [&text](const wchar_t* tag) {
            const size_t at = text.find(tag);
            if (at == std::wstring::npos)
                return false;
            text.erase(at, wcslen(tag));
            return true;
        };
        const bool muted = stripTag(L" (MUTED)");
        const bool voice = stripTag(L" (VOICE)");

        int right = w - 8;
        if (muted)
            right -= DrawChip(hdc, L"MUTED", right, cy, enabled ? kDanger : dim, rowBg) + 4;
        if (voice)
            right -= DrawChip(hdc, L"VOICE", right, cy, enabled ? kSuccess : dim, rowBg) + 4;

        RECT icon{ 10, 0, 28, h };
        DrawGlyph(hdc, muted ? glyph::Mute : glyph::Volume, icon,
                  muted && enabled ? kDanger : (selected ? fg : dim), FontRole::IconSmall);
        RECT name{ 34, 0, right - 4, h };
        DrawTextIn(hdc, text, name, muted ? dim : fg, FontRole::Body,
                   DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_END_ELLIPSIS);
        return;
    }

    // Clip rows are formatted as "N. start - end".
    std::wstring index;
    std::wstring range = text;
    const size_t dot = text.find(L". ");
    if (dot != std::wstring::npos && dot < 5)
    {
        index = text.substr(0, dot);
        range = text.substr(dot + 2);
    }
    const size_t dash = range.find(L" - ");
    if (dash != std::wstring::npos)
        range.replace(dash, 3, L"   \x2192   ");

    int x = 10;
    if (!index.empty())
    {
        RECT badge{ x, cy - 9, x + 22, cy + 9 };
        FillRound(hdc, badge, 4.0f, selected && enabled ? kAccent : kButton);
        DrawTextIn(hdc, index, badge, selected && enabled ? RGB(255, 255, 255) : dim, FontRole::Header,
                   DT_CENTER | DT_VCENTER | DT_SINGLELINE);
        x += 32;
    }
    RECT body{ x, 0, w - 8, h };
    DrawTextIn(hdc, range, body, fg, FontRole::Body, DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_END_ELLIPSIS);
}

// ---- Labels -----------------------------------------------------------------

void PaintLabel(HWND hwnd, HDC target)
{
    RECT client{};
    GetClientRect(hwnd, &client);
    PaintBuffer buffer(target, client);
    HDC hdc = buffer.dc();
    RECT rc = buffer.local();

    LabelInfo info;
    if (auto it = g_labels.find(hwnd); it != g_labels.end())
        info = it->second;
    FillSolid(hdc, rc, info.bg);

    const bool enabled = IsWindowEnabled(hwnd) != FALSE;
    std::wstring text = WindowText(hwnd);

    switch (info.kind)
    {
    case LabelKind::Header:
    {
        for (auto& ch : text)
            ch = static_cast<wchar_t>(towupper(ch));
        SetTextCharacterExtra(hdc, 1);
        DrawTextIn(hdc, text, rc, enabled ? kTextMuted : kTextDisabled, FontRole::Header,
                   DT_LEFT | DT_VCENTER | DT_SINGLELINE);
        SetTextCharacterExtra(hdc, 0);
        break;
    }
    case LabelKind::ValueSplit:
    {
        std::wstring name = text;
        std::wstring value;
        const size_t colon = text.find(L':');
        if (colon != std::wstring::npos)
        {
            name = text.substr(0, colon);
            value = text.substr(colon + 1);
            const size_t first = value.find_first_not_of(L' ');
            value = first == std::wstring::npos ? L"" : value.substr(first);
        }
        DrawTextIn(hdc, name, rc, enabled ? kTextMuted : kTextDisabled, FontRole::Body,
                   DT_LEFT | DT_VCENTER | DT_SINGLELINE);
        COLORREF valueColor = enabled ? kText : kTextDisabled;
        if (enabled && value == L"Mute")
            valueColor = kDanger;
        DrawTextIn(hdc, value, rc, valueColor, FontRole::Semibold, DT_RIGHT | DT_VCENTER | DT_SINGLELINE);
        break;
    }
    case LabelKind::Unit:
        DrawTextIn(hdc, text, rc, enabled ? kTextMuted : kTextDisabled, FontRole::Small,
                   DT_RIGHT | DT_VCENTER | DT_SINGLELINE);
        break;
    case LabelKind::Meta:
    case LabelKind::Caption:
    {
        for (size_t at = text.find(L'\n'); at != std::wstring::npos; at = text.find(L'\n', at))
            text.replace(at, 1, L"  \x00B7  ");
        const UINT align = info.kind == LabelKind::Meta ? DT_RIGHT : DT_LEFT;
        DrawTextIn(hdc, text, rc, enabled ? kTextMuted : kTextDisabled, FontRole::Small,
                   align | DT_VCENTER | DT_SINGLELINE | DT_END_ELLIPSIS);
        break;
    }
    case LabelKind::Muted:
    {
        const bool multiline = text.find(L'\n') != std::wstring::npos;
        DrawTextIn(hdc, text, rc, enabled ? kTextMuted : kTextDisabled, FontRole::Body,
                   multiline ? (DT_LEFT | DT_WORDBREAK)
                             : (DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_END_ELLIPSIS));
        break;
    }
    default:
        DrawTextIn(hdc, text, rc, enabled ? kText : kTextDisabled, FontRole::Body,
                   DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_END_ELLIPSIS);
        break;
    }
}

LRESULT CALLBACK LabelProc(HWND hwnd, UINT message, WPARAM wParam, LPARAM lParam)
{
    switch (message)
    {
    case WM_SETTEXT:
    {
        const LRESULT result = DefWindowProcW(hwnd, message, wParam, lParam);
        InvalidateRect(hwnd, nullptr, FALSE);
        return result;
    }
    case WM_ENABLE:
        InvalidateRect(hwnd, nullptr, FALSE);
        return 0;
    case WM_ERASEBKGND:
        return 1;
    case WM_NCHITTEST:
        return HTTRANSPARENT;
    case WM_PAINT:
    {
        PAINTSTRUCT ps;
        HDC hdc = BeginPaint(hwnd, &ps);
        PaintLabel(hwnd, hdc);
        EndPaint(hwnd, &ps);
        return 0;
    }
    case WM_NCDESTROY:
        g_labels.erase(hwnd);
        break;
    }
    return DefWindowProcW(hwnd, message, wParam, lParam);
}

// ---- Status bar -------------------------------------------------------------

std::vector<std::wstring> Split(const std::wstring& text, const std::wstring& separator)
{
    std::vector<std::wstring> parts;
    size_t start = 0;
    while (true)
    {
        const size_t at = text.find(separator, start);
        if (at == std::wstring::npos)
        {
            parts.push_back(text.substr(start));
            break;
        }
        parts.push_back(text.substr(start, at - start));
        start = at + separator.size();
    }
    return parts;
}

void PaintStatusBar(HWND hwnd, HDC target)
{
    RECT client{};
    GetClientRect(hwnd, &client);
    PaintBuffer buffer(target, client);
    HDC hdc = buffer.dc();
    const RECT rc = buffer.local();
    const int h = buffer.height();
    FillSolid(hdc, rc, kSurface);
    RECT line{ 0, 0, rc.right, 1 };
    FillSolid(hdc, line, kBorder);

    const std::wstring text = WindowText(hwnd);
    const std::vector<std::wstring> parts = Split(text, L" | ");
    int x = 16;
    const int cy = (h + 1) / 2;
    auto draw = [&](const std::wstring& s, COLORREF color, FontRole role) {
        RECT r{ x, 1, rc.right - 8, h };
        DrawTextIn(hdc, s, r, color, role, DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_END_ELLIPSIS);
        x += MeasureText(hdc, s, role).cx;
    };

    if (parts.size() == 1)
    {
        draw(text, kTextMuted, FontRole::Body);
        return;
    }

    for (size_t i = 0; i < parts.size(); ++i)
    {
        const std::wstring& part = parts[i];
        if (i > 0)
        {
            x += 14;
            RECT separator{ x, cy - 7, x + 1, cy + 7 };
            FillSolid(hdc, separator, kBorderStrong);
            x += 15;
        }

        const size_t slash = part.find(L" / ");
        if (i == 0 && slash != std::wstring::npos)
        {
            draw(part.substr(0, slash), kText, FontRole::Time);
            draw(L"  /  " + part.substr(slash + 3), kTextMuted, FontRole::Body);
        }
        else if (part == L"Playing" || part == L"Paused")
        {
            const bool playing = part == L"Playing";
            FillEllipse(hdc, static_cast<float>(x + 4), static_cast<float>(cy), 3.5f,
                        playing ? kSuccess : kTextMuted);
            x += 14;
            draw(part, playing ? kText : kTextMuted, FontRole::Body);
        }
        else if (part.rfind(L"Waveforms", 0) == 0)
        {
            int percent = 0;
            for (wchar_t ch : part)
            {
                if (ch >= L'0' && ch <= L'9')
                    percent = percent * 10 + (ch - L'0');
            }
            percent = (std::min)(100, percent);
            draw(L"Waveforms", kTextMuted, FontRole::Body);
            x += 10;
            RECT bar{ x, cy - 2, x + 64, cy + 2 };
            FillRound(hdc, bar, 2.0f, kBorderStrong);
            RECT fill{ x, cy - 2, x + (64 * percent) / 100, cy + 2 };
            if (fill.right > fill.left + 1)
                FillRound(hdc, fill, 2.0f, kAccent);
            x += 72;
            draw(std::to_wstring(percent) + L"%", kTextMuted, FontRole::Small);
        }
        else
        {
            draw(part, kTextMuted, FontRole::Body);
        }
    }
}

LRESULT CALLBACK StatusProc(HWND hwnd, UINT message, WPARAM wParam, LPARAM lParam)
{
    switch (message)
    {
    case WM_SETTEXT:
    {
        const LRESULT result = DefWindowProcW(hwnd, message, wParam, lParam);
        InvalidateRect(hwnd, nullptr, FALSE);
        return result;
    }
    case WM_ERASEBKGND:
        return 1;
    case WM_PAINT:
    {
        PAINTSTRUCT ps;
        HDC hdc = BeginPaint(hwnd, &ps);
        PaintStatusBar(hwnd, hdc);
        EndPaint(hwnd, &ps);
        return 0;
    }
    }
    return DefWindowProcW(hwnd, message, wParam, lParam);
}

void RegisterClasses(HINSTANCE instance)
{
    static bool registered = false;
    if (registered)
        return;
    registered = true;

    WNDCLASSW label{};
    label.lpfnWndProc = LabelProc;
    label.hInstance = instance;
    label.lpszClassName = kLabelClass;
    label.hCursor = LoadCursor(nullptr, IDC_ARROW);
    RegisterClassW(&label);

    WNDCLASSW status{};
    status.lpfnWndProc = StatusProc;
    status.hInstance = instance;
    status.lpszClassName = kStatusClass;
    status.hCursor = LoadCursor(nullptr, IDC_ARROW);
    RegisterClassW(&status);
}

void EnableDarkMenus()
{
    // Undocumented uxtheme exports used by Explorer for dark context menus and
    // scrollbars (ordinal 135: SetPreferredAppMode, 136: FlushMenuThemes).
    HMODULE uxtheme = LoadLibraryExW(L"uxtheme.dll", nullptr, LOAD_LIBRARY_SEARCH_SYSTEM32);
    if (!uxtheme)
        return;
    using SetPreferredAppModeFn = int(WINAPI*)(int);
    using FlushMenuThemesFn = void(WINAPI*)();
    auto setMode = reinterpret_cast<SetPreferredAppModeFn>(
        GetProcAddress(uxtheme, MAKEINTRESOURCEA(135)));
    auto flush = reinterpret_cast<FlushMenuThemesFn>(
        GetProcAddress(uxtheme, MAKEINTRESOURCEA(136)));
    if (setMode)
        setMode(2); // ForceDark
    if (flush)
        flush();
}
} // namespace

void Init(HINSTANCE instance)
{
    if (!g_gdiplusReady)
    {
        Gdiplus::GdiplusStartupInput input;
        g_gdiplusReady = Gdiplus::GdiplusStartup(&g_gdiplusToken, &input, nullptr) == Gdiplus::Ok;
    }
    EnableDarkMenus();
    RegisterClasses(instance);
}

void Shutdown()
{
    for (auto& entry : g_brushes)
        DeleteObject(entry.second);
    g_brushes.clear();
    for (auto& font : g_fonts)
    {
        if (font)
            DeleteObject(font);
        font = nullptr;
    }
    if (g_gdiplusReady)
    {
        Gdiplus::GdiplusShutdown(g_gdiplusToken);
        g_gdiplusReady = false;
    }
}

HBRUSH Brush(COLORREF color)
{
    auto it = g_brushes.find(color);
    if (it != g_brushes.end())
        return it->second;
    HBRUSH brush = CreateSolidBrush(color);
    g_brushes.emplace(color, brush);
    return brush;
}

HFONT Font(FontRole role)
{
    HFONT& font = g_fonts[static_cast<int>(role)];
    if (font)
        return font;
    switch (role)
    {
    case FontRole::Body:      font = MakeFont(14, FW_NORMAL, L"Segoe UI"); break;
    case FontRole::Semibold:  font = MakeFont(14, FW_SEMIBOLD, L"Segoe UI"); break;
    case FontRole::Small:     font = MakeFont(12, FW_NORMAL, L"Segoe UI"); break;
    case FontRole::Header:    font = MakeFont(11, FW_BOLD, L"Segoe UI"); break;
    case FontRole::Time:      font = MakeFont(15, FW_SEMIBOLD, L"Segoe UI"); break;
    case FontRole::Icon:      font = MakeFont(16, FW_NORMAL, IconFace()); break;
    case FontRole::IconSmall: font = MakeFont(12, FW_NORMAL, IconFace()); break;
    case FontRole::IconLarge: font = MakeFont(44, FW_NORMAL, IconFace()); break;
    case FontRole::Title:     font = MakeFont(18, FW_SEMIBOLD, L"Segoe UI"); break;
    }
    return font;
}

void SetFont(HWND hwnd, FontRole role)
{
    SendMessageW(hwnd, WM_SETFONT, reinterpret_cast<WPARAM>(Font(role)), TRUE);
}

void StyleButton(HWND button, ButtonStyle style, wchar_t glyphCode, COLORREF parentBg, bool iconOnly)
{
    ButtonInfo& info = g_buttons[button];
    info.style = style;
    info.glyph = glyphCode;
    info.parentBg = parentBg;
    info.iconOnly = iconOnly;
    SetFont(button, style == ButtonStyle::Primary ? FontRole::Semibold : FontRole::Body);
}

void SetButtonGlyph(HWND button, wchar_t glyphCode)
{
    auto it = g_buttons.find(button);
    if (it == g_buttons.end() || it->second.glyph == glyphCode)
        return;
    it->second.glyph = glyphCode;
    InvalidateRect(button, nullptr, FALSE);
}

void SetButtonAlternate(HWND button, const wchar_t* text, wchar_t glyphCode, bool danger)
{
    ButtonInfo& info = g_buttons[button];
    info.altText = text ? text : L"";
    info.altGlyph = glyphCode;
    info.altDanger = danger;
}

HWND CreateLabel(HWND parent, const wchar_t* text, LabelKind kind, COLORREF bg)
{
    HINSTANCE instance = reinterpret_cast<HINSTANCE>(GetWindowLongPtrW(parent, GWLP_HINSTANCE));
    RegisterClasses(instance);
    HWND label = CreateWindowExW(0, kLabelClass, text, WS_CHILD | WS_VISIBLE,
                                 0, 0, 10, 10, parent, nullptr, instance, nullptr);
    if (label)
        g_labels[label] = LabelInfo{ kind, bg };
    return label;
}

void SetLabelKind(HWND label, LabelKind kind)
{
    auto it = g_labels.find(label);
    if (it == g_labels.end() || it->second.kind == kind)
        return;
    it->second.kind = kind;
    InvalidateRect(label, nullptr, FALSE);
}

void StyleListBox(HWND list, ListKind kind, const wchar_t* emptyText)
{
    g_lists[list] = ListInfo{ kind, emptyText ? emptyText : L"" };
    SetFont(list, FontRole::Body);
    SendMessageW(list, LB_SETITEMHEIGHT, 0, 30);
    SetWindowSubclass(list, ListSubclassProc, 0, 0);
}

void StyleSlider(HWND slider, int centerPosition)
{
    g_sliders[slider] = centerPosition;
    SendMessageW(slider, TBM_SETTHUMBLENGTH, 22, 0);
    SetWindowSubclass(slider, SliderSubclassProc, 0, 0);
}

void SetFieldRect(HWND key, const RECT& box)
{
    for (auto& field : g_fields)
    {
        if (field.first == key)
        {
            field.second = box;
            return;
        }
    }
    g_fields.emplace_back(key, box);
}

void PaintFields(HDC hdc)
{
    const HWND focus = GetFocus();
    for (const auto& field : g_fields)
    {
        if (!IsWindowVisible(field.first))
            continue;
        wchar_t className[16] = {};
        GetClassNameW(field.first, className, static_cast<int>(std::size(className)));
        const bool focused = focus == field.first && _wcsicmp(className, L"Edit") == 0;
        FillRound(hdc, field.second, 7.0f, kField);
        StrokeRound(hdc, field.second, 7.0f, focused ? kAccent : kBorder);
    }
}

void InvalidateField(HWND key)
{
    for (const auto& field : g_fields)
    {
        if (field.first != key)
            continue;
        RECT rc = field.second;
        InflateRect(&rc, 2, 2);
        InvalidateRect(GetParent(key), &rc, FALSE);
    }
}

HWND CreateStatusBar(HWND parent)
{
    HINSTANCE instance = reinterpret_cast<HINSTANCE>(GetWindowLongPtrW(parent, GWLP_HINSTANCE));
    RegisterClasses(instance);
    return CreateWindowExW(0, kStatusClass, L"No video loaded", WS_CHILD | WS_VISIBLE,
                           0, 0, 10, 10, parent, nullptr, instance, nullptr);
}

bool HandleNotify(LPARAM lParam, LRESULT& result)
{
    auto* header = reinterpret_cast<NMHDR*>(lParam);
    if (!header || header->code != NM_CUSTOMDRAW)
        return false;
    auto* cd = reinterpret_cast<NMCUSTOMDRAW*>(lParam);

    if (auto button = g_buttons.find(header->hwndFrom); button != g_buttons.end())
    {
        if (cd->dwDrawStage == CDDS_PREPAINT)
        {
            PaintButton(header->hwndFrom, button->second, cd->hdc, cd->rc, cd->uItemState);
            result = CDRF_SKIPDEFAULT;
        }
        else
        {
            result = CDRF_DODEFAULT;
        }
        return true;
    }

    if (auto slider = g_sliders.find(header->hwndFrom); slider != g_sliders.end())
    {
        switch (cd->dwDrawStage)
        {
        case CDDS_PREPAINT:
            result = CDRF_NOTIFYITEMDRAW;
            return true;
        case CDDS_ITEMPREPAINT:
            if (cd->dwItemSpec == TBCD_CHANNEL)
                PaintSliderTrack(header->hwndFrom, slider->second, cd->hdc);
            else if (cd->dwItemSpec == TBCD_THUMB)
                PaintSliderThumb(header->hwndFrom, slider->second, cd->hdc, cd->uItemState);
            result = CDRF_SKIPDEFAULT;
            return true;
        default:
            result = CDRF_DODEFAULT;
            return true;
        }
    }
    return false;
}

bool HandleDrawItem(LPARAM lParam)
{
    auto* di = reinterpret_cast<DRAWITEMSTRUCT*>(lParam);
    if (!di || di->CtlType != ODT_LISTBOX)
        return false;
    PaintListItem(di);
    return true;
}

bool HandleMeasureItem(LPARAM lParam)
{
    auto* mi = reinterpret_cast<MEASUREITEMSTRUCT*>(lParam);
    if (!mi || mi->CtlType != ODT_LISTBOX)
        return false;
    mi->itemHeight = 30;
    return true;
}

HBRUSH HandleCtlColor(UINT message, HDC hdc, HWND control)
{
    wchar_t className[32] = {};
    GetClassNameW(control, className, static_cast<int>(std::size(className)));
    COLORREF bg = kSurface;
    COLORREF fg = IsWindowEnabled(control) ? kText : kTextDisabled;
    if (_wcsicmp(className, L"Edit") == 0 || _wcsicmp(className, L"ListBox") == 0)
    {
        bg = kField;
    }
    else if (auto button = g_buttons.find(control); button != g_buttons.end())
    {
        bg = button->second.parentBg;
    }
    (void)message;
    SetTextColor(hdc, fg);
    SetBkColor(hdc, bg);
    return Brush(bg);
}

void FillSolid(HDC hdc, const RECT& rc, COLORREF color)
{
    FillRect(hdc, &rc, Brush(color));
}

void FillRound(HDC hdc, const RECT& rc, float radius, COLORREF fill)
{
    if (!g_gdiplusReady)
    {
        FillSolid(hdc, rc, fill);
        return;
    }
    Gdiplus::Graphics g(hdc);
    PrepareGraphics(g);
    Gdiplus::GraphicsPath path;
    AddRoundRect(path, static_cast<float>(rc.left), static_cast<float>(rc.top),
                 static_cast<float>(rc.right - rc.left), static_cast<float>(rc.bottom - rc.top), radius);
    Gdiplus::SolidBrush brush(ToColor(fill));
    g.FillPath(&brush, &path);
}

void StrokeRound(HDC hdc, const RECT& rc, float radius, COLORREF stroke, float width)
{
    if (!g_gdiplusReady)
    {
        FrameRect(hdc, &rc, Brush(stroke));
        return;
    }
    Gdiplus::Graphics g(hdc);
    PrepareGraphics(g);
    Gdiplus::GraphicsPath path;
    const float inset = width / 2.0f;
    AddRoundRect(path, rc.left + inset, rc.top + inset,
                 (rc.right - rc.left) - width, (rc.bottom - rc.top) - width, radius - inset);
    Gdiplus::Pen pen(ToColor(stroke), width);
    g.DrawPath(&pen, &path);
}

void FillEllipse(HDC hdc, float cx, float cy, float radius, COLORREF fill, BYTE alpha)
{
    if (!g_gdiplusReady)
    {
        RECT rc{ static_cast<LONG>(cx - radius), static_cast<LONG>(cy - radius),
                 static_cast<LONG>(cx + radius), static_cast<LONG>(cy + radius) };
        FillSolid(hdc, rc, fill);
        return;
    }
    Gdiplus::Graphics g(hdc);
    PrepareGraphics(g);
    Gdiplus::SolidBrush brush(ToColor(fill, alpha));
    g.FillEllipse(&brush, cx - radius, cy - radius, radius * 2.0f, radius * 2.0f);
}

void DrawGlyph(HDC hdc, wchar_t glyphCode, const RECT& rc, COLORREF color, FontRole role)
{
    RECT r = rc;
    HGDIOBJ old = SelectObject(hdc, Font(role));
    SetTextColor(hdc, color);
    SetBkMode(hdc, TRANSPARENT);
    DrawTextW(hdc, &glyphCode, 1, &r, DT_CENTER | DT_VCENTER | DT_SINGLELINE | DT_NOPREFIX);
    SelectObject(hdc, old);
}
} // namespace ui
