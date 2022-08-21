#define SFML_STATIC 1
#include <SFML/Graphics.hpp>
#include <SFML/Network.hpp>
#include <iostream>
#include <chrono>
#include <fstream>
#include "protocol.h"
#include <unordered_map>
using namespace std;

#ifdef _DEBUG
#pragma comment (lib, "lib/sfml-graphics-s-d.lib")
#pragma comment (lib, "lib/sfml-window-s-d.lib")
#pragma comment (lib, "lib/sfml-system-s-d.lib")
#pragma comment (lib, "lib/sfml-network-s-d.lib")
#else
#pragma comment (lib, "lib/sfml-graphics-s.lib")
#pragma comment (lib, "lib/sfml-window-s.lib")
#pragma comment (lib, "lib/sfml-system-s.lib")
#pragma comment (lib, "lib/sfml-network-s.lib")
#endif
#pragma comment (lib, "opengl32.lib")
#pragma comment (lib, "winmm.lib")
#pragma comment (lib, "ws2_32.lib")

//#include "..\..\iocp_single\iocp_single\protocol.h"

sf::TcpSocket socket;


constexpr auto BUF_SIZE = 256;
constexpr auto SCREEN_WIDTH = 20;
constexpr auto SCREEN_HEIGHT = 20;

constexpr auto TILE_WIDTH = 64;
constexpr auto WINDOW_WIDTH = TILE_WIDTH * SCREEN_WIDTH  /1.5+ 10;   // size of window
constexpr auto WINDOW_HEIGHT = TILE_WIDTH * SCREEN_WIDTH /1.5+ 10;
//constexpr auto BUF_SIZE = MAX_BUFFER;

int g_myid;
int g_x_origin;
int g_y_origin;

sf::RenderWindow* g_window;
sf::Font g_font;
bool CHAT_BOOL = false;

char name[10];// 로그인 시 입력 아이디 

class OBJECT {
private:
	bool m_showing;
	sf::Sprite m_sprite;
	sf::Text m_name;
	sf::Text m_chat;

	chrono::system_clock::time_point m_mess_end_time;
public:
	int m_x, m_y;
	short m_hp, m_maxhp;
	int m_exp;
	short m_level;
	char name[15];
	OBJECT(sf::Texture& t, int x, int y, int x2, int y2) {
		m_showing = false;
		m_sprite.setTexture(t);
		m_sprite.setTextureRect(sf::IntRect(x, y, x2, y2));
		set_name("NONAME");
		m_mess_end_time = chrono::system_clock::now();
	}
	OBJECT() {
		m_showing = false;
	}
	void show()
	{
		m_showing = true;
	}
	void hide()
	{
		m_showing = false;
	}

	void a_move(int x, int y) {
		m_sprite.setPosition((float)x, (float)y);
	}

	void a_draw() {
		g_window->draw(m_sprite);
	}

	void move(int x, int y) {
		m_x = x;
		m_y = y;
	}
	void draw() {
		if (false == m_showing) return;
		float rx = (m_x - g_x_origin) * 65.0f + 8;
		float ry = (m_y - g_y_origin) * 65.0f + 8;
		m_sprite.setPosition(rx, ry);
		g_window->draw(m_sprite);
		if (m_mess_end_time < chrono::system_clock::now()) {
			m_name.setPosition(rx - 10, ry - 20);
			g_window->draw(m_name);
		}
		else {
			m_chat.setPosition(rx - 10, ry - 20);
			g_window->draw(m_chat);
		}
	}
	void set_name(const char str[]) {
		m_name.setFont(g_font);
		m_name.setString(str);
		m_name.setFillColor(sf::Color(255, 255, 0));
		m_name.setStyle(sf::Text::Bold);
	}
	void set_chat(const char str[]) {
		m_chat.setFont(g_font);
		m_chat.setString(str);
		m_chat.setFillColor(sf::Color(0, 0, 0));
		m_chat.setStyle(sf::Text::Bold);
		m_mess_end_time = chrono::system_clock::now() + chrono::seconds(5);  //5초
	}
};
class UI   //hp와 레벨, exp 등 띄우자 
{
private:
	int x, y;
	bool m_showing;
	sf::Text message;

public:
	UI()
	{
		m_showing = true;
		x = 450;
		y = 0;
	}

	void draw()
	{
		if (false == m_showing) return;
		message.setPosition(x, y);
		g_window->draw(message);
	}

	void add_chat(char chat[]) {
		message.setFont(g_font);
		message.setString(chat);
		message.setFillColor(sf::Color(200, 0, 0));
		message.setCharacterSize(40);
		message.setStyle(sf::Text::Bold);
	}
};

struct UI_TextTime {
	sf::Text text;
	chrono::high_resolution_clock::time_point time_over;
};
class Board   //상태 메세지 밑에 띄우자 
{
private:
	bool m_showing;
	char m_mess[100];
	list<UI_TextTime> m_text;
public:
	int m_x, m_y;
	Board() {
		m_showing = true;
		m_x = 0;
		m_y = 1400;
	}


	void draw() {
		if (false == m_showing) return;
		int space = 0; 
		for (list<UI_TextTime>::iterator time = m_text.begin(); time != m_text.end(); time)
		{
			space -= 20;
			if (chrono::high_resolution_clock::now() < time->time_over) {
				time->text.setPosition(m_x, m_y + space);  //간격을 계속 띄우자 
				g_window->draw(time->text);
				time++;
			}
			else
				m_text.erase(time++);  //지우자 
		}
	}

	void add_chat(char chat[], int type) {
		UI_TextTime time;
		time.text.setFont(g_font);
		switch (type)
		{
		case SEND_DIE:
			time.text.setFillColor(sf::Color(200, 200, 200));  // 죽으면 흰색
			break;
		case SEND_DAMAGE:
			time.text.setFillColor(sf::Color(200, 0, 0));// 데미지관련 빨간색
			break;
		case SEND_LEVELUP:
			time.text.setFillColor(sf::Color(200, 200, 0));// 레벨업면 노란색
			break;
		case SEND_REBORN:
			time.text.setFillColor(sf::Color(0, 200, 0));// 부활은 초록색
			break;
		default:
			break;
		}
		time.text.setString(chat);
		time.text.setStyle(sf::Text::Bold);
		time.time_over = chrono::high_resolution_clock::now() + 3s;  //3초후에 사라짐

		m_text.emplace_front(time);
	}
};
OBJECT avatar;  //나 
unordered_map <int, OBJECT> npcs;	// 새로운 캐릭터가 들어오면 id - 객체로 관리
OBJECT players[MAX_USER + MAX_NPC];

OBJECT white_tile;
OBJECT black_tile;

sf::Texture* board;
sf::Texture* pieces;
sf::Texture* upgrade; //레벨업 시 진화 png

UI Game_UI; //ui
Board status_message;


OBJECT tile[10];
void client_initialize()
{
	board = new sf::Texture;
	pieces = new sf::Texture;
	upgrade = new sf::Texture;
	if (false == g_font.loadFromFile("cour.ttf")) {
		cout << "Font Loading Error!\n";
		while (true);
	}
	board->loadFromFile("chessmap.bmp");
	pieces->loadFromFile("chess2.png");
	upgrade->loadFromFile("grey.png");
	white_tile = OBJECT{ *board, 5, 5, TILE_WIDTH, TILE_WIDTH };
	black_tile = OBJECT{ *board, 69, 5, TILE_WIDTH, TILE_WIDTH };
	//avatar = OBJECT{ *pieces, 128, 0, 64, 64 };
	for (auto& pl : players) {
		pl = OBJECT{ *pieces, 45, 0, 45, 64 };
	}


	avatar = OBJECT{ *pieces, 0, 0, 45, 64 }; //조절
}
void send_packet(void* packet)  //  
{
	char* p = reinterpret_cast<char*>(packet);
	size_t sent;
	socket.send(p, p[0], sent);
}



void ProcessPacket(char* ptr)
{
	static bool first_time = true;
	switch (ptr[1])
	{
	case SC_PACKET_LOGIN_OK:
	{
		sc_packet_login_ok* packet = reinterpret_cast<sc_packet_login_ok*>(ptr);
		g_myid = packet->id;
		avatar.m_x = packet->x;
		avatar.m_y = packet->y;
		avatar.m_hp = packet->hp;
		avatar.m_maxhp = packet->maxhp;
		avatar.m_level = packet->level;
		avatar.m_exp = packet->exp;
		g_x_origin = packet->x - SCREEN_WIDTH / 2;
		g_y_origin = packet->y - SCREEN_WIDTH / 2;
		avatar.move(packet->x, packet->y);
		avatar.show();
	}
	break;
	case SC_PACKET_PUT_OBJECT:  //여기수정필요 
	{
		sc_packet_put_object* my_packet = reinterpret_cast<sc_packet_put_object*>(ptr);
		int id = my_packet->id;

		if (id == g_myid) {//나는 아구몬 
		//	players[id].set_name(my_packet->name);
			players[id].move(my_packet->x, my_packet->y);
			g_x_origin = my_packet->x - SCREEN_WIDTH / 2;
			g_y_origin = my_packet->y - SCREEN_WIDTH / 2;
			players[id].show();
		}
		else if (id != g_myid && id < MAX_USER) { // 다른 플레이어는 텐타몬 
			players[id] = OBJECT{ *pieces, 83, 0, 52, 64 };
			players[id].set_name(my_packet->name);
			players[id].move(my_packet->x, my_packet->y);
			players[id].show();
		}
		else {  // NPC  일단 가트몬 
			players[id].set_name(my_packet->name);
			players[id].move(my_packet->x, my_packet->y);
			players[id].show();
		}
		break;
	}
	case SC_PACKET_MOVE:
	{
		sc_packet_move* my_packet = reinterpret_cast<sc_packet_move*>(ptr);
		int other_id = my_packet->id;
		if (other_id == g_myid) {
			avatar.move(my_packet->x, my_packet->y);
			g_x_origin = my_packet->x - SCREEN_WIDTH / 2;
			g_y_origin = my_packet->y - SCREEN_WIDTH / 2;
		}
		else if (other_id < MAX_USER) {
			players[other_id].move(my_packet->x, my_packet->y);
		}
		else {
			players[other_id].move(my_packet->x, my_packet->y);
		}
		break;
	}

	case SC_PACKET_REMOVE_OBJECT:
	{
		sc_packet_remove_object* my_packet = reinterpret_cast<sc_packet_remove_object*>(ptr);
		int other_id = my_packet->id;
		if (other_id == g_myid) {
			avatar.hide();
		}
		else if (other_id < MAX_USER) {
			players[other_id].hide();
		}
		else {
			players[other_id].hide();
		}
		break;
	}
	case SC_PACKET_CHAT:
	{
		sc_packet_chat* my_packet = reinterpret_cast<sc_packet_chat*>(ptr);
		int other_id = my_packet->id;
		if (other_id == g_myid) {
			avatar.set_chat(my_packet->message);
		}
		else if (other_id < MAX_USER) {
			players[other_id].set_chat(my_packet->message);
		}
		else {
			players[other_id].set_chat(my_packet->message);
		}
		break;
	}
	case SC_PACKET_LOGIN_FAIL:
	{
		sc_packet_login_fail* my_packet = reinterpret_cast<sc_packet_login_fail*>(ptr);
		int why{ my_packet->reason };
		if (why == 0) {
			cout << "중복 ID입니다." << endl;
			cout << "다른 ID를 입력하세요: ";
			cin >> name;

			cs_packet_login l_packet;
			l_packet.size = sizeof(l_packet);
			l_packet.type = CS_PACKET_LOGIN;
			sprintf_s(l_packet.name, "%s", name);
			strcpy_s(avatar.name, l_packet.name);
			avatar.set_name(l_packet.name);
			send_packet(&l_packet);
			//cs_packet_login 다시 보내기 
		}
		else {  //1이면 
			cout << "사용자가 꽉차 이용할 수 없습니다." << endl;
			return;
		}

	
		break;
	}
	case SC_PACKET_STATUS_CHANGE:
	{
		sc_packet_status_change* p = reinterpret_cast<sc_packet_status_change*>(ptr);

		string str;
		str += "ID: ";
		str += name;
		str += " LV.";
		str += to_string(p->level);
		str += " HP: " + to_string(p->hp);
		str += " EXP: " + to_string(p->exp);

		char c[50];
		strcpy_s(c, str.c_str());
		Game_UI.add_chat(c);
		//if (p->level == 2) {  //그레이몬으로 바꾸고싶은데 잘 안되네 
		//	avatar = OBJECT{ *upgrade, 0, 0, 45, 64 }; //조절
		//	avatar.draw();
		//}
		break;
	}
	case SC_PACKET_STATUS_MESSAGE:
	{
		sc_packet_status_message* my_packet = reinterpret_cast<sc_packet_status_message*>(ptr);
		status_message.add_chat(my_packet->message, my_packet->mess_type);
	}
	default:break;
	//	printf("Unknown PACKET type [%d]\n", ptr[1]);
	}
}

void process_data(char* net_buf, size_t io_byte)
{
	char* ptr = net_buf;
	static size_t in_packet_size = 0;
	static size_t saved_packet_size = 0;
	static char packet_buffer[BUF_SIZE];

	while (0 != io_byte) {
		if (0 == in_packet_size) in_packet_size = ptr[0];
		if (io_byte + saved_packet_size >= in_packet_size) {
			memcpy(packet_buffer + saved_packet_size, ptr, in_packet_size - saved_packet_size);
			ProcessPacket(packet_buffer);
			ptr += in_packet_size - saved_packet_size;
			io_byte -= in_packet_size - saved_packet_size;
			in_packet_size = 0;
			saved_packet_size = 0;
		}
		else {
			memcpy(packet_buffer + saved_packet_size, ptr, io_byte);
			saved_packet_size += io_byte;
			io_byte = 0;
		}
	}
}

bool client_main()
{
	char net_buf[BUF_SIZE];
	size_t	received;

	auto recv_result = socket.receive(net_buf, BUF_SIZE, received);
	if (recv_result == sf::Socket::Error)
	{
		wcout << L"Recv 에러!";
		while (true);
	}
	if (recv_result == sf::Socket::Disconnected)
	{
		wcout << L"서버 접속 종료.\n";
		return false;
	}
	if (recv_result != sf::Socket::NotReady)
		if (received > 0) process_data(net_buf, received);

	for (int i = 0; i < SCREEN_WIDTH; ++i)
		for (int j = 0; j < SCREEN_HEIGHT; ++j)
		{
			int tile_x = i + g_x_origin;
			int tile_y = j + g_y_origin;
			if ((tile_x < 0) || (tile_y < 0)) continue;
			if ((((tile_x / 3) + (tile_y / 3)) % 2) == 1) {
				white_tile.a_move(TILE_WIDTH * i + 7, TILE_WIDTH * j + 7);
				white_tile.a_draw();
			}
			else
			{
				black_tile.a_move(TILE_WIDTH * i + 7, TILE_WIDTH * j + 7);
				black_tile.a_draw();
			}
		}
	avatar.draw();
	for (auto& pl : players) pl.draw();
	for (auto& npc : npcs) npc.second.draw();
	sf::Text text;
	text.setFont(g_font);
	char buf[100];
	sprintf_s(buf, "(%d, %d)", avatar.m_x, avatar.m_y);
	text.setString(buf);
	text.setCharacterSize(45);
	g_window->draw(text);
	status_message.draw();
	Game_UI.draw();
	return true;
}

void send_move_packet(char dr)
{
	cs_packet_move packet;
	packet.size = sizeof(packet);
	packet.type = CS_PACKET_MOVE;
	packet.direction = dr;
	size_t sent = 0;
	socket.send(&packet, sizeof(packet), sent);
}

void send_login_packet(string& name)
{
	cs_packet_login packet;
	packet.size = sizeof(packet);
	packet.type = CS_PACKET_LOGIN;
	strcpy_s(packet.name, name.c_str());
	size_t sent = 0;
	socket.send(&packet, sizeof(packet), sent);
}

void send_attack_packet()
{
	cs_packet_attack packet;
	packet.size = sizeof(packet);
	packet.type = CS_PACKET_ATTACK;
	size_t sent = 0;
	socket.send(&packet, sizeof(packet), sent);
}
void send_update_packet(int id)
{
	cs_packet_update_packet packet;
	packet.size = sizeof(packet);
	packet.type = CS_UPDATE;
	packet.update_type = 3;
	packet.id = id;
	size_t sent = 0;
	socket.send(&packet, sizeof(packet), sent);
}



void input_message()
{
	string chat;
	cout << "[채팅]: ";
	cin >> chat;
	cs_packet_chat p;
	p.size = sizeof(p);
	p.type = CS_PACKET_CHAT;
	strcpy_s(p.message, chat.c_str());
	send_packet(&p);

	CHAT_BOOL = 1 - CHAT_BOOL;
}

int main()
{
	wcout.imbue(locale("korean"));
	sf::Socket::Status status = socket.connect("127.0.0.1", SERVER_PORT);


	socket.setBlocking(false);

	if (status != sf::Socket::Done) {
		wcout << L"서버와 연결할 수 없습니다.\n";
		while (true);
	}

	client_initialize();

	
	cout << "ID를 입력하세요: ";
	
	cin >> name;
	cs_packet_login l_packet;
	l_packet.size = sizeof(l_packet);
	l_packet.type = CS_PACKET_LOGIN;
	sprintf_s(l_packet.name, "%s", name);
	strcpy_s(avatar.name, l_packet.name);
	avatar.set_name(l_packet.name);
	send_packet(&l_packet);

	sf::RenderWindow window(sf::VideoMode(WINDOW_WIDTH, WINDOW_HEIGHT), "server term");
	g_window = &window;

	sf::View view = g_window->getView();
	view.zoom(2.f);
	view.move(SCREEN_WIDTH * TILE_WIDTH / 4, SCREEN_HEIGHT * TILE_WIDTH / 4);
	g_window->setView(view);

	while (window.isOpen())
	{
		sf::Event event;
		while (window.pollEvent(event))
		{
			if (event.type == sf::Event::Closed)
				window.close();
			if (event.type == sf::Event::KeyPressed) {
				int direction = -1;
				switch (event.key.code) {
				case sf::Keyboard::Left:
					direction = 2;
					send_move_packet(direction);
					break;
				case sf::Keyboard::Right:
					direction = 3;
					send_move_packet(direction);
					break;
				case sf::Keyboard::Up:
					direction = 0;
					send_move_packet(direction);
					break;
				case sf::Keyboard::Down:
					direction = 1;
					send_move_packet(direction);
					break;
				case sf::Keyboard::Escape:
					window.close();
					break;
				case sf::Keyboard::A:  //공격 
					send_attack_packet();
					break;
				case sf::Keyboard::C:  //채팅
					CHAT_BOOL = 1 - CHAT_BOOL;
					if (CHAT_BOOL)
						input_message();
					break;
				case sf::Keyboard::S:    //DB저장
					send_update_packet(g_myid);
					break;
				}
						
				//if (-1 != direction) send_move_packet(direction);
			}
		}

		window.clear();
		if (false == client_main())
			window.close();
		window.display();
	}
	delete board;
	delete pieces;
	delete upgrade;

	return 0;
}