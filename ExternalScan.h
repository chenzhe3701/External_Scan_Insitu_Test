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

//unsigned 16 bit
typedef uInt16 niDaqScalar;
#define DAQmxReadBinary16 DAQmxReadBinaryU16

//signed 16 bit
//typedef int16 niDaqScalar;
//#define DAQmxReadBinary16 DAQmxReadBinaryI16

struct ExternalScan {
	std::string xPath, yPath;//path to analog output channels for scan control
	std::string etdPath;//path to analog input channel for etd
	uInt64 width, height;//dimensions of the scan
	float64 dwell;//dwell time in us
	float64 vRange;//voltage range for scan (scan will go from -vRange -> +vRange in the larger direction)
	bool vertical;//true/false to scan in vertical/horiztonal direction
	bool snake;//true/false to snake/raster
	float64 minEtd, maxEtd;//anticipated range of voltages out of the etd
	TaskHandle hInput, hOutput;

	ExternalScan() : vRange(4.0), vertical(false), minEtd(0.0), maxEtd(5.0), hInput(0), hOutput(0) {}

	void DAQmxTry(int32 error, std::string message) {
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
			throw std::runtime_error("NI-DAQmx error " + std::to_string(error) + ":\n" + std::string(buff.data()));
		}
	}

	std::vector<niDaqScalar> execute(std::time_t* start = NULL, std::time_t* end = NULL) {
		//check scan rate, the microscope is limited to a 300 ns dwell at 768 x 512
		//3.33 x factor of safety -> require at least 768 us to cover full -4 -> +4 V scan
		const float64 minDwell = (768.0 / (vertical ? height : width)) * (4.0 / vRange);//minimum dwell time in us
		if(dwell < minDwell) throw std::runtime_error("Dwell time too short - dwell must be at least " + std::to_string(minDwell) + " us for " + std::to_string(vertical ? height : width) + " pixel scan lines");

		//create tasks
		int32 error;
		TaskHandle hInput, hOutput;
		DAQmxTry(DAQmxCreateTask("", &hInput), "creating input task");
		DAQmxTry(DAQmxCreateTask("", &hOutput), "creating output task");

		//create channels
		DAQmxTry(DAQmxCreateAIVoltageChan(hInput, etdPath.c_str(), "", DAQmx_Val_Cfg_Default, minEtd, maxEtd, DAQmx_Val_Volts, NULL), "creating input channel");
		DAQmxTry(DAQmxCreateAOVoltageChan(hOutput, (xPath + "," + yPath).c_str(), "", -vRange, vRange, DAQmx_Val_Volts, NULL), "creating output channel");

		//configure timing and start triggering
		const uInt64 totalPixels = width * height;
		float64 sampleRate = 1000000.0 / dwell;
		DAQmxTry(DAQmxCfgSampClkTiming(hInput, "", sampleRate, DAQmx_Val_Rising, DAQmx_Val_FiniteSamps, totalPixels), "configuring input timing");
		DAQmxTry(DAQmxCfgSampClkTiming(hOutput, "", sampleRate, DAQmx_Val_Rising, DAQmx_Val_FiniteSamps, totalPixels), "configuring output timing");

		//setup one task to start automatically when the other does
		uInt32 numDevices;
		std::string trigName;
		DAQmxTry(DAQmxGetTaskNumDevices(hInput, &numDevices), "getting number of devices");
		for(uInt32 i = 1; i <= numDevices; i++) {//the microscope has some embedded national instuments daqs, get appriopriate device
			char device[256];
			int32 productCategory;
			DAQmxTry(DAQmxGetNthTaskDevice(hInput, i++, device, sizeof(device)), "getting nth device");
			DAQmxTry(DAQmxGetDevProductCategory(device, &productCategory), "getting nth device");
			if(productCategory != DAQmx_Val_CSeriesModule && productCategory != DAQmx_Val_SCXIModule) {
				trigName = "/" + std::string(device) + "/ai/StartTrigger";
				break;
			}
		}
		DAQmxTry(DAQmxCfgDigEdgeStartTrig(hOutput, trigName.c_str(), DAQmx_Val_Rising), "setting start trigger");

		//generate uniformly spaced grid of points from -vRange -> vRange in largest direction
		std::vector<float64> xData(width), yData(height);
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

		//generate scan
		std::vector<float64> data;
		data.reserve(2 * totalPixels);
		if(snake) {
			if(vertical) {
				for(size_t i = 0; i < width; i++) data.insert(data.end(), height, xData[i]);
				for(size_t i = 0; i < width; i++) {
					if(i % 2 == 0)
						data.insert(data.end(), yData.begin(), yData.end());
					else
						data.insert(data.end(), yData.rbegin(), yData.rend());
				}
			} else {
				for(size_t i = 0; i < height; i++) {
					if(i % 2 == 0)
						data.insert(data.end(), xData.begin(), xData.end());
					else
						data.insert(data.end(), xData.rbegin(), xData.rend());
				}
				for(size_t i = 0; i < height; i++) data.insert(data.end(), width, yData[i]);
			}
		} else {
			if(vertical) {
				for(size_t i = 0; i < width; i++) data.insert(data.end(), height, xData[i]);
				for(size_t i = 0; i < width; i++) data.insert(data.end(), yData.begin(), yData.end());
			} else {
				for(size_t i = 0; i < height; i++) data.insert(data.end(), xData.begin(), xData.end());
				for(size_t i = 0; i < height; i++) data.insert(data.end(), width, yData[i]);
			}
		}

		//write scan data to device buffer
		int32 written;
		DAQmxTry(DAQmxWriteAnalogF64(hOutput, totalPixels, FALSE, DAQmx_Val_WaitInfinitely, DAQmx_Val_GroupByChannel, data.data(), &written, NULL), "writing scan to buffer");
		DAQmxTry(totalPixels - written, "writing all scan data to buffer");

		//execute scan
		if(NULL != start) *start = std::time(NULL);
		DAQmxTry(DAQmxStartTask(hOutput), "starting output task");
		DAQmxTry(DAQmxStartTask(hInput), "starting input task");
		
		//wait for scan to complete
		float64 scanTime = float64(totalPixels) * dwell / 1000000.0 + 5.0;//allow an extra 5s
		DAQmxTry(DAQmxWaitUntilTaskDone(hOutput, scanTime), "waiting for output task");
		DAQmxTry(DAQmxWaitUntilTaskDone(hInput, scanTime), "waiting for input task");
		if(NULL != end) *end = std::time(NULL);

		//read image from buffer (over top of scan data)
		int32 read;
		std::vector<niDaqScalar> intData(totalPixels);
		DAQmxTry(DAQmxReadBinary16(hInput, totalPixels, DAQmx_Val_WaitInfinitely, DAQmx_Val_GroupByChannel, intData.data(), intData.size(), &read, NULL), "reading data from buffer");
		DAQmxTry(read - totalPixels, "reading all data from buffer");

		//clean up tasks and return
		DAQmxStopTask(hInput);
		DAQmxClearTask(hInput);
		DAQmxStopTask(hOutput);
		DAQmxClearTask(hOutput);

		//convert snake to raster if needed
		if(snake) {
			if(vertical) {
				for(uInt64 i = 1; i < width; i+=2)
					std::reverse(intData.begin() + i * height, intData.begin() + i * height + height);
			} else {
				for(uInt64 i = 1; i < height; i+=2)
					std::reverse(intData.begin() + i * width, intData.begin() + i * width + width);
			}
		}

		//reorder data to be in row major order if needed
		if(vertical) {
			std::vector<niDaqScalar> transposed(totalPixels);
			for(uInt64 j = 0; j < height; j++) {
				for(uInt64 i = 0; i < width; i++) {
					transposed[width*j+i] = intData[height*i+j];
				}
			}
			transposed.swap(intData);
		}
		return intData;
	}
};

#endif