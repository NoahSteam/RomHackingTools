///////////////////////////////////////////////////////////////////////////
#pragma comment(lib, "user32")

#include <windows.h>

#include "imgui.h"
#include "imgui_impl_win32.h"

#include "Platform\Windows\WindowsPlatform.h"
#include "Rendering\API\D3D11\D3D11Interface.h"

#include "resource.h"
#include "NSSaturnTool.h"

PlatformInterface* GPlatformInterface = nullptr;

HWND GWindowHandle;

static UINT g_ResizeWidth = 0, g_ResizeHeight = 0;
NSSaturnTool* GSaturnTool = nullptr;

///////////////////////////////////////////////////////////////////////////////////////////////////
// Forward declare message handler from imgui_impl_win32.cpp
extern IMGUI_IMPL_API LRESULT ImGui_ImplWin32_WndProcHandler(HWND hWnd, UINT msg, WPARAM wParam, LPARAM lParam);

LRESULT WINAPI WndProc(HWND hWnd, UINT msg, WPARAM wParam, LPARAM lParam)
{
	if (ImGui_ImplWin32_WndProcHandler(hWnd, msg, wParam, lParam))
	{
		return true;
	}

	switch (msg)
	{
		case WM_SIZE:
		{
			if (wParam == SIZE_MINIMIZED)
				return 0;
			g_ResizeWidth = (UINT)LOWORD(lParam); // Queue resize
			g_ResizeHeight = (UINT)HIWORD(lParam);
			return 0;
		}

		case WM_SYSCOMMAND:
		{
			if ((wParam & 0xfff0) == SC_KEYMENU) // Disable ALT application menu
				return 0;
			break;
		}

		case WM_DESTROY:
		{
			::PostQuitMessage(0);
			return 0;
		}

		case WM_COMMAND:
		{
			switch (LOWORD(wParam))
			{
				case ID_FILE_LOAD:
				{
					std::string fileName;
					if(GPlatformInterface->GetLoadFileName(fileName, NSFileTypes::Memory))
					{
						GSaturnTool->LoadMemoryFile(fileName);
					}
				}break;
			}
		}break;
	}
	return ::DefWindowProcW(hWnd, msg, wParam, lParam);
}


int WINAPI WinMain(HINSTANCE /*hInstance*/, HINSTANCE /*hPrevInstance*/, LPSTR /*lpCmdLine*/ , int /*nShowCmd*/)
{
	NSSaturnTool saturnTool;
	GSaturnTool = &saturnTool;

	WNDCLASSEXW winClass;
	winClass.cbSize        = sizeof(winClass);
	winClass.style         = CS_CLASSDC;
	winClass.lpfnWndProc   = WndProc;
	winClass.cbClsExtra    = sizeof(LONG_PTR);
	winClass.cbWndExtra    = 0;
	winClass.hInstance     = GetModuleHandle(0);
	winClass.hIcon         = 0;
	winClass.hCursor       = LoadCursor(NULL, IDC_CROSS);
	winClass.hbrBackground = (HBRUSH)GetStockObject(COLOR_WINDOWFRAME);
	winClass.lpszClassName = L"NSSaturnTool";
	winClass.lpszMenuName  = MAKEINTRESOURCEW(IDR_MENU1);
	winClass.hIconSm       = 0;

	::RegisterClassExW(&winClass);

	GWindowHandle = ::CreateWindowW(winClass.lpszClassName, L"NSSaturnTool", WS_OVERLAPPEDWINDOW, 100, 100, 1280, 720, nullptr, nullptr, winClass.hInstance, nullptr);

	GPlatformInterface = new WindowsPlatformInterface();
	GPlatformInterface->InitializePlatform();
	
	// Show the window
	::ShowWindow(GWindowHandle, SW_SHOWDEFAULT);
	::UpdateWindow(GWindowHandle);

	saturnTool.InitializeApp();

#define GET_X_LPARAM(lp) ((int)(short)LOWORD(lp))
#define GET_Y_LPARAM(lp) ((int)(short)HIWORD(lp))

	while (true)
	{
		if (g_ResizeWidth != 0 && g_ResizeHeight != 0)
		{
			GGraphicsInterface->OnRecreateResources(g_ResizeWidth, g_ResizeHeight);
			g_ResizeWidth = g_ResizeHeight = 0;
		}

		MSG msg;
		while (PeekMessageA(&msg, nullptr, 0, 0, PM_REMOVE))
		{
			switch(msg.message)
			{
				case WM_KEYDOWN:
				{
					switch (msg.wParam)
					{
						case VK_ESCAPE:
						{
							return 0;
						}
						break;
					}
				}break;

				case WM_MOUSEMOVE:
				{
					POINT pt;
					GetCursorPos(&pt); // Retrieves the cursor's position in screen coordinates
					ScreenToClient(GWindowHandle, &pt); // Converts the screen coordinates to client coordinates
				}break;
			}

			DispatchMessageA(&msg);
		}

		saturnTool.UpdateApp();
	}
}
