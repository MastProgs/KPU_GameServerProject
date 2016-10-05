#pragma comment(lib, "ws2_32")
#include<WinSock2.h>
#include"protocol.h"
#include<stdlib.h>
#include<time.h>

#include<iostream>
#include<cstdio>
#include<vector>
#include<thread>
#include<fstream>
#include<unordered_set>
#include<mutex>
#include<list>
#include<queue>

extern "C" {
#include "lua.h"
#include "lauxlib.h"
#include "lualib.h"
}
using namespace std;

void GetServerIpAddress();
void ServerClose();
void error_display(char *, int, int);
void error_quit(wchar_t *, int);
int checkCpuCore();

void Initialize();
void Init_worldmap();
void make_worldmap();
void workerThreads();
void acceptThread();

void ProcessPacket(unsigned long long, const Packet[]);
void SendPacket(unsigned long long, Packet *);
bool is_range(unsigned long long, unsigned long long);
void addView(unsigned long long, unsigned long long);
void removeView(unsigned long long, unsigned long long);
void Do_Move(unsigned long long);
void heart_beat(unsigned long long);
void AI_Thread_Start();
void Timer_Thread_Start();

const bool Is_NPC(const unsigned long long);
const bool Is_Active(const unsigned long long);

bool ServerShutdown{ false };
HANDLE g_hIocp;
unsigned long long playerIndex{ _UI64_MAX };
 
using Ovlp_ex = struct Overlap_ex {
	OVERLAPPED original_overlap;
	int operation;
	WSABUF wsabuf;
	Packet iocp_buffer[MaxBufSize];
};

using View_list = struct viewlist {
	mutex lock;
	unordered_set<unsigned long long> view_list;
};

using XYnum = struct Mines {
	//mutex lock;
	XY pos;
	unsigned long long id;
};
mutex minelock;
list<XYnum> all_mines;
list<XYnum>::iterator bomb_explosive_find(const XY&);

struct event_type {
	unsigned long long obj_id;
	unsigned int wakeup_time;
	int event_id;
};

class mycomp
{
public:
	bool operator() (const event_type lhs, const event_type rhs) const
	{
		return (lhs.wakeup_time > rhs.wakeup_time);
	}
};

priority_queue<event_type, vector<event_type>, mycomp> timer_queue;
mutex timer_lock;

using clinfo = struct Client_INFO {
	SOCKET s;
	unsigned long long id;
	bool connected;
	Ovlp_ex recv_overlap;
	int packet_size;
	int previous_size;
	Packet packet_buff[MaxBufSize];
	XY position;
	BYTE level;
	BYTE hp;
	BYTE exp;
	View_list view;
	list<XY> mine;
	bool is_active;
	unsigned int last_move_time;
};

WorldMap worldmap[WIDTH][HEIGHT] = { 0 };
clinfo clients[NUM_OF_NPC];
unsigned long long ranking[7]{ 0 };

int main(void) {
	_wsetlocale(LC_ALL, L"korean");
	GetServerIpAddress();
	Initialize();
	int cpuCore = checkCpuCore();

	vector<thread *> worker_threads;
	worker_threads.reserve(cpuCore);

	for (int i = 0; i < (cpuCore - 1); ++i) { worker_threads.push_back(new thread{ workerThreads }); }
	
	thread accept_thread{ acceptThread };
	thread timer_thread{ Timer_Thread_Start };

	while (ServerShutdown) { Sleep(1000); }

	for (auto thread : worker_threads) {
		thread->join();
		delete thread;
	}

	accept_thread.join();
	ServerClose();
}

void GetServerIpAddress() {
	WSADATA wsaData;
	WSAStartup(MAKEWORD(2, 2), &wsaData);

	PHOSTENT hostinfo;
	char hostname[50];
	char ipaddr[50];
	memset(hostname, 0, sizeof(hostname));
	memset(ipaddr, 0, sizeof(ipaddr));

	int nError = gethostname(hostname, sizeof(hostname));
	if (nError == 0)
	{
		hostinfo = gethostbyname(hostname);
		strcpy(ipaddr, inet_ntoa(*reinterpret_cast<struct in_addr*>(hostinfo->h_addr_list[0])));
	}
	WSACleanup();
	printf("This Server's IP address : %s\n", ipaddr);
}

void ServerClose() {
	WSACleanup();
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

int checkCpuCore() {
	SYSTEM_INFO si;
	GetSystemInfo(&si);
	int cpuCore = static_cast<int>(si.dwNumberOfProcessors) * 2;
	printf("CPU Core Count = %d, threads = %d\n", cpuCore / 2, cpuCore);
	return cpuCore;
}

void Initialize() {
	// 전체 플레이어 연결 끊김 설정
	for (int i = 0; i < MAX_USER; ++i) {
		clients[i].connected = false;
	}
	// AI 좌표 위치 설정
	for (int i = NPC_START; i < NUM_OF_NPC; ++i) {
		clients[i].connected = true;
		clients[i].hp = 1;
		do {
			clients[i].position.x = rand() % 300;
			clients[i].position.y = rand() % 300;
		} while (TILE_EMPTY != worldmap[clients[i].position.y][clients[i].position.x]);
	
	}

	// 전체 맵 설정 불러오기
	//make_worldmap(); // 새로 맵을 만들어야 할 경우 쓰는 함수
	Init_worldmap();

	WSADATA wsadata;
	WSAStartup(MAKEWORD(2, 2), &wsadata);

	g_hIocp = CreateIoCompletionPort(INVALID_HANDLE_VALUE, NULL, NULL, 0);
	if (g_hIocp == NULL) {
		int err_no = WSAGetLastError();
		error_quit(L"Initialize::CreateIoCompletionPort", err_no);
	}
}

void make_worldmap() {
	fstream out("worldmap.txt");
	for (int j = 0; j < HEIGHT; ++j) {
		if (j == 0 || j == HEIGHT - 1) {
			for (int i = 0; i < WIDTH; ++i) { out << TILE_BLOCK << ' ';	}
		}
		else {
			for (int i = 0; i < WIDTH; ++i) {
				if (i == 0 || i == WIDTH - 1) {	out << TILE_BLOCK << ' '; }
				else {
					int chance = rand() % 10;
					if (chance < 2) {
						out << TILE_BLOCK << ' ';
					}
					else {
						out << TILE_EMPTY << ' ';
					}
				}
			}
		}
		out << endl;
	}	
}

void Init_worldmap() {
	fstream in("worldmap.txt");
	for (int j = 0; j < HEIGHT; ++j) {
		for (int i = 0; i < WIDTH; ++i) {
			in >> worldmap[j][i];
			worldmap[j][i] -= 48;
		}
	}
}

void workerThreads() {
	while (!ServerShutdown) {
		DWORD key;
		DWORD iosize;
		Ovlp_ex *my_overlap;

		BOOL result = GetQueuedCompletionStatus(g_hIocp, &iosize, &key, reinterpret_cast<LPOVERLAPPED *>(&my_overlap), INFINITE);

		if (FALSE == result || 0 == iosize) {
			if (FALSE == result) {
				int err_no = WSAGetLastError();
				error_display("WorkerThreadStart::GetQueuedCompletionStatus", err_no, __LINE__);
			}

			closesocket(clients[key].s);
			clients[key].connected = false;

			/*
				view list 에서 빼주자
			*/

			Packet p[11];
			p[0] = 11;
			p[1] = DISCONNECTED;
			*((unsigned long long *)(&p[2])) = key;

			for (int i = 0; i < MAX_USER; ++i) {
				if (false == clients[i].connected) { continue; }
				//if (i == playerIndex) { continue; }

				SendPacket(i, p);
			}
			continue;
		}
		else if (OP_SERVER_RECV == my_overlap->operation) {
			Packet *buf_ptr = clients[key].recv_overlap.iocp_buffer;
			int remained = iosize;
			while (0 < remained) {
				if (0 == clients[key].packet_size) { clients[key].packet_size = buf_ptr[0]; }

				int required = clients[key].packet_size - clients[key].previous_size;

				if (remained >= required) {
					memcpy(clients[key].packet_buff + clients[key].previous_size, buf_ptr, required);

					ProcessPacket(key, clients[key].packet_buff);

					buf_ptr += required;
					remained -= required;

					clients[key].packet_size = 0;
					clients[key].previous_size = 0;
				}
				else {
					memcpy(clients[key].packet_buff + clients[key].previous_size, buf_ptr, remained);
					buf_ptr += remained;
					clients[key].previous_size += remained;
					remained = 0;
				}
			}
			DWORD flags = 0;
			int retval = WSARecv(clients[key].s, &clients[key].recv_overlap.wsabuf, 1, NULL, &flags, &clients[key].recv_overlap.original_overlap, NULL);
			if (SOCKET_ERROR == retval) {
				int err_no = WSAGetLastError();
				if (ERROR_IO_PENDING != err_no) {
					error_display("WorkerThreadStart::WSARecv", err_no, __LINE__);
				}
				continue;
			}
		}
		else if (OP_SERVER_SEND == my_overlap->operation) {
			delete my_overlap;
		}
		else if (OP_AI_MOVE == my_overlap->operation) {
			Do_Move(key);
			delete my_overlap;
		}
		else {
			cout << "Unknown IOCP event !!\n";
			exit(-1);
		}
	}
}

void acceptThread() {
	int retval{ 0 };

	// socket() - IPv4 ( AF_INET )
	SOCKET listen_sock = WSASocket(AF_INET, SOCK_STREAM, IPPROTO_TCP, NULL, 0, WSA_FLAG_OVERLAPPED);
	if (listen_sock == INVALID_SOCKET) {
		int err_no = WSAGetLastError();
		error_quit(L"socket()", err_no);
	};

	// bind()
	struct sockaddr_in serveraddr;
	ZeroMemory(&serveraddr, sizeof(serveraddr));
	serveraddr.sin_family = AF_INET;
	serveraddr.sin_addr.s_addr = htonl(INADDR_ANY);
	serveraddr.sin_port = htons(SERVERPORT);
	retval = ::bind(listen_sock, reinterpret_cast<struct sockaddr *>(&serveraddr), sizeof(serveraddr));
	if (retval == SOCKET_ERROR) {
		int err_no = WSAGetLastError();
		error_quit(L"socket()", err_no);
	}

	// listen()
	retval = listen(listen_sock, SOMAXCONN);
	if (retval == SOCKET_ERROR) {
		int err_no = WSAGetLastError();
		error_quit(L"socket()", err_no);
	}

	Packet p[18] = { 0 };
	p[0] = 18;

	while (!ServerShutdown) {
		// accept()
		struct sockaddr_in clientaddr;
		int addrlen = sizeof(clientaddr);
		SOCKET client_sock = WSAAccept(listen_sock, reinterpret_cast<sockaddr *>(&clientaddr), &addrlen, NULL, NULL);
		if (INVALID_SOCKET == client_sock) {
			int err_no = WSAGetLastError();
			error_display("Accept::WSAAccept", err_no, __LINE__);
			while (true);
		}

		/*
			DB 관련 login 기능이 여기에 추가되어야 한다
		*/

		playerIndex += 1;
		printf("[ No. %lld ] Client IP = %s, Port = %d is Connected\n", playerIndex, inet_ntoa(clientaddr.sin_addr), ntohs(clientaddr.sin_port));

		CreateIoCompletionPort(reinterpret_cast<HANDLE>(client_sock), g_hIocp, playerIndex, 0);

		clients[playerIndex].s = client_sock;
		clients[playerIndex].connected = true;
		clients[playerIndex].id = playerIndex;
		clients[playerIndex].packet_size = 0;
		clients[playerIndex].previous_size = 0;
		memset(&clients[playerIndex].recv_overlap.original_overlap, 0, sizeof(clients[playerIndex].recv_overlap.original_overlap));
		clients[playerIndex].recv_overlap.operation = OP_SERVER_RECV;
		clients[playerIndex].recv_overlap.wsabuf.buf = reinterpret_cast<char*>(&clients[playerIndex].recv_overlap.iocp_buffer);
		clients[playerIndex].recv_overlap.wsabuf.len = sizeof(clients[playerIndex].recv_overlap.iocp_buffer);

		srand(time(NULL));
		do {
			clients[playerIndex].position.x = rand() % 300;
			clients[playerIndex].position.y = rand() % 300;
		} while (TILE_EMPTY != worldmap[clients[playerIndex].position.y][clients[playerIndex].position.x]);
		clients[playerIndex].position.direction = UP;
		clients[playerIndex].hp = 100;
		clients[playerIndex].exp = 0;
		clients[playerIndex].level = 1;
		clients[playerIndex].view.lock.lock();
		clients[playerIndex].view.view_list.clear();
		clients[playerIndex].view.lock.unlock();

		// 기본 정보 및 데이터 전송, size, type, id (playerIndex), x, y, direction
		p[1] = CLIENT_INIT;
		*((unsigned long long*)(&p[2])) = playerIndex;
		*((short*)(&p[10])) = clients[playerIndex].position.x;
		*((short*)(&p[12])) = clients[playerIndex].position.y;
		p[14] = clients[playerIndex].position.direction;
		p[15] = clients[playerIndex].hp;
		p[16] = clients[playerIndex].exp;
		p[17] = clients[playerIndex].level;
		SendPacket(playerIndex, p);

		// 근처 플레이어에게 새로운 플레이어의 좌표를 전송
		for (unsigned long long i = 0; i < MAX_USER; ++i) {
			if (false == clients[i].connected) { continue; }
			if (i == playerIndex) { continue; }
			if (false == is_range(playerIndex, i)) { continue; }
			//addView(playerIndex, i);
			clients[i].view.lock.lock();
			clients[i].view.view_list.insert(playerIndex);
			clients[i].view.lock.unlock();

			SendPacket(i, p);
		}
		// 방금 접속한 플레이어에게 근처 플레이어의 좌표를 전송
		for (unsigned long long i = 0; i < MAX_USER; ++i) {
			if (false == clients[i].connected) { continue; }
			if (i == playerIndex) { continue; }
			if (false == is_range(playerIndex, i)) { continue; }

			clients[playerIndex].view.lock.lock();
			clients[playerIndex].view.view_list.insert(i);
			clients[playerIndex].view.lock.unlock();

			p[1] = CLIENT_KEY_INPUT;
			*((unsigned long long*)(&p[2])) = i;
			*((short*)(&p[10])) = clients[i].position.x;
			*((short*)(&p[12])) = clients[i].position.y;
			p[14] = clients[i].position.direction;
			p[15] = true;

			SendPacket(playerIndex, p);
		}

		// 맵 전송 - 기본 맵 및 장애물
		/*p[0] = 42;
		p[1] = REFRESH_MAP;*/

		// 클라이언트에서 응답오길 기다리기
		DWORD flags{ 0 };
		retval = WSARecv(client_sock, &clients[playerIndex].recv_overlap.wsabuf, 1, NULL, &flags, &clients[playerIndex].recv_overlap.original_overlap, NULL);
		if (SOCKET_ERROR == retval) {
			int err_no = WSAGetLastError();
			if (ERROR_IO_PENDING != err_no) {
				error_display("Accept::WSARecv", err_no, __LINE__);
			}
		}
	}
}

void ProcessPacket(unsigned long long id, const Packet buf[]) {
	// packet[0] = packet size
	// packet[1] = type
	// packet[...] = data

	if (MESSEAGE_SEND_ALL == buf[1]) {
		TCHAR temp[MaxBufSize]{ 0 };
		memcpy(temp, &buf[10], buf[0] - 10);
		printf("%lld : ", id);
		printf("%ls\n", temp);
		Packet tmp[MaxBufSize]{ 0 };
		memcpy(reinterpret_cast<Packet*>(&tmp), buf, buf[0]);

		for (int i = 0; i < MAX_USER; ++i) {
			if (false == clients[i].connected) { continue; }
			//if (i == id) { continue; }

			SendPacket(i, tmp);
		}
	}
	else if (DIE == buf[1]) {
		Packet revive[12]{ 0 };

		revive[0] = 12;
		revive[1] = DIE;

		do {
			clients[id].position.x = rand() % 300;
			clients[id].position.y = rand() % 300;
		} while (TILE_EMPTY != worldmap[clients[id].position.y][clients[id].position.x]);
		revive[6] = clients[id].position.direction = UP;
		revive[7] = clients[id].hp = 100;
		revive[8] = clients[id].exp = 0;
		revive[9] = clients[id].level = 1;
		clients[id].view.lock.lock();
		clients[id].view.view_list.clear();
		clients[id].view.lock.unlock();

		*((short*)(&revive[2])) = clients[id].position.x;
		*((short*)(&revive[4])) = clients[id].position.y;
		// 부활 시간 초
		*((short*)(&revive[10])) = 5000;
		
		SendPacket(id, revive);

		// 지뢰 지우기 - 일단 생략
		// view_list 지우기
		vector <int> remove_list;
		clients[id].view.lock.lock();
		for (auto i : clients[id].view.view_list) { remove_list.push_back(i); }
		for (auto i : remove_list) { clients[id].view.view_list.erase(i); }
		clients[id].view.lock.unlock();
		
		Packet p[16]{ 0 };
		p[0] = 16;
		p[1] = CLIENT_KEY_INPUT;
		*((short*)(&p[10])) = 0;
		*((short*)(&p[12])) = 0;
		p[15] = false;

		for (auto i : remove_list) {
			clients[i].view.lock.lock();
			if (0 != clients[i].view.view_list.count(id)) {
				clients[i].view.view_list.erase(id);
				clients[i].view.lock.unlock();

				if (false == clients[i].connected) { continue; }

				*((unsigned long long*)(&p[2])) = id;
				SendPacket(i, p);
			}
			else {
				clients[i].view.lock.unlock();
			}
		}
	}
	else if (CLIENT_KEY_INPUT == buf[1]) {
		list<XYnum>::iterator ptr = all_mines.end();;
		int mincnt = all_mines.size();
		if (UP == buf[2]) {
			if ((0 < clients[id].position.y) && (TILE_BLOCK != worldmap[clients[id].position.y - 1][clients[id].position.x])) {
				--clients[id].position.y;
				if (0 != mincnt) { ptr = bomb_explosive_find(clients[id].position); }
			}
			clients[id].position.direction = UP;
		}
		else if (DOWN == buf[2]) {
			if ((HEIGHT - 1 > clients[id].position.y) && (TILE_BLOCK != worldmap[clients[id].position.y + 1][clients[id].position.x])) {
				++clients[id].position.y;
				if (0 != mincnt) { ptr = bomb_explosive_find(clients[id].position); }
			}
			clients[id].position.direction = DOWN;
		}
		else if (LEFT == buf[2]) {
			if ((0 < clients[id].position.x) && (TILE_BLOCK != worldmap[clients[id].position.y][clients[id].position.x - 1])) {
				--clients[id].position.x;
				if (0 != mincnt) { ptr = bomb_explosive_find(clients[id].position); }
			}
			clients[id].position.direction = LEFT;
		}
		else if (RIGHT == buf[2]) {
			if ((WIDTH - 1 > clients[id].position.x) && (TILE_BLOCK != worldmap[clients[id].position.y][clients[id].position.x + 1])) {
				++clients[id].position.x;
				if (0 != mincnt) { ptr = bomb_explosive_find(clients[id].position); }
			}
			clients[id].position.direction = RIGHT;
		}
		else if (SPACEBAR == buf[2]) {
			if (5 <= clients[id].mine.size()) {
				XY mineTemp = *(clients[id].mine.begin());
				clients[id].mine.pop_front();
				auto ptr = bomb_explosive_find(mineTemp);
				if (ptr != all_mines.end()) {
					minelock.lock();
					all_mines.erase(ptr);
					minelock.unlock();
				}
			}
			clients[id].mine.push_back(clients[id].position);
			XYnum temp;
			temp.id = id;
			temp.pos = clients[id].position;
			minelock.lock();
			all_mines.push_back(move(temp));
			minelock.unlock();
		}

		// 수정해야 할 몇가지...******************************************************
		// 2. 지뢰를 밟고 이동한 후에 반응이 온다.

		//printf("%lld player x = %hd, y = %hd\n", id, clients[id].position.x, clients[id].position.y);

		// 근처 플레이어에게 전송
		Packet p[19];
		p[0] = 19;
		p[1] = CLIENT_KEY_INPUT;
		*((unsigned long long*)(&p[2])) = id;
		*((short*)(&p[10])) = clients[id].position.x;
		*((short*)(&p[12])) = clients[id].position.y;
		p[14] = clients[id].position.direction;
		p[15] = true;
		p[16] = clients[id].hp;
		p[17] = clients[id].exp;
		p[18] = clients[id].level;
		if ((ptr != all_mines.end()) && (0 != mincnt)) {
			clients[id].hp -= 20;
			if (100 <= clients[ptr->id].exp) {
				clients[ptr->id].exp -= 100;
				++clients[ptr->id].level;
			}
			if (0 >= clients[id].hp) {
				clients[id].hp = 0;
				if (id != ptr->id) {
					clients[ptr->id].exp += 20;
				}
			}

			Packet BombPacket[6];
			BombPacket[0] = 6;
			BombPacket[1] = BOMB_EXPLORED;
			*((short*)(&BombPacket[2])) = ptr->pos.x;
			*((short*)(&BombPacket[4])) = ptr->pos.y;
			SendPacket(ptr->id, BombPacket);

			minelock.lock();
			all_mines.erase(ptr);
			minelock.unlock();
		}
		
		SendPacket(id, p);

		// ************************* WARNING ***************************
		// 3명 이상 접속 시 몇개는 패킷을 못받음 정확한 원인은 아직 파악 못함...
		// 문제는 확인함 -> 동시에 시야에 들어올 경우 하나만 패킷을 받게됨

		/*unordered_set<unsigned long long> new_list;
		for (unsigned long long i = 0; i < MAX_USER; ++i) {
			if (false == clients[i].connected) { continue; }
			if (i == id) { continue; }
			if (false == is_range(id, i)) { continue; }
			new_list.insert(i);
		}

		for (auto d : new_list) { addView(id, d); }

		vector<unsigned long long> remove_list;
		clients[id].view.lock.lock();
		for (auto i : clients[id].view.view_list) {
			if (0 != new_list.count(i)) { continue; }
			remove_list.push_back(i);
		}
		clients[id].view.lock.unlock();

		for (auto d : remove_list) { removeView(id, d);	}*/

		unordered_set <int> new_list;
		for (auto i = 0; i < NUM_OF_NPC; ++i) {
			if (false == clients[i].connected) continue;
			if (i == id) continue;
			if (false == is_range(i, id)) continue;
			new_list.insert(i);
		}
		
		for (auto i : new_list) {
			clients[id].view.lock.lock();
			if (0 == clients[id].view.view_list.count(i)) { // 새로 뷰리스트에 들어오는 객체 처리
				clients[id].view.view_list.insert(i);
				clients[id].view.lock.unlock();

				if (Is_NPC(i)) {
					if (!Is_Active(i)) {
						clients[i].is_active = true;
						event_type temp;
						temp.obj_id = i;
						temp.wakeup_time = GetTickCount() + 1000;
						temp.event_id = OP_AI_MOVE;
						timer_queue.push(temp);
					}
					continue;
				};

				*((unsigned long long*)(&p[2])) = i;
				*((short*)(&p[10])) = clients[i].position.x;
				*((short*)(&p[12])) = clients[i].position.y;
				p[14] = clients[i].position.direction;
				p[16] = clients[i].hp;
				p[17] = clients[i].exp;
				p[18] = clients[i].level;
		
				SendPacket(id, p);
		
				clients[i].view.lock.lock();
				if (0 == clients[i].view.view_list.count(id)) {
					clients[i].view.view_list.insert(id);
					clients[i].view.lock.unlock();
		
					*((unsigned long long*)(&p[2])) = id;
					*((short*)(&p[10])) = clients[id].position.x;
					*((short*)(&p[12])) = clients[id].position.y;
					p[14] = clients[id].position.direction;
					p[16] = clients[id].hp;
					p[17] = clients[id].exp;
					p[18] = clients[id].level;
		
					SendPacket(i, p);
				}
				else {
					clients[i].view.lock.unlock();
					SendPacket(i, p);
				}
			}
			else { // 뷰리스트에 계속 유지되어 있는 객체 처리
				clients[id].view.lock.unlock();
				if (Is_NPC(i)) { continue; }
				clients[i].view.lock.lock();
				if (1 == clients[i].view.view_list.count(id)) {
					clients[i].view.lock.unlock();
					SendPacket(i, p);
				}
				else {
					clients[i].view.view_list.insert(id);
					clients[i].view.lock.unlock();
					SendPacket(i, p);
				}
			}
		}
		
		*((short*)(&p[10])) = 0;
		*((short*)(&p[12])) = 0;
		p[15] = false;
		
		// 뷰리스트에서 나가는 객체 처리
		vector <int> remove_list;
		clients[id].view.lock.lock();
		for (auto i : clients[id].view.view_list) {
			if (0 != new_list.count(i)) continue;
			remove_list.push_back(i);
		}
		for (auto i : remove_list) { clients[id].view.view_list.erase(i); }
		clients[id].view.lock.unlock();
		
		for (auto i : remove_list) {
			*((unsigned long long*)(&p[2])) = i;
			SendPacket(id, p);
		}
		
		for (auto i : remove_list) {
			if (Is_NPC(i)) { continue; }
			clients[i].view.lock.lock();
			if (0 != clients[i].view.view_list.count(id)) {
				clients[i].view.view_list.erase(id);
				clients[i].view.lock.unlock();

				if (false == clients[i].connected) { continue; }

				*((unsigned long long*)(&p[2])) = id;
				SendPacket(i, p);
			}
			else {
				clients[i].view.lock.unlock();
			}
		}
	}
	else {
		printf("ERROR, Unknown signal -> [ %lld ] protocol num = %d\n", id, buf[1]);
		exit(-1);
	}
}

void SendPacket(unsigned long long id, Packet *packet) {
	// packet[0] = packet size
	// packet[1] = type
	// packet[...] = data

	Ovlp_ex *over = new Ovlp_ex;
	memset(over, 0, sizeof(Ovlp_ex));
	over->operation = OP_SERVER_SEND;
	over->wsabuf.buf = reinterpret_cast<char *>(over->iocp_buffer);
	over->wsabuf.len = packet[0];
	memcpy(over->iocp_buffer, packet, packet[0]);

	DWORD flags{ 0 };
	int retval = WSASend(clients[id].s, &over->wsabuf, 1, NULL, flags, &over->original_overlap, NULL);
	if (SOCKET_ERROR == retval) {
		int err_no = WSAGetLastError();
		if (ERROR_IO_PENDING != err_no) {
			error_display("SendPacket::WSASend", err_no, __LINE__);
			while (true);
		}
	}
}

bool is_range(unsigned long long curr_player_id, unsigned long long other_player_id) {
	double x_range( clients[curr_player_id].position.x - clients[other_player_id].position.x );
	double y_range( clients[curr_player_id].position.y - clients[other_player_id].position.y );
	x_range *= x_range;
	y_range *= y_range;
	return (VIEW_RANGE * VIEW_RANGE >= (x_range + y_range));
}

void addView(unsigned long long id1, unsigned long long id2) {
	Packet p[16];
	p[0] = 16;
	p[1] = CLIENT_KEY_INPUT;
	*((unsigned long long*)(&p[2])) = id1;
	*((short*)(&p[10])) = clients[id1].position.x;
	*((short*)(&p[12])) = clients[id1].position.y;
	p[14] = clients[id1].position.direction;
	p[15] = true;

	clients[id2].view.lock.lock();
	if (0 == clients[id2].view.view_list.count(id1)) {
		clients[id2].view.view_list.insert(id1);
		clients[id2].view.lock.unlock();
		SendPacket(id2, p);
	}
	else {
		clients[id2].view.lock.unlock();
	}
	
	*((unsigned long long*)(&p[2])) = id2;
	*((short*)(&p[10])) = clients[id2].position.x;
	*((short*)(&p[12])) = clients[id2].position.y;
	p[14] = clients[id2].position.direction;
	p[15] = true;

	clients[id1].view.lock.lock();
	if (0 == clients[id1].view.view_list.count(id2)) {
		clients[id1].view.view_list.insert(id2);
	}
	clients[id1].view.lock.unlock();
	SendPacket(id1, p);
}

void removeView(unsigned long long id1, unsigned long long id2) {
	Packet p[16];
	p[0] = 16;
	p[1] = CLIENT_KEY_INPUT;

	*((unsigned long long*)(&p[2])) = id1;
	p[15] = false;

	clients[id2].view.lock.lock();
	if (1 == clients[id2].view.view_list.count(id1)) {
		clients[id2].view.view_list.erase(id1);
	}
	clients[id2].view.lock.unlock();
	SendPacket(id2, p);
	
	*((unsigned long long*)(&p[2])) = id2;
	p[15] = false;

	clients[id1].view.lock.lock();
	if (1 == clients[id1].view.view_list.count(id2)) {
		clients[id1].view.view_list.erase(id2);
	}
	clients[id1].view.lock.unlock();
	SendPacket(id1, p);
}

list<XYnum>::iterator bomb_explosive_find(const XY &position) {
	auto p = all_mines.begin();
	for (; p != all_mines.end(); ++p) {
		if (p->pos.x != position.x) { continue; }
		if (p->pos.y != position.y) { continue; }
		break;
	}
	return p;
}

void Do_Move(unsigned long long id) {
	if (false == clients[id].is_active) { return; }

	/*volatile int k;
	volatile int sum;
	for (k = 0; k < 10000; k++) sum += k;*/

	int x = clients[id].position.x;
	int y = clients[id].position.y;

	unordered_set<int> view_list;
	for (auto pl = 0; pl < MAX_USER; pl++) {
		if (false == clients[pl].connected) continue;
		if (false == is_range(id, pl)) continue;
		view_list.insert(pl);
	}
	switch (rand() % 4) {
	case 0: if ((x < WIDTH - 1) && (TILE_BLOCK != worldmap[clients[id].position.y][clients[id].position.x + 1])) { ++x; } break;
	case 1: if ((x > 0) && (TILE_BLOCK != worldmap[clients[id].position.y][clients[id].position.x - 1])) { --x; } break;
	case 2: if ((y < HEIGHT - 1) && (TILE_BLOCK != worldmap[clients[id].position.y + 1][clients[id].position.x])) { ++y; } break;
	case 3: if ((y > 0) && (TILE_BLOCK != worldmap[clients[id].position.y - 1][clients[id].position.x])) { --y; } break;
	}
	clients[id].position.x = x;
	clients[id].position.y = y;

	XY tempPos;
	tempPos.x = x;
	tempPos.y = y;

	auto ptr = bomb_explosive_find(tempPos);
	if (ptr != all_mines.end()) {
		clients[id].is_active = false;
		clients[id].connected = false;
		clients[id].hp -= 1;
		if (100 <= clients[ptr->id].exp) {
			clients[ptr->id].exp -= 100;
			++clients[ptr->id].level;
		}
		if (0 >= clients[id].hp) {
			clients[id].hp = 0;
			if (id != ptr->id) {
				clients[ptr->id].exp += 20;
			}
		}

		Packet BombPacket[6];
		BombPacket[0] = 6;
		BombPacket[1] = BOMB_EXPLORED;
		*((short*)(&BombPacket[2])) = ptr->pos.x;
		*((short*)(&BombPacket[4])) = ptr->pos.y;
		SendPacket(ptr->id, BombPacket);

		minelock.lock();
		all_mines.erase(ptr);
		minelock.unlock();
	}

	unordered_set <int> new_list;
	for (auto pl = 0; pl < MAX_USER; pl++) {
		if (false == clients[pl].connected) continue;
		if (false == is_range(id, pl)) continue;
		new_list.insert(pl);
	}

	for (auto pl : view_list)
	{
		if (0 == new_list.count(pl)) {
			clients[pl].view.lock.lock();
			clients[pl].view.view_list.erase(id);
			clients[pl].view.lock.unlock();

			Packet packet[16]{ 0 };
			packet[0] = 16;
			packet[1] = CLIENT_KEY_INPUT;
			*((unsigned long long*)(&packet[2])) = id;
			*((short*)(&packet[10])) = clients[pl].position.x;
			*((short*)(&packet[12])) = clients[pl].position.y;
			packet[14] = MONSTER;
			packet[15] = false;

			SendPacket(pl, packet);
		}
		else {
			Packet packet[16]{ 0 };
			packet[0] = 16;
			packet[1] = CLIENT_KEY_INPUT;
			*((unsigned long long*)(&packet[2])) = id;
			*((short*)(&packet[10])) = x;
			*((short*)(&packet[12])) = y;
			packet[14] = MONSTER;
			packet[15] = true;
			SendPacket(pl, packet);
		}
	}
	for (auto pl : new_list) {
		if (0 != view_list.count(pl)) { continue; }

		Packet packet[16]{ 0 };
		packet[0] = 16;
		packet[1] = CLIENT_KEY_INPUT;
		*((unsigned long long*)(&packet[2])) = id;
		*((short*)(&packet[10])) = x;
		*((short*)(&packet[12])) = y;
		packet[14] = MONSTER;
		packet[15] = true;

		SendPacket(pl, packet);
	}
	int now = GetTickCount();
	if (NPC_START == id) { cout << "NPC Move duaration: " << now - clients[id].last_move_time << "\n"; }
	clients[id].last_move_time = now;

	event_type temp;
	temp.obj_id = id;
	temp.wakeup_time = GetTickCount() + 1000;
	temp.event_id = OP_AI_MOVE;

	timer_lock.lock();
	timer_queue.push(temp);
	timer_lock.unlock();

	if (true == new_list.empty()) { clients[id].is_active = false; }
}

void heart_beat(unsigned long long npc)
{
	if ((clients[npc].last_move_time + 1000) <= GetTickCount()){ Do_Move(npc); }		
}

void AI_Thread_Start()
{
	while (true) {
		for (int i = NPC_START; i < NUM_OF_NPC; ++i)
			heart_beat(i);
		Sleep(1);
	}
}

void Timer_Thread_Start()
{
	while (true) {
		Sleep(1);
		timer_lock.lock();
		while (false == timer_queue.empty()) {
			if (timer_queue.top().wakeup_time > GetTickCount()) break;
			event_type ev = timer_queue.top();
			timer_queue.pop();
			timer_lock.unlock();
			Overlap_ex *over = new Overlap_ex;
			over->operation = ev.event_id;
			PostQueuedCompletionStatus(g_hIocp, 1, ev.obj_id, &(over->original_overlap));
			timer_lock.lock();
		}
		timer_lock.unlock();
	}
}

const bool Is_NPC(const unsigned long long id)
{
	return id >= NPC_START;
}

const bool Is_Active(const unsigned long long npc)
{
	return clients[npc].is_active;
}