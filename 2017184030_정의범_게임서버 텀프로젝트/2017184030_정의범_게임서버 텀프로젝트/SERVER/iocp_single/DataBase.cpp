#include "DataBase.h"
#include <string>
using namespace std;
DataBase::DataBase()  //������ ���̽� ���� 
{



	retcode = SQLAllocHandle(SQL_HANDLE_ENV, SQL_NULL_HANDLE, &henv);
	retcode = SQLSetEnvAttr(henv, SQL_ATTR_ODBC_VERSION, (SQLPOINTER*)SQL_OV_ODBC3, 0);
	retcode = SQLAllocHandle(SQL_HANDLE_DBC, henv, &hdbc);
	SQLSetConnectAttr(hdbc, SQL_LOGIN_TIMEOUT, (SQLPOINTER)5, 0);
	retcode = SQLConnect(hdbc, (SQLWCHAR*)L"2017184030_TERM", SQL_NTS, (SQLWCHAR*)NULL, SQL_NTS, NULL, SQL_NTS);
	if (retcode == SQL_SUCCESS || retcode == SQL_SUCCESS_WITH_INFO)
	{
		cout << "DB ���� �Ϸ�" << endl;
	}
	else
		cout << "DB ���� ����" << endl;
	
}

DataBase::~DataBase()
{
	SQLCancel(hstmt);
	SQLFreeHandle(SQL_HANDLE_STMT, hstmt);
	SQLDisconnect(hdbc);
	SQLFreeHandle(SQL_HANDLE_DBC, hdbc);
	SQLFreeHandle(SQL_HANDLE_ENV, henv);

}


void DataBase::HandleDiagnosticRecord(SQLHANDLE hHandle, SQLSMALLINT hType, RETCODE RetCode)  //���� �������
{
	SQLSMALLINT iRec = 0;
	SQLINTEGER iError;
	WCHAR wszMessage[1000];
	WCHAR wszState[SQL_SQLSTATE_SIZE + 1];

	if (RetCode == SQL_INVALID_HANDLE) {
		fwprintf(stderr, L"Invalid handle!\n");
		return;
	}
	while (SQLGetDiagRec(hType, hHandle, ++iRec, wszState, &iError, wszMessage,
		(SQLSMALLINT)(sizeof(wszMessage) / sizeof(WCHAR)), (SQLSMALLINT*)NULL) == SQL_SUCCESS) {
		if (wcsncmp(wszState, L"01004", 5)) {
			fwprintf(stderr, L"[%5.5s] %s (%d)\n", wszState, wszMessage, iError);
		}
	}
}  

void DataBase::SetLogin(wstring id)
{
	wstring qu{};
	qu += L"EXEC Set_Connect ";  //��� 1�� ������ 
	qu += id;

	retcode = SQLAllocHandle(SQL_HANDLE_STMT, hdbc, &hstmt);
	retcode = SQLExecDirect(hstmt, (SQLWCHAR*)qu.c_str(), SQL_NTS);
	if (retcode == SQL_SUCCESS || retcode == SQL_SUCCESS_WITH_INFO)
	{
		cout << "�α��� ���� Ȯ�� �Ϸ�" << endl;
	}
	else
		cout << "�α��� ���� Ȯ�� ����" << endl;

}

void DataBase::ResetLogin()
{
	wstring qu{};
	qu += L"EXEC Reset_connet ";
	retcode = SQLAllocHandle(SQL_HANDLE_STMT, hdbc, &hstmt);
	retcode = SQLExecDirect(hstmt, (SQLWCHAR*)qu.c_str(), SQL_NTS);
	if (retcode == SQL_SUCCESS || retcode == SQL_SUCCESS_WITH_INFO)
	{
		cout << "���� ������ �ʱ�ȭ �Ϸ�" << endl;
	}
	else
		cout << "���� ������ �ʱ�ȭ ����" << endl;
}

void DataBase::Add_DB(wstring id)
{
	wstring qu{};
	qu += L"EXEC Add_UserData ";
	qu += id;
	qu += L", 10, 10, 1, 0, 1, 100";  //x,y ,����, ����ġ, �α���, hp

	retcode = SQLAllocHandle(SQL_HANDLE_STMT, hdbc, &hstmt);
	retcode = SQLExecDirect(hstmt, (SQLWCHAR*)qu.c_str(), SQL_NTS);
	if (retcode == SQL_SUCCESS || retcode == SQL_SUCCESS_WITH_INFO)
	{
		cout << "�� ���� ������ DB�� �߰��߽��ϴ�." << endl;
	}
	else
		cout << "�� ���� ������ DB�� �߰��ϴµ��� �����߽��ϴ�." << endl;

}



bool DataBase::GetData(wstring id, short* posX, short* posY, int* level, int* exp, short* HP)
{
	wstring qu{};
	qu += L"EXEC GetUserData ";
	qu += id;

	retcode = SQLAllocHandle(SQL_HANDLE_STMT, hdbc, &hstmt);
	                                  //���� id�ִ°� ������ 
	retcode = SQLExecDirect(hstmt, (SQLWCHAR*)qu.c_str(), SQL_NTS);
	if (retcode == SQL_SUCCESS || retcode == SQL_SUCCESS_WITH_INFO)
	{
		retcode = SQLBindCol(hstmt, 1, SQL_C_SHORT, &p_X, 10, &cbPosX);
		retcode = SQLBindCol(hstmt, 2, SQL_C_SHORT, &p_Y, 10, &cbPosY);
		retcode = SQLBindCol(hstmt, 3, SQL_C_LONG, &p_Level, 10, &cbLevel);
		retcode = SQLBindCol(hstmt, 4, SQL_C_LONG, &p_Exp, 10, &cbExp);
		retcode = SQLBindCol(hstmt, 5, SQL_C_LONG, &p_Login, 10, &cbIsLogin);
		retcode = SQLBindCol(hstmt, 6, SQL_C_SHORT, &p_HP, 10, &cbHP);

		retcode = SQLFetch(hstmt);	//SQLFetch�� ����ؼ� ���ϰ��� �޴´�
		if (retcode == SQL_ERROR)
			HandleDiagnosticRecord(hstmt, retcode, SQL_HANDLE_STMT);

		if (retcode == SQL_SUCCESS || retcode == SQL_SUCCESS_WITH_INFO)
		{
			if (p_Login == 1) return false;	// �̹� �α��� �� ���̵�

			*posX = p_X;
			*posY = p_Y;
			*level = p_Level;
			*exp = p_Exp;
			*HP = p_HP;

			SetLogin(id);

			return true;
		}
		else  //������ �߰����� 
		{
			Add_DB(id);   //db�� �߰����� 
			*posX = 10;//���⼱ �÷����ϰ� ���ְ� 
			*posY = 10;
			*level = 1;
			*exp = 0;
			*HP = 100;
			return true;
		}
	}
}

void DataBase::Update_DB(wstring id, short posX, short posY, short level, int exp, short HP)
{
	wstring qu{};

	qu += L"EXEC update_DB ";
	qu += id;
	qu += L", ";
	qu += to_wstring(posX);
	qu += L", ";
	qu += to_wstring(posY);
	qu += L", ";
	qu += to_wstring(level);
	qu += L", ";
	qu += to_wstring(exp);
	qu += L", ";
	qu += to_wstring(HP);

	retcode = SQLAllocHandle(SQL_HANDLE_STMT, hdbc, &hstmt);
	retcode = SQLExecDirect(hstmt, (SQLWCHAR*)qu.c_str(), SQL_NTS);
	if (retcode == SQL_SUCCESS || retcode == SQL_SUCCESS_WITH_INFO)
	{
		cout << "update �Ϸ�!" << endl;
	
	}
	else
		cout << "update ����" << endl;

}

