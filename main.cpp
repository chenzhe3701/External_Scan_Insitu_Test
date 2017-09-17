#include <iostream>

#include "ExternalScan.h"
#include "tif.hpp"

static const std::string ifastRunner("C:\\Program Files\\FEI\\iFAST\\iFastAutoRecipeRunner.exe");
static const std::string xPath("Dev0/ao0");
static const std::string yPath("Dev0/ao1");
static const std::string etdPath("Dev0/ai0");
static const float64 vRange = 4.0;//voltage range for scan

void runScript(std::string script, DWORD timeoutMS) {
	//setup variables
    STARTUPINFO si = { 0 };
    si.cb = sizeof(si);
    si.dwFlags = STARTF_USESTDHANDLES | STARTF_USESHOWWINDOW;
    si.hStdInput = GetStdHandle(STD_INPUT_HANDLE);
    si.hStdOutput =  GetStdHandle(STD_OUTPUT_HANDLE);
    si.hStdError = GetStdHandle(STD_ERROR_HANDLE);
    si.wShowWindow = SW_HIDE;

	PROCESS_INFORMATION pi = {NULL, NULL, 0, 0};
	std::string command(ifastRunner + " " + script);

	//create process and wait for it to finish
	if(!CreateProcess(NULL, const_cast<char*>(command.c_str()), NULL, NULL, false, CREATE_NEW_CONSOLE, NULL, NULL, &si, &pi)) throw std::runtime_error("failed to execute " + ifastRunner);
	DWORD waitVal = WaitForSingleObject(pi.hProcess, timeoutMS);
    if(WAIT_TIMEOUT == waitVal) throw std::runtime_error("timed out waiting for `" + ifastRunner + "' to execute");

	//get return code, close handles, and check exit code
	DWORD exitCode;
	if(!GetExitCodeProcess(pi.hProcess, &exitCode)) throw std::runtime_error("failed to get exit code of " + ifastRunner);
	CloseHandle(pi.hProcess);
	CloseHandle(pi.hThread);
	if(0 != exitCode) throw std::runtime_error(ifastRunner + " returned " + std::to_string(exitCode) + " instead of 0");
}

int main(int argc, char *argv[]) {
	try {
		//parse arguments
		ExternalScan scan;
		scan.xPath = xPath;
		scan.yPath = yPath;
		scan.etdPath = etdPath;
		scan.vRange = vRange;//voltage range for scan
		std::string imageName, preImageScript, postImageScript;

		if(argc < 6) {
			std::cout << "usage: " << argv[0] << "width height dwell dir output [scriptBefore scriptAfter]\n";
			std::cout << "\twidth: image width in pixels\n";
			std::cout << "\theight: image height in pixels\n";
			std::cout << "\tdwell: dwell time in us\n";
			std::cout << "\tdir: scan direction (must be one of 'horizontal' / 'vertical')\n";
			std::cout << "\toutput: output image name (tif format)\n";
			std::cout << "\tscriptBefore [optional]: ifast script to execute before collecting image\n";
			std::cout << "\tscriptAfter [optional]: ifast script to execute after collecting image\n";
			return EXIT_FAILURE;
		}

		//image size, dwell time, scan direction, and output name are required (in that order)
		scan.width = atoi(argv[1]);
		scan.height = atoi(argv[2]);
		scan.dwell = (float64) atof(argv[3]);
		std::string dir(argv[4]);
		if(0 == dir.compare("horizontal")) scan.vertical = false;
		else if(0 == dir.compare("vertical")) scan.vertical = true;
		else throw std::runtime_error("unknown scan direction " + dir);
		imageName = std::string(argv[5]);

		//pre and post script are optional
		if(argc > 6) preImageScript = std::string(argv[6]);
		if(argc > 7) postImageScript = std::string(argv[7]);
		if(argc > 8) std::cout << "warning: ignoring " << argc - 8 << " extra arguments\n";

		//run pre image script
		if(!preImageScript.empty()) runScript(preImageScript, INFINITE);

		//execute scan, and write image
		std::vector<float> image = scan.execute();
		writeTif(image.data(), scan.height, scan.width, imageName);

		//run post image script
		if(!postImageScript.empty()) runScript(postImageScript, INFINITE);
	} catch (std::exception& e) {
		std::cout << e.what();
		return EXIT_FAILURE;
	}
    return EXIT_SUCCESS;
}
