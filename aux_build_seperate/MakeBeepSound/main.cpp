#include <iostream>
#include <fstream>
#include <windows.h>
#include <stdio.h>
#include <ctime>

// functions does not belong to class ExternalScan


int main(int argc, char *argv[])
{
	int freq = 3000;
	int time = 10000;
	if (argc > 1) freq = atoi(argv[1]);
	if (argc > 2) time = atoi(argv[2]);
	Beep(freq, time);
	return 0;
}