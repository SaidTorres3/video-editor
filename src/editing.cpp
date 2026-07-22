#include "editing.h"
#include "video_player.h"
#include "ui_updates.h"
#include "progress_window.h"
#include "options_window.h"
#include <commdlg.h>
#include <thread>
#include <string>
#include <filesystem>
#include <algorithm>
#include "b2_upload.h"
#include "catbox_upload.h"
#include "file_handling.h"

// Forward declarations
void UpdateCutInfoLabel(HWND hwnd);
void UpdateCutTimeEdits();
void UpdateTimeline();

// Build the resolved output path from exportation settings.
// Returns the full path including .mp4 extension.
static std::wstring BuildExportPath(const std::wstring& loadedFile)
{
    namespace fs = std::filesystem;
    // Determine folder
    std::wstring folder;
    if (!g_exportDefaultFolder.empty())
        folder = g_exportDefaultFolder;
    else if (!loadedFile.empty())
        folder = fs::path(loadedFile).parent_path().wstring();
    else if (!g_lastSaveDir.empty())
        folder = g_lastSaveDir;

    // Determine name from template
    std::wstring stem;
    if (!loadedFile.empty())
        stem = fs::path(loadedFile).stem().wstring();
    else
        stem = L"video";

    std::wstring name = g_exportSaveName;
    // Replace $[filename] with the original filename stem
    size_t pos = name.find(L"$[filename]");
    while (pos != std::wstring::npos) {
        name.replace(pos, 11, stem);
        pos = name.find(L"$[filename]", pos + stem.size());
    }

    if (folder.empty())
        return name + L".mp4";
    return (fs::path(folder) / (name + L".mp4")).wstring();
}

// Global variables
extern VideoPlayer *g_videoPlayer;
extern double g_cutStartTime, g_cutEndTime;
extern HWND g_hStatusText, g_hProgressBar;
extern EncoderSelection g_encoderSelection;
extern bool g_autoUpload;
extern bool g_useCatbox;
extern bool g_useB2;
std::wstring g_catboxUploadedUrl;
std::wstring g_b2UploadedUrl;
bool g_catboxUploadSuccess = false;
bool g_b2UploadSuccess = false;
bool g_uploadSuccess = false;
bool g_lastOperationWasExport = false;
bool g_isExporting = false;
std::wstring g_lastOutputFile;

static std::vector<ClipSegment> NormalizeSegments(std::vector<ClipSegment> segments, double duration)
{
    for (auto& segment : segments) {
        segment.start = std::clamp(segment.start, 0.0, duration);
        segment.end = std::clamp(segment.end, 0.0, duration);
    }
    segments.erase(std::remove_if(segments.begin(), segments.end(), [](const ClipSegment& segment) {
        return segment.end <= segment.start;
    }), segments.end());
    std::sort(segments.begin(), segments.end(), [](const ClipSegment& a, const ClipSegment& b) {
        return a.start < b.start;
    });

    std::vector<ClipSegment> normalized;
    for (const auto& segment : segments) {
        if (!normalized.empty() && segment.start <= normalized.back().end + 0.001)
            normalized.back().end = normalized.back().end > segment.end ? normalized.back().end : segment.end;
        else
            normalized.push_back(segment);
    }
    return normalized;
}

void OnSetStartClicked(HWND hwnd)
{
    if (!g_videoPlayer || !g_videoPlayer->IsLoaded()) return;
    g_cutStartTime = g_videoPlayer->GetCurrentTime();
    if (g_cutEndTime >= 0 && g_cutStartTime >= g_cutEndTime)
    {
        g_cutEndTime = -1.0; // Invalidate end time if it's before new start time
    }
    UpdateCutInfoLabel(hwnd);
    UpdateCutTimeEdits();
    UpdateTimeline();
}

void OnSetEndClicked(HWND hwnd)
{
    if (!g_videoPlayer || !g_videoPlayer->IsLoaded()) return;
    double currentTime = g_videoPlayer->GetCurrentTime();
    if (g_cutStartTime >= 0 && currentTime <= g_cutStartTime)
    {
        MessageBoxW(hwnd, L"End point must be after the start point.", L"Invalid Time", MB_OK | MB_ICONWARNING);
        return;
    }
    g_cutEndTime = currentTime;
    UpdateCutInfoLabel(hwnd);
    UpdateCutTimeEdits();
    UpdateTimeline();
}

void OnAddClipClicked(HWND hwnd)
{
    if (!g_enableMultiClipEditing)
        return;
    if (!g_videoPlayer || g_cutStartTime < 0 || g_cutEndTime <= g_cutStartTime) {
        MessageBoxW(hwnd, L"Set a valid start and end point before adding a clip.", L"Invalid Clip", MB_OK | MB_ICONWARNING);
        return;
    }

    g_cutSegments.push_back({g_cutStartTime, g_cutEndTime});
    g_cutSegments = NormalizeSegments(std::move(g_cutSegments), g_videoPlayer->GetDuration());
    g_selectedCutSegment = -1;
    g_cutStartTime = -1.0;
    g_cutEndTime = -1.0;
    RefreshCutSegmentList();
    UpdateCutInfoLabel(hwnd);
    UpdateCutTimeEdits();
    UpdateTimeline();
}

void OnUpdateClipClicked(HWND hwnd)
{
    if (!g_enableMultiClipEditing)
        return;
    if (!g_videoPlayer || g_selectedCutSegment < 0 ||
        g_selectedCutSegment >= static_cast<int>(g_cutSegments.size()) ||
        g_cutStartTime < 0 || g_cutEndTime <= g_cutStartTime) {
        MessageBoxW(hwnd, L"Select a clip and set valid start and end points before updating it.",
                    L"Invalid Clip", MB_OK | MB_ICONWARNING);
        return;
    }

    const double midpoint = (g_cutStartTime + g_cutEndTime) * 0.5;
    g_cutSegments[g_selectedCutSegment] = {g_cutStartTime, g_cutEndTime};
    g_cutSegments = NormalizeSegments(std::move(g_cutSegments), g_videoPlayer->GetDuration());
    g_selectedCutSegment = -1;
    for (size_t i = 0; i < g_cutSegments.size(); ++i) {
        if (midpoint >= g_cutSegments[i].start && midpoint <= g_cutSegments[i].end) {
            g_selectedCutSegment = static_cast<int>(i);
            break;
        }
    }
    if (g_selectedCutSegment >= 0) {
        g_cutStartTime = g_cutSegments[g_selectedCutSegment].start;
        g_cutEndTime = g_cutSegments[g_selectedCutSegment].end;
    }
    RefreshCutSegmentList();
    UpdateCutInfoLabel(hwnd);
    UpdateCutTimeEdits();
    UpdateTimeline();
}

void OnRemoveClipClicked(HWND hwnd)
{
    if (!g_enableMultiClipEditing)
        return;
    if (g_selectedCutSegment < 0 || g_selectedCutSegment >= static_cast<int>(g_cutSegments.size()))
        return;
    g_cutSegments.erase(g_cutSegments.begin() + g_selectedCutSegment);
    g_selectedCutSegment = -1;
    g_cutStartTime = -1.0;
    g_cutEndTime = -1.0;
    RefreshCutSegmentList();
    UpdateCutInfoLabel(hwnd);
    UpdateCutTimeEdits();
    UpdateTimeline();
}

void OnClearClipsClicked(HWND hwnd)
{
    if (!g_enableMultiClipEditing)
        return;
    g_cutSegments.clear();
    g_selectedCutSegment = -1;
    g_cutStartTime = -1.0;
    g_cutEndTime = -1.0;
    RefreshCutSegmentList();
    UpdateCutInfoLabel(hwnd);
    UpdateCutTimeEdits();
    UpdateTimeline();
}

void OnPlayAllClipsClicked(HWND hwnd)
{
    if (!g_enableMultiClipEditing)
        return;
    if (!g_videoPlayer || g_cutSegments.empty()) {
        MessageBoxW(hwnd, L"Add at least one clip before previewing the sequence.",
                    L"No Clips", MB_OK | MB_ICONINFORMATION);
        return;
    }
    g_videoPlayer->PlayClips(g_cutSegments);
    UpdateControls();
}

void OnCutSegmentSelectionChanged(HWND hwnd)
{
    if (!g_enableMultiClipEditing)
        return;
    extern HWND g_hListBoxCutSegments;
    int selected = static_cast<int>(SendMessage(g_hListBoxCutSegments, LB_GETCURSEL, 0, 0));
    if (selected == LB_ERR || selected < 0 || selected >= static_cast<int>(g_cutSegments.size())) {
        g_selectedCutSegment = -1;
        UpdateControls();
        return;
    }

    g_selectedCutSegment = selected;
    g_cutStartTime = g_cutSegments[selected].start;
    g_cutEndTime = g_cutSegments[selected].end;
    if (g_videoPlayer && g_videoPlayer->IsLoaded()) {
        if (g_videoPlayer->IsPlaying())
            g_videoPlayer->SeekWhilePlaying(g_cutStartTime);
        else
            g_videoPlayer->SeekToTimeExact(g_cutStartTime);
    }
    UpdateCutInfoLabel(hwnd);
    UpdateCutTimeEdits();
    UpdateTimeline();
}

void OnPlayClipClicked(HWND hwnd)
{
    if (!g_videoPlayer || g_cutStartTime < 0 || g_cutEndTime <= g_cutStartTime)
    {
        MessageBoxW(hwnd, L"Please set valid start and end points for the clip.", L"Error", MB_OK | MB_ICONERROR);
        return;
    }
    g_videoPlayer->PlayClip(g_cutStartTime, g_cutEndTime);
    UpdateControls();
}

void OnPlayEndClipClicked(HWND hwnd)
{
    if (!g_videoPlayer || g_cutStartTime < 0 || g_cutEndTime <= g_cutStartTime)
    {
        MessageBoxW(hwnd, L"Please set valid start and end points for the clip.", L"Error", MB_OK | MB_ICONERROR);
        return;
    }

    double previewStart = g_cutEndTime - 3.0; // Play 3 seconds before the end
    if (previewStart < g_cutStartTime)
        previewStart = g_cutStartTime;
    if (previewStart < 0)
        previewStart = 0;

    g_videoPlayer->PlayClip(previewStart, g_cutEndTime);
    UpdateControls();
}

void OnCutClicked(HWND hwnd)
{
    if (g_isExporting)
        return;
    g_lastOperationWasExport = false;
    if (!g_videoPlayer)
    {
        return;
    }

    std::vector<ClipSegment> segments = g_enableMultiClipEditing
                                        ? g_cutSegments
                                        : std::vector<ClipSegment>{};
    if (g_cutStartTime >= 0 && g_cutEndTime > g_cutStartTime) {
        if (g_enableMultiClipEditing && g_selectedCutSegment >= 0 &&
            g_selectedCutSegment < static_cast<int>(segments.size()))
            segments[g_selectedCutSegment] = {g_cutStartTime, g_cutEndTime};
        else
            segments.push_back({g_cutStartTime, g_cutEndTime});
    }
    segments = NormalizeSegments(std::move(segments), g_videoPlayer->GetDuration());
    if (segments.empty())
    {
        MessageBoxW(hwnd, L"Please add at least one valid clip.", L"Error", MB_OK | MB_ICONERROR);
        return;
    }

    OPENFILENAMEW ofn;
    wchar_t szFile[260] = { 0 };
    std::wstring resolvedPath;

    if (g_exportAutoSave) {
        resolvedPath = BuildExportPath(g_videoPlayer ? g_videoPlayer->loadedFilename : L"");
        if (std::filesystem::exists(resolvedPath)) {
            int res = MessageBoxW(hwnd, (L"File already exists:\n" + resolvedPath + L"\n\nOverwrite?").c_str(),
                                  L"Confirm Overwrite", MB_YESNO | MB_ICONWARNING);
            if (res != IDYES)
                return;
        }
    } else {
        // Pre-fill with export settings
        std::wstring prefilledPath = BuildExportPath(g_videoPlayer ? g_videoPlayer->loadedFilename : L"");
        wcsncpy_s(szFile, prefilledPath.c_str(), _TRUNCATE);

        ZeroMemory(&ofn, sizeof(ofn));
        ofn.lStructSize = sizeof(ofn);
        ofn.hwndOwner = hwnd;
        ofn.lpstrFile = szFile;
        ofn.nMaxFile = sizeof(szFile) / sizeof(wchar_t);
        ofn.lpstrFilter = L"MP4 Video\0*.mp4\0All Files\0*.*\0";
        ofn.nFilterIndex = 1;
        ofn.Flags = OFN_PATHMUSTEXIST | OFN_OVERWRITEPROMPT;
        ofn.lpstrDefExt = L"mp4";

        std::wstring initDir;
        if (!g_exportDefaultFolder.empty())
            initDir = g_exportDefaultFolder;
        else if (!g_lastSaveDir.empty())
            initDir = g_lastSaveDir;
        if (!initDir.empty())
            ofn.lpstrInitialDir = initDir.c_str();

        if (!GetSaveFileNameW(&ofn))
            return;

        resolvedPath = szFile;
        g_lastSaveDir = std::filesystem::path(szFile).parent_path().wstring();
    }

    {
        SetWindowTextW(g_hStatusText, L"Cutting video... You can keep using the editor.");

        bool mergeAudio = IsDlgButtonChecked(hwnd, 1014) == BST_CHECKED; // ID_CHECKBOX_MERGE_AUDIO
        bool convertH264 = SendMessage(GetDlgItem(hwnd, 1016), BM_GETCHECK, 0, 0) == BST_CHECKED; // ID_RADIO_H264
        if (segments.size() > 1)
            convertH264 = true;
        wchar_t bitrateText[32];
        GetWindowTextW(GetDlgItem(hwnd, 1017), bitrateText, 32); // ID_EDIT_BITRATE
        int bitrate = _wtoi(bitrateText);

        wchar_t sizeText[32];
        GetWindowTextW(GetDlgItem(hwnd, 1022), sizeText, 32); // ID_EDIT_TARGETSIZE
        int targetSize = _wtoi(sizeText);

        bool useSize = SendMessage(GetDlgItem(hwnd, 1025), BM_GETCHECK, 0, 0) == BST_CHECKED; // ID_RADIO_USE_SIZE

        double selectedDuration = 0.0;
        for (const auto& segment : segments)
            selectedDuration += segment.end - segment.start;

        if (convertH264 && useSize && targetSize > 0) {
            double duration = selectedDuration;
            int audioKbps = 0;
            if (mergeAudio) {
                audioKbps = 128; // single AAC track
            } else {
                for (const auto& track : g_videoPlayer->audioTracks) {
                    if (track->isMuted) continue;
                    AVCodecParameters* par = g_videoPlayer->formatContext->streams[track->streamIndex]->codecpar;
                    int64_t br = par->bit_rate > 0 ? par->bit_rate : 128000;
                    audioKbps += static_cast<int>(br / 1000);
                }
            }
            int totalKbps = static_cast<int>((targetSize * 8192) / duration);
            bitrate = totalKbps > audioKbps ? (totalKbps - audioKbps) : totalKbps / 2;
        }

        g_isExporting = true;
        UpdateControls();
        ShowProgressWindow(hwnd);
        UpdateProgressStatus(L"Preparing to cut video...");
        std::wstring outFile = resolvedPath;
        std::thread([hwnd, outFile, mergeAudio, convertH264, bitrate, segments]() {
            g_uploadSuccess = false;
            g_catboxUploadSuccess = false;
            g_b2UploadSuccess = false;
            g_catboxUploadedUrl.clear();
            g_b2UploadedUrl.clear();
            bool ok = g_videoPlayer->CutVideo(outFile, segments,
                                             mergeAudio, convertH264, g_encoderSelection, g_qualityPreset,
                                             bitrate, g_hProgressBar, &g_cancelExport);
            g_lastOutputFile = ok ? outFile : L"";
            if (ok && g_autoUpload && (g_useCatbox || g_useB2)) {
                UpdateProgressStatus(L"Uploading video...");
                std::wstring title = L"Uploading to ";
                if (g_useCatbox && g_useB2)
                    title += L"catbox.moe and Backblaze B2";
                else if (g_useCatbox)
                    title += L"catbox.moe";
                else
                    title += L"Backblaze B2";
                SetWindowTextW(g_hProgressWindow, title.c_str());
                if (g_useCatbox) {
                    std::string url;
                    if (UploadToCatbox(outFile, url, g_hProgressBar)) {
                        int sz = MultiByteToWideChar(CP_UTF8, 0, url.c_str(), -1, nullptr, 0);
                        g_catboxUploadedUrl.assign(sz - 1, 0);
                        MultiByteToWideChar(CP_UTF8, 0, url.c_str(), -1, g_catboxUploadedUrl.data(), sz);
                        g_catboxUploadSuccess = true;
                    }
                }
                if (g_useB2) {
                    std::string url;
                    if (UploadToB2(outFile, url, g_hProgressBar)) {
                        int sz = MultiByteToWideChar(CP_UTF8, 0, url.c_str(), -1, nullptr, 0);
                        g_b2UploadedUrl.assign(sz - 1, 0);
                        MultiByteToWideChar(CP_UTF8, 0, url.c_str(), -1, g_b2UploadedUrl.data(), sz);
                        g_b2UploadSuccess = true;
                    }
                }
                g_uploadSuccess = (!g_useCatbox || g_catboxUploadSuccess) && (!g_useB2 || g_b2UploadSuccess);
            }
            PostMessage(hwnd, (WM_APP + 1), ok ? 1 : 0, 0); // WM_APP_CUT_DONE
        }).detach();
    }
}

void OnExportClicked(HWND hwnd)
{
    if (g_isExporting)
        return;
    g_lastOperationWasExport = true;
    if (!g_videoPlayer)
        return;

    OPENFILENAMEW ofn;
    wchar_t szFile[260] = { 0 };
    std::wstring resolvedPath;

    if (g_exportAutoSave) {
        resolvedPath = BuildExportPath(g_videoPlayer->loadedFilename);
        if (std::filesystem::exists(resolvedPath)) {
            int res = MessageBoxW(hwnd, (L"File already exists:\n" + resolvedPath + L"\n\nOverwrite?").c_str(),
                                  L"Confirm Overwrite", MB_YESNO | MB_ICONWARNING);
            if (res != IDYES)
                return;
        }
    } else {
        // Pre-fill with export settings
        std::wstring prefilledPath = BuildExportPath(g_videoPlayer->loadedFilename);
        wcsncpy_s(szFile, prefilledPath.c_str(), _TRUNCATE);

        ZeroMemory(&ofn, sizeof(ofn));
        ofn.lStructSize = sizeof(ofn);
        ofn.hwndOwner = hwnd;
        ofn.lpstrFile = szFile;
        ofn.nMaxFile = sizeof(szFile) / sizeof(wchar_t);
        ofn.lpstrFilter = L"MP4 Video\0*.mp4\0All Files\0*.*\0";
        ofn.nFilterIndex = 1;
        ofn.Flags = OFN_PATHMUSTEXIST | OFN_OVERWRITEPROMPT;
        ofn.lpstrDefExt = L"mp4";

        std::wstring initDir;
        if (!g_exportDefaultFolder.empty())
            initDir = g_exportDefaultFolder;
        else if (!g_lastSaveDir.empty())
            initDir = g_lastSaveDir;
        if (!initDir.empty())
            ofn.lpstrInitialDir = initDir.c_str();

        if (!GetSaveFileNameW(&ofn))
            return;

        resolvedPath = szFile;
        g_lastSaveDir = std::filesystem::path(szFile).parent_path().wstring();
    }

    {
        SetWindowTextW(g_hStatusText, L"Exporting video... You can keep using the editor.");

        bool mergeAudio = IsDlgButtonChecked(hwnd, 1014) == BST_CHECKED;
        bool convertH264 = SendMessage(GetDlgItem(hwnd, 1016), BM_GETCHECK, 0, 0) == BST_CHECKED;
        wchar_t bitrateText[32];
        GetWindowTextW(GetDlgItem(hwnd, 1017), bitrateText, 32);
        int bitrate = _wtoi(bitrateText);

        wchar_t sizeText[32];
        GetWindowTextW(GetDlgItem(hwnd, 1022), sizeText, 32);
        int targetSize = _wtoi(sizeText);

        bool useSize = SendMessage(GetDlgItem(hwnd, 1025), BM_GETCHECK, 0, 0) == BST_CHECKED;

        double startTime = 0.0;
        double endTime = g_videoPlayer->GetDuration();

        if (convertH264 && useSize && targetSize > 0) {
            double duration = endTime - startTime;
            int audioKbps = 0;
            if (mergeAudio) {
                audioKbps = 128;
            } else {
                for (const auto& track : g_videoPlayer->audioTracks) {
                    if (track->isMuted) continue;
                    AVCodecParameters* par = g_videoPlayer->formatContext->streams[track->streamIndex]->codecpar;
                    int64_t br = par->bit_rate > 0 ? par->bit_rate : 128000;
                    audioKbps += static_cast<int>(br / 1000);
                }
            }
            int totalKbps = static_cast<int>((targetSize * 8192) / duration);
            bitrate = totalKbps > audioKbps ? (totalKbps - audioKbps) : totalKbps / 2;
        }

        g_isExporting = true;
        UpdateControls();
        ShowProgressWindow(hwnd);
        UpdateProgressStatus(L"Preparing to export video...");
        std::wstring outFile = resolvedPath;
        std::thread([hwnd, outFile, mergeAudio, convertH264, bitrate, startTime, endTime]() {
            g_uploadSuccess = false;
            g_catboxUploadSuccess = false;
            g_b2UploadSuccess = false;
            g_catboxUploadedUrl.clear();
            g_b2UploadedUrl.clear();
            bool ok = g_videoPlayer->CutVideo(outFile, startTime, endTime,
                                             mergeAudio, convertH264, g_encoderSelection, g_qualityPreset,
                                             bitrate, g_hProgressBar, &g_cancelExport);
            g_lastOutputFile = ok ? outFile : L"";
            if (ok && g_autoUpload && (g_useCatbox || g_useB2)) {
                std::wstring title = L"Uploading to ";
                if (g_useCatbox && g_useB2)
                    title += L"catbox.moe and Backblaze B2";
                else if (g_useCatbox)
                    title += L"catbox.moe";
                else
                    title += L"Backblaze B2";
                SetWindowTextW(g_hProgressWindow, title.c_str());
                if (g_useCatbox) {
                    std::string url;
                    if (UploadToCatbox(outFile, url, g_hProgressBar)) {
                        int sz = MultiByteToWideChar(CP_UTF8, 0, url.c_str(), -1, nullptr, 0);
                        g_catboxUploadedUrl.assign(sz - 1, 0);
                        MultiByteToWideChar(CP_UTF8, 0, url.c_str(), -1, g_catboxUploadedUrl.data(), sz);
                        g_catboxUploadSuccess = true;
                    }
                }
                if (g_useB2) {
                    std::string url;
                    if (UploadToB2(outFile, url, g_hProgressBar)) {
                        int sz = MultiByteToWideChar(CP_UTF8, 0, url.c_str(), -1, nullptr, 0);
                        g_b2UploadedUrl.assign(sz - 1, 0);
                        MultiByteToWideChar(CP_UTF8, 0, url.c_str(), -1, g_b2UploadedUrl.data(), sz);
                        g_b2UploadSuccess = true;
                    }
                }
                g_uploadSuccess = (!g_useCatbox || g_catboxUploadSuccess) && (!g_useB2 || g_b2UploadSuccess);
            }
            PostMessage(hwnd, (WM_APP + 1), ok ? 1 : 0, 0);
        }).detach();
    }
}
