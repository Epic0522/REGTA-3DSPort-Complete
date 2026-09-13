#pragma once

#ifdef _3DS
#define DEFAULT_MAX_NUMBER_OF_PEDS 25.0f
#define DEFAULT_MAX_NUMBER_OF_PEDS_INTERIOR 40.0f
#define DEFAULT_MAX_NUMBER_OF_CARS 12.0f
#else
#define DEFAULT_MAX_NUMBER_OF_PEDS 25.0f
#define DEFAULT_MAX_NUMBER_OF_PEDS_INTERIOR 40.0f
#define DEFAULT_MAX_NUMBER_OF_CARS 30.0f
#endif

class CIniFile
{
public:
	static void LoadIniFile();

	static float PedNumberMultiplier;
	static float CarNumberMultiplier;
};
