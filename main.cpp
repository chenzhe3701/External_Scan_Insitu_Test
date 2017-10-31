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

#include <iostream>
#include <fstream>

#include "ExternalScan.h"

static const float64 maxVoltage = 4.0;//hard coded limit on voltage amplitude to protect scan coils

int main(int argc, char *argv[]) {
	try {
		//arguments
		std::string xPath = "dev2/ao0";
		std::string yPath = "dev2/ao1";
		std::string ePath = "dev2/ai2";
		std::string output = "";
		float64 scanVoltageH = 3.86;	//horizontal voltage
		float64 scanVoltageV = 3.9;	//vertical voltage
		uInt64 dwellSamples = 2;

		bool snake = true;
		std::string timeLog = "d:\\timgLog.txt";
		uInt64 width = 4096;
		uInt64 height = 4096;

		float64 vBlack = -1.25;			//voltage for black
		float64 vWhite = 1.75;			//voltage for whilte
		bool correct = true;			//use fft to correct
		bool saveAverageOnly = true;	//whether to save averaged figure ony
		uInt64 nFrames = 2;				//frames from last to integrate
		float64 maxShift = 10.0;		//maximum pixel shift to correct
		bool delayTF = 1;				//whether to use dealy at left edge
		uInt64 autoLoop = 0;			//whether use this code to do an auto image test with iFast
		std::string output_raw;			// records the raw output name

		//build help string
		std::stringstream ss;
		ss << "usage: " + std::string(argv[0]) + " -x path -y path -e path -s dwellSamples -a voltage -o file [-w width] [-h height] [-r] [-t file]\n";
		ss << "\t -x : path to X analog out channel (e.g. 'Dev0/ao0') (defaults to " << xPath << ")\n";
		ss << "\t -y : path to Y analog out channel (defaults to " << yPath << ")\n";
		ss << "\t -e : path to ETD analog in channel (defaults to " << ePath << ")\n";
		ss << "\t -s : dwellSamples per pixel (defaults to " << dwellSamples << ")\n";
		ss << "\t -a : half amplitude of scan in volts, horizontal (defaults to " << scanVoltageH << ")\n";
		ss << "\t -b : half amplitude of scan in volts, vertical (defaults to " << scanVoltageV << ")\n";
		ss << "\t -o : output image name (tif format) (defaults to " << output << ")\n";
		ss << "\t[-w]: scan width in pixels (defaults to " << width << ")\n";
		ss << "\t[-h]: scan height in pixels (defaults to " << height << ")\n";
		ss << "\t[-r]: scan in a raster instead of snake pattern (defaults to false, i.e., using snake)\n";
		ss << "\t[-t]: append image aquisitions times to log file (defaults to " << timeLog << ")\n";
		ss << "\t[-k]: voltage for black pixel (defaults to " << vBlack << ")\n";
		ss << "\t[-i]: voltage for white pixel (defaults to " << vWhite << ")\n";
		ss << "\t[-f]: max number of pixels to shift (defaults to " << maxShift << ")\n";
		ss << "\t[-c]: correct frames or not (defaults to " << correct << ")\n";
		ss << "\t[-v]: save averaged image only (defaults to " << saveAverageOnly << ")\n";
		ss << "\t[-n]: # of frames from last to integrate (defaults to " << nFrames << ")\n";
		ss << "\t[-d]: delay to compensate for distortion at left edge (defaults to " << delayTF << ")\n";
		ss << "\t[-p]: autoLoop until stop signal (3000Hz) and auto fileName, default = " << autoLoop << ")\n";

		//parse arguments
		for (int i = 1; i < argc; i++) {
			//make sure flag(s) exist and start with a '-'
			const size_t flagCount = strlen(argv[i]) - 1;
			if ('-' != argv[i][0] || flagCount == 0) throw std::runtime_error(std::string("unknown option: ") + argv[i]);

			//parse each option in this group
			for (size_t j = 0; j < flagCount; j++) {
				//check if this flag has a corresponding argument and make sure that argument exists
				bool requiresOption = true;		// requiresOption means --> switch should be followed by another 
				switch (argv[i][j + 1]) {
				case 'r':
					requiresOption = false;
				}
				if (requiresOption && (i + 1 == argc || j + 1 != flagCount)) throw std::runtime_error(std::string("missing argument for option ") + argv[i][j]);

				switch (argv[i][j + 1]) {
				case 'x': xPath = std::string(argv[i + 1]); break;
				case 'y': yPath = std::string(argv[i + 1]); break;
				case 'e': ePath = std::string(argv[i + 1]); break;
				case 's': dwellSamples = atoi(argv[i + 1]); break;
				case 'a': scanVoltageH = atof(argv[i + 1]); break;
				case 'o': {
					output = std::string(argv[i + 1]);
					output_raw = output;
					break;
				}
				case 'w': width = atoi(argv[i + 1]); break;
				case 'h': height = atoi(argv[i + 1]); break;
				case 'r': snake = false; break;
				case 't': timeLog = std::string(argv[i + 1]); break;

				case 'k': vBlack = atof(argv[i + 1]); break;
				case 'i': vWhite = atof(argv[i + 1]); break;
				case 'f': maxShift = atof(argv[i + 1]); break;
				case 'c': correct = atoi(argv[i + 1]); break;
				case 'v': saveAverageOnly = atoi(argv[i + 1]); break;
				case 'n': nFrames = atoi(argv[i + 1]); break;
				case 'd': delayTF = atoi(argv[i + 1]); break;
				case 'b': scanVoltageV = atof(argv[i + 1]); break;
				case 'p': autoLoop = atoi(argv[i + 1]); break;
				}
				if (requiresOption) ++i;//double increment if the next agrument isn't a flag
			}
			if (nFrames > dwellSamples){
				std::cout << "nFrames > dwellSamples found, so just use the latter value" << std::endl;
				nFrames = dwellSamples;
			}
		}

		//make sure required arguments were passed
		if (xPath.empty()) throw std::runtime_error(ss.str() + "(x flag missing)\n");
		if (yPath.empty()) throw std::runtime_error(ss.str() + "(y flag missing)\n");
		if (ePath.empty()) throw std::runtime_error(ss.str() + "(e flag missing)\n");
		if (output.empty()) throw std::runtime_error(ss.str() + "(o flag missing)\n");
		if (0.0 == scanVoltageH) throw std::runtime_error(ss.str() + "(a flag missing or empty)\n");
		if (0.0 == scanVoltageV) throw std::runtime_error(ss.str() + "(b flag missing or empty)\n");
		if (scanVoltageH > maxVoltage) throw std::runtime_error(ss.str() + "(scan amplitude is too large - passed " + std::to_string(scanVoltageH) + ", max " + std::to_string(maxVoltage) + ")\n");
		if (scanVoltageV > maxVoltage) throw std::runtime_error(ss.str() + "(scan amplitude is too large - passed " + std::to_string(scanVoltageV) + ", max " + std::to_string(maxVoltage) + ")\n");

		// chenzhe, detectFrequency and do the scanning
		int iR = 0;
		int iC = 0;
		bool continueTF = true;
		while (continueTF){
			if (1 == autoLoop){
				int frequency = detectFrequency() - 1000;
				std::cout << "safitfactory freq detected" << std::endl;
				iR = frequency / 100;
				iC = (frequency - 100 * iR) / 4;
				output = output_raw + "_r" + std::to_string(iR) + "c" + std::to_string(iC) + ".tif";
				if (iR > 19) return EXIT_SUCCESS;	// stop by sending a >=3000Hz sound. Or iR >= 20.
			}
			else{
				output = output_raw;
				continueTF = false;
			}

			//create scan opject
			ExternalScan scan(xPath, yPath, ePath, dwellSamples, scanVoltageH, width, height, snake, vBlack, vWhite, delayTF, scanVoltageV);

			//execute scan and write image
			std::time_t start = std::time(NULL);
			scan.execute(output, correct, saveAverageOnly, nFrames, maxShift);
			std::time_t end = std::time(NULL);

			//append time stamps to log if needed
			if (!timeLog.empty()) {
				//check if log file already exists
				std::ifstream is(timeLog);
				bool exists = is.good();
				is.close();

				//write time stamp to time stamp log
				std::ofstream of(timeLog, std::ios_base::app);
				if (!exists) of << "filename\timage start\timage start (unix)\timage end\timage end (unix)\n";//write header on first entry
				std::string startTime = std::asctime(std::localtime(&start));
				startTime.pop_back();
				std::string endTime = std::asctime(std::localtime(&end));
				endTime.pop_back();
				of << output << "\t" << startTime.data() << "\t" << start << "\t" << endTime.data() << "\t" << end << "\n";
			}

			//Beep(3000, 10000);	// beep to let MPC know scan finished
		}
	}
	catch (std::exception& e) {
		std::cout << e.what();
		return EXIT_FAILURE;
	}
	return EXIT_SUCCESS;
}