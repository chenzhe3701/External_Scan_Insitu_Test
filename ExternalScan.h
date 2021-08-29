/* * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * *
 *                                                                                 *
 * Copyright (c) 2017, Reagents of the University of California                    *
 * All rights reserved.                                                            *
 * Author William C. Lenthe                                                        *
 *                                                                                 *
 * Redistribution and use in source and binary forms, with or without              *
 * modification, are permitted provided that the following conditions are met:     *
 *                                                                                 *
 * 1. Redistributions of source code must retain the above copyright notice, this  *
 *    list of conditions and the following disclaimer.                             *
 *                                                                                 *
 * 2. Redistributions in binary form must reproduce the above copyright notice,    *
 *    this list of conditions and the following disclaimer in the documentation    *
 *    and/or other materials provided with the distribution.                       *
 *                                                                                 *
 * 3. Neither the name of the copyright holder nor the names of its                *
 *    contributors may be used to endorse or promote products derived from         *
 *    this software without specific prior written permission.                     *
 *                                                                                 *
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS"     *
 * AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE       *
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE  *
 * DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT HOLDER OR CONTRIBUTORS BE LIABLE    *
 * FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL      *
 * DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR      *
 * SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER      *
 * CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY,   *
 * OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE   *
 * OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.            *
 *                                                                                 *
 * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * */

#ifndef _ExternalScan_H_
#define _ExternalScan_H_

#include <string>
#include <vector>
#include <numeric>
#include <algorithm>
#include <stdexcept>
#include <ctime>

#include "tif.hpp"
#include "alignment.hpp"

#ifndef NOMINMAX
#define NOMINMAX//windows min/max definitions conflict with std
#endif
//This can limit extra windows includes, but limit the ability to use more windows features
//#ifndef WIN32_LEAN_AND_MEAN
//	#define WIN32_LEAN_AND_MEAN
//#endif
#include <windows.h>
#include "NIDAQmx.h"
// #include "MachineTalkControl.hpp"	// add this to use the computer's audio system, virtual keyboard, and virtual mouse

class ExternalScan {
private:
	std::string xPath, yPath;                     //path to analog output channels for scan control
	std::string etdPath;                          //path to analog input channel for etd
	uInt64 nDwellSamples;                         //samples per pixel (collection occurs at fastest possible speed)
	float64 vRangeH, vRangeV;					  //voltage ranges (horizontal and vertical) for scan (scan will go from -vRange -> +vRange in the larger direction)
	uInt64 width, height;						  //dimensions of the scan
	bool snake;                                   //true/false to snake/raster
	TaskHandle hInput, hOutput;                   //handles to input and output tasks
	float64 sampleRate;                           //maximum device sample rate
	uInt64 iRow;                                  //current row being collected
	//uInt64 iFrame;									// current frame being collected
	std::vector<int16> buffer;                    //working array to read rows from device buffer

	std::vector<std::vector<std::vector<std::vector<int16> > > > frameImagesRaw;		// working array to hold entire frame, [nLineInt][nRS][nDwellSamples] pages of vector(height x width)
	std::vector<std::vector<std::vector<uInt16> > > frameImagesD;		// has [nFrameInt]*[nLineInt*nRS*nDwellSamples] pages
	std::vector<std::vector<uInt16> > frameImagesF;		// has nFrame pages
	std::vector<uInt16> frameImagesA;						// one page holding the average value


	uInt64 nRS;			// A parameter affected by raster/snake.  nRS=2 if raster, we have an additional nDwellSamples layers of image in the reverse scan direction
	uInt64 nLineInt;	// number for line integration
	uInt64 nFrameInt;	// number for frame integration
	std::vector<float64> scanData;	// holds the scan data

	float64 vBlack, vWhite;		// voltage corresponding to black and white pixel
	float64 maxShift;			// maximum pixel shift for fft to correct
	uInt64 width_m;				// the initial width value in the input.  If delay is used, the 'width' is modified.


	//@brief: check a DAQmx return code and convert to an exception if needed
	//@param error: return code to check
	//@param message (optional): description of action being attempted to include in exception
	void DAQmxTry(int32 error, std::string message = std::string());

	//@brief: generate interleaved x/y voltages for scan
	//@return: scan data
	std::vector<float64> generateScanData() const;

	//@brief: check scan parameters, configure DAQmx tasks, and write scan pattern to buffer
	void configureScan();

	//@brief: stop and clear configured tasks
	void clearScan();

	//@brief: read row of raw data from buffer (large images with many samples may be too large to hold in the device buffer)
	int32 readRow();

public:
	static int32 CVICALLBACK EveryNCallback(TaskHandle taskHandle, int32 everyNsamplesEventType, uInt32 nSamples, void *callbackData) {
		return reinterpret_cast<ExternalScan*>(callbackData)->readRow();
	}

	//chenzhe: add width and witdh_i part.  add more inputs.
	ExternalScan(std::string x, std::string y, std::string e, uInt64 s, float64 a, float64 b, uInt64 w, uInt64 h, bool sn, float64 black, float64 white, uInt64 ls, uInt64 fs) {
		xPath = x;
		yPath = y;
		etdPath = e;
		nDwellSamples = s;
		vRangeH = a;
		vRangeV = b;
		width = w;
		height = h;
		snake = sn;
		vBlack = black;
		vWhite = white;
		nLineInt = ls;
		nFrameInt = fs;

		// externalOnOff();	// chenzhe, when constructing, first turn external on
		if (snake){
			width_m = width + 2 * (uInt64)(width * 0.01);	// I think if > 0.05, it becomes very dangerous !!!
			nRS = 2;
		}
		else{
			width_m = width + 2 * (uInt64)(width * 0.16);
			nRS = 1;
		}
		scanData = generateScanData();

		frameImagesD.assign(nFrameInt, std::vector<std::vector<uInt16> >(nLineInt*nRS*nDwellSamples, std::vector<uInt16>((size_t)width * height)));
		frameImagesF.assign(nFrameInt, std::vector<uInt16>((size_t)width*height));
		frameImagesA.assign((size_t)width * height, 0);
		// configureScan(); 
	}
	~ExternalScan() {
		// externalOnOff();	// chenzhe, when destructing, turn external off
		clearScan();
	}

	//@brief: collect and image with the current parameter set and write to disk
	void execute(std::string fileName, bool saveAverageOnly, float64 maxShift, bool correctTF);	// chenzhe, add input variables "correct", "saveAverageOnly", "nFrames", "maxShift", 
};

void ExternalScan::DAQmxTry(int32 error, std::string message) {
	//if (!message.empty())	std::cout << message << std::endl; // for debug, can add a display of the 'message'

	if (0 != error) {
		//get error message
		int32 buffSize = DAQmxGetExtendedErrorInfo(NULL, 0);
		if (buffSize < 0) buffSize = 8192;//sometimes the above returns an error code itself
		std::vector<char> buff(buffSize, 0);
		DAQmxGetExtendedErrorInfo(buff.data(), (uInt32)buff.size());

		//stop and clear tasks and throw
		if (0 != hInput) {
			DAQmxStopTask(hInput);
			DAQmxClearTask(hInput);
		}
		if (0 != hOutput) {
			DAQmxStopTask(hOutput);
			DAQmxClearTask(hOutput);
		}
		if (message.empty())
			throw std::runtime_error("NI-DAQmx error " + std::to_string(error) + ":\n" + std::string(buff.data()));
		else
			throw std::runtime_error("NI-DAQmx error " + message + " " + std::to_string(error) + ":\n" + std::string(buff.data()));
	}
}

std::vector<float64> ExternalScan::generateScanData() const {
	//generate uniformly spaced square grid of points from -vRange -> vRange in largest direction
	std::vector<float64> xData((size_t)width), yData((size_t)height);
	std::iota(xData.begin(), xData.end(), 0.0);
	std::iota(yData.begin(), yData.end(), 0.0);
	float64 scaleX = xData.back();
	float64 scaleY = yData.back();	// scaleY is range in vertical direction
	std::for_each(xData.begin(), xData.end(), [scaleX](float64& v){v /= scaleX; });//0->100% of scan size
	std::for_each(yData.begin(), yData.end(), [scaleY](float64& v){v /= scaleY; });//0->100% of scan size
	scaleX = xData.back() / 2;
	std::for_each(xData.begin(), xData.end(), [scaleX](float64& v){v -= scaleX; });//make symmetric about 0
	scaleY = yData.back() / 2;
	std::for_each(yData.begin(), yData.end(), [scaleY](float64& v){v -= scaleY; });//make symmetric about 0
	scaleX = 2.0 * vRangeH;
	scaleY = 2.0 * vRangeV;
	std::for_each(xData.begin(), xData.end(), [scaleX](float64& v){v *= scaleX; });//scale so limits are +/- vRangeH
	std::for_each(yData.begin(), yData.end(), [scaleY](float64& v){v *= scaleY; });//scale so limits are +/- vRangeV

	float64 d1 = (xData[1] - xData[0])/2/4;	// dividing by 2 equals to using the same step size in voltage jump, because e.g., 4 volts on both side is 8 volts...
	// If snake, insert at begin&end.  If raster, only insert at begin.
	for (int i = 0; i < (width_m - width) / 2; ++i){
		xData.insert(xData.begin(), xData.front() - d1);
		if (snake){
			xData.insert(xData.end(), xData.back() + d1);
		}
		else{
			xData.insert(xData.begin(), xData.front() - d1);
		}
	}
	std::cout << xData.front() << ",," << xData.back();
	// std::reverse(yData.begin(), yData.end());	// y should be reversed to get positive image for FEI Teneo. But not necessary for Tescan

	//generate single pass scan, double the data if we always use snake.  If use raster, do not double.
	std::vector<float64> scan;
	const uInt64 scanPixels = width_m * height * nRS * nLineInt;
	scan.reserve(2 * (size_t)scanPixels);
	if (snake) {
		for (uInt64 i = 0; i < height; i++) {
			for (uInt64 j = 0; j < nLineInt; ++j){
				scan.insert(scan.end(), xData.begin(), xData.end());
				scan.insert(scan.end(), xData.rbegin(), xData.rend());
			}
		}
		for (uInt64 i = 0; i < height; i++) {
			for (uInt64 j = 0; j < nLineInt; ++j){
				scan.insert(scan.end(), (size_t)width_m, yData[(size_t)i]);
				scan.insert(scan.end(), (size_t)width_m, yData[(size_t)i]);
			}
		}
	}
	else {
		for (uInt64 i = 0; i < height; i++) {
			for (uInt64 j = 0; j < nLineInt; ++j){
				scan.insert(scan.end(), xData.begin(), xData.end());
			}
		}
		for (uInt64 i = 0; i < height; i++) {
			for (uInt64 j = 0; j < nLineInt; ++j){
				scan.insert(scan.end(), (size_t)width_m, yData[(size_t)i]);
			}
		}
	}
	return scan;
}

void ExternalScan::configureScan() {
	float factorT = 1.2;	// just a factor
	//create tasks and channels
	clearScan();//clear existing scan if needed
	DAQmxTry(DAQmxCreateTask("scan generation", &hOutput), "creating output task");
	DAQmxTry(DAQmxCreateTask("etd reading", &hInput), "creating input task");
	// chenzhe, modify (-vRange, vRange) use vRange of vRangeV
	DAQmxTry(DAQmxCreateAOVoltageChan(hOutput, (xPath + "," + yPath).c_str(), "", -(vRangeH>vRangeV ? vRangeH : vRangeV)*factorT, factorT*(vRangeH>vRangeV ? vRangeH : vRangeV), DAQmx_Val_Volts, NULL), "creating output channel");
	DAQmxTry(DAQmxCreateAIVoltageChan(hInput, etdPath.c_str(), "", DAQmx_Val_Cfg_Default, vBlack, vWhite, DAQmx_Val_Volts, NULL), "creating input channel"); // chenzhe: change "-10.0, 10.0" to "vBlack, vWhite"

	//get the maximum anolog input rate supported by the daq
	DAQmxTry(DAQmxGetSampClkMaxRate(hInput, &sampleRate), "getting device maximum input frequency");
	sampleRate = 1000000;	// Note: sometimes reduce the sample rate can affect the error "writing scan to buffer".
	const float64 effectiveDwell = (1000000.0 * nDwellSamples) / sampleRate;

	//check scan rate, the microscope is limited to a 300 ns dwell at 768 x 512
	//3.33 x factor of safety -> require at least 768 us to cover full -4 -> +4 V scan
	const float64 minDwell = (768.0 / width_m) * (4.0 / ((vRangeH > vRangeV ? vRangeH : vRangeV)*factorT));	//minimum dwell time in us.  vRangeH correspond to the lineScan direction, so vRangeV shouldn't affect this.
	if (effectiveDwell < minDwell) throw std::runtime_error("Dwell time too short - dwell must be at least " + std::to_string(minDwell) + " us for " + std::to_string(width) + " pixel scan lines");

	//configure timing
	const uInt64 scanPoints = width_m * height * nRS * nLineInt;
	DAQmxTry(DAQmxCfgSampClkTiming(hOutput, "", sampleRate / nDwellSamples, DAQmx_Val_Rising, DAQmx_Val_FiniteSamps, scanPoints), "configuring output timing");

	//configure device buffer / data transfer
	const uInt64 rowDataPoints = width_m * nDwellSamples * nRS * nLineInt;
	uInt64 bufferSize = 4 * rowDataPoints;//allocate buffer big enough to hold 4 rows of data
	DAQmxTry(DAQmxSetBufInputBufSize(hInput, (uInt32)bufferSize), "set buffer size");
	DAQmxTry(DAQmxCfgSampClkTiming(hInput, "", sampleRate, DAQmx_Val_Rising, DAQmx_Val_ContSamps, bufferSize), "configuring input timing");
	DAQmxRegisterEveryNSamplesEvent(hInput, DAQmx_Val_Acquired_Into_Buffer, (uInt32)(width_m * nDwellSamples * nRS * nLineInt), 0, ExternalScan::EveryNCallback, reinterpret_cast<void*>(this));

	//configure start triggering
	std::string trigName = "/" + xPath.substr(0, xPath.find('/')) + "/ai/StartTrigger";//use output trigger to start input
	DAQmxTry(DAQmxCfgDigEdgeStartTrig(hOutput, trigName.c_str(), DAQmx_Val_Rising), "setting start trigger");

	//write scan data to device buffer
	int32 written;
	DAQmxTry(DAQmxWriteAnalogF64(hOutput, (int32)scanPoints, FALSE, DAQmx_Val_WaitInfinitely, DAQmx_Val_GroupByChannel, scanData.data(), &written, NULL), "writing scan to buffer");
	if (scanPoints != written) throw std::runtime_error("failed to write all scan data to buffer");

	//allocate arrays to hold single row of data points and entire image
	buffer.assign((size_t)rowDataPoints, 0);
	frameImagesRaw.assign(nLineInt, std::vector<std::vector<std::vector<int16> > >(nRS, std::vector<std::vector<int16> >(nDwellSamples, std::vector<int16>((size_t)width_m * height))));	//hold each frame as one block of memory, but expand one line into 2 lines
}

void ExternalScan::clearScan() {
	if (NULL != hInput) {
		DAQmxStopTask(hInput);
		DAQmxClearTask(hInput);
		hInput = NULL;
		// std::cout << "clear input task while destructing" << std::endl;	// can add a message for debug
	}
	if (NULL != hOutput) {
		DAQmxStopTask(hOutput);
		DAQmxClearTask(hOutput);
		hOutput = NULL;
		// std::cout << "clear output task while destructing" << std::endl;	// can add a message for debug
	}
}

// Whether raster or snake, after readrow, the image is positive.  No backward lines.
int32 ExternalScan::readRow() {
	int32 read;
	DAQmxTry(DAQmxReadBinaryI16(hInput, (int32)buffer.size(), DAQmx_Val_WaitInfinitely, DAQmx_Val_GroupByChannel, buffer.data(), (uInt32)buffer.size(), &read, NULL), "reading data from buffer");
	if (iRow >= height) return 0;	//input is continuous so samples will be collected after the scan is complete
	std::cout << "\rcompleted row " << (iRow + 1) << "/" << height;
	if (buffer.size() != read) throw std::runtime_error("failed to read all scan data from buffer");

	for (uInt64 iLineInt = 0; iLineInt < nLineInt; ++iLineInt){
		for (uInt64 iRS = 0; iRS < nRS; ++iRS){
			if (snake && (1 == iRS)) {
				const uInt64 rowOffset = width_m * iRow + width_m - 1;	//offset of row end
				for (uInt64 iDwellSamples = 0; iDwellSamples < nDwellSamples; iDwellSamples++)
					for (uInt64 iCol = 0; iCol < width_m; iCol++)
						frameImagesRaw[iLineInt][iRS][iDwellSamples][(size_t)(rowOffset - iCol)] = buffer[(size_t)(iLineInt*nRS*nDwellSamples*width_m + iRS*nDwellSamples*width_m + nDwellSamples*iCol + iDwellSamples)];
			}
			else {
				const uInt64 rowOffset = width_m * iRow;		//offset of row start
				for (uInt64 iDwellSamples = 0; iDwellSamples < nDwellSamples; iDwellSamples++) {
					for (uInt64 iCol = 0; iCol < width_m; iCol++) {
						frameImagesRaw[iLineInt][iRS][iDwellSamples][(size_t)(rowOffset + iCol)] = buffer[(size_t)(iLineInt*nRS*nDwellSamples*width_m + iRS*nDwellSamples*width_m + nDwellSamples*iCol + iDwellSamples)];
					}
				}
			}
		}
	}

	++iRow;
	return 0;
}

void ExternalScan::execute(std::string fileName, bool saveAverageOnly, float64 maxShift, bool correctTF) {
	for (int iFrameInt = 0; iFrameInt < nFrameInt; ++iFrameInt){
		configureScan();
		//execute scan
		iRow = 0;
		DAQmxTry(DAQmxStartTask(hOutput), "starting output task");
		DAQmxTry(DAQmxStartTask(hInput), "starting input task");

		//wait for scan to complete
		float64 scanTime = float64(width_m * height * nDwellSamples * nRS * nLineInt) / sampleRate + 5.0;//allow an extra 5s
		std::cout << "imaging (expected duration ~" << scanTime - 5.0 << "s)\n";
		//DAQmxTry(DAQmxWaitUntilTaskDone(hOutput, scanTime), "waiting for output task");
		DAQmxWaitUntilTaskDone(hOutput, DAQmx_Val_WaitInfinitely);	// just wait.  dUsing DAQmxTry is not good, maybe returns too early.
		//Sleep((DWORD)(1 + (1000 * nDwellSamples) / sampleRate)); //give the input task enough time to be sure that it is finished.

		DAQmxTry(DAQmxStopTask(hInput), "stopping input task");
		std::cout << '\n';

		// Correct image data range to 0-65535 value range
		for (size_t iLineInt = 0; iLineInt < nLineInt; ++iLineInt){
			for (size_t iRS = 0; iRS < nRS; ++iRS){
				for (size_t iDS = 0; iDS < nDwellSamples; iDS++){
					if (snake){
						// No need to flip image anymore, because its already done in readrow().  Just need to reorder the page # (the 'ind' value here) 
						size_t ind;
						if (0 == iRS){
							ind = iLineInt*nRS*nDwellSamples + iRS*nDwellSamples + iDS;
						}
						else{
							ind = iLineInt*nRS*nDwellSamples + iRS*nDwellSamples + (nDwellSamples - 1) - iDS;
						}

						// Because sometimes we use a dealy, we need to process the data row-by-row instead of just copying the whole directly:
						// std::transform(frameImagesRaw[i].begin(), frameImagesRaw[i].end(), frameImagesDL[iFrameInt].begin(), [](const int16& a){return uInt16(a) + 32768; });
						for (size_t j = 0; j < height; ++j){
							std::transform(frameImagesRaw[iLineInt][iRS][iDS].begin() + j*width_m + (width_m - width) / 2,
								frameImagesRaw[iLineInt][iRS][iDS].begin() + j*width_m + (width_m + width) / 2,
								frameImagesD[iFrameInt][ind].begin() + j * width, [](const int16& a){return uInt16(a) + 32768; });
						}

					}
					else{
						size_t ind = iLineInt*nRS*nDwellSamples + iRS*nDwellSamples + iDS;
						// This is for raster, i.e., not backward scan
						for (size_t j = 0; j < height; ++j){
							std::transform(frameImagesRaw[iLineInt][iRS][iDS].begin() + j*width_m + width_m - width, frameImagesRaw[iLineInt][iRS][iDS].begin() + j*width_m + width_m,
								frameImagesD[iFrameInt][ind].begin() + j * width, [](const int16& a){return uInt16(a) + 32768; });
						}
					}
				}
			}
		}
	}

	for (size_t iFrameInt = 0; iFrameInt < nFrameInt; ++iFrameInt){
		// need to apply average between these lineInts.  Backward scan already reversed and repositioned, so it's the same line integration.
		std::vector< std::vector<uInt16> > frameImagesL(nLineInt, std::vector<uInt16>((size_t)width * height));	// temp for all the lineInt images under this frame

		for (size_t iLineInt = 0; iLineInt < nLineInt; ++iLineInt){
			// copy each LineInt to a temp vector (nRS = either 1 or 2,)
			std::vector<std::vector<uInt16> > tempV(nRS*nDwellSamples, std::vector<uInt16>((size_t)width*height));
			std::vector<std::vector<uInt16> >::iterator it = frameImagesD[iFrameInt].begin();
			std::copy(it + iLineInt*nRS*nDwellSamples, it + iLineInt*nRS*nDwellSamples + nRS*nDwellSamples, tempV.begin());

			if (!saveAverageOnly) {
				std::string fileNameRS = fileName;
				fileNameRS.insert(fileNameRS.find("."), "_Frame_");
				fileNameRS.insert(fileNameRS.find("."), std::to_string(iFrameInt));
				fileNameRS.insert(fileNameRS.find("."), "_Line_");
				fileNameRS.insert(fileNameRS.find("."), std::to_string(iLineInt));
				fileNameRS.insert(fileNameRS.find("."), "_RSs_noFFT");
				Tif::Write(tempV, (uInt32)width, (uInt32)height, fileNameRS);
			}

			// apply shift correction
			if (correctTF){
				try{
					std::vector<float> shifts = correlateRows<float>(tempV, height, width, FALSE, maxShift);	// Backward scan reversed, so this is always raster.
				}
				catch (std::exception &e){
				}
			}

			// average and assign to frameImagesL,
			for (int iPixel = 0; iPixel < (size_t)(width * height); ++iPixel){
				for (uInt64 ii = 0; ii < nRS*nDwellSamples; ++ii){
					frameImagesL[iLineInt][iPixel] += tempV[ii][iPixel] / (nRS*nDwellSamples);
				}
			}
		}

		for (int iPixel = 0; iPixel < (size_t)(width * height); ++iPixel){
			for (uInt64 iLineInt = 0; iLineInt < nLineInt; ++iLineInt){
				frameImagesF[iFrameInt][iPixel] += frameImagesL[iLineInt][iPixel] / nLineInt;
			}
		}

		if (!saveAverageOnly) {
			std::string fileNameL = fileName;
			fileNameL.insert(fileNameL.find("."), "_LinesInFrame_");
			fileNameL.insert(fileNameL.find("."), std::to_string(iFrameInt));
			Tif::Write(frameImagesL, (uInt32)width, (uInt32)height, fileNameL);
		}
	}


	// average frameimagesP into frameImagesA
	for (int iPixel = 0; iPixel < (size_t)(width * height); ++iPixel){
		for (uInt64 iFrameInt = 0; iFrameInt < nFrameInt; ++iFrameInt){
			frameImagesA[iPixel] += frameImagesF[iFrameInt][iPixel] / nFrameInt;
		}
	}

	std::string fileNameS = fileName;	//make a new file name for the stacked image
	fileNameS.insert(fileNameS.find("."), "_Frames");

	if (!saveAverageOnly) Tif::Write(frameImagesF, (uInt32)width, (uInt32)height, fileNameS);
	Tif::Write(frameImagesA, (uInt32)width, (uInt32)height, fileName);

}

#endif