#pragma once
#include<Windows.h>

#define SERVERPORT 9000
#define MaxBufSize 256

#define MAX_USER 500
#define MAX_CHAT_LOG_SIZE 15

#define NPC_START 500
#define NUM_OF_NPC 3000

// iocp buf operation
#define OP_SERVER_RECV 1
#define OP_SERVER_SEND 2
#define OP_AI_MOVE 3

// packet[1] operation
#define DISCONNECTED 0
#define CLIENT_INIT 1
#define MESSEAGE_SEND_ALL 2
#define CLIENT_KEY_INPUT 3
#define REFRESH_MAP 4
#define BOMB_EXPLORED 5
#define DIE 6

// world map
#define WIDTH 300
#define HEIGHT 300

// key
#define ENTER 13
#define SPACEBAR 32

#define TAB 9

#define SHIFT 16
#define CTRL 17
#define ALT 18

#define PAUSEBREAK 19
#define CAPSLOOK 20

#define KOREAN_ENGLISH 21
#define CHINESS 25

#define ESC	27

#define PAGEUP 33
#define PAGEDOWN 34
#define END 35
#define HOME 36

#define LEFT 75
#define UP 72
#define RIGHT 77
#define DOWN 80

// map tile type
#define TILE_EMPTY 0
#define TILE_CANT_SEE 1
#define TILE_PLAYER_UP 2
#define TILE_PLAYER_DOWN 3
#define TILE_PLAYER_LEFT 4
#define TILE_PLAYER_RIGHT 5
#define TILE_BLOCK 6
#define TILE_OTHER_PLAYER_UP 7
#define TILE_OTHER_PLAYER_DOWN 8
#define TILE_OTHER_PLAYER_LEFT 9
#define TILE_OTHER_PLAYER_RIGHT 10

// player_type
#define PLAYER_OTHER 0
#define MONSTER 1

// view
#define VIEW_RANGE 7

using Packet = unsigned char;
using WorldMap = BYTE;

using XY = struct Position {
	short x;
	short y;
	BYTE direction;
};