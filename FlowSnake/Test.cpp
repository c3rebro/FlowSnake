//const uint g_numNodes = 16000;
//extern Node g_nodes[g_numNodes];
//extern bool g_endgame;
//extern short g_numActiveNodes;
//
//extern float frand();
//extern HRESULT Update(double deltaTime);

LARGE_INTEGER freqTime;

Counter nearestNeighborCounter;
Counter binningCounter;
Counter positionUpdate;
Counter updateTime;

inline void BeginCounter(Counter* counter)
{
	QueryPerformanceCounter(&counter->start);
}

inline void EndCounter(Counter* counter)
{
	QueryPerformanceCounter(&counter->end);
}

double GetCounter(Counter& counter)
{
	return double(counter.end.QuadPart - counter.start.QuadPart) / freqTime.QuadPart;
}

void testFirstUpdate()
{
	const uint numUpdateLoops = 100;

	double aveDeltaTime = 0;
	double aveBinning = 0;
	double aveNN = 0;
	double avePos = 0;

	// Each run starts with a new set of initial random positions
	// But each test pass will have the same set of initial position sets
	for (uint i = 0; i < numUpdateLoops; i++)
	{
		BeginCounter(&updateTime);
		Update(0.016);
		EndCounter(&updateTime);

		if (i >= 10) // Skip the first c iterations to warm it up a bit
		{
			aveDeltaTime += GetCounter(updateTime);
			aveBinning += GetCounter(binningCounter);
			aveNN += GetCounter(nearestNeighborCounter);
			avePos += GetCounter(positionUpdate);
		}
		
		// Reset the sim after each pass (always start with seperated nodes)
		memset(g_nodes, 0, sizeof(g_nodes));
		for (uint i = 0; i < g_numNodes; i++)
		{
			g_nodes[i].position.setX(frand()*2 - 1);
			g_nodes[i].position.setY(frand()*2 - 1);
		}
	}
	
	printf("------------- Initial Update() Test ---------------------\n");
	printf("Average Update duration = %.3f ms\n", aveDeltaTime/numUpdateLoops * 1000.0f); 
	printf("Average Binning duration = %.3f ms\n", aveBinning/numUpdateLoops * 1000.0f); 
	printf("Average Nearest Neighbor duration = %.3f ms\n", aveNN/numUpdateLoops * 1000.0f); 
	printf("Average Position Update duration = %.3f ms\n", avePos/numUpdateLoops * 1000.0f); 
}

void testSim()
{
	uint i = 0;

	double aveDeltaTime = 0;
	double aveBinning = 0;
	double aveNN = 0;
	double avePos = 0;

	Counter simTime;
	LARGE_INTEGER frameTime;
	LARGE_INTEGER prevFrameTime;
	

	// Set up our initial state
	memset(g_nodes, 0, sizeof(g_nodes));
	for (uint i = 0; i < g_numNodes; i++)
	{
		g_nodes[i].position.setX(frand()*2 - 1);
		g_nodes[i].position.setY(frand()*2 - 1);
	}
	
	g_numActiveNodes = g_numNodes;
	BeginCounter(&simTime);
	for (i = 0; g_endgame == false && g_numActiveNodes > 1; i++)
	{
		double runningAve = 0.001;
		
		__int64 elapsed;

		QueryPerformanceCounter(&frameTime);
        elapsed = frameTime.QuadPart - prevFrameTime.QuadPart;
        prevFrameTime = frameTime;

		BeginCounter(&updateTime);
		Update(double(elapsed) / freqTime.QuadPart);
		EndCounter(&updateTime);

		aveDeltaTime += GetCounter(updateTime);
		aveBinning += GetCounter(binningCounter);
		aveNN += GetCounter(nearestNeighborCounter);
		avePos += GetCounter(positionUpdate);

		runningAve = 0.9*runningAve + 0.1*GetCounter(updateTime);

		if ( i % 1000 == 0)
		{
			printf("Num Active Verts: %u\n", g_numActiveNodes);
			printf("Average Update Time: %lf ms\n", runningAve * 1000.0f);
		}
	}
	EndCounter(&simTime);
	
	printf("------------- Simulation Update() Test ---------------------\n");
	printf("Simulation completion time: %.3f sec\n", GetCounter(simTime));
	printf("Average Update duration = %.3f ms\n", aveDeltaTime/i * 1000.0f); 
	printf("Average Binning duration = %.3f ms\n", aveBinning/i * 1000.0f); 
	printf("Average Nearest Neighbor duration = %.3f ms\n", aveNN/i * 1000.0f); 
	printf("Average Position Update duration = %.3f ms\n", avePos/i * 1000.0f); 
}

// Let's set up a reproduceable test environment...
int testMain (int argc, char* argv[])
{
    QueryPerformanceFrequency(&freqTime);
	testFirstUpdate();
	//testSim();

	return 0;
}
