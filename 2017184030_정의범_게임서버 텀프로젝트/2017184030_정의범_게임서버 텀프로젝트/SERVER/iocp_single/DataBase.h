#pragma once
#include <windows.h>  
#include <stdio.h>  
#include <sqlext.h>  
#include <iostream>
#pragma comment(lib, "odbc32.lib")

using namespace std;

class DataBase
{
public:
	DataBase();   //여기서 연결시키자 
	~DataBase();

	void HandleDiagnosticRecord(SQLHANDLE hHandle, SQLSMALLINT hType, RETCODE RetCode);  //오류출력하자 
	void SetLogin(wstring id);
	void ResetLogin();
	bool GetData(wstring id, short* posX, short* posY, int* level, int* exp, short* HP);
	void Update_DB(wstring id, short posX, short posY, short level, int exp, short HP);
	void Add_DB(wstring id);



private:
	SQLHENV henv;
	SQLHDBC hdbc;
	SQLHSTMT hstmt = 0;
	SQLRETURN retcode;

	SQLINTEGER  p_Level, p_Exp, p_Login;
	SQLSMALLINT p_X, p_Y, p_HP;
	SQLLEN cbID = 0, cbPosX = 0, cbPosY = 0, cbLevel = 0, cbExp = 0, cbIsLogin = 0, cbHP = 0;

};

