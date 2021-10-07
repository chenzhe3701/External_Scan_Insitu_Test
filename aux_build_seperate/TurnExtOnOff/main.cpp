#include <iostream>
#include <string>
#include <fstream>
#include <windows.h>
#include <stdio.h>
#include <ctime>

// functions does not belong to class ExternalScan
void simReturn(){
	Sleep(200);
	INPUT ip;

	ip.type = INPUT_KEYBOARD;
	ip.ki.wScan = 0;
	ip.ki.time = 0;
	ip.ki.dwExtraInfo = 0;

	// Press 
	ip.ki.wVk = VK_RETURN;
	ip.ki.dwFlags = 0; // 0 for key press
	SendInput(1, &ip, sizeof(INPUT));

	// Release
	ip.ki.wVk = VK_RETURN;
	ip.ki.dwFlags = KEYEVENTF_KEYUP;
	SendInput(1, &ip, sizeof(INPUT));

	Sleep(100); // pause for 0.1 second
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

	// Press the "V" key
	ip.ki.wVk = VkKeyScanEx(inputChar, GetKeyboardLayout(0));
	ip.ki.dwFlags = 0; // 0 for key press
	SendInput(1, &ip, sizeof(INPUT));

	// Release the "V" key
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
void externalOnOff(std::string A = "Microscope Control v7.6.1   User: supervisor"){
	std::wstring WA(A.begin(), A.end());
	HWND hwnd = FindWindow(NULL, WA.c_str());
	if (hwnd) {
		SetForegroundWindow(hwnd);
		Sleep(1000);
		//simMouseClick(50, 200);	// click quad 1
		simMouseClick(1300, 200);	// click quad 2, sometimes 50 overlapped by sth else
		Sleep(1000);
		simAltKey('c');
		Sleep(1000);
		simMouseClick(400, 290);	// click 'external scan'
		Sleep(1000);
	}
	else{
		std::cout << A << " window not found" << std::endl;
	}
}

int main(int argc, char *argv[])
{
	if (argc > 1){
		std::string str(argv[1]);
		externalOnOff(str);
	}
	else{
		externalOnOff("Microscope Control v7.6.1   User: supervisor"); // supervisor support
	}
		
	return 0;
}