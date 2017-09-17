/* * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * *
 *                                                             *
 * ExternalScan.h                                              *
 *                                                             *
 * Created by: William C. Lenthe,                              *
 * Copyright (c) 2016 University of California, Santa Barbara  *
 * All Rights Reserved                                         *
 *                                                             *
 * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * */
#ifndef _ExternalScan_H_
#define _ExternalScan_H_

#include <string>
#include <vector>
#include <numeric>
#include <algorithm>
#include <stdexcept>
#include <ctime>

#ifndef NOMINMAX
	#define NOMINMAX//windows min/max definitions conflict with std
#endif
#ifndef WIN32_LEAN_AND_MEAN
	#define WIN32_LEAN_AND_MEAN//limit extra windows includes
#endif
#include <windows.h>
#include "NIDAQmx.h"

class ExternalScan {
	private:
		std::string xPath, yPath;  //path to analog output channels for scan control
		std::string etdPath;       //path to analog input channel for etd
		uInt64 width, height;      //dimensions of the scan
		uInt64 samples;            //samples per pixel (collection occurs at fastest possible speed)
		float64 vRange;            //voltage range for scan (scan will go from -vRange -> +vRange in the larger direction)
		bool vertical;             //true/false to scan in vertical/horizontal direction
		bool snake;                //true/false to snake/raster
		float64 minEtd, maxEtd;    //anticipated range of voltages out of the etd
		TaskHandle hInput, hOutput;//handles to input and output tasks
		bool configured;           //true/false if the current scan parameters have been written to the hardware buffer
		float64 sampleRate;        //maximum device sample rate

		//@brief: check a DAQmx return code and convert to an exception if needed
		//@param error: return code to check
		//@param message (optional): description of action being attempted to include in exception
		void DAQmxTry(int32 error, std::string message = std::string());

		//@brief: generate interleaved x/y voltages for scan
		//@return: scan data
		std::vector<float64> generateScanData() const;

		//@brief: stop and clear configured tasks
		void clearScan();

		template <typename T> void setValue(T& v, const T& newV) {if(v != newV) {configured = false; v = newV;}}

	public:
		ExternalScan() : width(512), height(512), vRange(0.0), vertical(false), snake(true), minEtd(0.0), maxEtd(5.0), hInput(NULL), hOutput(NULL), configured(false) {}
		~ExternalScan() {clearScan();}

		void setPaths(const std::string x, const std::string y, const std::string etd) {setValue(xPath, x); setValue(yPath, y); setValue(etdPath, etd);}
		void setDimensions(const uInt64 scanWidth, const uInt64 scanHeight) {setValue(width, scanWidth); setValue(height, scanHeight);}
		void setSamples(const uInt64 sampsPerPix) {setValue(samples, sampsPerPix);}
		void setScanType(const bool scanVertical, const bool scanSnake) {setValue(vertical, scanVertical); setValue(snake, scanSnake);}
		void setCalibrations(const float64 maxScanVoltage, const float64 black, const float64 white) {setValue(vRange, maxScanVoltage); setValue(minEtd, black); setValue(maxEtd, white);}

		const uInt64& getWidth() {return width;}
		const uInt64& getHeight() {return height;}
		const float64& getVoltageRange() {return vRange;}
		const float64& getEtdBlack() {return minEtd;}
		const float64& getEtdWhite() {return maxEtd;}
		const uInt64& getSamples() {return samples;}

		//@brief: check scan parameters, configure DAQmx tasks, and write scan pattern to buffer
		void configureScan();

		//@brief: collect and image with the current parameter set
		//@return: image data with origin in bottom left corner
		std::vector< std::vector<int16> > execute();
};

void ExternalScan::DAQmxTry(int32 error, std::string message) {
	if(0 != error) {
		//get error message
		int32 buffSize = DAQmxGetExtendedErrorInfo(NULL, 0);
		if(buffSize < 0) buffSize = 8192;//sometimes the above returns an error code itself
		std::vector<char> buff(buffSize, 0);
		DAQmxGetExtendedErrorInfo(buff.data(), buff.size());

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
	std::vector<float64> xData((size_t)width), yData((size_t)height);
	std::iota(xData.begin(), xData.end(), 0.0);
	std::iota(yData.begin(), yData.end(), 0.0);
	float64 scale = std::max(xData.back(), yData.back());
	std::for_each(xData.begin(), xData.end(), [scale](float64& v){v /= scale;});//0->100% of scan size
	std::for_each(yData.begin(), yData.end(), [scale](float64& v){v /= scale;});//0->100% of scan size
	scale = xData.back() / 2;
	std::for_each(xData.begin(), xData.end(), [scale](float64& v){v -= scale;});//make symmetric about 0
	scale = yData.back() / 2;
	std::for_each(yData.begin(), yData.end(), [scale](float64& v){v -= scale;});//make symmetric about 0
	scale = 2.0 * vRange;	
	std::for_each(xData.begin(), xData.end(), [scale](float64& v){v *= scale;});//scale so limits are +/- vRange
	std::for_each(yData.begin(), yData.end(), [scale](float64& v){v *= scale;});//scale so limits are +/- vRange

	//generate single pass scan
	std::vector<float64> scan;
	const uInt64 scanPixels = width * height;
	scan.reserve(2 * (size_t)scanPixels);
	if(snake) {
		if(vertical) {
			for(uInt64 i = 0; i < width; i++) scan.insert(scan.end(), (size_t)height, xData[(size_t)i]);
			for(uInt64 i = 0; i < width; i++) {
				if(i % 2 == 0)
					scan.insert(scan.end(), yData. begin(), yData. end());
				else
					scan.insert(scan.end(), yData.rbegin(), yData.rend());
			}
		} else {
			for(uInt64 i = 0; i < height; i++) {
				if(i % 2 == 0)
					scan.insert(scan.end(), xData. begin(), xData. end());
				else
					scan.insert(scan.end(), xData.rbegin(), xData.rend());
			}
			for(uInt64 i = 0; i < height; i++) scan.insert(scan.end(), (size_t)width, yData[(size_t)i]);
		}
	} else {
		if(vertical) {
			for(uInt64 i = 0; i < width ; i++) scan.insert(scan.end(), (size_t)height, xData[(size_t)i]);
			for(uInt64 i = 0; i < width ; i++) scan.insert(scan.end(), yData.begin() , yData.end()     );
		} else {
			for(uInt64 i = 0; i < height; i++) scan.insert(scan.end(), xData.begin() , xData.end()     );
			for(uInt64 i = 0; i < height; i++) scan.insert(scan.end(), (size_t)width , yData[(size_t)i]);
		}
	}
	return scan;
}

void ExternalScan::clearScan() {
	if(NULL != hInput) {
		DAQmxStopTask(hInput);
		DAQmxClearTask(hInput);
		hInput = NULL;
	}
	if(NULL != hOutput) {
		DAQmxStopTask(hOutput);
		DAQmxClearTask(hOutput);
		hOutput = NULL;
	}
}

void ExternalScan::configureScan() {
	//create tasks and channels
	clearScan();//clear existing scan if needed
	DAQmxTry(DAQmxCreateTask("", &hOutput), "creating output task");
	DAQmxTry(DAQmxCreateTask("", &hInput), "creating input task");
	DAQmxTry(DAQmxCreateAOVoltageChan(hOutput, (xPath + "," + yPath).c_str(), "", -vRange, vRange, DAQmx_Val_Volts, NULL), "creating output channel");
	DAQmxTry(DAQmxCreateAIVoltageChan(hInput, etdPath.c_str(), "", DAQmx_Val_Cfg_Default, minEtd, maxEtd, DAQmx_Val_Volts, NULL), "creating input channel");

	//get the maximum anolog input rate supported by the daq
	DAQmxTry(DAQmxGetSampClkMaxRate(hInput, &sampleRate), "getting device maximum input frequency");
	const float64 effectiveDwell = (1000000.0 * samples) / sampleRate;

	//check scan rate, the microscope is limited to a 300 ns dwell at 768 x 512
	//3.33 x factor of safety -> require at least 768 us to cover full -4 -> +4 V scan
	const float64 minDwell = (768.0 / (vertical ? height : width)) * (4.0 / vRange);//minimum dwell time in us
	if(effectiveDwell < minDwell) throw std::runtime_error("Dwell time too short - dwell must be at least " + std::to_string(minDwell) + " us for " + std::to_string(vertical ? height : width) + " pixel scan lines");

	//configure timing and start triggering
	const uInt64 scanPoints = width * height;
	DAQmxTry(DAQmxCfgSampClkTiming(hOutput, "", sampleRate / samples, DAQmx_Val_Rising, DAQmx_Val_FiniteSamps, scanPoints), "configuring output timing");
	DAQmxTry(DAQmxCfgSampClkTiming(hInput, "", sampleRate, DAQmx_Val_Rising, DAQmx_Val_FiniteSamps, scanPoints * samples), "configuring input timing");
	std::string trigName = "/" + xPath.substr(0, xPath.find('/')) + "/ai/StartTrigger";//use output trigger to start input
	DAQmxTry(DAQmxCfgDigEdgeStartTrig(hOutput, trigName.c_str(), DAQmx_Val_Rising), "setting start trigger");

	//write scan data to device buffer
	int32 written;
	std::vector<float64> scan = generateScanData();
	DAQmxTry(DAQmxWriteAnalogF64(hOutput, (int32)scanPoints, FALSE, DAQmx_Val_WaitInfinitely, DAQmx_Val_GroupByChannel, scan.data(), &written, NULL), "writing scan to buffer");
	if(scanPoints != written) throw std::runtime_error("failed to write all scan data to buffer");
	configured = true;//mark scan as configured
}

std::vector< std::vector<int16> > ExternalScan::execute() {
	//configure scan if needed (writing to the buffer can be a little slow)
	if(!configured) configureScan();

	//execute scan
	DAQmxTry(DAQmxStartTask(hOutput), "starting output task");
	DAQmxTry(DAQmxStartTask(hInput), "starting input task");
	
	//wait for scan to complete
	const uInt64 rowDataPoints = width * samples;
	const uInt64 scanDataPoints = width * height * samples;
	float64 scanTime = float64(scanDataPoints) / sampleRate + 5.0;//allow an extra 5s
	DAQmxTry(DAQmxWaitUntilTaskDone(hOutput, scanTime), "waiting for output task");
	DAQmxTry(DAQmxWaitUntilTaskDone(hInput, scanTime), "waiting for input task");

	//read raw data from buffer row by row, interleave nth sample, and convert from snake to raster if needed
	std::vector<int16> buffer((size_t)rowDataPoints);
	std::vector< std::vector<int16> > frameImages((size_t)samples, std::vector<int16>((size_t)width * height));//hold each frame as one block of memory
	for(uInt64 j = 0; j < height; j++) {
		int32 read;
		DAQmxTry(DAQmxReadBinaryI16(hInput, (int32)rowDataPoints, DAQmx_Val_WaitInfinitely, DAQmx_Val_GroupByChannel, buffer.data(), buffer.size(), &read, NULL), "reading data from buffer");
		if(rowDataPoints != read) throw std::runtime_error("failed to read all scan data from buffer");
		if(snake && j % 2 == 1) {
			const uInt64 rowOffset = width * j + width - 1;//offset of row end
			for(uInt64 k = 0; k < samples; k++)
				for(uInt64 i = 0; i < width; i++)
					frameImages[(size_t)k][(size_t)(rowOffset-i)] = buffer[(size_t)(i*samples+k)];
		} else {
			const uInt64 rowOffset = width * j;//offset of row start
			for(uInt64 k = 0; k < samples; k++) {
				for(uInt64 i = 0; i < width; i++) {
					frameImages[(size_t)k][(size_t)(rowOffset+i)] = buffer[(size_t)(i*samples+k)];
				}
			}
		}
	}

	//reorder data to be in row major order if needed
	if(vertical) {
		std::vector<int16> transposed((size_t)scanDataPoints);
		for(uInt64 k = 0; k < samples; k++) {
			for(uInt64 j = 0; j < height; j++) {
				for(uInt64 i = 0; i < width; i++) {
					transposed[(size_t)(width*j+i)] = frameImages[(size_t)k][(size_t)(height*i+j)];
				}
			}
			transposed.swap(frameImages[(size_t)k]);
		}
	}
	return frameImages;
}

#endif