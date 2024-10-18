#include<stdio.h>
#include<string.h>
#include <stdlib.h>
#include <time.h>
#include<WinSock2.h> //windows网络头文件
#pragma comment(lib,"ws2_32.lib") //windows网络库文件

#define MAX_CLIENTS 100 // 最大客户端数量

// 全局变量用于存储所有客户端的套接字和ID
struct ClientInfo {
	SOCKET socket;
	int id;
};

// 全局变量用于存储所有客户端的套接字
struct ClientInfo clients[MAX_CLIENTS];
int client_count = 1;

void broadcast_message(int sender_id, const char* message)
{
	for (int i = 0; i < client_count; ++i)
	{
		if (clients[i].id != sender_id) // 不向发送者转发消息
		{
			send(clients[i].socket, message, (int)strlen(message), 0);
		}
	}
}

// 广播消息给指定客户端
void broadcast_message_to(int client_id, const char* message)
{
	for (int i = 0; i < client_count; ++i)
	{
		if (clients[i].id == client_id)
		{
			send(clients[i].socket, message, strlen(message), 0);
			return; // 找到并发送后立即返回
		}
	}
}

DWORD WINAPI receive_thread_func(LPVOID lpThreadParameter)
{
	SOCKET client_socket = ((SOCKET*)lpThreadParameter)[0];
	int client_id = ((SOCKET*)lpThreadParameter)[1]; // 获取客户端ID
	
	char welcome_message[1024];
	sprintf(welcome_message, "用户%d进入聊天室", client_id);
	printf("%s\n", welcome_message);
	broadcast_message(client_id, welcome_message);
	char name_message[1024];
	sprintf(name_message, "你是用户%d", client_id);
	broadcast_message_to(client_id, name_message);
	while (1)
	{
		char buffer[1024] = { 0 }; // 接收数据
		int ret = recv(client_socket, buffer, 1024, 0); // 接收数据
		if (ret <= 0)
		{
			break;
		}

		// 检测退出信号
		if (strncmp(buffer, "QUIT", 4) == 0)
		{
			// 构造退出消息并广播
			char quit_message[1024];
			sprintf(quit_message, "用户%d退出聊天室", client_id);
			broadcast_message(client_id, quit_message);
			printf("用户%d退出聊天室 \n", client_id);
			break;
		}

		// 检查是否为私聊消息
		if (strncmp(buffer, "PRIVATE:", 8) == 0)
		{
			// 处理私聊消息
			int target_id;
			char message[1024];
			if (sscanf(buffer + 8, "%d:%[^:]", &target_id, message) == 2)
			{
				// 发送私聊消息给目标用户
				for (int i = 0; i < client_count; ++i)
				{
					if (clients[i].id == target_id && clients[i].id != client_id)
					{
						char private_message[1024];
						printf("私聊消息来自用户%d发送给用户%d: %s \n", client_id, target_id, message);
						sprintf(private_message, "私聊消息来自用户%d: %s", client_id, message);
						send(clients[i].socket, private_message, strlen(private_message), 0);
					}
				}
			}
			else
			{
				// 如果解析失败，可以打印错误信息或忽略该消息
				printf("私聊消息格式错误。\n");
			}
		}
		else
		{
			// 获取当前时间
			time_t now = time(NULL);
			struct tm tm_info;
			localtime_s(&tm_info, &now);
			char time_str[20];
			strftime(time_str, sizeof(time_str), "%X", &tm_info); // 格式化时间为字符串

			printf("%d: %s   [%s]\n", client_id, buffer, time_str); // 打印

			// 将接收到的消息广播给其他客户端，并加上时间戳
			char broad_message[1024];
			sprintf(broad_message, "用户%d: %s   [%s]", client_id, buffer, time_str);
			broadcast_message(client_id, broad_message);
		}

	}
	
	// 从客户端数组中移除断开连接的客户端
	for (int i = 0; i < client_count; ++i)
	{
		if (clients[i].socket == client_socket)
		{
			memmove(&clients[i], &clients[i + 1], sizeof(struct ClientInfo) * (client_count - i - 1));
			--client_count;
			break;
		}
	}

	closesocket(client_socket);
	return 0;
}

int main()
{
	//windows上开启网络权限
	WSADATA wsaData;
	if (WSAStartup(MAKEWORD(2, 2), &wsaData) != 0)
	{
		printf("开启网络权限失败 %d\n", GetLastError());
		return -1;
	}
	// 1.创建socket套接字
	/*SOCKET socket(
		int af,    //协议地址 IPV4/IPV6 AF_INET/AF_INET6
		int type,  //类型 流式协议 帧式协议 SOCK_STREAM/SOCK_DGRAM
		int protocol //保护协议 0
	);*/
	SOCKET listen_socket = socket(AF_INET, SOCK_STREAM, 0);
	//printf("%d\n", listen_socket);
	if (INVALID_SOCKET == listen_socket) //INVALID_SOCKET = -1
	{
		printf("创建socket失败 errcode: %d\n",GetLastError());
		return -1;
	}

	//2.给socket绑定端口号
	struct sockaddr_in local = { 0 };

	/*struct sockaddr_in
	{
		ADDRESS_FAMILY sin_family; //协议地址
		USHORT sin_port; //端口号
		IN_ADDR sin_addr; //IP地址
		CHAR sin_zero[8]; //保留字节
	};*/

	local.sin_family = AF_INET;
	local.sin_port = htons(8080);//中间设备使用的是大端序（路由器）
	//local.sin_addr.s_addr = INADDR_ANY; //服务器 选项 网卡 127.0.0.1（本地环回）只接受哪个网卡的数据 一般写全0地址表示全部都接受
	local.sin_addr.s_addr = inet_addr("0.0.0.0"); //字符串IP地址转换成整数IP

	/*int bind
	(
		SOCKET s,
		const struct sockaddr* name,
		int namelen
	);*/ 

	if (-1 == bind(listen_socket, (struct sockaddr*)&local, sizeof(local)))
	{
		printf("绑定端口号失败 errcode: %d\n", GetLastError());
		closesocket(listen_socket);
		return -1;
	}

	//3. 给socket开启监听属性
	/*int listen
	(
		SOCKET s,
		int backlog
	);*/
	if (-1 == listen(listen_socket, 10)) 
	{
		printf("开启监听失败 errcode: %d\n", GetLastError());
		closesocket(listen_socket);
		return -1;
	}
	
	// 4.等待客户端连接
	/*SOCKET accept(
		SOCKET s, //监听socket
		struct sockaddr* addr, //客户端ip地址和端口号
		int* addrlen //结构的大小
	);*/

	//返回的客户端socket才是跟客户端可以通讯的一个socket
	// accept是阻塞函数，等待有客户端连接进来就接受连接，然后返回，否则一直阻塞
	while(1)
	{
		SOCKET client_socket = accept(listen_socket, NULL, NULL);
		if (INVALID_SOCKET == client_socket) //连接失败
		{
			continue;
		}
		int client_id = client_count; // 分配当前客户端的ID

		SOCKET* sockfd = (SOCKET*)malloc(sizeof(SOCKET)*2);
		sockfd[0] = client_socket;
		sockfd[1] = client_id;

		// 将新的客户端套接字添加到数组中
		if (client_count < MAX_CLIENTS)
		{
			clients[client_count].socket = client_socket;
			clients[client_count].id = client_id;
			++client_count;
		}

		// 创建接收线程
		HANDLE hReceiveThread = CreateThread(NULL, 0, receive_thread_func, sockfd, 0, NULL);
		if (hReceiveThread == NULL)
		{
			printf("创建线程失败 %lu\n", GetLastError());
			free(sockfd);
			closesocket(client_socket);
			continue;
		}
	}
	closesocket(listen_socket);
	WSACleanup();
	return 0;
}