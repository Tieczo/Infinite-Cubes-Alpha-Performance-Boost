// Test-Hilfsprogramm, nur im Test-Prefix: Eingaben an Cube.exe.
// hand.exe wait | click X Y | key VK | type TEXT | hold VK MS | mouse BTN MS | title
#include <windows.h>
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
static HWND g;
static BOOL CALLBACK en(HWND h, LPARAM l){ char c[256]; DWORD pid; if(!IsWindowVisible(h)) return TRUE; GetClassNameA(h,c,255); GetWindowThreadProcessId(h,&pid);
 char t[256]; GetWindowTextA(h,t,255); RECT r; GetClientRect(h,&r); if(l) printf("win %p class=%s title=%s %ldx%ld\n",h,c,t,r.right,r.bottom); if(r.right>=640 && (strstr(t,"Cube")||strstr(c,"Cube")||strstr(c,"Plasma"))) g=h; return TRUE; }
static void find(void){ g=0; EnumWindows(en,0); }
static void abs_move(int x,int y){ POINT p={x,y}; ClientToScreen(g,&p); int sw=GetSystemMetrics(SM_CXSCREEN), sh=GetSystemMetrics(SM_CYSCREEN); INPUT i={0}; i.type=INPUT_MOUSE; i.mi.dx=(p.x*65535)/(sw-1); i.mi.dy=(p.y*65535)/(sh-1); i.mi.dwFlags=MOUSEEVENTF_MOVE|MOUSEEVENTF_ABSOLUTE; SendInput(1,&i,sizeof i); SetCursorPos(p.x,p.y); }
static void btn(DWORD f){ INPUT i={0}; i.type=INPUT_MOUSE; i.mi.dwFlags=f; SendInput(1,&i,sizeof i); }
static void key(WORD vk,int up){ INPUT i={0}; i.type=INPUT_KEYBOARD; i.ki.wVk=vk; i.ki.wScan=MapVirtualKeyA(vk,0); if(up) i.ki.dwFlags=KEYEVENTF_KEYUP; SendInput(1,&i,sizeof i); }
static void shot(const char*path){ RECT r; GetClientRect(g,&r); int w=r.right,h=r.bottom; HDC d=GetDC(g), m=CreateCompatibleDC(d); HBITMAP b=CreateCompatibleBitmap(d,w,h); SelectObject(m,b); BitBlt(m,0,0,w,h,d,0,0,SRCCOPY); BITMAPINFOHEADER bi={0}; bi.biSize=sizeof bi; bi.biWidth=w; bi.biHeight=h; bi.biPlanes=1; bi.biBitCount=24; int st=(w*3+3)&~3; char*px=malloc(st*h); GetDIBits(m,b,0,h,px,(BITMAPINFO*)&bi,DIB_RGB_COLORS); FILE*f=fopen(path,"wb"); BITMAPFILEHEADER fh={0}; fh.bfType=0x4d42; fh.bfOffBits=sizeof fh+sizeof bi; fh.bfSize=fh.bfOffBits+st*h; fwrite(&fh,sizeof fh,1,f); fwrite(&bi,sizeof bi,1,f); fwrite(px,st*h,1,f); fclose(f); printf("Bild %dx%d\n",w,h); }
int main(int c,char**v){ setvbuf(stdout,0,_IONBF,0); if(c<2) return 2; if(!strcmp(v[1],"list")){ EnumWindows(en,1); return 0; }
 for(int i=0;i<60&&!g;i++){ find(); if(!g) Sleep(1000);} if(!g){ printf("kein Fenster\n"); return 1; }
 { BOOL a=SetForegroundWindow(g); HWND fg=GetForegroundWindow(); if(strcmp(v[1],"shot")) printf("vorn=%d fg=%p g=%p\n",a,fg,g); } SetFocus(g); Sleep(100);
 if(!strcmp(v[1],"shot")){ shot(v[2]); return 0; }
 if(!strcmp(v[1],"wait")){ printf("Fenster %p\n",g); return 0; }
 if(!strcmp(v[1],"click")){ int x=atoi(v[2]),y=atoi(v[3]); abs_move(x,y); Sleep(300); abs_move(x+1,y+1); Sleep(300); btn(MOUSEEVENTF_LEFTDOWN); Sleep(120); btn(MOUSEEVENTF_LEFTUP); Sleep(200); return 0; }
 if(!strcmp(v[1],"key")){ WORD k=(WORD)atoi(v[2]); key(k,0); Sleep(60); key(k,1); return 0; }
 if(!strcmp(v[1],"hold")){ WORD k=(WORD)atoi(v[2]); key(k,0); Sleep(atoi(v[3])); key(k,1); return 0; }
 if(!strcmp(v[1],"mouse")){ DWORD d=atoi(v[2])==1?MOUSEEVENTF_LEFTDOWN:MOUSEEVENTF_RIGHTDOWN,u=atoi(v[2])==1?MOUSEEVENTF_LEFTUP:MOUSEEVENTF_RIGHTUP; btn(d); Sleep(atoi(v[3])); btn(u); return 0; }
 if(!strcmp(v[1],"type")){ for(char*p=v[2];*p;p++){ PostMessageA(g,WM_CHAR,(unsigned char)*p,0); Sleep(100);} return 0; }
 return 2; }
