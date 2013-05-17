#ifndef CaptureH
#define CaptureH

#include <stdlib.h>
#include <malloc.h>
#include <memory.h>
#include <tchar.h>

#include <StrSafe.h>
#include <string>
#include <iostream>

#define HEIGHT 768	
#define WIDTH 1366
#define RGB_PLANES 4

using namespace std;

class Capture
{
public:
	HBITMAP hbmScreen;
	HDC hdcScreen; // bitmap object
protected:
	HWND hWnd; //
	 // handle to device driver
	HDC hdcCompatible; // compatible dc to device driver handle
	
	int Top; // top left vertical co-ordinate
	int Left; // top left horizontal co-ordinate
	int Bottom; // bottom right vertical co-ordinate
	int Right; // bottom right horizontal co-ordinate
	bool havedata; // states if we have captured the screen

public:
	Capture(HWND hSethWnd); // class constructor
	~Capture(); // class destructor

	void TakePic(int top, int left, int bottom, int right);
	void CopyTo(HWND hwnd);
	void WriteBMP(const HBITMAP bitmap, LPTSTR filename, HDC hDC);
	void ScreenShot(wstring fileName);
private:
	HWND GetConsoleHwnd();



};
#endif