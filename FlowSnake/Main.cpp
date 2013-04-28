#define WIN32_LEAN_AND_MEAN
#include <Windows.h>
#include <gl\GL.h>
#include <stdio.h> // Needed for debugging. Can disable for Release

/********** Defines *******************************/
#define countof(x) (sizeof(x)/sizeof(x[0])) // Defined in stdlib, but define here to avoid the header cost
#define IFC(x) if (FAILED(hr = x)) { goto Cleanup; }

/********** Function Declarations *****************/
LRESULT WINAPI MsgHandler(HWND hWnd, UINT msg, WPARAM wParam, LPARAM lParam);
void Error(const wchar_t* pStr, ...);

/********** Globals *******************************/

HRESULT Update(UINT deltaTime)
{
	glClearColor(0.1f, 0.1f, 0.2f, 0.0f);
	glClear(GL_COLOR_BUFFER_BIT);


	return S_OK;
}

HRESULT Render()
{
	return S_OK;
}

HRESULT InitWindow(HWND& hWnd, int width, int height)
{
    LPCWSTR wndName = L"Flow Snake";

	// Create our window
	WNDCLASSEX wndClass;
	wndClass.cbSize = sizeof(WNDCLASSEX);
	wndClass.style = CS_CLASSDC;
	wndClass.lpfnWndProc = MsgHandler;
	wndClass.cbClsExtra = 0;
	wndClass.cbWndExtra = 0;
	wndClass.hInstance = GetModuleHandle(0);
	wndClass.hIcon = NULL;
	wndClass.hCursor = LoadCursor(0, IDC_ARROW);
	wndClass.hbrBackground = NULL;
	wndClass.lpszMenuName = NULL;
	wndClass.lpszClassName = wndName;
	wndClass.hIconSm = NULL;
    RegisterClassEx(&wndClass);
	
    DWORD wndStyle = WS_SYSMENU | WS_CAPTION | WS_VISIBLE | WS_POPUP ;
    DWORD wndExStyle = WS_EX_APPWINDOW | WS_EX_WINDOWEDGE;

	RECT wndRect;
	SetRect(&wndRect, 0, 0, width, height);
    AdjustWindowRectEx(&wndRect, wndStyle, FALSE, wndExStyle);
    int wndWidth = wndRect.right - wndRect.left;
    int wndHeight = wndRect.bottom - wndRect.top;
    int wndTop = GetSystemMetrics(SM_CYSCREEN) / 2 - wndHeight / 2;
    int wndLeft = GetSystemMetrics(SM_XVIRTUALSCREEN) + 
				  GetSystemMetrics(SM_CXSCREEN) / 2 - wndWidth / 2;

	hWnd = CreateWindowEx(0, wndName, wndName, wndStyle, wndLeft, wndTop, 
						  wndWidth, wndHeight, NULL, NULL, NULL, NULL);
	if (hWnd == NULL)
	{
		Error(L"Window creation failed with error: %x\n", GetLastError());
		return E_FAIL;
	}

	return S_OK;
}

INT WINAPI WinMain(HINSTANCE hInst, HINSTANCE ignoreMe0, LPSTR ignoreMe1, INT ignoreMe2)
{
	HRESULT hr = S_OK;
	HWND hWnd = NULL;
	HDC hDC = NULL;
	HGLRC hRC = NULL;
	MSG msg = {};
	PIXELFORMATDESCRIPTOR pfd;
	
    LARGE_INTEGER previousTime;
    LARGE_INTEGER freqTime;

	IFC( InitWindow(hWnd, 1024, 768) );
    hDC = GetDC(hWnd);

    // Create the GL context.
    ZeroMemory(&pfd, sizeof(pfd));
    pfd.nSize = sizeof(pfd);
    pfd.nVersion = 1;
    pfd.dwFlags = PFD_DRAW_TO_WINDOW | PFD_SUPPORT_OPENGL | PFD_DOUBLEBUFFER;
    pfd.iPixelType = PFD_TYPE_RGBA;
    pfd.cColorBits = 24;
    pfd.cDepthBits = 16;
    pfd.iLayerType = PFD_MAIN_PLANE;
    int pixelFormat = ChoosePixelFormat(hDC, &pfd);
	SetPixelFormat(hDC, pixelFormat, &pfd);
    
	hRC = wglCreateContext(hDC);
    wglMakeCurrent(hDC, hRC);

    QueryPerformanceFrequency(&freqTime);
    QueryPerformanceCounter(&previousTime);
	
    // -------------------
    // Start the Game Loop
    // -------------------
    while (msg.message != WM_QUIT)
    {
        if (PeekMessage(&msg, 0, 0, 0, PM_REMOVE))
        {
            TranslateMessage(&msg); 
            DispatchMessage(&msg);
        }
        else
        {
            LARGE_INTEGER currentTime;
            __int64 elapsed;
            double deltaTime;

            QueryPerformanceCounter(&currentTime);
            elapsed = currentTime.QuadPart - previousTime.QuadPart;
            deltaTime = elapsed * 1000000.0 / freqTime.QuadPart;
            previousTime = currentTime;

            Update((UINT) deltaTime);
			Render();
            SwapBuffers(hDC);
            if (glGetError() != GL_NO_ERROR)
            {
                Error(L"OpenGL error.\n");
            }
        }
    }

Cleanup:
	if(hRC) wglDeleteContext(hRC);
    //UnregisterClassA(szName, wc.hInstance);

    return 0;
}

LRESULT WINAPI MsgHandler(HWND hWnd, UINT msg, WPARAM wParam, LPARAM lParam)
{
    switch (msg)
    {
	case WM_CLOSE:
		PostQuitMessage(0);
		break;

    case WM_KEYDOWN:
        switch (wParam)
        {
            case VK_ESCAPE:
                PostQuitMessage(0);
                break;
        }
        break;
    }

    return DefWindowProc(hWnd, msg, wParam, lParam);
}

void Error(const wchar_t* pStr, ...)
{
    wchar_t msg[1024] = {0};
	va_list args;
	va_start(args, pStr);
    _vsnwprintf_s(msg, countof(msg), _TRUNCATE, pStr, args);
    OutputDebugStringW(msg);
}
