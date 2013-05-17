#include <stdlib.h>
#include <malloc.h>
#include <memory.h>
#include <tchar.h>

#include <StrSafe.h>
#include <string>
#include <iostream>
#include <Windows.h>
#include "Capture.h"

using namespace std;

// class constructor
Capture::Capture(HWND hSethWnd)
{
	hWnd = GetConsoleWindow();
	havedata = false; // no image stored at start.
	       //  wrong = create a device-context, this will be the source to copy from.
	hdcScreen = CreateDC(L"DISPLAY", // pointer to string specifying driver name 
						 NULL,	     // pointer to string specifying device name 
						 NULL,  	 // do not use; set to NULL 
						 NULL);		 // pointer to optional printer data 
}
// class destructor
Capture::~Capture()
{
	if (hbmScreen > 0)
		DeleteObject(hbmScreen); // delete bitmap object
	if (hdcScreen>0) 
		DeleteDC(hdcScreen); // delete this device-context
	if (hdcCompatible > 0)
		DeleteDC(hdcCompatible); // delete this one too
}

HWND Capture::GetConsoleHwnd(void)
{
#define MY_BUFSIZE 1024 // Размер буфера для заголовка консольного окна.
	HWND hwndFound;         // Это то, что будет возвращено.
	char pszNewWindowTitle[MY_BUFSIZE]; // Уникальный заголовок окна.
	char pszOldWindowTitle[MY_BUFSIZE]; // Изначальный заголовок окна.
	GetConsoleTitle((LPWSTR)pszOldWindowTitle, MY_BUFSIZE);
	wsprintf((LPWSTR)pszNewWindowTitle, L"%d/%d",
		GetTickCount(),
		GetCurrentProcessId());

	

	SetConsoleTitle((LPWSTR)pszNewWindowTitle);

	

	Sleep(40);

	

	hwndFound=FindWindow(NULL, (LPCWSTR)pszNewWindowTitle);

	

	SetConsoleTitle((LPCWSTR)pszOldWindowTitle);

	return(hwndFound);
} 
// copies the stored image to another application handle
void Capture::CopyTo(HWND hwnd)
{
	if (!havedata) return; // cannot proceed, we have nothing to copy!
	HDC screendata =  GetDC(hwnd); // the area to copy to...
	// make the transfer using stored data
	BitBlt(	screendata,		// handle to destination device context 
		Left,			// x-coordinate of destination rectangle's upper-left corner
		Top,			// y-coordinate of destination rectangle's upper-left corner
		Right,			// width of destination rectangle 
		Bottom,			// height of destination rectangle 
		hdcCompatible,	// handle to source device context 
		Left,			// x-coordinate of source rectangle's upper-left corner  
		Top,			// y-coordinate of source rectangle's upper-left corner
		SRCCOPY 		// raster operation code 
		);
	ReleaseDC(hwnd, screendata); // release device-context
}

void Capture::TakePic(int top, int left, int bottom, int right)
{
	// initialize capture settings
	Top = top;
	Left = left;
	Bottom = bottom; 
	Right = right; // save co-ordinates
	int width = right-left; // width for bitmap object
	int height = bottom; // height for bitmap object

	

	// we need to create a CompatibleDC with 'hdcScreen' because
	// CreateCompatibleBitmap() only accepts this type of handle.
	hdcCompatible = CreateCompatibleDC(hdcScreen); 

	// now we define the handle to the compatible bitmap
	// and we point it to 'hdcScreen' where it is saved.
	hbmScreen =  CreateCompatibleBitmap(
		hdcScreen,	// handle to device context 
		width,		// width of bitmap, in pixels  
		height 		// height of bitmap, in pixels  
		);

	// The SelectObject() function selects an object into the specified device context.
	// The new object replaces the previous object of the same type.
	SelectObject(hdcCompatible, hbmScreen); 

	// now we copy data from the source to our destination 'hdcCompatible'
	// which, is actually our handle to the bitmap object we created.
	BitBlt(	hdcCompatible,	// handle to destination device context 
		left,			// x-coordinate of destination rectangle's upper-left corner
		top,			// y-coordinate of destination rectangle's upper-left corner
		right,			// width of destination rectangle 
		bottom,			// height of destination rectangle 
		hdcScreen,		// handle to source device context 
		left,			// x-coordinate of source rectangle's upper-left corner  
		top,			// y-coordinate of source rectangle's upper-left corner
		SRCCOPY 		// raster operation code 
		);

	havedata = true; // we now have a stored image.
}
void Capture::WriteBMP(const HBITMAP hBitmap, LPTSTR filename, HDC hDC)
{
	BITMAP bitmap; 
	PBITMAPINFO pBitmapInfo; 
	WORD cClrBits; 
	HANDLE fileHandle; // file handle 
	BITMAPFILEHEADER bitmapFileHeader; // bitmap file-header 
	PBITMAPINFOHEADER pBitmapInfoHeader; // bitmap info-header 
	LPBYTE lpBits; // memory pointer 
	DWORD dwTotal; // total count of bytes 
	DWORD cb; // incremental count of bytes 
	BYTE *hp; // byte pointer 
	DWORD dwTmp; 

	// create the bitmapinfo header information

	if (!GetObject(hBitmap, sizeof(BITMAP), (LPVOID)&bitmap)){
		//AfxMessageBox("Could not retrieve bitmap info");
		return;
	}
	
	// Convert the color format to a count of bits. 
	cClrBits = (WORD)(bitmap.bmPlanes * bitmap.bmBitsPixel); 
	if (cClrBits == 1) 
		cClrBits = 1; 
	else if (cClrBits <= 4) 
		cClrBits = 4; 
	else if (cClrBits <= 8) 
		cClrBits = 8; 
	else if (cClrBits <= 16) 
		cClrBits = 16; 
	else if (cClrBits <= 24) 
		cClrBits = 24; 
	else cClrBits = 32; 

	// Allocate memory for the BITMAPINFO structure.
	if (cClrBits != 24) 
		pBitmapInfo = (PBITMAPINFO) LocalAlloc(LPTR, 
		sizeof(BITMAPINFOHEADER) + sizeof(RGBQUAD) * (1<< cClrBits)); 
	else 
		pBitmapInfo = (PBITMAPINFO) LocalAlloc(LPTR, sizeof(BITMAPINFOHEADER)); 

	// Initialize the fields in the BITMAPINFO structure. 
	pBitmapInfo->bmiHeader.biSize = sizeof(BITMAPINFOHEADER); 
	pBitmapInfo->bmiHeader.biWidth = bitmap.bmWidth; 
	pBitmapInfo->bmiHeader.biHeight = bitmap.bmHeight; 
	pBitmapInfo->bmiHeader.biPlanes = bitmap.bmPlanes; 
	pBitmapInfo->bmiHeader.biBitCount = bitmap.bmBitsPixel; 
	if (cClrBits < 24) 
		pBitmapInfo->bmiHeader.biClrUsed = (1<<cClrBits); 

	// If the bitmap is not compressed, set the BI_RGB flag. 
	pBitmapInfo->bmiHeader.biCompression = BI_RGB; 

	// Compute the number of bytes in the array of color 
	// indices and store the result in biSizeImage. 
	pBitmapInfo->bmiHeader.biSizeImage = (pBitmapInfo->bmiHeader.biWidth + 7) /8 * pBitmapInfo->bmiHeader.biHeight * cClrBits; 
	// Set biClrImportant to 0, indicating that all of the 
	// device colors are important. 
	pBitmapInfo->bmiHeader.biClrImportant = 0; 

	// now open file and save the data
	pBitmapInfoHeader = (PBITMAPINFOHEADER) pBitmapInfo; 
	lpBits = (LPBYTE) GlobalAlloc(GMEM_FIXED, pBitmapInfoHeader->biSizeImage);

	if (!lpBits) {
		//AfxMessageBox("writeBMP::Could not allocate memory");
		return;
	}

	// Retrieve the color table (RGBQUAD array) and the bits 
	if (!GetDIBits(hDC, HBITMAP(hBitmap), 0, (WORD) pBitmapInfoHeader->biHeight, lpBits, pBitmapInfo, 
		DIB_RGB_COLORS)) {
			//AfxMessageBox("writeBMP::GetDIB error");
			return;
	}

	// Create the .BMP file. 
	fileHandle = CreateFile(filename, GENERIC_READ | GENERIC_WRITE, (DWORD) 0, 
		NULL, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, 
		(HANDLE) NULL); 
	if (fileHandle == INVALID_HANDLE_VALUE){
		//MessageBox(hWND,CHAR("Could not create file for writing"), NULL, NULL);
		return;
	}
	bitmapFileHeader.bfType = 0x4d42; // 0x42 = "B" 0x4d = "M" 
	// Compute the size of the entire file. 
	bitmapFileHeader.bfSize = (DWORD) (sizeof(BITMAPFILEHEADER) + 
		pBitmapInfoHeader->biSize + pBitmapInfoHeader->biClrUsed 
		* sizeof(RGBQUAD) + pBitmapInfoHeader->biSizeImage); 
	bitmapFileHeader.bfReserved1 = 0; 
	bitmapFileHeader.bfReserved2 = 0; 

	// Compute the offset to the array of color indices. 
	bitmapFileHeader.bfOffBits = (DWORD) sizeof(BITMAPFILEHEADER) + 
		pBitmapInfoHeader->biSize + pBitmapInfoHeader->biClrUsed 
		* sizeof (RGBQUAD); 

	// Copy the BITMAPFILEHEADER into the .BMP file. 
	if (!WriteFile(fileHandle, (LPVOID) &bitmapFileHeader, sizeof(BITMAPFILEHEADER), 
		(LPDWORD) &dwTmp, NULL)) {
			//AfxMessageBox("Could not write in to file");
			return;
	}

	// Copy the BITMAPINFOHEADER and RGBQUAD array into the file. 
	if (!WriteFile(fileHandle, (LPVOID) pBitmapInfoHeader, sizeof(BITMAPINFOHEADER) 
		+ pBitmapInfoHeader->biClrUsed * sizeof (RGBQUAD), 
		(LPDWORD) &dwTmp, ( NULL))){
			//AfxMessageBox("Could not write in to file");
			return;
	}


	// Copy the array of color indices into the .BMP file. 
	dwTotal = cb = pBitmapInfoHeader->biSizeImage; 
	hp = lpBits; 
	if (!WriteFile(fileHandle, (LPSTR) hp, (int) cb, (LPDWORD) &dwTmp,NULL)){
		// AfxMessageBox("Could not write in to file");
		return;
	}
	
	// Close the .BMP file. 
	if (!CloseHandle(fileHandle)){
		//AfxMessageBox("Could not close file");
		return;
	}

	// Free memory. 
	GlobalFree((HGLOBAL)lpBits);
}

void Capture::ScreenShot(wstring fileName)
{
	TakePic(0,0,HEIGHT,WIDTH); 
	wstring outFile = L"D:\\Test\\" + fileName + L".bitmap"; //
	
	WriteBMP(hbmScreen, (LPTSTR)outFile.c_str(), hdcScreen);
}

