#pragma comment(lib, "ws2_32")
#include<WinSock2.h>
#include"protocol.h"
#include<string.h>
#include<conio.h>

#include<iostream>
#include<cstdio>
#include<string>
#include<list>
#include<fstream>
#include<string>
#include<locale>
#include<thread>
#include<chrono>
using namespace std;

void error_display(char *, int, int);
void error_quit(wchar_t *, int);

void ConnectingServer();
void Disconnecting();

void InitingData();
void ProcessPacket(const Packet[]);

bool is_range(short, short);

void Draw();

// 최대 36자로 제한하자. 안그러면 cmd 창 화면이 깨질듯 str[36] = 아이디 제외 시, 한글 최대 16자 가능
list<wstring> chat_log;
struct Player {
	unsigned long long myNum;
	XY position;
	BYTE hp;
	BYTE exp;
	BYTE level;
};
struct Player player;

struct Other_Player {
	unsigned long long index;
	BYTE object_type;
	XY position;
};
list<Other_Player> other_player;
list<XY> mine;

SOCKET sock;
Packet recvBuf[MaxBufSize]{ 0 };

WorldMap worldmap[WIDTH][HEIGHT]{ 0 };

int main() {
	_wsetlocale(LC_ALL, L"korean");
	wcin.imbue(locale("korean"));
	wcout.imbue(locale("korean"));

	ConnectingServer();
	InitingData();

	Draw();

	int retval{ 0 };
	bool firstcheck{ true };
	while (true)
	{
		if (kbhit()) {
			int key = getch();
			if (ENTER == key) {
				if (firstcheck) {
					// 처음 엔터 입력시 버퍼 삭제용도
					char temp;
					scanf("%c", &temp);
					firstcheck = false;
				}
				// MESSEAGE_SEND_ALL
				printf("input Message : ");
				Packet chatBuf[MaxBufSize]{ 3, MESSEAGE_SEND_ALL, 0 };
				*((unsigned long long *)(&chatBuf[2])) = player.myNum;
				fgetws(reinterpret_cast<wchar_t *>(&chatBuf[10]), 18, stdin);
				chatBuf[0] = wcslen(reinterpret_cast<wchar_t *>(&chatBuf[10])) * 2 + 10;
				chatBuf[chatBuf[0] - 1] = chatBuf[chatBuf[0] - 2] = '\0';

				retval = send(sock, reinterpret_cast<char*>(&chatBuf), chatBuf[0], 0);
				if (retval == SOCKET_ERROR) {
					if (WSAGetLastError() != WSAEWOULDBLOCK) {
						int err_no = WSAGetLastError();
						error_quit(L"kbhit()::enter_input", err_no);
					}
				}
			}
			else if (UP == key || DOWN == key || LEFT == key || RIGHT == key || SPACEBAR == key) {
				Packet p[3];
				p[0] = 3;
				p[1] = CLIENT_KEY_INPUT;
				p[2] = key;

				if (SPACEBAR == key) {
					if (5 <= mine.size()) { mine.pop_front(); }
					mine.push_back(XY(player.position));
				}

				retval = send(sock, reinterpret_cast<char*>(&p), p[0], 0);
				if (retval == SOCKET_ERROR) {
					if (WSAGetLastError() != WSAEWOULDBLOCK) {
						int err_no = WSAGetLastError();
						error_quit(L"kbhit()::key_input", err_no);
					}
				}
			}
			else {

			}
		}

		retval = recv(sock, reinterpret_cast<char *>(&recvBuf), MaxBufSize, 0);
		if (retval == SOCKET_ERROR) {
			// 비동기 소켓이라 그냥 리턴, 검사 해주어야 함
			if (WSAGetLastError() != WSAEWOULDBLOCK) {
				int err_no = WSAGetLastError();
				error_quit(L"connect()", err_no);
			}
		}
		else {
			ProcessPacket(recvBuf);
		}
	}

	Disconnecting();
}

void error_display(char *msg, int err_no, int line) {
	WCHAR *lpMsgBuf;
	FormatMessage(FORMAT_MESSAGE_ALLOCATE_BUFFER | FORMAT_MESSAGE_FROM_SYSTEM, NULL, err_no,
		MAKELANGID(LANG_NEUTRAL, SUBLANG_DEFAULT), (LPTSTR)&lpMsgBuf, 0, NULL);
	printf("[ %s - %d ]", msg, line);
	wprintf(L"에러 %s\n", lpMsgBuf);
	LocalFree(lpMsgBuf);
}

void error_quit(wchar_t *msg, int err_no) {
	WCHAR *lpMsgBuf;
	FormatMessage(FORMAT_MESSAGE_ALLOCATE_BUFFER | FORMAT_MESSAGE_FROM_SYSTEM, NULL, err_no,
		MAKELANGID(LANG_NEUTRAL, SUBLANG_DEFAULT), (LPTSTR)&lpMsgBuf, 0, NULL);
	MessageBox(NULL, (LPCTSTR)lpMsgBuf, msg, MB_ICONERROR);
	LocalFree(lpMsgBuf);
	exit(-1);
}

void ConnectingServer() {
	int retval{ 0 };
	char serverIP[32];

	// 서버 주소 입력 받기
	printf("Example = 127.0.0.1\nInput Server IP : ");
	scanf("%s", serverIP);
	printf("\n\n*** Connecting Server, Please Wait ***\n");

	// 윈속
	WSADATA wsadata;
	if (WSAStartup(MAKEWORD(2, 2), &wsadata) != 0) {
		int err_no = WSAGetLastError();
		error_quit(L"WSAStartup ERROR", err_no);
	}

	// 소켓 생성
	sock = socket(AF_INET, SOCK_STREAM, 0);
	if (INVALID_SOCKET == sock) {
		int err_no = WSAGetLastError();
		error_quit(L"socket()", err_no);
	}

	// WSAAsyncSelect - 넌블로킹 소켓 자동 전환
	/*HWND hWnd = GetConsoleHwnd();
	if (NULL == hWnd) {
	printf("Cannot find Consol Window, ERROR : %d\n", __LINE__);
	exit(-1);
	}

	retval = WSAAsyncSelect(sock, hWnd, WM_USER + 1, FD_READ | FD_CLOSE);
	if (SOCKET_ERROR == retval) {
	int err_no = WSAGetLastError();
	error_quit(L"ioctlsocket()", err_no);
	}*/

	// 넌블로킹 소켓 전환
	DWORD on = 1;
	retval = ioctlsocket(sock, FIONBIO, &on);
	if (SOCKET_ERROR == retval) {
		int err_no = WSAGetLastError();
		error_quit(L"ioctlsocket()", err_no);
	}

	// connect
	SOCKADDR_IN serveraddr;
	ZeroMemory(&serveraddr, sizeof(serveraddr));
	serveraddr.sin_family = AF_INET;
	serveraddr.sin_addr.s_addr = inet_addr(serverIP);
	serveraddr.sin_port = htons(SERVERPORT);
	retval = connect(sock, (SOCKADDR *)&serveraddr, sizeof(serveraddr));
	if (retval == SOCKET_ERROR) {
		// 비동기 소켓이라 그냥 리턴, 검사 해주어야 함
		if (WSAGetLastError() != WSAEWOULDBLOCK) {
			int err_no = WSAGetLastError();
			error_quit(L"connect()", err_no);
		}
	}
}

void Disconnecting() {
	closesocket(sock);
	WSACleanup();
}

void InitingData() {
	// 기본 정보를 받아온다
setInit1:
	int retval = recv(sock, reinterpret_cast<char *>(&recvBuf), 18, 0);
	if (retval == SOCKET_ERROR) {
		if (WSAGetLastError() != WSAEWOULDBLOCK) {
			int err_no = WSAGetLastError();
			error_quit(L"connect()", err_no);
		}
		goto setInit1;
	}

	player.myNum = *((unsigned long long*)(&recvBuf[2]));
	player.position.x = *((short*)(&recvBuf[10]));
	player.position.y = *((short*)(&recvBuf[12]));
	player.position.direction = recvBuf[14];
	player.hp = recvBuf[15];
	player.exp = recvBuf[16];
	player.level = recvBuf[17];

	// 전체 맵 셋팅
	fstream in("worldmap.txt");
	for (int j = 0; j < HEIGHT; ++j) {
		for (int i = 0; i < WIDTH; ++i) {
			in >> worldmap[j][i];
			worldmap[j][i] -= 48;
		}
	}
}

void ProcessPacket(const Packet buf[]) {
	if (DISCONNECTED == buf[1]) {
		if (chat_log.size() > MAX_CHAT_LOG_SIZE) { chat_log.pop_front(); }
		wstring player_index = to_wstring(*((unsigned long long*)(&buf[2])));
		wstring one{ L"[" };
		one = one + player_index + L"] 플레이어가 퇴장하였습니다.";
		chat_log.push_back(one);

		unsigned long long tempIndex = *((unsigned long long*)(&buf[2]));
		auto p = other_player.begin();
		for (auto d : other_player) {
			if (d.index == *((unsigned long long*)(&buf[2]))) { break; }
			++p;
		}
		if (p != other_player.end()) {
			other_player.erase(p);
		}

		Draw();
	}
	else if (CLIENT_INIT == buf[1]) {
		if (chat_log.size() > MAX_CHAT_LOG_SIZE) { chat_log.pop_front(); }
		wstring player_index = to_wstring(*((unsigned long long*)(&buf[2])));
		wstring one{ L"[" };
		one = one + player_index + L"] 플레이어가 입장하였습니다.";
		chat_log.push_back(one);

		if (true == is_range(*((short*)(&buf[10])), *((short*)(&buf[12])))) {
			struct Other_Player temp;
			temp.index = *((unsigned long long*)(&buf[2]));
			temp.object_type = PLAYER_OTHER;
			temp.position.x = *((short*)(&buf[10]));
			temp.position.y = *((short*)(&buf[12]));
			temp.position.direction = buf[14];
			other_player.push_back(temp);
		}

		Draw();
	}
	else if (MESSEAGE_SEND_ALL == buf[1]) {
		if (chat_log.size() > MAX_CHAT_LOG_SIZE) { chat_log.pop_front(); }
		wstring player_index = to_wstring(*((unsigned long long*)(&buf[2])));
		wstring one;
		TCHAR temp[MaxBufSize]{ 0 };
		memcpy(temp, &recvBuf[10], recvBuf[0] - 10);
		one = one + player_index + L" : " + temp;
		chat_log.push_back(one);

		Draw();
	}
	else if (CLIENT_KEY_INPUT == buf[1]) {
		if (player.myNum == *((unsigned long long*)(&buf[2]))) {
			// 1. 이동 및 키를 입력한 사람이 나 일 경우
			player.position.x = *((short*)(&buf[10]));
			player.position.y = *((short*)(&buf[12]));
			player.position.direction = buf[14];

			if (player.hp != buf[16]) {
				if (chat_log.size() > MAX_CHAT_LOG_SIZE) { chat_log.pop_front(); }
				wstring hpdown = L"-20 피해를 입었습니다.";
				chat_log.push_back(hpdown);
				player.hp = buf[16];
				printf("\a");

				for (auto p = mine.begin(); p != mine.end(); ++p) { if ((p->x == player.position.x) && (p->y == player.position.y)) { mine.erase(p); } }
				if (0 >= player.hp) {
					system("cls");
					printf("\n\tGame Over. You Died.\n\tWait For a Respon Time\n\n");
					char revive[2];
					revive[0] = 2;
					revive[1] = DIE;
					int retval = send(sock, revive, revive[0], 0);
					if (retval == SOCKET_ERROR) {
						if (WSAGetLastError() != WSAEWOULDBLOCK) {
							int err_no = WSAGetLastError();
							error_quit(L"ProcessPacket()::CLIENT_KEY_INPUT::DIE", err_no);
						}
					}
					Packet temp[12]{ 0 };
					while (true)
					{
						retval = recv(sock, reinterpret_cast<char *>(&temp), 12, 0);
						if (retval == SOCKET_ERROR) {
							// 비동기 소켓이라 그냥 리턴, 검사 해주어야 함
							if (WSAGetLastError() != WSAEWOULDBLOCK) {
								int err_no = WSAGetLastError();
								error_quit(L"connect()", err_no);
							}
						}
						else {
							break;
						}
					}

					short revive_time = *((short*)(&temp[10]));
					auto start = chrono::high_resolution_clock::now();

					//for (list<Other_Player>::iterator ptr = other_player.begin(); ptr != other_player.end(); ++ptr) { other_player.erase(ptr); }

					if (chat_log.size() > MAX_CHAT_LOG_SIZE) { chat_log.pop_front(); }
					wstring dead = L"캐릭터가 사망하였습니다.";
					chat_log.push_back(dead);

					player.position.x = *((short*)(&temp[2]));
					player.position.y = *((short*)(&temp[4]));
					player.position.direction = temp[6];
					player.hp = temp[7];
					player.exp = temp[8];
					player.level = temp[9];

					while (true)
					{
						auto ended = chrono::high_resolution_clock::now() - start;
						if (chrono::duration_cast<chrono::milliseconds>(ended).count() > revive_time) {
							break;
						}
					}
					Draw();
					return;
				}
			}
			if (player.exp != buf[17]) {
				if (chat_log.size() > MAX_CHAT_LOG_SIZE) { chat_log.pop_front(); }
				wstring expup = L"+20 경헙치를 얻었습니다.";
				chat_log.push_back(expup);
				player.exp = buf[17];
				printf("\a");
			}
			if (player.level != buf[18]) {
				if (chat_log.size() > MAX_CHAT_LOG_SIZE) { chat_log.pop_front(); }
				wstring lvup = L"+1 레벨이 올랐습니다.";
				chat_log.push_back(lvup);
				player.level = buf[18];
				printf("\a");
			}
			Draw();
		}
		else {
			// 2. 이동 및 키를 입력한 사람이 다른 유저일 경우 ( true = 추가 또는 유지, false = 현재 플레이어 제거 )
			// *********** 동시에 두 개 이상의 패킷이 날아올 경우, 클라이언트에서 받을 수 없다...
			if (true == buf[15]) {
				bool player_exist{ false };
				for (auto& d : other_player) {
					if (d.index == *((unsigned long long*)(&buf[2]))) {
						player_exist = true;
						d.position.x = *((short*)(&buf[10]));
						d.position.y = *((short*)(&buf[12]));
						d.position.direction = buf[14];
						break;
					}
				}
				if (false == player_exist) {
					struct Other_Player temp;
					temp.index = *((unsigned long long*)(&buf[2]));

					if (MONSTER == buf[14]) { temp.object_type = MONSTER; }
					else{ temp.object_type = PLAYER_OTHER; }

					temp.position.x = *((short*)(&buf[10]));
					temp.position.y = *((short*)(&buf[12]));
					temp.position.direction = buf[14];
					other_player.push_back(temp);
				}
			}
			else if (false == buf[15]) {
				auto p = other_player.begin();
				for (auto d : other_player) {
					if (d.index == *((unsigned long long*)(&buf[2]))) { break; }
					++p;
				}
				if (p != other_player.end()) {
					other_player.erase(p);
				}
			}
			Draw();
		}
	}
	else if (BOMB_EXPLORED == buf[1]) {
		for (auto p = mine.begin(); p != mine.end(); ++p) {
			if ((p->x == *((short*)(&buf[2]))) && (p->y == *((short*)(&buf[4])))) {
				mine.erase(p);
				break;
			}
		}
	}
	else {
		printf("ERROR, Unknown signal -> protocol num = %d\n", buf[1]);
		exit(-1);
	}
}

void Draw() {
	if (0 != other_player.size()) {
		for (auto p = other_player.begin(); p != other_player.end();) {
			if (false == is_range(p->position.x, p->position.y)) {
				other_player.erase(p++);
			}
			else { ++p; }
		}
	}

	WorldMap display[21][21] = { 0 };
	int xIndex, yIndex;
	bool xView, yView;

	if (10 > player.position.x) { xIndex = 0; xView = true; }
	else if ((WIDTH - 11) < player.position.x) { xIndex = (WIDTH - 21); xView = true; }
	else { xIndex = player.position.x - 10; xView = false; }

	if (10 > player.position.y) { yIndex = 0; yView = true; }
	else if ((HEIGHT - 11) < player.position.y) { yIndex = (HEIGHT - 21); yView = true; }
	else { yIndex = player.position.y - 10; yView = false; }

	for (int j = 0; j < 21; ++j) {
		for (int i = 0; i < 21; ++i) {
			display[j][i] = worldmap[yIndex + j][xIndex + i];
		}
	}

	short state{ 0 };
	// 플레이어 좌표
	if (xView && yView) {
		if (0 == xIndex) { xIndex = player.position.x; }
		else { xIndex = 21 - (WIDTH - player.position.x); }
		if (0 == yIndex) { yIndex = player.position.y; }
		else { yIndex = 21 - (HEIGHT - player.position.y); }

		if (UP == player.position.direction) { display[yIndex][xIndex] = TILE_PLAYER_UP; }
		else if (DOWN == player.position.direction) { display[yIndex][xIndex] = TILE_PLAYER_DOWN; }
		else if (LEFT == player.position.direction) { display[yIndex][xIndex] = TILE_PLAYER_LEFT; }
		else if (RIGHT == player.position.direction) { display[yIndex][xIndex] = TILE_PLAYER_RIGHT; }
	}
	else if (xView && !yView) {
		state = 1;
		if (0 == xIndex) { xIndex = player.position.x; }
		else { xIndex = 21 - (WIDTH - player.position.x); }

		if (UP == player.position.direction) { display[10][xIndex] = TILE_PLAYER_UP; }
		else if (DOWN == player.position.direction) { display[10][xIndex] = TILE_PLAYER_DOWN; }
		else if (LEFT == player.position.direction) { display[10][xIndex] = TILE_PLAYER_LEFT; }
		else if (RIGHT == player.position.direction) { display[10][xIndex] = TILE_PLAYER_RIGHT; }
	}
	else if (!xView && yView) {
		state = 2;
		if (0 == yIndex) { yIndex = player.position.y; }
		else { yIndex = 21 - (HEIGHT - player.position.y); }

		if (UP == player.position.direction) { display[yIndex][10] = TILE_PLAYER_UP; }
		else if (DOWN == player.position.direction) { display[yIndex][10] = TILE_PLAYER_DOWN; }
		else if (LEFT == player.position.direction) { display[yIndex][10] = TILE_PLAYER_LEFT; }
		else if (RIGHT == player.position.direction) { display[yIndex][10] = TILE_PLAYER_RIGHT; }
	}
	else {
		state = 3;
		if (UP == player.position.direction) { display[10][10] = TILE_PLAYER_UP; }
		else if (DOWN == player.position.direction) { display[10][10] = TILE_PLAYER_DOWN; }
		else if (LEFT == player.position.direction) { display[10][10] = TILE_PLAYER_LEFT; }
		else if (RIGHT == player.position.direction) { display[10][10] = TILE_PLAYER_RIGHT; }
	}

	if (0 == state) {}
	else if (1 == state) { yIndex = 10; }
	else if (2 == state) { xIndex = 10; }
	else if (3 == state) { yIndex = 10; xIndex = 10; }

	// 다른 유저 좌표
	for (auto d : other_player) {
		// display[ yIndex + tempY ][ xIndex + tempX ];
		short tempX = d.position.x - player.position.x;
		short tempY = d.position.y - player.position.y;

		if (PLAYER_OTHER == d.object_type) {
			if (UP == d.position.direction) { display[yIndex + tempY][xIndex + tempX] = TILE_OTHER_PLAYER_UP; }
			else if (DOWN == d.position.direction) { display[yIndex + tempY][xIndex + tempX] = TILE_OTHER_PLAYER_DOWN; }
			else if (LEFT == d.position.direction) { display[yIndex + tempY][xIndex + tempX] = TILE_OTHER_PLAYER_LEFT; }
			else if (RIGHT == d.position.direction) { display[yIndex + tempY][xIndex + tempX] = TILE_OTHER_PLAYER_RIGHT; }
		}
		else if (MONSTER == d.object_type) {
			display[yIndex + tempY][xIndex + tempX] = MONSTER;
		}
		else {

		}
	}

	// 지뢰 좌표
	for (auto d : mine) {
		short tempX = d.x - player.position.x + xIndex;
		short tempY = d.y - player.position.y + yIndex;
		if (0 <= (tempY) && (tempY) <= 20 && 0 <= (tempX) && (tempX) <= 20) { display[tempY][tempX] = TILE_BOMB; }
	}

	system("cls");
	list<wstring>::iterator chat_log_ptr = chat_log.begin();
	{
		printf("H P [");
		int per_hp{ player.hp / 10 };
		for (int i = 0; i < per_hp; ++i) { printf("♥"); }
		for (int i = 0; i < 10 - per_hp; ++i) { printf("♡"); }
		printf("] LV. %d\t   ", player.level);
		if (chat_log_ptr != chat_log.end()) {
			wcout << *chat_log_ptr;
			++chat_log_ptr;
		}
		printf("\n");
	}
	{
		printf("EXP [");
		int per_exp{ player.exp / 10 };
		for (int i = 0; i < per_exp; ++i) { printf("▣"); }
		for (int i = 0; i < 10 - per_exp; ++i) { printf("〓"); }
		printf("]\t\t   ");
		if (chat_log_ptr != chat_log.end()) {
			wcout << *chat_log_ptr;
			++chat_log_ptr;
		}
		printf("\n");
	}
	{
		printf("X : %d\tY : %d\t", player.position.x, player.position.y);
		printf("\t\t\t   ");
		if (chat_log_ptr != chat_log.end()) {
			wcout << *chat_log_ptr;
			++chat_log_ptr;
		}
		printf("\n");
	}

	for (int i = 0; i < 21; ++i) {
		for (int j = 0; j < 21; ++j) {
			if (TILE_EMPTY == display[i][j]) { printf("  "); }
			else if (TILE_BLOCK == display[i][j]) {	printf("■"); }
			else if (TILE_PLAYER_DOWN == display[i][j]) { printf("▼");	}
			else if (TILE_PLAYER_UP == display[i][j]) {	printf("▲"); }
			else if (TILE_PLAYER_LEFT == display[i][j]) { printf("◀");	}
			else if (TILE_PLAYER_RIGHT == display[i][j]) { printf("▶"); }
			else if (TILE_OTHER_PLAYER_DOWN == display[i][j]) {	printf("▽"); }
			else if (TILE_OTHER_PLAYER_UP == display[i][j]) { printf("△"); }
			else if (TILE_OTHER_PLAYER_LEFT == display[i][j]) {	printf("◁"); }
			else if (TILE_OTHER_PLAYER_RIGHT == display[i][j]) { printf("▷"); }
			else if (TILE_BOMB == display[i][j]) { printf("†"); }
			else if (MONSTER == display[i][j]) { printf("◎"); }
			else {}
		}
		printf(" ");
		if (chat_log_ptr != chat_log.end()) {
			wcout << *chat_log_ptr;
			++chat_log_ptr;
		}
		printf("\n");
	}
}

bool is_range(short x, short y) {
	double x_range(player.position.x - x);
	double y_range(player.position.y - y);
	x_range *= x_range;
	y_range *= y_range;
	return (VIEW_RANGE * VIEW_RANGE >= (x_range + y_range));
}