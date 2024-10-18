#include<stdio.h>
#include <string.h>
#include <stdlib.h>
#include <time.h>
#include<WinSock2.h>
#pragma comment(lib,"ws2_32.lib")

#define MAX_CLIENTS 100 // 最大客户端数量

// 全局变量用于存储所有客户端的套接字和ID
struct ClientInfo {
	SOCKET socket;
	int id;
};

// 初始化全局变量
struct ClientInfo clients[MAX_CLIENTS];
int client_count = 1; // 初始化用户ID

DWORD WINAPI recv_thread_func(LPVOID lpThreadParameter)
{
	int* threadParams = (int*)lpThreadParameter;
	int client_socket = threadParams[0];
	int client_id = threadParams[1];  // 获取客户端ID

	// 输出欢迎信息
	char welcome_message[1024];
	sprintf(welcome_message, "欢迎进入聊天室", client_id);
	printf("%s\n", welcome_message);

	while (1)
	{
		char rbuffer[1024] = { 0 }; // 接受缓冲区
		int ret = recv(client_socket, rbuffer, 1024, 0);
		if (ret <= 0)
		{
			break;
		}
		printf("%s\n", rbuffer);
	}
	printf("Receive thread: socket %llu, disconnect.\n", client_socket);
	return 0;
}

DWORD WINAPI send_thread_func(LPVOID lpThreadParameter)
{
	int* threadParams = (int*)lpThreadParameter;
	int client_socket = threadParams[0];
	int client_id = threadParams[1]; // 获取客户端ID

	while (1)
	{
		char sbuffer[1024] = { 0 }; // 发送缓冲区
		//printf("please enter: ");
		fgets(sbuffer, 1024, stdin); // 使用 fgets 来读取带有换行符的输入

		// 移除换行符
		sbuffer[strcspn(sbuffer, "\n")] = 0;

		// 检查用户是否输入了 "quit"
		if (strcmp(sbuffer, "quit") == 0)
		{
			send(client_socket, "QUIT", 4, 0); // 发送退出信号
			printf("您已结束聊天！\n");
			break;
		}

		// 检查是否为私聊消息
		if (strncmp(sbuffer, "PRIVATE:", 8) == 0)
		{
			// 发送私聊消息
			send(client_socket, sbuffer, strlen(sbuffer), 0);
		}
		else
		{
			// 发送用户输入的数据
			send(client_socket, sbuffer, strlen(sbuffer), 0);
		}
	}
	//printf("Send thread: socket %llu, disconnect.\n", client_socket);
	return 0;
}

int main()
{
	WSADATA wsaData;
	if (WSAStartup(MAKEWORD(2, 2), &wsaData) != 0)
	{
		printf("开启网络权限失败 %d\n", GetLastError());
		return -1;
	}

	//1.创建socket套接字
	SOCKET client_socket = socket(AF_INET, SOCK_STREAM, 0);
	if (INVALID_SOCKET == client_socket) {
		printf("创建socket失败 \n");
		return -1;
	}

	//2.连接服务器
	struct sockaddr_in target;
	target.sin_family = AF_INET;
	target.sin_port = htons(8080);
	target.sin_addr.s_addr = inet_addr("127.0.0.1");

	if (-1 == connect(client_socket, (struct sockaddr*)&target, sizeof(target)))
	{
		printf("连接服务器失败 \n");
		closesocket(client_socket);
		return -1;
	}
	else
	{
		int client_id = client_count; // 分配当前客户端的ID

		// 创建接收线程的参数
		int* recv_threadParams = (int*)malloc(sizeof(int) * 2);
		recv_threadParams[0] = (int)client_socket;
		recv_threadParams[1] = client_id;

		// 创建发送线程的参数
		int* send_threadParams = (int*)malloc(sizeof(int) * 2);
		send_threadParams[0] = (int)client_socket;
		send_threadParams[1] = client_id;

		// 将新的客户端套接字添加到数组中
		if (client_count < MAX_CLIENTS)
		{
			clients[client_count].socket = client_socket;
			clients[client_count].id = client_id;
			++client_count;
		}
		

		// 创建接收线程
		HANDLE hRecvThread = CreateThread(NULL, 0, recv_thread_func, recv_threadParams, 0, NULL);
		if (hRecvThread == NULL)
		{
			printf("创建接收线程失败  %lu\n", GetLastError());
			closesocket(client_socket);
			return -1;
		}

		// 创建发送线程
		HANDLE hSendThread = CreateThread(NULL, 0, send_thread_func, send_threadParams, 0, NULL);
		if (hSendThread == NULL)
		{
			printf("创建发送线程失败 %lu\n", GetLastError());
			closesocket(client_socket);
			return -1;
		}

		// 等待两个线程完成
		WaitForSingleObject(hRecvThread, INFINITE);
		WaitForSingleObject(hSendThread, INFINITE);

		// 关闭线程句柄
		CloseHandle(hRecvThread);
		CloseHandle(hSendThread);

		//4.关闭连接
		closesocket(client_socket);
		WSACleanup();
	}

	return 0;

}