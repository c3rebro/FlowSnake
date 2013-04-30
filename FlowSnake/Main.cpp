#define WIN32_LEAN_AND_MEAN
#include <Windows.h>
#include <GL\GL.h>
#include <math.h>  // sqrt
#include <stdio.h> // _vsnwprintf_s. Can disable for Release
#include "glext.h" // glGenBuffers, glBindBuffers, ...


/********** Defines *******************************/
#define countof(x) (sizeof(x)/sizeof(x[0])) // Defined in stdlib, but define here to avoid the header cost
#ifdef _DEBUG
#	define IFC(x) { if (FAILED(hr = x)) { char buf[256]; sprintf_s(buf, "IFC Failed at Line %u\n", __LINE__); OutputDebugString(buf); goto Cleanup; } }
#	define ASSERT(x) if (!(x)) { OutputDebugString("Assert Failed!\n"); DebugBreak(); }
#else
#	define IFC(x) {if (FAILED(hr = x)) { goto Cleanup; }}
#	define ASSERT(x)
#endif

#define E_NOTARGETS 0x8000000f
#define MAX_SHORTF 32767.0f
#define MAX_INTF 2147483647.0f 

#define ATTRIBS_MASK_HASPARENT 0x8000
#define ATTRIBS_MASK_HASCHILD  0x4000
#define ATTRIBS_MASK_TARGETID  0x3FFF

#define ATTRIB_HASPARENT(x) (x & ATTRIBS_MASK_HASPARENT)
#define ATTRIB_HASCHILD(x)  (x & ATTRIBS_MASK_HASCHILD)
#define ATTRIB_TARGETID(x)  (x & ATTRIBS_MASK_TARGETID)

typedef UINT uint;
typedef unsigned short ushort;

/********** Function Declarations *****************/
LRESULT WINAPI MsgHandler(HWND hWnd, uint msg, WPARAM wParam, LPARAM lParam);
void Error(const char* pStr, ...);
float frand();
short srand();

PFNGLGENBUFFERSPROC glGenBuffers;
PFNGLBINDBUFFERPROC glBindBuffer;
PFNGLBUFFERDATAPROC glBufferData;
PFNGLENABLEVERTEXATTRIBARRAYPROC glEnableVertexAttribArray;
PFNGLVERTEXATTRIBPOINTERPROC glVertexAttribPointer;
PFNGLCREATEPROGRAMPROC glCreateProgram;
PFNGLDELETEPROGRAMPROC glDeleteProgram;
PFNGLUSEPROGRAMPROC	   glUseProgram;  
PFNGLATTACHSHADERPROC  glAttachShader;  
PFNGLLINKPROGRAMPROC   glLinkProgram;   
PFNGLGETPROGRAMIVPROC  glGetProgramiv;  
PFNGLGETPROGRAMINFOLOGPROC glGetProgramInfoLog;
PFNGLGETSHADERINFOLOGPROC glGetShaderInfoLog;
PFNGLCREATESHADERPROC glCreateShader; 
PFNGLDELETESHADERPROC glDeleteShader; 
PFNGLSHADERSOURCEPROC glShaderSource;
PFNGLCOMPILESHADERPROC glCompileShader;
PFNGLGETSHADERIVPROC glGetShaderiv;

/********** Structure Definitions******************/
struct float2
{
	float x;
	float y;

	float2 operator= (float a)
	{
		x = a;
		y = a;
	}

	float getLength()
	{
		if (x == 0 && y == 0)
			return 0;
		else
			return sqrt(x*x + y*y);
	}

	float2 operator- (float2 a)
	{
		float2 ret = {x - a.x, y - a.y};
		return ret;
	}

	float2 operator+ (float2 a)
	{
		float2 ret = {x + a.x, y + a.y};
		return ret;
	}

	float2 operator/ (float a)
	{
		float2 ret = {x/a, y/a};
		return ret;
	}
	
	float2 operator* (float a)
	{
		float2 ret = {x*a, y*a};
		return ret; 
	}
};

struct short2
{
	void setX(float a)
	{
		//x = int(a * MAX_SHORTF);
		x=a;
	}

	void setY(float a)
	{
		//y = int(a * MAX_SHORTF);
		y=a;
	}

	float getX()
	{
		//return x/MAX_SHORTF;
		return x;
	}

	float getY()
	{
		//return y/MAX_SHORTF;
		return y;
	}

	//short x;
	//short y;
	float x;
	float y;
};

struct Attribs 
{
	ushort hasParent  : 1;
	ushort hasChild   : 1;
	ushort targetID : 14;
};

/********** Global Constants***********************/
const uint g_numVerts = 500;
const float g_tailDist = 0.001f;
const float g_speed = 0.3f; // in Screens per second

/********** Globals Variables *********************/
// Use SOA instead of AOS from optimal cache usage
// We'll traverse each array linearly in each stage of the algorithm

// Initialize these to nonzero so they go into .DATA and not .BSS (and show in the executable size)
short2  g_positions[g_numVerts] = {{1,1}};
Attribs g_attribs[g_numVerts] = {{0, 0, 1}};

// 128k total memory. 
// I think I can pack particle attributes into 6 bytes.
// (We'll need to do some complex searching at chomp time, to avoid our tail)
// Targeting 16000 particles, that leaves 35072 bytes (34.25k)
// Leave 4.25k for the .exe and extra .data
// That's 30k for the spatial partioning scheme
// With run-time variable resolution we'll have to do some math to calculate bin size
// What's fixed: the number of bins, or their size? ... the number of bins (should make the math easier)
// TotalBinCost = 2 * SlotsPerBin * NumBins ....
// No, no. How about a dynamic number of bins, based on the number of active particles. 
// Start with 16k particles, 1 bin for every 4 particles. 4000 bins, 4 slots each, 2 bytes per slot. 
// Then reduce the number of active bins every time we halve the number of active particles.
// Well it's a start. That's 32000 bytes for spatial partitioning, plus 96000 for the particles = 128000. 
// That leaves 3072 bytes for the .exe and extra .data. Close to doable!
short g_bins[16000];

GLuint g_vboPos;
bool g_endgame = false;
uint g_width;
uint g_height;

/**************************************************/

inline bool IsValidTarget(short target, short current)
{
	if (target == current) return false;					// Can't chase ourselves
	if (g_attribs[target].hasChild == true) return false;	// It can't already have a child

	// Can't chase our own tail
	short chain = target;
	while (g_attribs[chain].hasParent)
	{
		chain = g_attribs[chain].targetID;
		if (chain == current)
		{
			return false;
		}
	}

	return true;
}

float SmoothStep(float a, float b, float t)
{
	return a + (pow(t,2)*(3-2*t))*(b - a);
}

HRESULT EndgameUpdate(uint deltaTime)
{
	//static uint absoluteTime = 0;
	//const float timeLimit = 5000000.0f; // 5 seconds

	//absoluteTime += deltaTime;

	//for (uint i = 0; i < g_numVerts; i++)
	//{	
	//	if (g_positions[i].x > 1.0f || g_positions[i].x < 0.0f)
	//		g_vectors[i].x = -g_vectors[i].x;
	//	if (g_positions[i].y > 1.0f || g_positions[i].y < 0.0f)
	//		g_vectors[i].y = -g_vectors[i].y;

	//	float velx = SmoothStep(g_vectors[i].x, 0.0f, float(absoluteTime)/timeLimit);
	//	float vely = SmoothStep(g_vectors[i].y, 0.0f, float(absoluteTime)/timeLimit);

	//	g_positions[i].x += + velx * float(deltaTime)/1000000.0f;
	//	g_positions[i].y += + vely * float(deltaTime)/1000000.0f;
	//}

	//if (absoluteTime > timeLimit)
	//{
	//	g_endgame = false;
	//	absoluteTime = 0;

	//	for (uint i = 0; i < g_numVerts; i++)
	//	{
	//		g_hasParent[i] = false;
	//		g_hasChild[i] = false;
	//	}
	//}

	return S_OK;
}

HRESULT Endgame()
{
	//g_endgame = true;

	//// TODO: Add "shaking" before we explode. The snake should continue
	////		 to swim along, then start vibrating, then EXPLODE.

	//for (uint i = 0; i < g_numVerts; i++)
	//{
	//	float maxVelocity = 0.5f; // screens per second
	//	g_vectors[i].x = (rand()*2.0f - 1.0f) * maxVelocity;
	//	g_vectors[i].y = (rand()*2.0f - 1.0f) * maxVelocity;
	//}

	return S_OK;
}

HRESULT FindNearestNeighbor(short i)
{
	if (g_attribs[i].hasParent == false) // Find the nearest potential parent
	{
		short nearest = -1;
		float minDist = 1.0f;

		for (uint j = 0; j < g_numVerts; j++)
		{
			if (IsValidTarget(j, i))
			{
				float diffx = g_positions[j].getX() - g_positions[i].getX();
				float diffy = g_positions[j].getY() - g_positions[i].getY();
				float dist = sqrt(diffx*diffx + diffy*diffy) / MAX_INTF;
				if (dist < minDist)
				{
					minDist = dist;
					nearest = j;
				}
			}
		}

		if (nearest == -1)
			return E_NOTARGETS;

		g_attribs[i].targetID = nearest;
	}

	return S_OK;
}

HRESULT Chomp(short chomper)
{
	short target = g_attribs[chomper].targetID;
	
	if (IsValidTarget(target, chomper))
	{
		g_attribs[chomper].hasParent = true;
		g_attribs[target].hasChild = true;
	}

	return S_OK;
}

HRESULT Update(uint deltaTime)
{
	HRESULT hr = S_OK;

	// TODO: Optimize for data-drive design

	// Sort into buckets

	// Determine nearest neighbors
	for (uint i = 0; i < g_numVerts; i++)
	{
		IFC( FindNearestNeighbor(i) );
	}

	for (uint i = 0; i < g_numVerts; i++)
	{
		// Get target vector
		short target = g_attribs[i].targetID;
		float2 targetVec;
		targetVec.x = g_positions[target].getX() - g_positions[i].getX();
		targetVec.y = g_positions[target].getY() - g_positions[i].getY();

		float dist = targetVec.getLength();
		float2 dir = targetVec;
		if (dist != 0)
			dir = dir / dist;
		
		// Calculate change in position
		float2 offset;
		if (g_attribs[i].hasParent)
		{
			// This controls wigglyness. Perhaps it should be a function of velocity? (static is more wiggly)
			float parentPaddingRadius = g_tailDist;// + (rand() * 2 - 1)*g_tailDist*0.3f;
			offset = targetVec - dir * parentPaddingRadius;
		}
		else
			offset = dir * g_speed * float(deltaTime)/1000000.0f;
		
		// Update positions
		g_positions[i].setX(g_positions[i].getX() + offset.x);
		g_positions[i].setY(g_positions[i].getY() + offset.y);
		
		// Check for chomps
		if (dist <= g_tailDist)
			Chomp(i);
	}

Cleanup:
	if (hr == E_NOTARGETS)
		return Endgame();

	return hr;
}

HRESULT Render()
{
	glClearColor(0.1f, 0.1f, 0.2f, 0.0f);
	glClear(GL_COLOR_BUFFER_BIT);

	glBindBuffer(GL_ARRAY_BUFFER, g_vboPos);
	glBufferData(GL_ARRAY_BUFFER, sizeof(g_positions), g_positions, GL_STREAM_DRAW); 

	glDrawArrays(GL_POINTS, 0, g_numVerts);
	return S_OK;
}

HRESULT CreateProgram(GLuint* program)
{
	HRESULT hr = S_OK;

	ASSERT(program != nullptr);

	const char* vertexShaderString = "\
		#version 330\n \
		layout(location = 0) in vec4 position; \
		void main() \
		{ \
			gl_Position = position; \
		}";

	const char* pixelShaderString = "\
		#version 330\n \
		out vec4 outputColor; \
		void main() \
		{ \
			outputColor = 1.0f; \
		} ";

	GLint success;
	GLchar errorsBuf[256];
	GLuint vs = glCreateShader(GL_VERTEX_SHADER);
	GLuint ps = glCreateShader(GL_FRAGMENT_SHADER);
	GLuint prgm = glCreateProgram();

	glShaderSource(vs, 1, &vertexShaderString, NULL);
	glCompileShader(vs);
    glGetShaderiv(vs, GL_COMPILE_STATUS, &success);
    if (!success)
	{
		glGetShaderInfoLog(vs, sizeof(errorsBuf), 0, errorsBuf);
		Error("Vertex Shader Errors:\n%s", errorsBuf);
		hr = E_FAIL;
	}

	glShaderSource(ps, 1, &pixelShaderString, NULL);
	glCompileShader(ps);
    glGetShaderiv(ps, GL_COMPILE_STATUS, &success);
    if (!success)
	{
		glGetShaderInfoLog(ps, sizeof(errorsBuf), 0, errorsBuf);
		Error("Pixel Shader Errors:\n%s", errorsBuf);
		hr = E_FAIL;
	}
	
	if (FAILED(hr)) return hr;

	glAttachShader(prgm, vs);
	glAttachShader(prgm, ps);
	glLinkProgram(prgm);
	glGetProgramiv(prgm, GL_LINK_STATUS, &success);
	if (!success)
	{
		glGetProgramInfoLog(prgm, sizeof(errorsBuf), 0, errorsBuf);
		Error("Program Link Errors:\n%s", errorsBuf);
		hr = E_FAIL;
	}

	*program = prgm;

	return hr;
}

HRESULT Init()
{
	HRESULT hr = S_OK;

	// Get OpenGL functions
	glGenBuffers = (PFNGLGENBUFFERSPROC)wglGetProcAddress("glGenBuffers");
    glBindBuffer = (PFNGLBINDBUFFERPROC)wglGetProcAddress("glBindBuffer");
    glBufferData = (PFNGLBUFFERDATAPROC)wglGetProcAddress("glBufferData");
	glEnableVertexAttribArray = (PFNGLENABLEVERTEXATTRIBARRAYPROC)wglGetProcAddress("glEnableVertexAttribArray");
    glVertexAttribPointer = (PFNGLVERTEXATTRIBPOINTERPROC)wglGetProcAddress("glVertexAttribPointer");
	glCreateProgram = (PFNGLCREATEPROGRAMPROC)wglGetProcAddress("glCreateProgram");
	glDeleteProgram = (PFNGLDELETEPROGRAMPROC)wglGetProcAddress("glDeleteProgram");
	glUseProgram =	  (PFNGLUSEPROGRAMPROC)wglGetProcAddress("glUseProgram");
	glAttachShader =  (PFNGLATTACHSHADERPROC)wglGetProcAddress("glAttachShader");
	glLinkProgram =   (PFNGLLINKPROGRAMPROC)wglGetProcAddress("glLinkProgram");
	glGetProgramiv =  (PFNGLGETPROGRAMIVPROC)wglGetProcAddress("glGetProgramiv");
	glGetProgramInfoLog = (PFNGLGETPROGRAMINFOLOGPROC)wglGetProcAddress("glGetProgramInfoLog");
	glGetShaderInfoLog = (PFNGLGETSHADERINFOLOGPROC)wglGetProcAddress("glGetShaderInfoLog");
	glCreateShader = (PFNGLCREATESHADERPROC)wglGetProcAddress("glCreateShader");
	glDeleteShader = (PFNGLDELETESHADERPROC)wglGetProcAddress("glDeleteShader");
	glShaderSource = (PFNGLSHADERSOURCEPROC)wglGetProcAddress("glShaderSource");
	glCompileShader = (PFNGLCOMPILESHADERPROC)wglGetProcAddress("glCompileShader");
	glGetShaderiv = (PFNGLGETSHADERIVPROC)wglGetProcAddress("glGetShaderiv");

	GLuint program;
	IFC( CreateProgram(&program) );
	glUseProgram(program);

	// Calculate random starting positions
	for (uint i = 0; i < g_numVerts; i++)
	{
		g_positions[i].setX(frand()*2 - 1);
		g_positions[i].setY(frand()*2 - 1);
	}

	// Initilialize our node attributes
	for (uint i = 0; i < g_numVerts; i++)
	{
	}

	// Initialize buffers
	uint positionSlot = 0;
    GLsizei stride = sizeof(g_positions[0]);
	GLsizei totalSize = sizeof(g_positions);
    glGenBuffers(1, &g_vboPos);
    glBindBuffer(GL_ARRAY_BUFFER, g_vboPos);
    glBufferData(GL_ARRAY_BUFFER, totalSize, g_positions, GL_STREAM_DRAW);
    glEnableVertexAttribArray(positionSlot);
    glVertexAttribPointer(positionSlot, 2, GL_FLOAT, GL_TRUE, stride, 0);

Cleanup:
	return hr;
}

HRESULT InitWindow(HWND& hWnd, int width, int height)
{
    LPCSTR wndName = "Flow Snake";

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
		Error("Window creation failed with error: %x\n", GetLastError());
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
	double aveDeltaTime = 0.0;

	g_width = 1024;
	g_height = 768;
	IFC( InitWindow(hWnd, g_width, g_height) );
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

	IFC( Init() );

    QueryPerformanceFrequency(&freqTime);
    QueryPerformanceCounter(&previousTime);

	uint test = sizeof(Attribs);

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
			aveDeltaTime = aveDeltaTime * 0.9 + 0.1 * deltaTime;
            previousTime = currentTime;

			if (g_endgame == false)
			{
				IFC( Update((uint) deltaTime));
			}
			else
			{
				IFC( EndgameUpdate((uint) deltaTime));
			}

			Render();
            SwapBuffers(hDC);
            if (glGetError() != GL_NO_ERROR)
            {
                Error("OpenGL error.\n");
            }
        }
    }

Cleanup:
	if(hRC) wglDeleteContext(hRC);
    //UnregisterClassA(szName, wc.hInstance);

	char strBuf[256];
	sprintf_s(strBuf, "Average frame duration = %.3f ms\n", aveDeltaTime/1000.0f); 
	OutputDebugString(strBuf);

    return FAILED(hr);
}

LRESULT WINAPI MsgHandler(HWND hWnd, uint msg, WPARAM wParam, LPARAM lParam)
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

void Error(const char* pStr, ...)
{
    char msg[1024] = {0};
	va_list args;
	va_start(args, pStr);
    _vsnprintf_s(msg, countof(msg), _TRUNCATE, pStr, args);
    OutputDebugString(msg);
}

// Produces random short from 0 to SHRT_MAX (32767)
short srand()
{
	static uint seed = 123456789;

	seed = (214013*seed+2531011); 
	return (seed>>16)&0x7FFF; 
}

// Produces a random float from 0 to 1
float frand()
{
	#define RAND_MAX 32767.0f
	return float(srand()/RAND_MAX); 
}