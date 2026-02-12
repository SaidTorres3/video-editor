#pragma once

#include <d3d11.h>
#include <string>

// Forward declarations
class VideoPlayer;

// ---- DX11 globals (used by main.cpp and imgui_ui.cpp) ----
extern ID3D11Device*            g_pd3dDevice;
extern ID3D11DeviceContext*     g_pd3dDeviceContext;
extern IDXGISwapChain*          g_pSwapChain;
extern ID3D11RenderTargetView*  g_mainRenderTargetView;
extern bool                     g_SwapChainOccluded;
extern UINT                     g_ResizeWidth;
extern UINT                     g_ResizeHeight;

// ---- DX11 helper functions ----
bool CreateDeviceD3D(HWND hWnd);
void CleanupDeviceD3D();
void CreateRenderTarget();
void CleanupRenderTarget();

// ---- ImGui UI rendering ----
void InitImGuiStyle();
void RenderUI(HWND hwnd);
void CleanupVideoTexture();

// ---- Video texture for ImGui ----
extern ID3D11ShaderResourceView* g_pVideoSRV;
extern ID3D11Texture2D*          g_pVideoTexture;
extern int                       g_videoTexWidth;
extern int                       g_videoTexHeight;

void UpdateVideoTexture();
