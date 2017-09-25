#include <MachineTalkControl.hpp>

// Detect a frequency which is >= 1000Hz, and multiples of 4.
// Default is 3000Hz.

int main(int argc, char *argv[]){
	
	int targetFreq = 3000;
	int currentFreq = -1;
	if (argc > 1) { targetFreq = atoi(argv[1]); }
	while (currentFreq != targetFreq) {
		currentFreq = detectFrequency();
	}
	return currentFreq;
}