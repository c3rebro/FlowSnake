#pragma once

struct Counter 
{
	LARGE_INTEGER start;
	LARGE_INTEGER end;
};

extern Counter nearestNeighborCounter;
extern Counter binningCounter;
extern Counter positionUpdate;

inline void BeginCounter(Counter* counter);
inline void EndCounter(Counter* counter);
double GetCounter(Counter& counter);