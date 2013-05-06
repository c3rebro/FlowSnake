#define WIN32_LEAN_AND_MEAN
#include <Windows.h>
#include <GL\GL.h>
#include <math.h>  // sqrt
#include <stdio.h> // _vsnwprintf_s. Can disable for Release
#include "glext.h" // glGenBuffers, glBindBuffers, ...
#include "Types.h" // float2, short2, Attribs

#ifdef _TEST
#	include "Test.h"
#else
#	define BeginCounter(x)
#	define EndCounter(x)
#endif

/********** Defines *******************************/
#define countof(x) (sizeof(x)/sizeof(x[0]))
#ifdef _DEBUG
#	define IFC(x) { if (FAILED(hr = x)) { char buf[256]; sprintf_s(buf, "IFC Failed at Line %u\n", __LINE__); OutputDebugString(buf); goto Cleanup; } }
#	define ASSERT(x) if (!(x)) { OutputDebugString("Assert Failed!\n"); DebugBreak(); }
#else
#	define IFC(x) {if (FAILED(hr = x)) { goto Cleanup; }}
#	define ASSERT(x)
#endif

#define S_BOUNDARY	0x20000001
#define E_NOTARGETS 0xA0000002
#define EMPTY_SLOT 0xffff

/********** Function Declarations *****************/
LRESULT WINAPI MsgHandler(HWND hWnd, uint msg, WPARAM wParam, LPARAM lParam);
HRESULT EndgameUpdate(double deltaTime);
HRESULT EndgameInit();
uint Distance(short2 current, short2 target);
float SmoothStep(float a, float b, float t);
void Resize(uint width, uint height);
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


/********** Global Constants***********************/
const uint g_numNodes = 16000;	  // Number of nodes (vertices / snake segments) in the scene
const uint g_numSlots = 8000;	  // Number of slots (indexes to nodes) avaiable for spatial binning
const float g_tailDist = 0.0001f; // Distance that children will stay from their parents (in 0..1 space)
float g_speed = 0.2f;			  // in Screens per second

/********** Globals Variables *********************/
GLuint g_vboPos = 0;
uint g_width = 1024;
uint g_height = 768;

short g_numActiveNodes = g_numNodes; // Number of head nodes that are actively seeking tails to chomp

uint g_numBinSplits = 4; // Divide the bins g_numBinSplits times in each dimension. Each bin group is updated periodically.
uint g_binUpdateIter = 0; // The current bin group to update (incremented every Update())
int g_binRangeX[2]; // The inclusive range of bins (X dimension) that are backed by memory (the active bin group)
int g_binRangeY[2]; // The inclusive range of bins (Y dimension) that are backed by memory (the active bin group)

uint g_binStride;	// Number of slots per bin. Each slot holds an index to a node
uint g_binCountX;	// Number of bins in the X dimension needed to fill the screen
uint g_binCountY;	// Number of bins in the X dimension needed to fill the screen
float g_binNWidth;  // Bin width in normalized (0..1) space
float g_binNHeight; // Bin height in normalized (0..1) space

bool g_endgame = false;

// Initialize these to nonzero so they go into .DATA and not .BSS (and show in the executable size)
Node g_nodes[g_numNodes] = {{{0,0,1}, {1,1}}};

// 128k total memory. 
// I think I can pack particle attributes into 6 bytes. (2 shorts for pos, 1 short for target and state)
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
// But wait! We don't need to calculate everybody's nearest neighbor every frame,
// If we save the targets, we can re-search every 4th or so frame (that's 64ms max, shouldn't be noticeable)
// Then we only need a quarter of the binning space! Woo! 
// So thats 4000 slots (1 short each) for 16000 particles. Which leaves 27072 bytes left over. Nice.
ushort g_slots[g_numSlots] = {0xFFFF}; 

/**************************************************/

//TODO: This access memory all over the place. Can we make this better?
inline bool IsValidTarget(short target, short current)
{
	if (target == current) return false;						// Can't chase ourselves
	if (g_nodes[target].attribs.hasChild == true) return false;	// It can't already have a child

	// Can't chase our own tail
	short chain = target;
	while (g_nodes[chain].attribs.hasParent)
	{
		chain = g_nodes[chain].attribs.targetID;
		if (chain == current)
		{
			return false;
		}
	}

	return true;
}

// The Node pointed to by node index is in range of it's target
// If it's still a valid target (no one chomped it this frame) 
// then join these two segments
HRESULT Chomp(short nodeIndex)
{
	short target = g_nodes[nodeIndex].attribs.targetID;
	
	if (IsValidTarget(target, nodeIndex))
	{
		g_nodes[nodeIndex].attribs.hasParent = true;
		g_nodes[target].attribs.hasChild = true;
		--g_numActiveNodes;
	}

	return S_OK;
}

// Given xy bin coordinates return the bin's index into the slot buffer
HRESULT Bin(int binX, int binY, int* bin)
{
	// TODO: Tiling/Swizzling the bin memory could make this more efficient... 
	if (bin) *bin = (binX - g_binRangeX[0]) + (binY - g_binRangeY[0]) * (g_binRangeX[1] - g_binRangeX[0]+1);

	// Return E_FAIL if the bin is outside the mem mapped zone
	if (binX < g_binRangeX[0] || binX > g_binRangeX[1] ||
		binY < g_binRangeY[0] || binY > g_binRangeY[1] ) 
		return E_FAIL;

	// Return S_BOUNDARY if this bin is on the outside edge (the buffer zone)
	if (binX == g_binRangeX[0] || binX == g_binRangeX[1] ||
		binY == g_binRangeY[0] || binY == g_binRangeY[1])
		return S_BOUNDARY;
	
	// Return S_OK if it is inside
	else
		return S_OK;
}

// Given a position in normalized 0..1 space, find the position's bin and 
// return its index into the slot buffer
HRESULT Bin(float posx, float posy, int* bin)
{
	int bucketX = uint(posx / g_binNWidth);
	int bucketY = uint(posy / g_binNHeight);
	return Bin(bucketX, bucketY, bin);
}

HRESULT FindNearestNeighbor(short index)
{
	HRESULT hr = S_OK;

	if (g_nodes[index].attribs.hasParent == true)
		return S_FALSE;

	float2 pos = {g_nodes[index].position.getX(), g_nodes[index].position.getY()};
	if (Bin(pos.x, pos.y, nullptr) != S_OK)
		return S_FALSE; // if we're not in a bin backed by memory, just keep our old neighbor

	int xrange[2] = {int(pos.x/g_binNWidth - 0.5f), int(pos.x/g_binNWidth + 0.5f)};
	int yrange[2] = {int(pos.y/g_binNHeight - 0.5f), int(pos.y/g_binNHeight + 0.5f)};

	uint minDist = -1;
	ushort nearest = -1;
	int bin;
	do {
		// Yes, we'll re-iterate over some bins, but the bin rows are stored linearly in memory
		// So the cache should make this pretty cheap (plus it's only one access since they'll probably be empty)
		for (int y = yrange[0]; y <= yrange[1]; y++)
		{
			for (int x = xrange[0]; x <= xrange[1]; x++)
			{
				hr = Bin(x, y, &bin);
				ASSERT(SUCCEEDED(hr)); // Bin fails if the bin isn't memory backed.

				for (uint slot = 0; slot < g_binStride; slot++)
				{
					// TODO: These large strides are going to kill the cache! 
					//		 We should probably switch to storing the node indexes linearly with the MSb denoting end of bucket
					//		 Then we'd have a separate table to index into this based on bucket
					// No, that won't work because inserts would be very difficult/expensive. The easiest way would be a linked
					//	   list, but that would obviously be super slow. I think I the first try was actually the best ;D
					ushort target = g_slots[bin*g_binStride + slot];
					if (target == EMPTY_SLOT)
						break;
					else if (IsValidTarget(target, index))
					{
						uint dist = Distance(g_nodes[index].position, g_nodes[target].position);
						if (dist < minDist)
						{
							minDist = dist;
							nearest = target;
						}
					}
				}
			}
		}
		if (xrange[0] > g_binRangeX[0]) xrange[0]--;
		if (xrange[1] < g_binRangeX[1]) xrange[1]++;
		if (yrange[0] > g_binRangeY[0]) yrange[0]--;
		if (yrange[1] < g_binRangeY[1]) yrange[1]++;

		// Do we need this? Could happen if a vert is in a quadrant of it's own
		if (xrange[1] - xrange[0] == g_binRangeX[1] - g_binRangeX[0] && 
			yrange[1] - yrange[0] == g_binRangeY[1] - g_binRangeY[0])
			break;

	} while (nearest == ushort(-1));

	if (nearest != ushort(-1)) g_nodes[index].attribs.targetID = nearest;
	else if (IsValidTarget(g_nodes[index].attribs.targetID, index) == false)
	{
		// If our current target is invalid, and we weren't able to find a new one, we'll have to revert to N^2
		for (uint i = 0; i < g_numNodes; i++)
		{
			if (IsValidTarget(i, index))
			{
				uint dist = Distance(g_nodes[index].position, g_nodes[i].position);
				if (dist < minDist)
				{
					minDist = dist;
					nearest = i;
				}
			}
		}
		ASSERT(nearest != -1);
		g_nodes[index].attribs.targetID = nearest; 
	}
	
	return S_OK;
}

HRESULT Update(double deltaTime)
{
	HRESULT hr = S_OK;

	if (g_endgame)
		return EndgameUpdate(deltaTime);

	// TODO: Optimize for cache coherency
	//		 We could attempt to store chains of nodes linearly in memory. That would make the update loop for nodes in those chains
	//		 super fast (since the most chains could probably fit in one cache line). But it would involve a lot of mem moves and 
	//		 could introduce some complexity. Since we're already under 1ms average, I'd say let's not do it.

	// Sort into buckets
	BeginCounter(&binningCounter);
	{
		float pixelsPerVert = float((g_width * g_height) / g_numActiveNodes);
		float binDiameterPixels = sqrt(pixelsPerVert); // conservative
	
		g_binNHeight = binDiameterPixels / g_height;
		g_binNWidth  = binDiameterPixels / g_width;

		g_binCountX  = uint(ceilf(1.0f / g_binNWidth) )+2;  // Add a boundary around the outside
		g_binCountY  = uint(ceilf(1.0f / g_binNHeight))+2;

		uint xiter = g_binUpdateIter % g_numBinSplits;
		uint yiter = g_binUpdateIter / g_numBinSplits;
		g_binRangeX[0] = (g_binCountX * xiter/g_numBinSplits)		  - 1;	// Subtract/Add 1 to each of these ranges for a buffer layer
		g_binRangeX[1] = (g_binCountX * (xiter+1)/g_numBinSplits - 1) + 1;	// This buffer layer will be overlap for each quadrant
		g_binRangeY[0] = (g_binCountY * yiter/g_numBinSplits)		  - 1;	// But without it verts would only target verts in their quadrant
		g_binRangeY[1] = (g_binCountY * (yiter+1)/g_numBinSplits - 1) + 1;
		g_binStride  = g_numSlots / ((g_binRangeX[1] - g_binRangeX[0] + 1) * (g_binRangeY[1] - g_binRangeY[0] + 1));

		int bin;
		memset(g_slots, EMPTY_SLOT, sizeof(g_slots));
		for (uint i = 0; i < g_numNodes; i++)
		{
			if (g_nodes[i].attribs.hasChild == true) continue; // Only bin the chompable tails
			hr = Bin(g_nodes[i].position.getX(), g_nodes[i].position.getY(), &bin);
			if (FAILED(hr)) // If this bin isn't backed by memory, we can't be a target this frame
				continue;

			// Find first empty bin slot
			for (uint slot = 0; slot < g_binStride; slot++) 
			{
				if (g_slots[bin*g_binStride + slot] == EMPTY_SLOT)
				{
					g_slots[bin*g_binStride + slot] = i;
					break;
				}
			}
			// If we overflow the bins, the vertex cannot be targeted. Haven't seen any cases yet...
		}
		g_binUpdateIter = (g_binUpdateIter+1) % (g_numBinSplits*g_numBinSplits);
	}
	EndCounter(&binningCounter);

	// Determine nearest neighbors
	BeginCounter(&nearestNeighborCounter);
	for (uint i = 0; i < g_numNodes; i++)
	{
		IFC( FindNearestNeighbor(i) );
	}
	EndCounter(&nearestNeighborCounter);

	BeginCounter(&positionUpdate);
	for (uint i = 0; i < g_numNodes; i++)
	{
		// Do our memory reads here so we can optimize our access patterns
		Node& current = g_nodes[i];
		Node& target = g_nodes[current.attribs.targetID];

		// Get target vector
		// For optimal precision, pull our shorts into floats and do all math at full precision...
		float2 targetVec;
		targetVec.x = target.position.getX() - current.position.getX(); 
		targetVec.y = target.position.getY() - current.position.getY();

		float dist = targetVec.getLength();
		float2 dir = targetVec;
		if (dist != 0)
			dir = dir / dist;
		
		// Calculate change in position
		float2 offset;
		if (current.attribs.hasParent)
		{
			// This controls wigglyness. Perhaps it should be a function of velocity? (static is more wiggly)
			float parentPaddingRadius = g_tailDist;// + (rand() * 2 - 1)*g_tailDist*0.3f;
			offset = targetVec - dir * parentPaddingRadius;
		}
		else
			offset = min(targetVec, dir * float(g_speed * deltaTime));
		
		// ... then finally, at the verrrry end, stuff our FP floats into 16-bit shorts
		current.position.setX(current.position.getX() + offset.x);
		current.position.setY(current.position.getY() + offset.y);
		
		// Check for chomps
		if (current.attribs.hasParent == false && dist <= g_tailDist)
			Chomp(i);
	}
	EndCounter(&positionUpdate);

Cleanup:
	if (g_numActiveNodes == 1)
		return EndgameInit();

	return hr;
}

HRESULT EndgameUpdate(double deltaTime)
{
	static short* velocityBuf = (short*)g_slots;
	static const uint numVels = g_numSlots/2;
	static double absoluteTime = 0;
	const float timeLimit = 5.0f; // 5 seconds

	absoluteTime += deltaTime;

	for (uint i = 0; i < g_numNodes; i++)
	{	
		float velx = velocityBuf[2*(i%numVels)] / MAX_SSHORTF;
		float vely = velocityBuf[2*(i%numVels)+1] / MAX_SSHORTF;

		velx = SmoothStep(velx, 0.0f, float(absoluteTime)/timeLimit);
		vely = SmoothStep(vely, 0.0f, float(absoluteTime)/timeLimit);

		g_nodes[i].position.setX(float(g_nodes[i].position.getX() + velx * deltaTime));
		g_nodes[i].position.setY(float(g_nodes[i].position.getY() + vely * deltaTime));
	}

	if (absoluteTime > timeLimit)
	{
		g_endgame = false;
		g_numActiveNodes = g_numNodes;
		absoluteTime = 0;

		for (uint i = 0; i < g_numNodes; i++)
		{
			g_nodes[i].attribs.hasChild = false;
			g_nodes[i].attribs.hasParent = false;
		}
	}

	return S_OK;
}

HRESULT EndgameInit()
{
	static short* velocityBuf = (short*)g_slots;
	static const uint numVels = g_numSlots/2;

	g_endgame = true;

	//// TODO: Add "shaking" before we explode. The snake should continue
	////		 to swim along, then start vibrating, then EXPLODE.

	for (uint i = 0; i < numVels; i++)
	{
		float maxVelocity = MAX_SSHORTF * 0.5f; // screens per second in signed short space
		velocityBuf[2*i] = short((frand()*2.0f - 1.0f) * maxVelocity);
		velocityBuf[2*i+1] = short((frand()*2.0f - 1.0f) * maxVelocity);
	}

	return S_OK;
}

HRESULT Render()
{
	glClearColor(0.1f, 0.1f, 0.2f, 0.0f);
	glClear(GL_COLOR_BUFFER_BIT);

	glBindBuffer(GL_ARRAY_BUFFER, g_vboPos);
	glBufferData(GL_ARRAY_BUFFER, sizeof(g_nodes), g_nodes, GL_STREAM_DRAW); 

	glDrawArrays(GL_POINTS, 0, g_numNodes);
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
			gl_Position = position * 2.0f - 1.0f; \
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
	for (uint i = 0; i < g_numNodes; i++)
	{
		g_nodes[i].position.setX(frand()*2 - 1);
		g_nodes[i].position.setY(frand()*2 - 1);
	}

	// Initialize buffers
	uint positionSlot = 0;
	GLsizei stride = sizeof(g_nodes[0]);
	GLsizei totalSize = sizeof(g_nodes);
	uint offset = (char*)&g_nodes[0].position - (char*)&g_nodes[0];
    glGenBuffers(1, &g_vboPos);
    glBindBuffer(GL_ARRAY_BUFFER, g_vboPos);
    glBufferData(GL_ARRAY_BUFFER, totalSize, g_nodes, GL_STREAM_DRAW);
    glEnableVertexAttribArray(positionSlot);
	glVertexAttribPointer(positionSlot, 2, GL_UNSIGNED_SHORT, GL_TRUE, stride, (GLvoid*)offset);

Cleanup:
	return hr;
}

HRESULT InitWindow(HWND& hWnd, int width, int height, LPCSTR name)
{
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
	wndClass.lpszClassName = name;
	wndClass.hIconSm = NULL;
    RegisterClassEx(&wndClass);
	
	DWORD wndStyle = WS_OVERLAPPEDWINDOW | WS_VISIBLE | WS_POPUP ;
    DWORD wndExStyle = WS_EX_APPWINDOW | WS_EX_WINDOWEDGE;

	RECT wndRect;
	SetRect(&wndRect, 0, 0, width, height);
    AdjustWindowRectEx(&wndRect, wndStyle, FALSE, wndExStyle);
    int wndWidth = wndRect.right - wndRect.left;
    int wndHeight = wndRect.bottom - wndRect.top;
    int wndTop = GetSystemMetrics(SM_CYSCREEN) / 2 - wndHeight / 2;
    int wndLeft = GetSystemMetrics(SM_XVIRTUALSCREEN) + 
				  GetSystemMetrics(SM_CXSCREEN) / 2 - wndWidth / 2;

	hWnd = CreateWindowEx(0, name, name, wndStyle, wndLeft, wndTop, 
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

    LPCSTR wndName = "Flow Snake";

	IFC( InitWindow(hWnd, g_width, g_height, wndName) );
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
            deltaTime = double(elapsed) / freqTime.QuadPart;
			aveDeltaTime = aveDeltaTime * 0.9 + 0.1 * deltaTime;
            previousTime = currentTime;

			IFC( Update(deltaTime) );

			Render();
            SwapBuffers(hDC);
            if (glGetError() != GL_NO_ERROR)
            {
                Error("OpenGL error.\n");
            }
        }
    }

Cleanup:
	if(hRC)  wglDeleteContext(hRC);
	if(hDC)  ReleaseDC(hWnd, hDC);
	if(hWnd) DestroyWindow(hWnd);

	char strBuf[256];
	sprintf_s(strBuf, "Average frame duration = %.3f ms\n", aveDeltaTime*1000.0f); 
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

	case WM_SIZE:
		Resize(LOWORD(lParam), HIWORD(lParam)); 
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

uint Distance(short2 current, short2 target)
{
	int diffx = abs(int(current.x - target.x));
	int diffy = abs(int(current.y - target.y));
	int dist = diffx + diffy; // Manhattan distance
	ASSERT(dist >= 0); // dist < 0 means overflow
	return dist;
}

void Resize(uint width, uint height)
{
	g_width = width;
	g_height = height;
	glViewport( 0, 0, ( GLint )width, ( GLint )height );
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

// Smoothly blend between a and b based on t
float SmoothStep(float a, float b, float t)
{
	return a + (pow(t,2)*(3-2*t))*(b - a);
}

#ifdef _TEST
#	include "Test.cpp"
#endif