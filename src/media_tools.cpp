#include "media_tools.h"
#include "editing.h"
#include "file_handling.h"
#include "media_processing.h"
#include "openfx_effect.h"
#include "video_player.h"
#include <algorithm>
#include <cmath>
#include <commctrl.h>
#include <commdlg.h>
#include <filesystem>
#include <functional>
#include <iomanip>
#include <sstream>
#include <thread>

extern VideoPlayer *g_videoPlayer;
extern HWND g_hListBoxAudioTracks;
namespace
{
std::wstring Wide(const std::string &s)
{
    int n = MultiByteToWideChar(CP_UTF8, 0, s.data(), static_cast<int>(s.size()), nullptr, 0);
    std::wstring result(n, L'\0');
    MultiByteToWideChar(CP_UTF8, 0, s.data(), static_cast<int>(s.size()), result.data(), n);
    return result;
}
std::string Utf8(const std::wstring &s)
{
    int n = WideCharToMultiByte(CP_UTF8, 0, s.data(), static_cast<int>(s.size()), nullptr, 0,
                                nullptr, nullptr);
    std::string result(n, '\0');
    WideCharToMultiByte(CP_UTF8, 0, s.data(), static_cast<int>(s.size()), result.data(), n, nullptr,
                        nullptr);
    return result;
}
struct DialogTemplate
{
    DLGTEMPLATE dialog{};
    WORD menu = 0, windowClass = 0, title = 0;
};
INT_PTR Dialog(HWND parent, DLGPROC proc, void *state, int width = 440, int height = 160)
{
    DialogTemplate t;
    t.dialog.style = WS_POPUP | WS_CAPTION | WS_SYSMENU | DS_MODALFRAME | DS_CENTER;
    t.dialog.cx = static_cast<short>(width);
    t.dialog.cy = static_cast<short>(height);
    return DialogBoxIndirectParamW(GetModuleHandle(nullptr), &t.dialog, parent, proc,
                                   reinterpret_cast<LPARAM>(state));
}
HWND Control(HWND parent, const wchar_t *type, const wchar_t *text, DWORD style, int x, int y,
             int w, int h, int id)
{
    HWND c = CreateWindowExW(wcscmp(type, L"EDIT") == 0 ? WS_EX_CLIENTEDGE : 0, type, text,
                             WS_CHILD | WS_VISIBLE | style, x, y, w, h, parent,
                             reinterpret_cast<HMENU>(static_cast<INT_PTR>(id)),
                             GetModuleHandle(nullptr), nullptr);
    SendMessage(c, WM_SETFONT, reinterpret_cast<WPARAM>(GetStockObject(DEFAULT_GUI_FONT)), TRUE);
    return c;
}
struct Job
{
    std::atomic<bool> &cancel;
    std::atomic<int> progress{0};
    std::function<void(const MediaProgress &)> work;
    std::thread worker;
    std::string error;
    const wchar_t *title;
};
constexpr UINT WM_JOB_DONE = WM_APP + 120;
INT_PTR CALLBACK JobProc(HWND window, UINT message, WPARAM w, LPARAM l)
{
    auto *job = reinterpret_cast<Job *>(GetWindowLongPtr(window, DWLP_USER));
    if (message == WM_INITDIALOG)
    {
        job = reinterpret_cast<Job *>(l);
        SetWindowLongPtr(window, DWLP_USER, l);
        SetWindowTextW(window, job->title);
        Control(window, L"STATIC", L"Processing media. You can cancel at any time.", 0, 16, 18, 470,
                24, 10);
        Control(window, PROGRESS_CLASSW, L"", 0, 16, 52, 470, 24, 11);
        SendDlgItemMessage(window, 11, PBM_SETRANGE32, 0, 100);
        Control(window, L"BUTTON", L"Cancel", WS_TABSTOP, 400, 96, 86, 28, IDCANCEL);
        SetTimer(window, 1, 100, nullptr);
        try
        {
            job->worker = std::thread([window, job] {
                try
                {
                    job->work([job](int p) { job->progress.store(p); });
                }
                catch (const std::exception &e)
                {
                    job->error = e.what();
                }
                catch (...)
                {
                    job->error = "Media processing failed.";
                }
                PostMessage(window, WM_JOB_DONE, 0, 0);
            });
        }
        catch (...)
        {
            job->error = "Could not start the media worker.";
            EndDialog(window, IDCANCEL);
        }
        return TRUE;
    }
    if (!job)
        return FALSE;
    if (message == WM_TIMER)
    {
        SendDlgItemMessage(window, 11, PBM_SETPOS, job->progress.load(), 0);
        return TRUE;
    }
    if (message == WM_CLOSE || (message == WM_COMMAND && LOWORD(w) == IDCANCEL))
    {
        job->cancel.store(true);
        EnableWindow(GetDlgItem(window, IDCANCEL), FALSE);
        SetDlgItemTextW(window, 10, L"Cancelling...");
        return TRUE;
    }
    if (message == WM_JOB_DONE)
    {
        if (job->worker.joinable())
            job->worker.join();
        KillTimer(window, 1);
        EndDialog(window, job->error.empty() ? IDOK : IDCANCEL);
        return TRUE;
    }
    return FALSE;
}
bool RunJob(HWND parent, const wchar_t *title, std::atomic<bool> &cancel,
            const std::function<void(const MediaProgress &)> &work)
{
    Job job{cancel};
    job.work = work;
    job.title = title;
    cancel = false;
    INT_PTR result = Dialog(parent, JobProc, &job, 310, 90);
    if (job.worker.joinable())
    {
        cancel = true;
        job.worker.join();
    }
    if (!job.error.empty() && !cancel.load())
        MessageBoxW(parent, Wide(job.error).c_str(), title, MB_ICONERROR);
    return result == IDOK && !cancel.load();
}
std::wstring SavePath(HWND parent, const std::wstring &input, const wchar_t *suffix)
{
    wchar_t path[32768] = {};
    auto proposed = std::filesystem::path(input).parent_path() /
                    (std::filesystem::path(input).stem().wstring() + suffix + L".mkv");
    wcsncpy_s(path, proposed.c_str(), _TRUNCATE);
    OPENFILENAMEW ofn{};
    ofn.lStructSize = sizeof(ofn);
    ofn.hwndOwner = parent;
    ofn.lpstrTitle = L"Save a new working copy (existing files are never overwritten)";
    ofn.lpstrFilter = L"Matroska working copy\0*.mkv\0";
    ofn.lpstrFile = path;
    ofn.nMaxFile = 32768;
    ofn.lpstrDefExt = L"mkv";
    ofn.Flags = OFN_PATHMUSTEXIST | OFN_NOCHANGEDIR;
    return GetSaveFileNameW(&ofn) ? path : L"";
}
bool Ready(HWND parent)
{
    if (g_isExporting || !g_videoPlayer || !g_videoPlayer->IsLoaded())
        return false;
    g_videoPlayer->Pause();
    return true;
}
struct Choice
{
    std::vector<std::string> labels;
    int selected = 0;
};
INT_PTR CALLBACK ChoiceProc(HWND window, UINT message, WPARAM w, LPARAM l)
{
    auto *choice = reinterpret_cast<Choice *>(GetWindowLongPtr(window, DWLP_USER));
    if (message == WM_INITDIALOG)
    {
        choice = reinterpret_cast<Choice *>(l);
        SetWindowLongPtr(window, DWLP_USER, l);
        SetWindowTextW(window, L"Select OpenFX effect");
        HWND list = Control(window, L"LISTBOX", L"",
                            WS_BORDER | WS_VSCROLL | WS_TABSTOP | LBS_NOTIFY, 12, 12, 500, 150, 10);
        for (auto &label : choice->labels)
            SendMessageW(list, LB_ADDSTRING, 0, reinterpret_cast<LPARAM>(Wide(label).c_str()));
        SendMessage(list, LB_SETCURSEL, 0, 0);
        Control(window, L"BUTTON", L"Select", WS_TABSTOP | BS_DEFPUSHBUTTON, 322, 176, 90, 28,
                IDOK);
        Control(window, L"BUTTON", L"Cancel", WS_TABSTOP, 422, 176, 90, 28, IDCANCEL);
        return TRUE;
    }
    if (message == WM_COMMAND && LOWORD(w) == IDOK && choice)
    {
        choice->selected = static_cast<int>(SendDlgItemMessage(window, 10, LB_GETCURSEL, 0, 0));
        if (choice->selected != LB_ERR)
            EndDialog(window, IDOK);
        return TRUE;
    }
    if (message == WM_CLOSE || (message == WM_COMMAND && LOWORD(w) == IDCANCEL))
    {
        EndDialog(window, IDCANCEL);
        return TRUE;
    }
    return FALSE;
}
struct Parameters
{
    OpenFxEffect &effect;
    std::vector<OpenFxParameter> values;
    int selected = 0;
};
void ShowParameter(HWND window, Parameters &p)
{
    const auto &v = p.values[p.selected];
    SetDlgItemTextW(window, 11, Wide(v.value).c_str());
    SetDlgItemTextW(window, 12, Wide(v.label + "\r\n" + v.hint).c_str());
}
bool SaveParameter(HWND window, Parameters &p)
{
    int n = GetWindowTextLengthW(GetDlgItem(window, 11));
    std::wstring text(n + 1, L'\0');
    GetDlgItemTextW(window, 11, text.data(), n + 1);
    text.resize(n);
    try
    {
        p.effect.SetParameter(p.values[p.selected].name, Utf8(text));
        p.values[p.selected].value = Utf8(text);
        return true;
    }
    catch (const std::exception &e)
    {
        MessageBoxW(window, Wide(e.what()).c_str(), L"OpenFX parameter", MB_ICONERROR);
        return false;
    }
}
INT_PTR CALLBACK ParametersProc(HWND window, UINT message, WPARAM w, LPARAM l)
{
    auto *p = reinterpret_cast<Parameters *>(GetWindowLongPtr(window, DWLP_USER));
    if (message == WM_INITDIALOG)
    {
        p = reinterpret_cast<Parameters *>(l);
        SetWindowLongPtr(window, DWLP_USER, l);
        SetWindowTextW(window, L"OpenFX parameters");
        HWND list = Control(window, L"LISTBOX", L"",
                            WS_BORDER | WS_VSCROLL | WS_TABSTOP | LBS_NOTIFY, 12, 12, 220, 210, 10);
        for (auto &value : p->values)
            SendMessageW(list, LB_ADDSTRING, 0,
                         reinterpret_cast<LPARAM>(Wide(value.label).c_str()));
        Control(window, L"EDIT", L"", WS_TABSTOP | ES_AUTOHSCROLL, 248, 12, 390, 28, 11);
        Control(window, L"STATIC", L"", 0, 248, 52, 390, 160, 12);
        Control(window, L"BUTTON", L"Render copy...", WS_TABSTOP | BS_DEFPUSHBUTTON, 420, 238, 110,
                28, IDOK);
        Control(window, L"BUTTON", L"Cancel", WS_TABSTOP, 540, 238, 98, 28, IDCANCEL);
        SendMessage(list, LB_SETCURSEL, 0, 0);
        ShowParameter(window, *p);
        return TRUE;
    }
    if (message == WM_COMMAND && p)
    {
        if (LOWORD(w) == 10 && HIWORD(w) == LBN_SELCHANGE)
        {
            int next = static_cast<int>(SendDlgItemMessage(window, 10, LB_GETCURSEL, 0, 0));
            if (SaveParameter(window, *p) && next != LB_ERR)
            {
                p->selected = next;
                ShowParameter(window, *p);
            }
            else
                SendDlgItemMessage(window, 10, LB_SETCURSEL, p->selected, 0);
            return TRUE;
        }
        if (LOWORD(w) == IDOK)
        {
            if (SaveParameter(window, *p))
                EndDialog(window, IDOK);
            return TRUE;
        }
    }
    if (message == WM_CLOSE || (message == WM_COMMAND && LOWORD(w) == IDCANCEL))
    {
        EndDialog(window, IDCANCEL);
        return TRUE;
    }
    return FALSE;
}
} // namespace

void AddMediaToolsMenu(HWND window)
{
    HMENU bar = CreateMenu(), tools = CreatePopupMenu();
    AppendMenuW(tools, MF_STRING, ID_MEDIA_ALIGN, L"Align audio tracks to selected track...");
    AppendMenuW(tools, MF_STRING, ID_MEDIA_OPENFX, L"Render OpenFX effect to copy...");
    AppendMenuW(bar, MF_POPUP, reinterpret_cast<UINT_PTR>(tools), L"Media");
    SetMenu(window, bar);
}
void AlignAudioTracks(HWND window)
{
    if (!Ready(window))
        return;
    int selected = static_cast<int>(SendMessage(g_hListBoxAudioTracks, LB_GETCURSEL, 0, 0));
    if (selected == LB_ERR || selected < 0 ||
        static_cast<size_t>(selected) >= g_videoPlayer->audioTracks.size() ||
        g_videoPlayer->audioTracks.size() < 2)
    {
        MessageBoxW(window,
                    L"Load a video with at least two audio tracks, then select the reference track "
                    L"in the audio list.",
                    L"Align audio", MB_ICONINFORMATION);
        return;
    }
    const auto input = g_videoPlayer->loadedFilename;
    const int reference = g_videoPlayer->audioTracks[selected]->streamIndex;
    std::atomic<bool> cancel{false};
    std::vector<AudioAlignment> matches;
    if (!RunJob(window, L"Analyzing shared audio", cancel, [&](const auto &p) {
            matches = AnalyzeAudioAlignment(input, reference, cancel, p);
        }))
        return;
    std::map<int, double> offsets;
    std::wostringstream report;
    report << L"Reference: " << Wide(g_videoPlayer->audioTracks[selected]->name) << L"\n\n";
    for (const auto &match : matches)
    {
        auto track =
            std::find_if(g_videoPlayer->audioTracks.begin(), g_videoPlayer->audioTracks.end(),
                         [&](const auto &t) { return t->streamIndex == match.streamIndex; });
        report << (track != g_videoPlayer->audioTracks.end() ? Wide((*track)->name) : L"Audio")
               << L": ";
        if (match.matched)
        {
            report << std::fixed << std::setprecision(3) << match.offsetSeconds << L" s, match "
                   << std::setprecision(0) << match.confidence * 100 << L"%\n";
            if (std::abs(match.offsetSeconds) > 1e-9)
                offsets[match.streamIndex] = match.offsetSeconds;
        }
        else
            report << L"No reliable match; unchanged.\n";
    }
    if (offsets.empty())
    {
        report << L"\nNo track needs a reliable adjustment.";
        MessageBoxW(window, report.str().c_str(), L"Audio alignment", MB_OK);
        return;
    }
    report
        << L"\nSave and open an aligned copy? The original is preserved.\nOpening the copy resets "
           L"cut and crop selections.\n\nAnalysis: first 120 seconds, up to +/-30 seconds. Fixed "
           L"offsets only.\nAdjusted audio is uncompressed, so the copy may be large.";
    if (MessageBoxW(window, report.str().c_str(), L"Audio alignment", MB_OKCANCEL) != IDOK)
        return;
    auto output = SavePath(window, input, L"-aligned");
    if (output.empty())
        return;
    if (RunJob(window, L"Saving aligned copy", cancel, [&](const auto &p) {
            ProcessMediaCopy(input, output, offsets, nullptr, cancel, p);
        }))
        LoadVideoFile(window, output);
}
void ApplyOpenFx(HWND window)
{
    if (!Ready(window))
        return;
    wchar_t path[32768] = {};
    OPENFILENAMEW ofn{};
    ofn.lStructSize = sizeof(ofn);
    ofn.hwndOwner = window;
    ofn.lpstrTitle = L"Select a Windows x64 OpenFX binary (bundle / Contents / Win64)";
    ofn.lpstrFilter = L"OpenFX effects\0*.ofx\0";
    ofn.lpstrFile = path;
    ofn.nMaxFile = 32768;
    ofn.Flags = OFN_FILEMUSTEXIST | OFN_PATHMUSTEXIST | OFN_NOCHANGEDIR;
    if (!GetOpenFileNameW(&ofn))
        return;
    std::atomic<bool> cancel{false};
    try
    {
        auto *stream = g_videoPlayer->formatContext->streams[g_videoPlayer->videoStreamIndex];
        double aspect = av_q2d(stream->sample_aspect_ratio);
        if (aspect <= 0)
            aspect = 1;
        auto make = [&](int i) {
            return std::make_unique<OpenFxEffect>(
                path, i, g_videoPlayer->frameWidth, g_videoPlayer->frameHeight,
                g_videoPlayer->frameRate, g_videoPlayer->duration, aspect, cancel);
        };
        Choice choice;
        {
            auto probe = make(-1);
            choice.labels = probe->PluginLabels();
        }
        if (choice.labels.size() > 1 && Dialog(window, ChoiceProc, &choice, 325, 135) != IDOK)
            return;
        auto effect = make(choice.selected);
        Parameters parameters{*effect, effect->Parameters()};
        if (!parameters.values.empty() &&
            Dialog(window, ParametersProc, &parameters, 405, 175) != IDOK)
            return;
        if (MessageBoxW(window,
                        L"Render the selected effect into a new lossless video copy?\nThe original "
                        L"is preserved. Opening the copy resets cut and crop selections.\n\nThe "
                        L"full source is processed. Working copies can be large.",
                        L"Render OpenFX", MB_OKCANCEL) != IDOK)
            return;
        auto input = g_videoPlayer->loadedFilename;
        auto output = SavePath(window, input, L"-effect");
        if (output.empty())
            return;
        if (RunJob(window, L"Rendering OpenFX copy", cancel, [&](const auto &p) {
                ProcessMediaCopy(input, output, {}, effect.get(), cancel, p);
            }))
            LoadVideoFile(window, output);
    }
    catch (const std::exception &e)
    {
        MessageBoxW(window, Wide(e.what()).c_str(), L"OpenFX", MB_ICONERROR);
    }
    catch (...)
    {
        MessageBoxW(window, L"This OpenFX effect could not be initialized.", L"OpenFX",
                    MB_ICONERROR);
    }
}
