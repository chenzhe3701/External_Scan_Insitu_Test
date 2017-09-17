#include <iostream>
#include <fstream>

#include "ExternalScan.h"
#include "tif.hpp"

static const float64 maxVoltage = 4.0;//hard coded limit on voltage amplitude to protect scan coils

//@brief: collect and image with the current parameter set
//@param argc: number of arguments
//@param argv: arguments
//@param scan: ExternalScan object to populate
//@param output: output image file name to populate
//@param log: output time log name to populate (or leave blank if the -t flag isn't used)
void parseArguments(int argc, char *argv[], ExternalScan& scan, std::string& output, std::string log);

int main(int argc, char *argv[]) {
	try {
		//parse arguments
		ExternalScan scan;
		std::string imageName, timeLog;
		parseArguments(argc, argv, scan, imageName, timeLog);

		//execute scan and write image
		std::time_t start = std::time(NULL);
		std::vector< std::vector<int16> > image = scan.execute();
		std::time_t end = std::time(NULL);
		Tif::Write(image, (std::uint32_t)scan.getWidth(), (std::uint32_t)scan.getHeight(), imageName);

		//write z projection of selected frames
		size_t frameSkip = 5;
		std::vector<std::int32_t> avgImage(image.front().size(), 0x00000000);
		for(size_t i = 0; i < image.size(); i++) std::transform(image[i].begin(), image[i].end(), avgImage.begin(), avgImage.begin(), [](const int16& lhs, const std::int32_t& rhs){return (std::int32_t)lhs + rhs;});
		Tif::Write(avgImage, (std::uint32_t)scan.getWidth(), (std::uint32_t)scan.getHeight(), "avg.tif");

		//append time stamps to log if needed
		if(!timeLog.empty()) {
			//check if log file already exists
			std::ifstream is(timeLog);
			bool exists = is.good();
			is.close();

			//write time stamp to time stamp log
			std::ofstream of(timeLog, std::ios_base::app);
			if(!exists) of << "filename\timage start\timage start (unix)\timage end\timage end (unix)\n";//write header on first entry
			of << imageName << "\t" << std::asctime(std::localtime(&start)) << "\t" << start << "\t" << std::asctime(std::localtime(&end)) << "\t" << end << "\n";
		}

	} catch (std::exception& e) {
		std::cout << e.what();
		return EXIT_FAILURE;
	}
	return EXIT_SUCCESS;
}

void parseArguments(int argc, char *argv[], ExternalScan& scan, std::string& output, std::string log) {
	//build help string
	scan = ExternalScan();
	std::stringstream ss;
	ss << "usage: " + std::string(argv[0]) + " -x path -y path -e path -s samples -a voltage -o file [-w width] [-h height] [-v] [-r] [-t file] [-b voltage] [-i voltage]\n";
	ss << "\t -x : path to X analog out channel (e.g. 'Dev0/ao0')\n";
	ss << "\t -y : path to Y analog out channel\n";
	ss << "\t -e : path to ETD analog in channel\n";
	ss << "\t -s : samples per pixel\n";
	ss << "\t -a : half amplitude of scan in volts\n";
	ss << "\t -o : output image name (tif format)\n";
	ss << "\t[-w]: scan width in pixels (defaults to " << scan.getWidth() << ")\n";
	ss << "\t[-h]: scan height in pixels (defaults to " << scan.getHeight() << ")\n";
	ss << "\t[-v]: scan Y instead of X direction first\n";
	ss << "\t[-r]: scan in a raster instead of snake pattern\n";
	ss << "\t[-t]: append image aquisitions times to log file\n";
	ss << "\t[-b]: ETD voltage for black (defaults to " << scan.getEtdWhite() << "V)\n";
	ss << "\t[-i]: ETD voltage for white (defaults to " << scan.getEtdWhite() << "V)\n";

	//required arguments
	std::string xPath, yPath, ePath;
	float64 scanVoltage = 0.0;
	uInt64 samples = 2;

	//optional arguments
	bool snake = true, vertical = false;
	uInt64 width = scan.getWidth(), height = scan.getHeight();
	float64 etdMin = scan.getEtdBlack(), etdMax = scan.getEtdWhite();

	for(int i = 1; i < argc; i++) {
		//make sure flag(s) exist and start with a '-'
		const size_t flagCount = strlen(argv[i]) - 1;
		if('-' != argv[i][0] || flagCount == 0) throw std::runtime_error(std::string("unknown option: ") + argv[i]);

		//parse each option in this group
		for(size_t j = 0; j < flagCount; j++) {
			//check if this flag has a corresponding argument and make sure that argument exists
			bool requiresOption = true;
			switch(argv[i][j+1]) {
				case 'v':
				case 'r':
					requiresOption = false;
			}
			if(requiresOption && (i+1 == argc || j+1 != flagCount)) throw std::runtime_error(std::string("missing argument for option ") + argv[i][j]);

			switch(argv[i][j+1]) {
				case 'x': xPath       = std::string(argv[i+1]); break;
				case 'y': yPath       = std::string(argv[i+1]); break;
				case 'e': ePath       = std::string(argv[i+1]); break;
				case 's': samples     = atoi(argv[i+1]); break;
				case 'a': scanVoltage = atof(argv[i+1]); break;
				case 'o': output      = std::string(argv[i+1]); break;
				case 'w': width       = atoi(argv[i+1]); break;
				case 'h': height      = atoi(argv[i+1]); break;
				case 'v': vertical    = true;  break;
				case 'r': snake       = false; break;
				case 't': log         = std::string(argv[i+1]); break;
				case 'b': etdMin      = atof(argv[i+1]); break;
				case 'i': etdMax      = atof(argv[i+1]); break;
			}
			if(requiresOption) ++i;//double increment if the next agrument isn't a flag
		}
	}

	//make sure required arguments were passed
	if(xPath.empty()) throw std::runtime_error(ss.str() + "(x flag missing)");
	if(yPath.empty()) throw std::runtime_error(ss.str() + "(y flag missing)");
	if(ePath.empty()) throw std::runtime_error(ss.str() + "(e flag missing)");
	if(output.empty()) throw std::runtime_error(ss.str() + "(o flag missing)");
	if(0.0 == scanVoltage) throw std::runtime_error(ss.str() + "(a flag missing or empty)");
	if(scanVoltage > maxVoltage) throw std::runtime_error(ss.str() + "(scan amplitude is too large - passed " + std::to_string(scanVoltage) + ", max " + std::to_string(maxVoltage) + ")");

	//configure scan object and write scan pattern to hardware buffer
	scan.setPaths(xPath, yPath, ePath);
	scan.setDimensions(width, height);
	scan.setSamples(samples);
	scan.setScanType(vertical, snake);
	scan.setCalibrations(scanVoltage, etdMin, etdMax);
	scan.configureScan();
}
