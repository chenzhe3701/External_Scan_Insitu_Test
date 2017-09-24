#include <stdio.h>
#include <iostream>

#ifndef NOMINMAX
#define NOMINMAX//windows min/max definitions conflict with std
#endif

#include <Windows.h>  
#include <vector>
#include <queue>
#include <algorithm>
#include <iterator>
#include <complex> 
#include <fftw3.h>
#include <fstream>
#include <ctime>
#include <sstream>
#pragma comment(lib, "winmm.lib")  

int detectFrequency(){

	HWAVEIN hWaveIn;		// input device handle
	WAVEFORMATEX waveform;	// structure, defines the format of waveform audio data
	WAVEHDR wHdr;			// structure defines the header used to identify a waveform-audio buffer
	BYTE *pBuffer;			// buffer
	FILE *pf;				// file pointer to save the wave data
	HANDLE          wait;
	int everyNmicroseconds = 2000;
	std::queue<UINT64> freqs({ 0, 0, 0, 0, 0 });

	waveform.wFormatTag = WAVE_FORMAT_PCM;	// format: PCM
	waveform.nSamplesPerSec = 10000;			// sample rate, Hz = times/second
	waveform.wBitsPerSample = 16;			// Bits per sample for the wFormatTag format type
	waveform.nChannels = 1;					// number of channel.  Has to be 1 for this code !!!!!!!!!!
	waveform.nBlockAlign = waveform.wBitsPerSample*waveform.nChannels / 8;	// block alignment in bytes = (nChannels × wBitsPerSample) / 8, for PCM
	waveform.nAvgBytesPerSec = waveform.nSamplesPerSec * waveform.nBlockAlign;	//Required average data-transfer rate, in bytes/second, = nSamplesPerSec × nBlockAlign
	waveform.cbSize = 0;	// size of extra format info, in bytes

	wait = CreateEvent(NULL, 0, 0, NULL);
	waveInOpen(&hWaveIn, WAVE_MAPPER, &waveform, (DWORD_PTR)wait, 0L, CALLBACK_EVENT);	// opens the input device

	DWORD bufsize = waveform.nAvgBytesPerSec*everyNmicroseconds / 1000;	// buffer can hold Nmicroseconds data
	pBuffer = new BYTE[bufsize];
	wHdr.lpData = (LPSTR)pBuffer;
	wHdr.dwBufferLength = bufsize;	// length in bytes, of the buffer
	wHdr.dwBytesRecorded = 0;
	wHdr.dwUser = 0;
	wHdr.dwFlags = 0;
	wHdr.dwLoops = 1;

	// prepare fft
	int N = waveform.nSamplesPerSec*everyNmicroseconds / 1000;
	std::vector<double> soundWave(N, 0);
	std::vector<fftw_complex> fftWave(N);
	fftw_plan p = fftw_plan_dft_r2c_1d(N, soundWave.data(), fftWave.data(), FFTW_ESTIMATE);
	int OkFreq = 0;
	while (0 == OkFreq){
		// record
		waveInPrepareHeader(hWaveIn, &wHdr, sizeof(WAVEHDR));	// prepare header
		waveInAddBuffer(hWaveIn, &wHdr, sizeof(WAVEHDR));		// prepare buffer
		waveInStart(hWaveIn);		// start recording
		Sleep(everyNmicroseconds + 100);	// record for N microseconds, but make sure there's enough time
		waveInReset(hWaveIn);		// end recording

		std::copy(reinterpret_cast<INT16*>(pBuffer), reinterpret_cast<INT16*>(pBuffer)+wHdr.dwBytesRecorded / sizeof(INT16), soundWave.begin());
		// fopen_s(&pf, "recorderTest.pcm", "wb");
		// fwrite(&soundWave[0], sizeof(double), N, pf); // write unsigned int 16
		// fclose(pf);

		// do fft
		fftw_execute(p);
		std::transform(fftWave.begin(), fftWave.end(), soundWave.begin(), [](fftw_complex a){return a[0] * a[0] + a[1] * a[1]; });		// use wave again to store the frequency domain data
		std::vector<double>::iterator result = std::max_element(soundWave.begin(), soundWave.begin() + N / 2 + 1);		// find iterator of the max element in first half
		freqs.pop();
		freqs.push(std::distance(soundWave.begin(), result) * waveform.nSamplesPerSec / N);		// calculate the max frequency and put in queue

		// find the time of appearance of satisfactory frequencies
		int freqCopy[5];
		for (int i = 0; i < 5; ++i){
			freqCopy[i] = freqs.front();
			freqs.push(freqCopy[i]);
			freqs.pop();
		}
		//std::cout << freqCopy[0] << "," << freqCopy[1] << "," << freqCopy[2] << "," << freqCopy[3] << "," << freqCopy[4];
		printf("\r max frequency detected: %6i", freqCopy[4]);
		
		for (int i = 0; i < 3; ++i){
			if ((freqCopy[i]>999) && (0 == (freqCopy[i] % 4))){
				int count = 0;
				for (int j = i; j < 5; ++j){
					if (freqCopy[i] == freqCopy[j]) ++count;
				}
				if (count > 2){
					OkFreq = freqCopy[i];
				}
			}
		}
	}

	waveInClose(hWaveIn);
	delete pBuffer;
	fftw_destroy_plan(p);

	return OkFreq;
}

void simAltKey(char inputChar){
	Sleep(1000);
	INPUT ip;

	ip.type = INPUT_KEYBOARD;
	ip.ki.wScan = 0;
	ip.ki.time = 0;
	ip.ki.dwExtraInfo = 0;

	// Press the "Ctrl" key
	ip.ki.wVk = VK_MENU;
	ip.ki.dwFlags = 0; // 0 for key press
	SendInput(1, &ip, sizeof(INPUT));

	// Press the inputchar key
	ip.ki.wVk = VkKeyScanEx(inputChar, GetKeyboardLayout(0));
	ip.ki.dwFlags = 0; // 0 for key press
	SendInput(1, &ip, sizeof(INPUT));

	// Release the inputchar key
	ip.ki.wVk = VkKeyScanEx(inputChar, GetKeyboardLayout(0));
	ip.ki.dwFlags = KEYEVENTF_KEYUP;
	SendInput(1, &ip, sizeof(INPUT));

	// Release the "Ctrl" key
	ip.ki.wVk = VK_MENU;
	ip.ki.dwFlags = KEYEVENTF_KEYUP;
	SendInput(1, &ip, sizeof(INPUT));

	Sleep(500);
}
void simMouseClick(int x, int y)
{
	SetCursorPos(x, y);
	INPUT ip;
	ip.type = INPUT_MOUSE;
	ip.mi.dy = 0;
	ip.mi.dx = 0;
	ip.mi.mouseData = 0;
	ip.mi.dwFlags = MOUSEEVENTF_ABSOLUTE | MOUSEEVENTF_VIRTUALDESK | MOUSEEVENTF_LEFTDOWN | MOUSEEVENTF_LEFTUP;
	ip.mi.time = 0;
	ip.mi.dwExtraInfo = 0;

	SendInput(1, &ip, sizeof(INPUT));
	Sleep(200);
}
void externalOnOff(){
	HWND hwnd = FindWindow(NULL, L"Microscope Control v7.6.1   User: supervisor");
	if (hwnd) {
		SetForegroundWindow(hwnd);
		Sleep(1000);
		simMouseClick(50, 200);	// click quad 1
		Sleep(1000);
		simAltKey('c');
		Sleep(1000);
		simMouseClick(400, 290);	// click 'external scan'
		Sleep(1000);
	}
}

