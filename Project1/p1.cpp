
#include "windows.h"
#include <vector>
#include "clipv.cpp"
#include <functional>

void CppClip(const char* SRC_FILE, const char* OUT_FILE, double from, double to);
//void CppClipKeep(const char* SRC_FILE, const char* OUT_FILE, double from, double to);

static std::vector<AVPicture*> pics;
void AddMsgFunc(int msg, std::function<int(int,int, int)> f);

static int offsetX = 0;

void out2rect(void *data, int w1, int h1, HDC &hdc, RECT  &rect) {

	BITMAPINFO lpBmpInfo;
	lpBmpInfo.bmiHeader.biBitCount = 32;
	lpBmpInfo.bmiHeader.biClrImportant = 0;
	lpBmpInfo.bmiHeader.biClrUsed = 0;
	lpBmpInfo.bmiHeader.biCompression = BI_RGB;
	lpBmpInfo.bmiHeader.biPlanes = 1;
	lpBmpInfo.bmiHeader.biXPelsPerMeter = 0;
	lpBmpInfo.bmiHeader.biYPelsPerMeter = 0;
	lpBmpInfo.bmiHeader.biSize = sizeof(lpBmpInfo.bmiHeader);
	lpBmpInfo.bmiHeader.biWidth = w1;
	lpBmpInfo.bmiHeader.biHeight = h1;
	lpBmpInfo.bmiHeader.biSizeImage = w1 * h1 * 4;



	SetStretchBltMode(hdc, COLORONCOLOR);
	StretchDIBits(hdc, rect.left, (rect.bottom - rect.top), rect.right - rect.left, -(rect.bottom - rect.top), 0, 0, w1, h1,
		data, &lpBmpInfo, DIB_RGB_COLORS, SRCCOPY);
	//int error=GetLastError();

	//TextOut(hdc,xxx,0,ss.GetBuffer(),ss.GetLength());
/*
			int  b=((BYTE*)data)[j*w1*2+k*2];
			SetPixel(hdc,k,j,RGB(r,g,b));
	*/
}

void RefreshView(HWND hWnd, HDC hdc) {
	//SetFocus(hWnd);
	RECT rect;
	GetClientRect(hWnd, &rect);
	for (int i = 0; i < pics.size();i++ ) {
		rect.left = i * 100+10 + offsetX;
		rect.top = 0;
		rect.right = rect.left + 100 -10;
		rect.bottom = rect.top+ 100;
		out2rect(pics[i]->data[0], 100, 100, hdc, rect);
	}
	//draw(hdc);
}

int Mouse(int hWnd,int wParam, int lParam) {
	int fwKeys = LOWORD(wParam);   /*   key   flags   */
	int zDelta = (short)HIWORD(wParam);

	offsetX += zDelta;
	/*   wheel   rotation   */
	int xPos = (short)LOWORD(lParam);
	/*   horizontal   position   of   pointer   */
	int yPos = (short)HIWORD(lParam);
	/*   vertical   position   of   pointer   */

	InvalidateRect((HWND)hWnd, NULL, false);

	return 0;
}

int main() {
	
	//CppClip("E:\\未命名1.mov", "test1.mp4", 0, 15);
	CppClip("test1.mp4", "clip.mp4", 3.001, 8.02);

	//VClip cp;
	//cp.getPictures("D:\\Download\\Media\\5455B4B60010A45B7F7600F267A14D52.rm",0,100,100,100, pics);
	//clipvs("E:\\Media\\[电影天堂www.dy2018.com]gong牛历险记BD国粤英三语中英双字.mp4", "3333.mp4", 45, 55);
	/*
	AddMsgFunc(WM_MOUSEWHEEL,Mouse);
	HWND hwnd = ::GetConsoleWindow();
	HINSTANCE instance = GetModuleHandle(NULL);
	ShowWindow(hwnd, 0);

	wWinMain(instance, NULL, NULL, 1);
	ShowWindow(hwnd, 1);
	*/
}