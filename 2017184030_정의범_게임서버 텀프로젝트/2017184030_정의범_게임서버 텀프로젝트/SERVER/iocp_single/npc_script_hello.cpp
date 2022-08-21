#include <iostream>
#include <WS2tcpip.h>
#include <MSWSock.h>
#include <thread>
#include <array>
#include <vector>
#include <mutex>
#include <unordered_set>
#include <queue>
#include <chrono>
#include <string>
#include <concurrent_priority_queue.h>
#include "DataBase.h"

#include "protocol.h"
using namespace std;
using namespace chrono;


extern "C" {
#include "include\lua.h"
#include "include\lauxlib.h"
#include "include\lualib.h"
}
#pragma comment (lib, "lua54.lib")

#pragma comment (lib, "WS2_32.LIB")
#pragma comment (lib, "MSWSock.LIB")

const int BUFSIZE = 256;
const int RANGE = 3;

HANDLE g_h_iocp;
SOCKET g_s_socket;
bool Lua_check = false;
enum EVENT_TYPE { EVENT_NPC_MOVE };

struct timer_event {
	int obj_id;
	chrono::system_clock::time_point	start_time;
	EVENT_TYPE ev;
	int target_id;
	constexpr bool operator < (const timer_event& _Left) const
	{
		return (start_time > _Left.start_time);
	}

};
concurrency::concurrent_priority_queue <timer_event> timer_queue;

struct event_type {
	int obj_id;
	system_clock::time_point start_time;
	int event_id;
	// int target_id;
	constexpr bool operator < (const event_type& _Left) const
	{
		return (start_time > _Left.start_time);
	}
};



concurrency::concurrent_priority_queue <event_type> time_queue;


void error_display(int err_no);
void do_npc_move(int npc_id);


enum COMP_OP { OP_RECV, OP_SEND, OP_ACCEPT, OP_NPC_MOVE, OP_PLAYER_MOVE, OP_NPC_OUT, OP_PLAYER_ATTACK,
	OP_NPC_REBORN, OP_NPC_ATTACK, OP_HEALING
};

//데이터 베이스
DataBase* my_db;
constexpr char DB_UPDATE = 0;  //업데이트 위함 
struct DB_event
{
	int id;
	int target_id;
	system_clock::time_point start_time;
	constexpr bool operator < (const DB_event& _Left) const
	{
		return (start_time > _Left.start_time);
	}
};

priority_queue<DB_event> DB_queue;
mutex DB_lock;




class EXP_OVER {
public:
	WSAOVERLAPPED	_wsa_over;
	COMP_OP			_comp_op;
	WSABUF			_wsa_buf;
	unsigned char	_net_buf[BUFSIZE];
	int				_target;
public:
	EXP_OVER(COMP_OP comp_op, char num_bytes, void* mess) : _comp_op(comp_op)
	{
		ZeroMemory(&_wsa_over, sizeof(_wsa_over));
		_wsa_buf.buf = reinterpret_cast<char*>(_net_buf);
		_wsa_buf.len = num_bytes;
		memcpy(_net_buf, mess, num_bytes);
	}

	EXP_OVER(COMP_OP comp_op) : _comp_op(comp_op) {}

	EXP_OVER()
	{
		_comp_op = OP_RECV;
	}

	~EXP_OVER()
	{
	}
};

enum STATE { ST_FREE, ST_ACCEPT, ST_INGAME };
class CLIENT {
public:
	char name[MAX_NAME_SIZE];
	int	   _id;
	short  x, y;
	unordered_set   <int>  viewlist;
	mutex vl;
	short	hp, maxhp;
	short level;
	int exp;
	lua_State* L;

	mutex state_lock;
	mutex clinet_lock;
	STATE _state;
	atomic_bool	_is_active;
	int		_type;   // 1.Player   2.NPC	

	EXP_OVER _recv_over;
	SOCKET  _socket;
	int		_prev_size;
	int		last_move_time;
	mutex m_lua;
	int target;

	//피스, 어그로
	int monster_type;
	//움직임 - 로밍, 픽스 
	int monster_move_type;
public:
	CLIENT() : _state(ST_FREE), _prev_size(0)
	{
		x = 0;
		y = 0;
		hp = 100;
		maxhp = 100;
		level = 1;
		exp = 0;
	}

	~CLIENT()
	{
		closesocket(_socket);
	}

	void do_recv()
	{
		DWORD recv_flag = 0;
		ZeroMemory(&_recv_over._wsa_over, sizeof(_recv_over._wsa_over));
		_recv_over._wsa_buf.buf = reinterpret_cast<char*>(_recv_over._net_buf + _prev_size);
		_recv_over._wsa_buf.len = sizeof(_recv_over._net_buf) - _prev_size;
		int ret = WSARecv(_socket, &_recv_over._wsa_buf, 1, 0, &recv_flag, &_recv_over._wsa_over, NULL);
		if (SOCKET_ERROR == ret) {
			int error_num = WSAGetLastError();
			if (ERROR_IO_PENDING != error_num)
				error_display(error_num);
		}
	}

	void do_send(int num_bytes, void* mess)
	{
		EXP_OVER* ex_over = new EXP_OVER(OP_SEND, num_bytes, mess);
		int ret = WSASend(_socket, &ex_over->_wsa_buf, 1, 0, 0, &ex_over->_wsa_over, NULL);
		if (SOCKET_ERROR == ret) {
			int error_num = WSAGetLastError();
			if (ERROR_IO_PENDING != error_num)
				error_display(error_num);
		}
	}
};

array <CLIENT, MAX_USER + MAX_NPC> clients;

void error_display(int err_no)
{
	WCHAR* lpMsgBuf;
	FormatMessage(
		FORMAT_MESSAGE_ALLOCATE_BUFFER | FORMAT_MESSAGE_FROM_SYSTEM,
		NULL, err_no,
		MAKELANGID(LANG_NEUTRAL, SUBLANG_DEFAULT),
		(LPTSTR)&lpMsgBuf, 0, 0);
	wcout << lpMsgBuf << endl;
	LocalFree(lpMsgBuf);
}

bool is_near(int a, int b)
{
	if (RANGE < abs(clients[a].x - clients[b].x)) return false;
	if (RANGE < abs(clients[a].y - clients[b].y)) return false;
	return true;
}

bool is_npc(int id)
{
	return (id >= NPC_ID_START) && (id <= NPC_ID_END);
}

bool is_player(int id)
{
	return (id >= 0) && (id < MAX_USER);
}

int get_new_id()
{
	static int g_id = 0;

	for (int i = 0; i < MAX_USER; ++i) {
		clients[i].state_lock.lock();
		if (ST_FREE == clients[i]._state) {
			clients[i]._state = ST_ACCEPT;
			clients[i].state_lock.unlock();
			return i;
		}
		clients[i].state_lock.unlock();
	}
	cout << "Maximum Number of Clients Overflow!!\n";
	return -1;
}



void send_login_ok_packet(int c_id)
{
	sc_packet_login_ok packet;
	packet.id = c_id;
	packet.size = sizeof(packet);
	packet.type = SC_PACKET_LOGIN_OK;
	packet.x = clients[c_id].x;
	packet.y = clients[c_id].y;
	packet.hp = clients[c_id].hp;
	packet.level = clients[c_id].level;
	packet.exp = clients[c_id].exp;
	clients[c_id].do_send(sizeof(packet), &packet);
}

void send_move_packet(int c_id, int mover)
{
	sc_packet_move packet;
	packet.id = mover;
	packet.size = sizeof(packet);
	packet.type = SC_PACKET_MOVE;
	packet.x = clients[mover].x;
	packet.y = clients[mover].y;
	packet.move_time = clients[mover].last_move_time;
	clients[c_id].do_send(sizeof(packet), &packet);
}

void send_remove_object(int c_id, int victim)
{
	sc_packet_remove_object packet;
	packet.id = victim;
	packet.size = sizeof(packet);
	packet.type = SC_PACKET_REMOVE_OBJECT;
	clients[c_id].do_send(sizeof(packet), &packet);
}

void send_put_object(int c_id, int target)
{
	sc_packet_put_object packet;
	packet.id = target;
	packet.size = sizeof(packet);
	packet.type = SC_PACKET_PUT_OBJECT;
	packet.x = clients[target].x;
	packet.y = clients[target].y;
	strcpy_s(packet.name, clients[target].name);
	packet.object_type = 0;
	clients[c_id].do_send(sizeof(packet), &packet);
}

void send_chat_packet(int user_id, int my_id, char* mess)
{
	sc_packet_chat packet;
	packet.id = my_id;
	packet.size = sizeof(packet);
	packet.type = SC_PACKET_CHAT;
	strcpy_s(packet.message, mess);
	clients[user_id].do_send(sizeof(packet), &packet);
}

void send_status_change_packet(int id)
{
	sc_packet_status_change packet;
	packet.size = sizeof(packet);
	packet.type = SC_PACKET_STATUS_CHANGE;
	packet.hp = clients[id].hp;
	packet.maxhp = clients[id].maxhp;
	packet.level = clients[id].level;
	packet.exp = clients[id].exp;

	clients[id].do_send(sizeof(packet), &packet);
}

void send_login_fail_packet(int id, int why)
{
	sc_packet_login_fail packet;
	packet.size = sizeof(packet);
	packet.type = SC_PACKET_LOGIN_FAIL;
	//why는 get Reason 해서 0이나 1값 줘서 판단하자
	packet.reason = why;    
	clients[id].do_send(sizeof(packet), &packet);
}

void send_staus_message_packet(int id, char* system_mess, int msg_type)
{
	sc_packet_status_message packet;
	packet.size = sizeof(packet);
	packet.type = SC_PACKET_STATUS_MESSAGE;
	packet.mess_type = msg_type;
	strcpy_s(packet.message, system_mess);
	clients[id].do_send(sizeof(packet), &packet);
}

void Disconnect(int c_id)
{
	CLIENT& cl = clients[c_id];
	cl.vl.lock();
	unordered_set <int> my_vl = cl.viewlist;
	cl.vl.unlock();
	for (auto& other : my_vl) {
		CLIENT& target = clients[other];
	
		if (true == is_npc(target._id)) break;

		if (ST_INGAME != target._state)
			continue;
		target.vl.lock();
		if (0 != target.viewlist.count(c_id)) {
			target.viewlist.erase(c_id);
			target.vl.unlock();
			send_remove_object(other, c_id);
		}
		else target.vl.unlock();
	}
	clients[c_id].state_lock.lock();
	closesocket(clients[c_id]._socket);
	clients[c_id]._state = ST_FREE;
	clients[c_id].state_lock.unlock();
}

std::wstring str_to_lcpw(const std::string& str)
{
	int len = MultiByteToWideChar(CP_UTF8, 0, &str[0], (int)str.size(), NULL, 0);
	std::wstring wstr(len, 0);
	MultiByteToWideChar(CP_UTF8, 0, &str[0], (int)str.size(), &wstr[0], len);
	return wstr;
}
void add_DB_event(int id, int targetID, system_clock::time_point time)//아직
{
	DB_event ev{ id, targetID, time };
	DB_lock.lock();
	DB_queue.push(ev);
	DB_lock.unlock();
}
bool process_login(int id, wstring name)  //데이터 베이스 연동하자 
{
	short x = 0;
	short y = 0;
	int level = 1;
	int exp = 0; 
	short hp = 100;
	short maxhp = 100;

	if (id > MAX_USER) {
		send_login_fail_packet(id, 1);  //가득찼다.
		return false;
	}
	if (true == my_db->GetData(name, &x, &y, &level, &exp, &hp))
	{
		clients[id].x = x;
		clients[id].y = y;
		clients[id].level = level;
		clients[id].exp = exp;
		clients[id].hp = hp;
		clients[id].maxhp = level * 100;
		clients[id].state_lock.lock();
		clients[id]._state = ST_INGAME;
		clients[id].state_lock.unlock();
		send_login_ok_packet(id);
		send_status_change_packet(id);

		string str{ clients[id].name };
		wstring wstr = str_to_lcpw(str);
		my_db->Update_DB(wstr, clients[id].x, clients[id].y, clients[id].level, clients[id].exp, clients[id].hp);

		return true;
	}
	else if(false == my_db->GetData(name, &x, &y, &level, &exp, &hp)){
		clients[id].x = x;
		clients[id].y = y;
		clients[id].level = level;
		clients[id].exp = exp;
		clients[id].hp = hp;
		clients[id].maxhp = level * 100;
		clients[id].state_lock.lock();
		clients[id]._state = ST_INGAME;
		clients[id].state_lock.unlock();
		send_login_ok_packet(id);
		send_status_change_packet(id);

		string str{ clients[id].name };
		wstring wstr = str_to_lcpw(str);
		return true;
		//send_login_fail_packet(id, 0);
		//return false;
	}


}
void Activate_Player_Move_Event(int target, int player_id)
{
	EXP_OVER* exp_over = new EXP_OVER;
	exp_over->_comp_op = OP_PLAYER_MOVE;
	exp_over->_target = player_id;
	PostQueuedCompletionStatus(g_h_iocp, 1, target, &exp_over->_wsa_over);
}



bool can_attack_range(int p1, int p2)  //거리 계산해서 1칸이하만 공격가능하게
{
	int dist = (clients[p1].x - clients[p2].x) * (clients[p1].x - clients[p2].x);
	dist += (clients[p1].y - clients[p2].y) * (clients[p1].y - clients[p2].y);

	return dist <= 1;
}



void process_levelup(int id)    
{
	string msg = "[LEVEL UP] ";

	msg += clients[id].name;
	msg += "'s level: ";
	msg += to_string(clients[id].level-1);
	msg += " -> ";
	msg += to_string(clients[id].level);

	char message[50];
	strcpy_s(message, msg.c_str());
	send_staus_message_packet(id, message, SEND_LEVELUP);

	clients[id].level += 1;
	clients[id].exp = 0;
	clients[id].maxhp = clients[id].level * 100;  //레벌업하면 피 다 채워주자
	clients[id].hp = clients[id].maxhp;
	add_DB_event(id, DB_UPDATE, system_clock::now());  
}
mutex timer_lock;
void add_timer(int obj_id, int ev_type, system_clock::time_point t)
{
	event_type ev{ obj_id, t, ev_type };

	timer_lock.lock();
	time_queue.push(ev);
	timer_lock.unlock();
}
void process_teleport(int id)  //아직 수정필요
{
	short preX = clients[id].x;
	short preY = clients[id].y;
	short x = 0;
	short y = 0;
	x = rand() % 2000;
	y = rand() % 2000;


	clients[id].x = x;
	clients[id].y = y;



	send_move_packet(id, id); 

	//시야처리 필요 함 


}


int teleportX, teleportY;  //나중에 lua로 초기화하자 
void process_die(int die_id, int attack_id)
{
	if (is_near(die_id, attack_id)) {   //죽인게 맞으면 
	
		if (true == is_npc(die_id))
		{
			clients[die_id].vl.lock();
			clients[die_id].viewlist.clear();
			clients[die_id].vl.unlock();

			clients[die_id].state_lock.lock();
			clients[die_id]._state = ST_FREE;
			clients[die_id].state_lock.unlock();

			clients[die_id].vl.lock();
			clients[attack_id].viewlist.erase(die_id);
			clients[die_id].vl.unlock();


			send_remove_object(attack_id, die_id);  //죽은 거보내기 
			send_remove_object(die_id, die_id);  //죽은 거보내기 
			// 30초 후에 부활
			add_timer(die_id, OP_NPC_REBORN, system_clock::now() + 30s);
		}
		else {// npc 아니면    //부활할때 경험, hp 반으로 깎아 

			process_teleport(die_id);
			clients[die_id].exp /= 2;
			clients[die_id].level = clients[die_id].level;
			clients[die_id].hp = clients[die_id].level * 100 / 2;
	
			send_status_change_packet(die_id);
		}
	}

}
void process_demage_mess(int attackID, int damageID, int damage)    //데미지 메시지 
{
	string msg = "[DAMAGE!!] ";
	msg += clients[damageID].name;
	msg += " was ";
	msg += to_string(damage);
	msg += " damaged by ";
	msg += clients[attackID].name;
	msg += "!!";

	char mess[50];
	strcpy_s(mess, msg.c_str());
	if (false == is_npc(attackID))
		send_staus_message_packet(attackID, mess, SEND_DAMAGE);
	if (false == is_npc(damageID))
		send_staus_message_packet(damageID, mess, SEND_DAMAGE);
}
void process_chat(int id, char* mess)   
{
	clients[id].vl.lock();
	auto viewlist = clients[id].viewlist;
	clients[id].vl.unlock();

	for (auto vl : viewlist) {
		send_chat_packet(vl, id, mess);
	}
}
void process_attack(int id)
{
	clients[id].vl.lock();
	auto v_list = clients[id].viewlist;
	clients[id].vl.unlock();

	for (const auto& vl : v_list)
	{

		if (true == can_attack_range(id, vl))  //공격 가능범위면 
		{
			int HP = clients[vl].hp;
			int damage = clients[id].level * 10;  //데미지는 이정도로하자 
			HP -= damage;	
		

			char mess[10] = "What!";
			send_chat_packet(id, vl, mess);

			process_demage_mess(id, vl, damage);
			//여기에요
			if (HP <= 0)
			{
				clients[id].viewlist.erase(vl);
				HP = 0;
				int exp = clients[vl].level;  ///수정
				cout << clients[vl].level << endl;
				exp = exp * exp * 2;
				int mon_type = clients[vl].monster_type;
				int mon_mv_type = clients[vl].monster_move_type;
				if (mon_type == AGRO_MONSTER)
					exp = exp * 2;
				if (mon_mv_type == ROAMING_MONSTER)
					exp = exp * 2;
				clients[id].exp += exp;

			
				if (clients[id].exp >= clients[id].level * 10) {  //레벨업 수치 수정해 *100으로
					cout << "레벨업" << endl;
					process_levelup(id);
				
				}

				int tmp = vl;
				process_die(vl, id); 
			}
			else  //안죽었어 
			{
				if (true == is_npc(vl))
				{
				
					clients[vl].target = id;
					add_timer(vl, OP_NPC_ATTACK, system_clock::now() + 2s);
				}
			}

			clients[vl].hp = HP;
			send_status_change_packet(id);  //경험치 받으면 보내줘야지 
		}
	}
}
void process_packet(int client_id, unsigned char* p)
{
	unsigned char packet_type = p[1];
	CLIENT& cl = clients[client_id];

	switch (packet_type) {
	case CS_PACKET_LOGIN: {
		cs_packet_login* packet = reinterpret_cast<cs_packet_login*>(p);
		clients[client_id].clinet_lock.lock();
		strcpy_s(cl.name, packet->name);
		clients[client_id].clinet_lock.unlock();
		//send_login_ok_packet(client_id);

		string name{ packet->name };

		wstring wName = str_to_lcpw(name);
		bool ret = false;
		ret = process_login(client_id, wName);

		
		if (ret == true) {
			cout << "접속" << endl;
			CLIENT& cl = clients[client_id];
				cl.state_lock.lock();
				cl._state = ST_INGAME;
				cl.state_lock.unlock();

				// 새로 접속한 플레이어의 정보를 주위 플레이어에게 보낸다
			for (auto& other : clients) {
				//if (true == is_npc(other._id)) continue;  
				if (true == is_npc(other._id)) break;
				if (other._id == client_id) continue;
				other.state_lock.lock();
				if (ST_INGAME != other._state) {
					other.state_lock.unlock();
					continue;
				}
				other.state_lock.unlock();

				if (false == is_near(other._id, client_id))    //가까이 있지 않으면 넘어감
					continue;


				other.vl.lock();
				other.viewlist.insert(client_id);
				other.vl.unlock();
				sc_packet_put_object packet;
				packet.id = client_id;
				strcpy_s(packet.name, cl.name);
				packet.object_type = 0;
				packet.size = sizeof(packet);
				packet.type = SC_PACKET_PUT_OBJECT;
				packet.x = cl.x;
				packet.y = cl.y;

				other.do_send(sizeof(packet), &packet);
			}

			// 새로 접속한 플레이어에게 주위 객체 정보를 보낸다
			for (auto& other : clients) {
				if (other._id == client_id) continue;
				other.state_lock.lock();
				if (ST_INGAME != other._state) {
					other.state_lock.unlock();
					continue;
				}
				other.state_lock.unlock();

				if (false == is_near(other._id, client_id)) continue;

				if (true == is_npc(other._id)) {// 새로 접속한 플레이어에게 이동하는 걸 보여줘야한다. 
					Activate_Player_Move_Event(other._id, cl._id);
				}

				clients[client_id].vl.lock();
				clients[client_id].viewlist.insert(other._id);
				clients[client_id].vl.unlock();

				sc_packet_put_object packet;
				packet.id = other._id;
				strcpy_s(packet.name, other.name);
				packet.object_type = 0;
				packet.size = sizeof(packet);
				packet.type = SC_PACKET_PUT_OBJECT;
				packet.x = other.x;
				packet.y = other.y;
				cl.do_send(sizeof(packet), &packet);
			}
		}
		break;
	}
	case CS_PACKET_MOVE: {
		cs_packet_move* packet = reinterpret_cast<cs_packet_move*>(p);
		cl.last_move_time = packet->move_time;
		int x = cl.x;
		int y = cl.y;
		switch (packet->direction) {
		case 0: if (y > 0) y--; break;
		case 1: if (y < (WORLD_HEIGHT - 1)) y++; break;
		case 2: if (x > 0) x--; break;
		case 3: if (x < (WORLD_WIDTH - 1)) x++; break;
		default:
			cout << "Invalid move in client " << client_id << endl;
			exit(-1);
		}
		cl.x = x;
		cl.y = y;

		unordered_set <int> nearlist;
		for (auto& other : clients) {


			if (ST_INGAME != other._state)
				continue;
			if (false == is_near(client_id, other._id))
				continue;
			if (true == is_npc(other._id)) {         //npc면 움직여
				Activate_Player_Move_Event(other._id, cl._id);
			}
			nearlist.insert(other._id);
		}
		nearlist.erase(client_id);

		send_move_packet(cl._id, cl._id);

		cl.vl.lock();
		unordered_set <int> my_vl{ cl.viewlist };
		cl.vl.unlock();

		// 새로시야에 들어온 플레이어 처리
		for (auto other : nearlist) {
			if (0 == my_vl.count(other)) {
				cl.vl.lock();
				cl.viewlist.insert(other);
				cl.vl.unlock();
				send_put_object(cl._id, other);


				if (true == is_npc(other)) {
					break;
				}


				clients[other].vl.lock();
				if (0 == clients[other].viewlist.count(cl._id)) {
					clients[other].viewlist.insert(cl._id);
					clients[other].vl.unlock();
					send_put_object(other, cl._id);
				}
				else {
					clients[other].vl.unlock();
					send_move_packet(other, cl._id);
				}
			}
			// 계속 시야에 존재하는 플레이어 처리
			else {
				if (true == is_npc(other)) continue;

				clients[other].vl.lock();
				if (0 != clients[other].viewlist.count(cl._id)) {
					clients[other].vl.unlock();
					send_move_packet(other, cl._id);
				}
				else {
					clients[other].viewlist.insert(cl._id);
					clients[other].vl.unlock();
					send_put_object(other, cl._id);
				}
			}
		}
		// 시야에서 사라진 플레이어 처리
		for (auto other : my_vl) {
			if (0 == nearlist.count(other)) {
				cl.vl.lock();
				cl.viewlist.erase(other);
				cl.vl.unlock();
				send_remove_object(cl._id, other);

				//시야에서 사라진게 npc면 움직일 필요 없다. 
				if (true == is_npc(other))
					break;

				clients[other].vl.lock();
				if (0 != clients[other].viewlist.count(cl._id)) {
					clients[other].viewlist.erase(cl._id);
					clients[other].vl.unlock();
					send_remove_object(other, cl._id);
				}
				else clients[other].vl.unlock();
			}
		}
		break;
	}
	case CS_PACKET_ATTACK: {
		cs_packet_attack* packet = reinterpret_cast<cs_packet_attack*>(p);
		process_attack(client_id);
		break;
	}
	case CS_PACKET_CHAT: {
		cs_packet_chat* packet = reinterpret_cast<cs_packet_chat*>(p);
		process_chat(client_id, packet->message);
		break;
	}
	case CS_PACKET_TELEPORT: {
		process_teleport(client_id);
		break;
	}
	case CS_UPDATE: {
		cs_packet_update_packet* packet = reinterpret_cast<cs_packet_update_packet*>(p);
		cout << "가자" << endl;
		string str{ clients[client_id].name };
		wstring wstr = str_to_lcpw(str);
		my_db->Update_DB(wstr, clients[client_id].x, clients[client_id].y, clients[client_id].level, clients[client_id].exp, clients[client_id].hp);
		break;
	}
	}
}

void NPC_Reborn(int id)//아직
{
	clients[id].hp = clients[id].level * 5;
	clients[id].state_lock.lock();
	clients[id]._state = ST_INGAME;
	clients[id].state_lock.unlock();

	short x = rand() % WORLD_WIDTH;
	short y = rand() % WORLD_HEIGHT;
	
	clients[id].x = x;
	clients[id].y = y;

	clients[id].target = -1;

	auto new_viewlist = clients[id].viewlist;

	for (auto vl : new_viewlist)
	{
		if (false == is_npc(vl))
		{
			if (id == vl) continue;
			clients[vl].clinet_lock.lock();
			if (false == clients[vl]._state == ST_INGAME) {
				clients[vl].clinet_lock.unlock();
				continue;
			}
			else clients[vl].clinet_lock.unlock();
			if (true == is_near(id, vl))
			{
				send_put_object(vl, id);

	
				string msg = "[";
				msg += clients[id].name;
				msg += " REBORN]";
				char mess[50];
				strcpy_s(mess, msg.c_str());
				send_staus_message_packet(vl, mess, SEND_REBORN);
			}
		}
	}

}
void process_die_mess(int id)  
{
	string msg = "[";
	msg += clients[id].name;
	msg += " DIED]";
	char mess[50];
	strcpy_s(mess, msg.c_str());
	send_staus_message_packet(id, mess, SEND_DIE);
}
void NPC_Attack(int id)
{
	if (clients[id].hp <= 0) return;

	int target = clients[id].target;
	if (true == can_attack_range(id, target))
	{
		int curHp = clients[target].hp;
		int damage = 10;
		curHp -= damage;

		process_demage_mess(target, id, damage);

		if (curHp <= 0)
		{
			clients[id].target = -1;
			process_die(target, id);
			process_die_mess(target);
		}
		else //피가 줄었거나 줄고 있거나 
		{
			clients[target].hp = curHp;
			send_status_change_packet(target);
			add_timer(target, OP_HEALING, system_clock::now() + 5s);
			add_timer(id, OP_NPC_ATTACK, system_clock::now() + 1s);
		}
	}
}

void process_healing(int id)
{
	if (true == is_npc(id)) return;
	if (false == clients[id]._state==ST_INGAME) return;

	int fullHP = clients[id].level * 30;
	int hp = fullHP * 0.1 + clients[id].hp;
	if (hp > fullHP) hp = fullHP;
	clients[id].hp = hp;

	send_status_change_packet(id);

	string msg = "[";
	msg += clients[id].name;
	msg += " HEAL]";
	char mess[50];
	strcpy_s(mess, msg.c_str());
	send_staus_message_packet(id, mess, SEND_HEALING);


	if (hp < fullHP)
	{
		add_timer(id, OP_HEALING, system_clock::now() + 5s);
	}


}

void worker()
{
	for (;;) {
		DWORD num_byte;
		LONG64 iocp_key;
		WSAOVERLAPPED* p_over;
		BOOL ret = GetQueuedCompletionStatus(g_h_iocp, &num_byte, (PULONG_PTR)&iocp_key, &p_over, INFINITE);
		int client_id = static_cast<int>(iocp_key);
		EXP_OVER* exp_over = reinterpret_cast<EXP_OVER*>(p_over);
		if (FALSE == ret) {
			int err_no = WSAGetLastError();
			cout << "GQCS Error : ";
			error_display(err_no);
			cout << endl;
			Disconnect(client_id);
			if (exp_over->_comp_op == OP_SEND)
				delete exp_over;
			continue;
		}

		switch (exp_over->_comp_op) {
		case OP_RECV: {
			if (num_byte == 0) {
				Disconnect(client_id);
				continue;
			}
			CLIENT& cl = clients[client_id];
			int remain_data = num_byte + cl._prev_size;
			unsigned char* packet_start = exp_over->_net_buf;
			int packet_size = packet_start[0];

			while (packet_size <= remain_data) {
				process_packet(client_id, packet_start);
				remain_data -= packet_size;
				packet_start += packet_size;
				if (remain_data > 0) packet_size = packet_start[0];
				else break;
			}

			if (0 < remain_data) {
				cl._prev_size = remain_data;
				memcpy(&exp_over->_net_buf, packet_start, remain_data);
			}
			cl.do_recv();
			break;
		}
		case OP_SEND: {
			if (num_byte != exp_over->_wsa_buf.len) {
				Disconnect(client_id);
			}
			delete exp_over;
			break;
		}
		case OP_ACCEPT: {
			cout << "Accept Completed.\n";
			SOCKET c_socket = *(reinterpret_cast<SOCKET*>(exp_over->_net_buf));
			int new_id = get_new_id();
			if (-1 == new_id) {
				cout << "Maxmum user overflow. Accept aborted.\n";
			}
			else {
				CLIENT& cl = clients[new_id];
				cl.x = rand() % WORLD_WIDTH;
				cl.y = rand() % WORLD_HEIGHT;
				cl._id = new_id;
				cl._prev_size = 0;
				cl._recv_over._comp_op = OP_RECV;
				cl._recv_over._wsa_buf.buf = reinterpret_cast<char*>(cl._recv_over._net_buf);
				cl._recv_over._wsa_buf.len = sizeof(cl._recv_over._net_buf);
				ZeroMemory(&cl._recv_over._wsa_over, sizeof(cl._recv_over._wsa_over));
				cl._socket = c_socket;

				CreateIoCompletionPort(reinterpret_cast<HANDLE>(c_socket), g_h_iocp, new_id, 0);
				cl.do_recv();
			}

			ZeroMemory(&exp_over->_wsa_over, sizeof(exp_over->_wsa_over));
			c_socket = WSASocket(AF_INET, SOCK_STREAM, IPPROTO_TCP, 0, 0, WSA_FLAG_OVERLAPPED);
			*(reinterpret_cast<SOCKET*>(exp_over->_net_buf)) = c_socket;
			AcceptEx(g_s_socket, c_socket, exp_over->_net_buf + 8, 0, sizeof(SOCKADDR_IN) + 16,
				sizeof(SOCKADDR_IN) + 16, NULL, &exp_over->_wsa_over);
			break;
		}
		case OP_NPC_MOVE: {
			clients[client_id].m_lua.lock();
			lua_State* L = clients[client_id].L;
			lua_getglobal(L, "event_npc_escape");
			lua_pushnumber(L, exp_over->_target);
			lua_pcall(L, 1, 1, 0);

			Lua_check = lua_toboolean(L, -1);

			lua_pop(L, 1);

			if (Lua_check == true)   // 트루면 랜덤 이동하자  
				do_npc_move(client_id);
			else if (Lua_check == false) // false 면(3번 이동하면 ) 이동 그만해라 
				clients[client_id]._is_active = false;

			clients[client_id].m_lua.unlock();
			delete exp_over;
			break;
		}
		case OP_PLAYER_MOVE: {
			clients[client_id].m_lua.lock();
			lua_State* L = clients[client_id].L;
			lua_getglobal(L, "event_player_move");
			lua_pushnumber(L, exp_over->_target);
			lua_pcall(L, 1, 0, 0);

			clients[client_id].m_lua.unlock();
			delete exp_over;
			break;
		}
		case OP_NPC_REBORN: {
			NPC_Reborn(client_id);
			delete exp_over;
			break;
		}
		case  OP_NPC_ATTACK: {
			NPC_Attack(client_id);
			delete exp_over;
			break;
		}


		case OP_HEALING:
		{
			process_healing(client_id);
			delete exp_over;
			break;
		}
		}
	}
}


int API_SendMessage(lua_State* L)
{
	int my_id = (int)lua_tointeger(L, -3);
	int user_id = (int)lua_tointeger(L, -2);
	char* mess = (char*)lua_tostring(L, -1);
	lua_pop(L, 4);

	send_chat_packet(user_id, my_id, mess);  //메세지 보내고 
	
	if (0 == _strcmpi(mess, "HELLO")) { \
		if (clients[my_id]._is_active) return 0;
		timer_event t_event;
		t_event.obj_id = my_id;
		t_event.start_time = chrono::system_clock::now() + 1s;
		t_event.ev = EVENT_NPC_MOVE;
		t_event.target_id = user_id;
		timer_queue.push(t_event);
		clients[my_id]._is_active = true;
	}


	return 0;
}

int API_get_x(lua_State* L)
{
	int user_id = (int)lua_tointeger(L, -1);
	lua_pop(L, 2);
	int x = clients[user_id].x;
	lua_pushnumber(L, x);
	return 1;
}

int API_get_y(lua_State* L)
{
	int user_id =
		(int)lua_tointeger(L, -1);
	lua_pop(L, 2);
	int y = clients[user_id].y;
	lua_pushnumber(L, y);
	return 1;
}

void Initialize_NPC()
{
	for(int i = NPC_ID_START; i < NPC_ID_END + 1; ++i) {
		sprintf_s(clients[i].name,"Cat");
		
		clients[i].x = rand() % WORLD_WIDTH;      //좌표는 랜덤 뿌리고 
		clients[i].y = rand() % WORLD_HEIGHT;
		clients[i]._state = ST_INGAME;
		clients[i]._type = 1;
		clients[i]._is_active = false;
		clients[i].monster_type = PEACE_MONSTER;
		clients[i].monster_move_type = FIX_MONSTER;

		lua_State* L = clients[i].L = luaL_newstate();
		luaL_openlibs(L);

		int error = luaL_loadfile(L, "monster.lua") ||
			lua_pcall(L, 0, 0, 0);
		lua_getglobal(L, "set_uid");
		lua_pushnumber(L, i);
		error = lua_pcall(L, 1, 1, 0);
			clients[i]._id = i;

		lua_getglobal(L, "npc_data");
		lua_pcall(L, 0, 4, 0);
		char* name = (char*)lua_tostring(L, -4);
		short maxhp = (short)lua_tonumber(L, -3);
		short hp = (short)lua_tonumber(L, -2);
		int level = (int)lua_tonumber(L, -1);

		char real_name[50]{};
		sprintf_s(real_name, "%s%d", name, i);
		strcpy_s(clients[i].name, real_name);
		clients[i].maxhp = maxhp;
		clients[i].hp = hp;
		clients[i].level = level;

		lua_pop(L, 1);
		lua_register(L, "API_SendMessage", API_SendMessage);
		lua_register(L, "API_get_x", API_get_x);
		lua_register(L, "API_get_y", API_get_y);
	}

}

void do_npc_move(int npc_id)
{
	unordered_set<int> old_viewlist;
	unordered_set<int> new_viewlist;


	for (auto& obj : clients) {
		if (obj._state != ST_INGAME)
			continue;
		if (true == is_npc(obj._id))
			break;
		if (true == is_near(npc_id, obj._id))
			old_viewlist.insert(obj._id);
	}
	if (old_viewlist.size() == 0) return;
	auto& x = clients[npc_id].x;
	auto& y = clients[npc_id].y;
	switch (rand() % 4)
	{
	case 0: if (y > 0) y--; break;
	case 1: if (y < WORLD_HEIGHT) y++; break;
	case 2: if (x > 0) x--; break;
	case 3: if (x < WORLD_WIDTH) x++; break;
	default:
		break;
	}

	for (auto& obj : clients) {
		if (obj._state != ST_INGAME) 
			continue;  
		if (true == is_npc(obj._id))
			break;  
		if (true == is_near(npc_id, obj._id)) {
			new_viewlist.insert(obj._id);
		}
	}

	int player = 0;
	// 새로 시야에 들어온 플레이어
	for (auto pl : new_viewlist) {
		if (0 == old_viewlist.count(pl)) {
			clients[pl].vl.lock();
			clients[pl].viewlist.insert(npc_id);
			clients[pl].vl.unlock();
			send_put_object(pl, npc_id);
		}
		else {
			send_move_packet(pl, npc_id);
		}
		player = pl;
	}

	// 시야에 사라진 경우
	for (auto pl : old_viewlist) {
		if (0 == new_viewlist.count(pl)) {
			clients[pl].vl.lock();
			clients[pl].viewlist.erase(npc_id);
			clients[pl].vl.unlock();
			send_remove_object(pl, npc_id);
		}
	}

	if (new_viewlist.size() == 0) {  
		return;
	}
	timer_event t_event;
	t_event.obj_id = npc_id;
	t_event.start_time = chrono::system_clock::now() + 1s;
	t_event.ev = EVENT_NPC_MOVE;
	t_event.target_id = player; // player id 
	timer_queue.push(t_event);
}

void do_timer() {

	int id = 0;
	timer_event t;
	bool check = false;
	chrono::system_clock::duration sub;
	while (true)
	{
		if (check == true) {    // npc 움직이자 
			
			check = false;
			EXP_OVER* exp_over = new EXP_OVER;
			exp_over->_target = id;  // target 정해줘야해
			exp_over->_comp_op = OP_NPC_MOVE;
			PostQueuedCompletionStatus(g_h_iocp, 1, t.obj_id, &exp_over->_wsa_over);
		}
		while (true)
		{

			if (false == timer_queue.empty())     //안비었으면 들어감
			{
				timer_event t_event;
				timer_queue.try_pop(t_event); //제일 위값 
				sub = t_event.start_time - chrono::system_clock::now();

				if (sub <= 0ms) { // npc 움직이자
					EXP_OVER* exp_over = new EXP_OVER;
					exp_over->_target = id;
					exp_over->_comp_op = OP_NPC_MOVE;
					PostQueuedCompletionStatus(g_h_iocp, 1, t_event.obj_id, &exp_over->_wsa_over);
				}
				else if (sub <= 10ms) {
					check = true;
					t = t_event;
					break;

				}

				else {
					timer_queue.push(t_event);
					break;
				}
				
			}
			//else
			//
				// timer_lock.unlock();
			//	break;
			//}
		}
		this_thread::sleep_for(sub);
	}
}
void DB_Update_thread()
{
	my_db->ResetLogin();

	while (true)
	{
		while (true)
		{
			DB_lock.lock();
			if (false == DB_queue.empty())
			{
				DB_event ev = DB_queue.top();

				if (ev.start_time > system_clock::now()) {
					DB_lock.unlock();
					break;
				}

				DB_queue.pop();
				DB_lock.unlock();

				if (ev.target_id == DB_UPDATE)
				{
					string str{ clients[ev.id].name };
					wstring wstr = str_to_lcpw(str);
					my_db->Update_DB(wstr, clients[ev.id].x, clients[ev.id].y, clients[ev.id].level, clients[ev.id].exp, clients[ev.id].hp);
				}
			
			}

			DB_lock.unlock();
			break;

		}
		this_thread::sleep_for(1ms);
	}
}

int main()
{

	my_db = new DataBase();


	wcout.imbue(locale("korean"));
	WSADATA WSAData;
	WSAStartup(MAKEWORD(2, 2), &WSAData);
	g_s_socket = WSASocket(AF_INET, SOCK_STREAM, IPPROTO_TCP, 0, 0, WSA_FLAG_OVERLAPPED);
	SOCKADDR_IN server_addr;
	ZeroMemory(&server_addr, sizeof(server_addr));
	server_addr.sin_family = AF_INET;
	server_addr.sin_port = htons(SERVER_PORT);
	server_addr.sin_addr.s_addr = htonl(INADDR_ANY);
	bind(g_s_socket, reinterpret_cast<sockaddr*>(&server_addr), sizeof(server_addr));
	listen(g_s_socket, SOMAXCONN);

	g_h_iocp = CreateIoCompletionPort(INVALID_HANDLE_VALUE, NULL, NULL, 0);
	CreateIoCompletionPort(reinterpret_cast<HANDLE>(g_s_socket), g_h_iocp, 0, 0);

	SOCKET c_socket = WSASocket(AF_INET, SOCK_STREAM, IPPROTO_TCP, 0, 0, WSA_FLAG_OVERLAPPED);
	char	accept_buf[sizeof(SOCKADDR_IN) * 2 + 32 + 100];
	EXP_OVER	accept_ex;
	*(reinterpret_cast<SOCKET*>(&accept_ex._net_buf)) = c_socket;
	ZeroMemory(&accept_ex._wsa_over, sizeof(accept_ex._wsa_over));
	accept_ex._comp_op = OP_ACCEPT;

	AcceptEx(g_s_socket, c_socket, accept_buf, 0, sizeof(SOCKADDR_IN) + 16,
		sizeof(SOCKADDR_IN) + 16, NULL, &accept_ex._wsa_over);
	cout << "Accept Called\n";



	cout << "Creating Worker Threads\n";

	Initialize_NPC();

	vector <thread> worker_threads;
	thread timer_thread{ do_timer };

	thread DBConnect_thread{ DB_Update_thread };//추가 

	for (int i = 0; i < 12; ++i)
		worker_threads.emplace_back(worker);
	for (auto& th : worker_threads)
		th.join();

	//ai_thread.join();
	timer_thread.join();
	DBConnect_thread.join();
	for (auto& cl : clients) {
		if (ST_INGAME == cl._state)
			Disconnect(cl._id);
	}
	closesocket(g_s_socket);
	WSACleanup();
}
