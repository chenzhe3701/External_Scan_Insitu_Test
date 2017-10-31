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
#include "MachineTalkControl.hpp"	// add this to use the computer's audio system, virtual keyboard, and virtual mouse

class ExternalScan {
	private:
		std::string xPath, yPath;                     //path to analog output channels for scan control
		std::string etdPath;                          //path to analog input channel for etd
		uInt64 dwellSamples;                               //samples per pixel (collection occurs at fastest possible speed)
		float64 vRangeH, vRangeV;					  //voltage ranges (horizontal and vertical) for scan (scan will go from -vRange -> +vRange in the larger direction)
		uInt64 width, height;						  //dimensions of the scan
		bool snake;                                   //true/false to snake/raster
		TaskHandle hInput, hOutput;                   //handles to input and output tasks
		float64 sampleRate;                           //maximum device sample rate
		uInt64 iRow;                                  //current row being collected
		std::vector<int16> buffer;                    //working array to read rows from device buffer
		
		std::vector< std::vector<int16> > frameImagesRaw;		//working array to hold entire image frame, frameImages[dwellFrame]<height x width dpts>
		std::vector<std::vector< std::vector<uInt16> > > frameImagesDL;	// correspond to frameImages, but hold uInt16 value, [nFrames][dwellSamples][height x width_i dpts].
		std::vector< std::vector<uInt16> > frameImagesP;	// array to hold entire (pages) of images, frameImagesP[frameInt][height x width_i dpts]
		std::vector<uInt16> frameImagesA;					// hold the average-valued image [height x width_i dpts]

		uInt64 iFrame;			// current frame being collected
		uInt64 nLineInt = 1;	// number for line integration
		uInt64 nFrameInt = 4;	// number for frame integration
		std::vector<float64> scanData;	// holds the scan data


		float64 vBlack, vWhite;		// voltage corresponding to black and white pixel
		float64 maxShift;			// maximum pixel shift for fft to correct
		uInt64 width_i;				// the initial width value in the input.  If delay is used, the 'width' is modified.
		bool delayTF;				// whether to use a dealy to compensate for the distortion on the left edge

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
		ExternalScan(std::string x, std::string y, std::string e, uInt64 s, float64 a, uInt64 w, uInt64 h, bool sn, float64 black, float64 white, bool dl, float64 b) {
			xPath = x;
			yPath = y;
			etdPath = e;
			dwellSamples = s;
			vRangeH = a;
			width = w;
			height = h;
			//snake = sn;
			snake = TRUE;
			vBlack = black;
			vWhite = white;
			delayTF = dl;
			vRangeV = b;
			
			// externalOnOff();	// chenzhe, when constructing, first turn external on
			width_i = width; 
			if (delayTF){
				width = width_i + 2 * (uInt64)(width * 1.01 - width_i);
			}
			else{
				width = width;
			}
			scanData = generateScanData();
			frameImagesDL.assign(nFrameInt, std::vector<std::vector<uInt16> > (dwellSamples*nLineInt, std::vector<uInt16>((size_t)2*width_i * height)));	// artificially double the dwellSamples
			frameImagesP.assign(nFrameInt, std::vector<uInt16>((size_t)width_i * height));
			frameImagesA.assign((size_t)width_i * height,0);
			// configureScan(); 
		}  
		~ExternalScan() {
			// externalOnOff();	// chenzhe, when destructing, turn external off
			clearScan();
		}

		//@brief: collect and image with the current parameter set and write to disk
		void execute(std::string fileName, bool correct, bool saveAverageOnly, uInt64 nFrames, float64 maxShift);	// chenzhe, add input variables "correct", "saveAverageOnly", "nFrames", "maxShift", 
};

void ExternalScan::DAQmxTry(int32 error, std::string message) {
	//if (!message.empty())	std::cout << message << std::endl; // for debug, can add a display of the 'message'

	if(0 != error) {
		//get error message
		int32 buffSize = DAQmxGetExtendedErrorInfo(NULL, 0);
		if(buffSize < 0) buffSize = 8192;//sometimes the above returns an error code itself
		std::vector<char> buff(buffSize, 0);
		DAQmxGetExtendedErrorInfo(buff.data(), (uInt32)buff.size());

		//stop and clear tasks and throw
		if(0 != hInput) {
			DAQmxStopTask(hInput);
			DAQmxClearTask(hInput);
		}
		if(0 != hOutput) {
			DAQmxStopTask(hOutput);
			DAQmxClearTask(hOutput);	
		}
		if(message.empty())
			throw std::runtime_error("NI-DAQmx error " + std::to_string(error) + ":\n" + std::string(buff.data()));
		else
			throw std::runtime_error("NI-DAQmx error " + message + " " + std::to_string(error) + ":\n" + std::string(buff.data()));
	}
}

std::vector<float64> ExternalScan::generateScanData() const {
	//generate uniformly spaced square grid of points from -vRange -> vRange in largest direction
	std::vector<float64> xData((size_t)width_i), yData((size_t)height);
	std::iota(xData.begin(), xData.end(), 0.0);
	std::iota(yData.begin(), yData.end(), 0.0);
	float64 scaleX = xData.back();
	float64 scaleY = yData.back();	// scaleY is range in vertical direction
	std::for_each(xData.begin(), xData.end(), [scaleX](float64& v){v /= scaleX;});//0->100% of scan size
	std::for_each(yData.begin(), yData.end(), [scaleY](float64& v){v /= scaleY;});//0->100% of scan size
	scaleX = xData.back() / 2;
	std::for_each(xData.begin(), xData.end(), [scaleX](float64& v){v -= scaleX;});//make symmetric about 0
	scaleY = yData.back() / 2;
	std::for_each(yData.begin(), yData.end(), [scaleY](float64& v){v -= scaleY;});//make symmetric about 0
	scaleX = 2.0 * vRangeH;
	scaleY = 2.0 * vRangeV;
	std::for_each(xData.begin(), xData.end(), [scaleX](float64& v){v *= scaleX;});//scale so limits are +/- vRangeH
	std::for_each(yData.begin(), yData.end(), [scaleY](float64& v){v *= scaleY;});//scale so limits are +/- vRangeV

	float64 n1 = xData[0];
	float64 n2 = xData[width_i-1];
	for (int i = 0; i < (width - width_i)/2; ++i){ xData.insert(xData.begin(), n1); }		// insert at begin
	for (int i = 0; i < (width - width_i)/2; ++i){ xData.insert(xData.end(), n2); }			// insert at end
	std::reverse(yData.begin(), yData.end());	// y should be reversed to get positive image

	//generate single pass scan, double the data because we always use snake
	std::vector<float64> scan;
	const uInt64 scanPixels = width * height * nLineInt;
	scan.reserve(2 * 2 * (size_t)scanPixels);
	if(snake) {
		for(uInt64 i = 0; i < 2*height; i++) {
			for (uInt64 j = 0; j < nLineInt; ++j){
				if (i % 2 == 0)
					scan.insert(scan.end(), xData.begin(), xData.end());
				else
					scan.insert(scan.end(), xData.rbegin(), xData.rend());
			}
		}
		for (uInt64 i = 0; i < height; i++) {
			for (uInt64 j = 0; j < nLineInt; ++j){
				scan.insert(scan.end(), (size_t)width, yData[(size_t)i]);
				scan.insert(scan.end(), (size_t)width, yData[(size_t)i]);
			}
		}
	} else {
		for (uInt64 i = 0; i < 2*height; i++) {
			for (uInt64 j = 0; j < nLineInt; ++j){
				scan.insert(scan.end(), xData.begin(), xData.end());
				scan.insert(scan.end(), xData.begin(), xData.end());
			}
		}
		for (uInt64 i = 0; i < 2*height; i++) {
			for (uInt64 j = 0; j < nLineInt; ++j){
				scan.insert(scan.end(), (size_t)width, yData[(size_t)i]);
				scan.insert(scan.end(), (size_t)width, yData[(size_t)i]);
			}
		}
	}
	return scan;
}

void ExternalScan::configureScan() {
	//create tasks and channels
	clearScan();//clear existing scan if needed
	DAQmxTry(DAQmxCreateTask("scan generation", &hOutput), "creating output task");
	DAQmxTry(DAQmxCreateTask("etd reading", &hInput), "creating input task");
	// chenzhe, modify (-vRange, vRange) use vRange of vRangeV
	DAQmxTry(DAQmxCreateAOVoltageChan(hOutput, (xPath + "," + yPath).c_str(), "", -(vRangeH>vRangeV ? vRangeH : vRangeV), (vRangeH>vRangeV ? vRangeH : vRangeV), DAQmx_Val_Volts, NULL), "creating output channel");	
	DAQmxTry(DAQmxCreateAIVoltageChan(hInput, etdPath.c_str(), "", DAQmx_Val_Cfg_Default, vBlack, vWhite, DAQmx_Val_Volts, NULL), "creating input channel"); // chenzhe: change "-10.0, 10.0" to "vBlack, vWhite"

	//get the maximum anolog input rate supported by the daq
	DAQmxTry(DAQmxGetSampClkMaxRate(hInput, &sampleRate), "getting device maximum input frequency");
	const float64 effectiveDwell = (1000000.0 * dwellSamples) / sampleRate;

	//check scan rate, the microscope is limited to a 300 ns dwell at 768 x 512
	//3.33 x factor of safety -> require at least 768 us to cover full -4 -> +4 V scan
	const float64 minDwell = (768.0 / width_i) * (4.0 / vRangeH);//minimum dwell time in us.  vRangeH correspond to the lineScan direction, so vRangeV shouldn't affect this.
	if(effectiveDwell < minDwell) throw std::runtime_error("Dwell time too short - dwell must be at least " + std::to_string(minDwell) + " us for " + std::to_string(width_i) + " pixel scan lines");

	//configure timing
	const uInt64 scanPoints = 2*width * height * nLineInt;
	DAQmxTry(DAQmxCfgSampClkTiming(hOutput, "", sampleRate / dwellSamples, DAQmx_Val_Rising, DAQmx_Val_FiniteSamps, scanPoints), "configuring output timing");

	//configure device buffer / data transfer
	const uInt64 rowDataPoints = width * dwellSamples * nLineInt;
	uInt64 bufferSize = 4 * rowDataPoints;//allocate buffer big enough to hold 4 rows of data
	DAQmxTry(DAQmxSetBufInputBufSize(hInput, (uInt32)bufferSize), "set buffer size");
	DAQmxTry(DAQmxCfgSampClkTiming(hInput, "", sampleRate, DAQmx_Val_Rising, DAQmx_Val_ContSamps, bufferSize), "configuring input timing");
	DAQmxRegisterEveryNSamplesEvent(hInput, DAQmx_Val_Acquired_Into_Buffer, (uInt32)(dwellSamples * width), 0, ExternalScan::EveryNCallback, reinterpret_cast<void*>(this));

	//configure start triggering
	std::string trigName = "/" + xPath.substr(0, xPath.find('/')) + "/ai/StartTrigger";//use output trigger to start input
	DAQmxTry(DAQmxCfgDigEdgeStartTrig(hOutput, trigName.c_str(), DAQmx_Val_Rising), "setting start trigger");

	//write scan data to device buffer
	int32 written;
	// std::vector<float64> scan = generateScanData();
	DAQmxTry(DAQmxWriteAnalogF64(hOutput, (int32)scanPoints, FALSE, DAQmx_Val_WaitInfinitely, DAQmx_Val_GroupByChannel, scanData.data(), &written, NULL), "writing scan to buffer");
	if(scanPoints != written) throw std::runtime_error("failed to write all scan data to buffer");

	//allocate arrays to hold single row of data points and entire image
	buffer.assign((size_t)rowDataPoints, 0);
	frameImagesRaw.assign((size_t) dwellSamples * nLineInt, std::vector<int16>((size_t)2 * width * height));	//hold each frame as one block of memory, but expand one line into 2 lines
}

void ExternalScan::clearScan() {
	if(NULL != hInput) {
		DAQmxStopTask(hInput);
		DAQmxClearTask(hInput);
		hInput = NULL;
		// std::cout << "clear input task while destructing" << std::endl;	// can add a message for debug
	}
	if(NULL != hOutput) {
		DAQmxStopTask(hOutput);
		DAQmxClearTask(hOutput);
		hOutput = NULL;
		// std::cout << "clear output task while destructing" << std::endl;	// can add a message for debug
	}
}

int32 ExternalScan::readRow() {
	int32 read;
	DAQmxTry(DAQmxReadBinaryI16(hInput, (int32)buffer.size(), DAQmx_Val_WaitInfinitely, DAQmx_Val_GroupByChannel, buffer.data(), (uInt32)buffer.size(), &read, NULL), "reading data from buffer");
	if(iRow >= 2 * height) return 0;//input is continuous so samples will be collected after the scan is complete
	std::cout << "\rcompleted row " << iRow+1 << "/" << height;
	if(buffer.size() != read) throw std::runtime_error("failed to read all scan data from buffer");
	if(snake && iRow % 2 == 1) {
		const uInt64 rowOffset = width * iRow + width - 1;//offset of row end
		for(uInt64 k = 0; k < dwellSamples * nLineInt; k++)
			for(uInt64 i = 0; i < width; i++)
				frameImagesRaw[(size_t)k][(size_t)(rowOffset-i)] = buffer[(size_t)(i*dwellSamples+k)];
	} else {
		uInt64 rowOffset = width * iRow;//offset of row start
		for(uInt64 k = 0; k < dwellSamples * nLineInt; k++) {
			for(uInt64 i = 0; i < width; i++) {
				frameImagesRaw[(size_t)k][(size_t)(rowOffset+i)] = buffer[(size_t)(i*dwellSamples+k)];
			}
		}
	}
	++iRow;
	return 0;
}

void ExternalScan::execute(std::string fileName, bool correct, bool saveAverageOnly, uInt64 nFrames, float64 maxShift) {
	for (int iFrame = 0; iFrame < nFrameInt; ++iFrame){
		configureScan();
		//execute scan
		iRow = 0;
		DAQmxTry(DAQmxStartTask(hOutput), "starting output task");
		DAQmxTry(DAQmxStartTask(hInput), "starting input task");

		//wait for scan to complete
		float64 scanTime = float64(width * height * dwellSamples * nLineInt * 2) / sampleRate + 5.0;//allow an extra 5s
		std::cout << "imaging (expected duration ~" << scanTime - 5.0 << "s)\n";
		DAQmxTry(DAQmxWaitUntilTaskDone(hOutput, scanTime), "waiting for output task");
		// Note: if too short, seems like it's missing data. So may need to increased it, such as Sleep((DWORD)(scanTime * 1000));
		// However, if it's too long, seems like it will cause error when width is too small. --> possibly due to buffer size (Maybe only for simulated device. I'll check later)
		//Sleep((DWORD)(1 + (1000 * dwellSamples) / sampleRate)); //give the input task enough time to be sure that it is finished.
		DAQmxTry(DAQmxStopTask(hInput), "stopping input task");
		std::cout << '\n';

		// Correct image data range to 0-65535 value range
		for (size_t iDwell = 0; iDwell < frameImagesRaw.size(); ++iDwell){
			// Because sometimes we use a dealy, we need to process the data row-by-row instead of just copying the whole directly:
			// std::transform(frameImagesRaw[i].begin(), frameImagesRaw[i].end(), frameImagesDL[iFrame].begin(), [](const int16& a){return uInt16(a) + 32768; });
			for (size_t j = 0; j < 2*height; ++j){
				std::transform(frameImagesRaw[iDwell].begin() + (j + 1)*width - width_i, frameImagesRaw[iDwell].begin() + (j + 1)*width, frameImagesDL[iFrame][iDwell].begin() + j * width_i,
					[](const int16& a){return uInt16(a) + 32768; });
			}
		}		
		Sleep(10000);
	}
	
	for (int iFrame = 0; iFrame < nFrameInt; ++iFrame){
		// compute and apply shift, correct the [iFrame] of frameImagesDL, which has [dwellSamples x nLineInt] pages of data
		try {
			//compute / apply shift
			std::vector<float> shifts = correlateRows<float>(frameImagesDL[iFrame], 2*height, width_i, snake, maxShift);	// consider maximum shift of "maxShift"
		}
		catch (std::exception &e){
		}
		
		// average the current [iFrame] and assign to frameImagesP[iFrame]
		for (int iRow = 0; iRow < (size_t)height; ++iRow){
			for (int iCol = 0; iCol < (size_t)width_i; ++iCol){
				for (int iDwell = 0; iDwell < frameImagesDL[iFrame].size(); ++iDwell){
					// do 2*iRow
					frameImagesP[iFrame][iRow*width_i + iCol] += frameImagesDL[iFrame][iDwell][(2*iRow)*width_i + iCol] / dwellSamples / 2;
					// do 2*iRow+1
					frameImagesP[iFrame][iRow*width_i + iCol] += frameImagesDL[iFrame][iDwell][(2*iRow+1)*width_i + iCol] / dwellSamples / 2;
				}
			}
		}
	}

	// average frameimagesP into frameImagesA
	for (int iPixel = 0; iPixel < (size_t)(width_i * height); ++iPixel){
		for (uInt64 iFrame = 0; iFrame < nFrameInt; ++iFrame){
			frameImagesA[iPixel] += frameImagesP[iFrame][iPixel] / nFrames;
		}
	}

	std::string fileNameS = fileName;	//make a new file name for the stacked image
	fileNameS.insert(fileNameS.find("."),"_stack");

	width = width_i;	// Change 'width' back for saving purpose.  Could use 'width_i', but prefer 'width' for better compliance with original code.
	
	if (!saveAverageOnly) Tif::Write(frameImagesP, (uInt32)width, (uInt32)height, fileNameS);
	Tif::Write(frameImagesA, (uInt32)width, (uInt32)height, fileName);

}

#endif