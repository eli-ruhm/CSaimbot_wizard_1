#include <iostream>
#include <Windows.h>
#include <TlHelp32.h>
#include "Offsets.h"

#define dwLocalPlayer 0xD30B94
#define dwEntityList 0x4D44A24
#define m_dwBoneMatrix 0x26A8
#define m_iTeamNum 0xF4
#define m_iHealth 0x100
#define m_vecOrigin 0x138
#define m_bDormant 0xED
//We get the resolution of our display, ex) 1920 by 1080
const int SCREEN_WIDTH = GetSystemMetrics(SM_CXSCREEN); const int xhairx = SCREEN_WIDTH / 2;
const int SCREEN_HEIGHT = GetSystemMetrics(SM_CYSCREEN); const int xhairy = SCREEN_HEIGHT / 2;
//Declare global variables so we can use them throughout code
HWND hwnd;
DWORD procId; //same as PID aka Process ID
HANDLE hProcess; //Our "tunnel" to communicate to CS:GO
uintptr_t moduleBase; //we will store the base of the panorama
HDC hdc;
int closest; //Used in a thread to save CPU usage.

uintptr_t GetModuleBaseAddress(const char* modName) {
	HANDLE hSnap = CreateToolhelp32Snapshot(TH32CS_SNAPMODULE | TH32CS_SNAPMODULE32, procId);
	if (hSnap != INVALID_HANDLE_VALUE) {
		MODULEENTRY32 modEntry;
		modEntry.dwSize = sizeof(modEntry);
		if (Module32First(hSnap, &modEntry)) {
			do {
				if (!strcmp(modEntry.szModule, modName)) {
					CloseHandle(hSnap);
					return (uintptr_t)modEntry.modBaseAddr;
				}
			} while (Module32Next(hSnap, &modEntry));
		}
	}
}
//Here is one to simplfy the reading memory process, its a template
template<typename T> T RPM(SIZE_T address) {
	T buffer;
	ReadProcessMemory(hProcess, (LPCVOID)address, &buffer, sizeof(T), NULL);
	return buffer;
}
//Here is a class for storing and creating Vector3s. A Simple way to store our enemy XYZ coordinates
class Vector3 {
public:
	float x, y, z;
	Vector3() : x(0.f), y(0.f), z(0.f) {}
	Vector3(float _x, float _y, float _z) : x(_x), y(_y), z(_z) {}
};

//Now lets code all of these functions to simplify our process!
int getTeam(uintptr_t player) { //the will get the team of a player, this is an "int" because the team is stored as an int ex:1,2
	return RPM<int>(player + m_iTeamNum);
}

uintptr_t GetLocalPlayer() { //This will get the address to localplayer aka US! this is a uintptr_t because it is good for storing addresses.
	return RPM< uintptr_t>(moduleBase + dwLocalPlayer); //we will define module base later in "int main"
}

uintptr_t GetPlayer(int index) {  //Each player has an index, we give the function an index AKA player and it returns their address
	return RPM< uintptr_t>(moduleBase + dwEntityList + index * 0x10); //We us index times 0x10 because the distance between each player
}

int GetPlayerHealth(uintptr_t player) { //we give this function a player and it returns to us what their health is, this is an int because health is stored as an int in csgo ex 100, 80 etc.
	return RPM<int>(player + m_iHealth);
}

Vector3 PlayerLocation(uintptr_t player) { //we make this a Vector3 because players location is XYZ, we give the function a player and it gives us their coordinates in return
	return RPM<Vector3>(player + m_vecOrigin);
}

bool DormantCheck(uintptr_t player) { //we give this function a player and it returns whether or not they are "real" or exit, this is so we dont aimbot "ghosts"
	return RPM<int>(player + m_bDormant);
}

Vector3 get_head(uintptr_t player) { //here we get the "head bone" of a player
	struct boneMatrix_t {
		byte pad3[12];
		float x; //we make a struct for the bone matrix XYZ and pad it
		byte pad1[12];
		float y;
		byte pad2[12];
		float z;
	};
	uintptr_t boneBase = RPM<uintptr_t>(player + m_dwBoneMatrix);
	boneMatrix_t boneMatrix = RPM<boneMatrix_t>(boneBase + (sizeof(boneMatrix) * 8 /*8 is the boneid for head*/));
	return Vector3(boneMatrix.x, boneMatrix.y, boneMatrix.z);
}

struct view_matrix_t { //making a struc for out view matrix to store the values
	float matrix[16];
} vm;
//here is out world screen function, this turns 3D coordinates (ex: XYZ) int 2D coordinates (ex: XY)
struct Vector3 WorldToScreen(const struct Vector3 pos, struct view_matrix_t matrix) { //This turns 3D coordinates (ex: XYZ) int 2D coordinates (ex: XY).
	struct Vector3 out;
	float _x = matrix.matrix[0] * pos.x + matrix.matrix[1] * pos.y + matrix.matrix[2] * pos.z + matrix.matrix[3];
	float _y = matrix.matrix[4] * pos.x + matrix.matrix[5] * pos.y + matrix.matrix[6] * pos.z + matrix.matrix[7];
	out.z = matrix.matrix[12] * pos.x + matrix.matrix[13] * pos.y + matrix.matrix[14] * pos.z + matrix.matrix[15];

	_x *= 1.f / out.z;
	_y *= 1.f / out.z;

	out.x = SCREEN_WIDTH * .5f; //this is why we needed our screen dimensions
	out.y = SCREEN_HEIGHT * .5f;

	out.x += 0.5f * _x * SCREEN_WIDTH + 0.5f;
	out.y -= 0.5f * _y * SCREEN_HEIGHT + 0.5f;

	return out;
}
 //we will need to use good old pythageroum theoum..... here is the equation in c++
float pythag(int x1, int y1, int x2, int y2) {
	return sqrt(pow(x2 - x1, 2) + pow(y2 - y1, 2));
}
//here is a function to find out closest enemy so the aimbot aims to the person closest to us
int FindClosestEnemy() {
	float Finish;
	int ClosestEntity = 1;
	Vector3 Calc = { 0, 0, 0 };
	float Closest = FLT_MAX;
	int localTeam = getTeam(GetLocalPlayer()); //here we start to actually use all those functions we created, it makes everything easier in the end!
	for (int i = 1; i < 64; i++) { //Loops through all the entitys, this is our "index" that we will use for our functions
		DWORD Entity = GetPlayer(i); //gets the address for each player using their number
		int EnmTeam = getTeam(Entity); if (EnmTeam == localTeam) continue; //a check
		int EnmHealth = GetPlayerHealth(Entity); if (EnmHealth < 1 || EnmHealth > 100) continue; // another check to make sure the player exsists aka is alive
		int Dormant = DormantCheck(Entity); if (Dormant) continue; //another check to make sure the player isnt dormant
		Vector3 headBone = WorldToScreen(get_head(Entity), vm); //getting the 3d coord of enemies head then making it 2d with world to screen
		Finish = pythag(headBone.x, headBone.y, xhairx, xhairy); //here we get the "path" to the enemies head, think of pythag and a triangle
		if (Finish < Closest) {
			Closest = Finish;
			ClosestEntity = i; //checking which one is the closest entity
		}
	}

	return ClosestEntity;
}
/* This is optional, but I used it during testing and you can too if you need to test or fix stuff, it uses GDI to draw a line to the enemies head instead of snapping the mouse, this is useful during testing, just comment out the set cursur part of the code to test.
*/
void DrawLine(float StartX, float StartY, float EndX, float EndY) { //This function is optional for debugging.
	int a, b = 0;
	HPEN hOPen;
	HPEN hNPen = CreatePen(PS_SOLID, 2, 0x0000FF /*red*/);// penstyle, width, color
	hOPen = (HPEN)SelectObject(hdc, hNPen);
	MoveToEx(hdc, StartX, StartY, NULL); //start of line
	a = LineTo(hdc, EndX, EndY); //end of line
	DeleteObject(SelectObject(hdc, hOPen));
} //pretty simple, draws a line

void FindClosestEnemyThread() { //here we make our thread code for finding the closest enemy
	while (1) {
		closest = FindClosestEnemy();
	}
}
 //Now we get to the int main where all the magic comes together!
int main() {
	hwnd = FindWindowA(NULL, "Counter-Strike: Global Offensive"); //makes a hwnd from the window name of CS:GO
	GetWindowThreadProcessId(hwnd, &procId); //gets the process ID of csgo from the hwnd
	moduleBase = GetModuleBaseAddress("client.dll"); //gets the process ID of csgo from the hwnd
	hProcess = OpenProcess(PROCESS_ALL_ACCESS, NULL, procId); //opens up a way to read/write, you can change it to just read since all we do is read memory, but it doesnt matter that much
	hdc = GetDC(hwnd); //for doing the draw line
	CreateThread(NULL, NULL, (LPTHREAD_START_ROUTINE)FindClosestEnemyThread, NULL, NULL, NULL); //starting the thread that gets the closest enemy

	while (!GetAsyncKeyState(VK_END)) { //press the "end" (kinda like esc key) to end the hack
		vm = RPM<view_matrix_t>(moduleBase + dwViewMatrix);
		Vector3 closestw2shead = WorldToScreen(get_head(GetPlayer(closest)), vm); //gets the head XYZ of the closest person
		DrawLine(xhairx, xhairy, closestw2shead.x, closestw2shead.y); //optinal for debugging
		
		//comment this out if you want to debug with DrawLine VVV
		if (GetAsyncKeyState(VK_MENU /*this is the alt key! press alt to use the aimbot*/) && closestw2shead.z >= 0.001f /*onscreen check*/)
			SetCursorPos(closestw2shead.x, closestw2shead.y); //NOTE you may need to change/turn off "raw input" in CSGO settings if it doesnt work
	}
	
}