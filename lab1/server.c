#include<stdio.h>
#include<string.h>
#include <stdlib.h>
#include <time.h>
#include<WinSock2.h> //windows����ͷ�ļ�
#pragma comment(lib,"ws2_32.lib") //windows������ļ�

#define MAX_CLIENTS 100 // ���ͻ�������

// ȫ�ֱ������ڴ洢���пͻ��˵��׽��ֺ�ID
struct ClientInfo {
	SOCKET socket;
	int id;
};

// ȫ�ֱ������ڴ洢���пͻ��˵��׽���
struct ClientInfo clients[MAX_CLIENTS];
int client_count = 1;

void broadcast_message(int sender_id, const char* message)
{
	for (int i = 0; i < client_count; ++i)
	{
		if (clients[i].id != sender_id) // ��������ת����Ϣ
		{
			send(clients[i].socket, message, (int)strlen(message), 0);
		}
	}
}

// �㲥��Ϣ��ָ���ͻ���
void broadcast_message_to(int client_id, const char* message)
{
	for (int i = 0; i < client_count; ++i)
	{
		if (clients[i].id == client_id)
		{
			send(clients[i].socket, message, strlen(message), 0);
			return; // �ҵ������ͺ���������
		}
	}
}

DWORD WINAPI receive_thread_func(LPVOID lpThreadParameter)
{
	SOCKET client_socket = ((SOCKET*)lpThreadParameter)[0];
	int client_id = ((SOCKET*)lpThreadParameter)[1]; // ��ȡ�ͻ���ID
	
	char welcome_message[1024];
	sprintf(welcome_message, "�û�%d����������", client_id);
	printf("%s\n", welcome_message);
	broadcast_message(client_id, welcome_message);
	char name_message[1024];
	sprintf(name_message, "�����û�%d", client_id);
	broadcast_message_to(client_id, name_message);
	while (1)
	{
		char buffer[1024] = { 0 }; // ��������
		int ret = recv(client_socket, buffer, 1024, 0); // ��������
		if (ret <= 0)
		{
			break;
		}

		// ����˳��ź�
		if (strncmp(buffer, "QUIT", 4) == 0)
		{
			// �����˳���Ϣ���㲥
			char quit_message[1024];
			sprintf(quit_message, "�û�%d�˳�������", client_id);
			broadcast_message(client_id, quit_message);
			printf("�û�%d�˳������� \n", client_id);
			break;
		}

		// ����Ƿ�Ϊ˽����Ϣ
		if (strncmp(buffer, "PRIVATE:", 8) == 0)
		{
			// ����˽����Ϣ
			int target_id;
			char message[1024];
			if (sscanf(buffer + 8, "%d:%[^:]", &target_id, message) == 2)
			{
				// ����˽����Ϣ��Ŀ���û�
				for (int i = 0; i < client_count; ++i)
				{
					if (clients[i].id == target_id && clients[i].id != client_id)
					{
						char private_message[1024];
						printf("˽����Ϣ�����û�%d���͸��û�%d: %s \n", client_id, target_id, message);
						sprintf(private_message, "˽����Ϣ�����û�%d: %s", client_id, message);
						send(clients[i].socket, private_message, strlen(private_message), 0);
					}
				}
			}
			else
			{
				// �������ʧ�ܣ����Դ�ӡ������Ϣ����Ը���Ϣ
				printf("˽����Ϣ��ʽ����\n");
			}
		}
		else
		{
			// ��ȡ��ǰʱ��
			time_t now = time(NULL);
			struct tm tm_info;
			localtime_s(&tm_info, &now);
			char time_str[20];
			strftime(time_str, sizeof(time_str), "%X", &tm_info); // ��ʽ��ʱ��Ϊ�ַ���

			printf("%d: %s   [%s]\n", client_id, buffer, time_str); // ��ӡ

			// �����յ�����Ϣ�㲥�������ͻ��ˣ�������ʱ���
			char broad_message[1024];
			sprintf(broad_message, "�û�%d: %s   [%s]", client_id, buffer, time_str);
			broadcast_message(client_id, broad_message);
		}

	}
	
	// �ӿͻ����������Ƴ��Ͽ����ӵĿͻ���
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
	//windows�Ͽ�������Ȩ��
	WSADATA wsaData;
	if (WSAStartup(MAKEWORD(2, 2), &wsaData) != 0)
	{
		printf("��������Ȩ��ʧ�� %d\n", GetLastError());
		return -1;
	}
	// 1.����socket�׽���
	/*SOCKET socket(
		int af,    //Э���ַ IPV4/IPV6 AF_INET/AF_INET6
		int type,  //���� ��ʽЭ�� ֡ʽЭ�� SOCK_STREAM/SOCK_DGRAM
		int protocol //����Э�� 0
	);*/
	SOCKET listen_socket = socket(AF_INET, SOCK_STREAM, 0);
	//printf("%d\n", listen_socket);
	if (INVALID_SOCKET == listen_socket) //INVALID_SOCKET = -1
	{
		printf("����socketʧ�� errcode: %d\n",GetLastError());
		return -1;
	}

	//2.��socket�󶨶˿ں�
	struct sockaddr_in local = { 0 };

	/*struct sockaddr_in
	{
		ADDRESS_FAMILY sin_family; //Э���ַ
		USHORT sin_port; //�˿ں�
		IN_ADDR sin_addr; //IP��ַ
		CHAR sin_zero[8]; //�����ֽ�
	};*/

	local.sin_family = AF_INET;
	local.sin_port = htons(8080);//�м��豸ʹ�õ��Ǵ����·������
	//local.sin_addr.s_addr = INADDR_ANY; //������ ѡ�� ���� 127.0.0.1�����ػ��أ�ֻ�����ĸ����������� һ��дȫ0��ַ��ʾȫ��������
	local.sin_addr.s_addr = inet_addr("0.0.0.0"); //�ַ���IP��ַת��������IP

	/*int bind
	(
		SOCKET s,
		const struct sockaddr* name,
		int namelen
	);*/ 

	if (-1 == bind(listen_socket, (struct sockaddr*)&local, sizeof(local)))
	{
		printf("�󶨶˿ں�ʧ�� errcode: %d\n", GetLastError());
		closesocket(listen_socket);
		return -1;
	}

	//3. ��socket������������
	/*int listen
	(
		SOCKET s,
		int backlog
	);*/
	if (-1 == listen(listen_socket, 10)) 
	{
		printf("��������ʧ�� errcode: %d\n", GetLastError());
		closesocket(listen_socket);
		return -1;
	}
	
	// 4.�ȴ��ͻ�������
	/*SOCKET accept(
		SOCKET s, //����socket
		struct sockaddr* addr, //�ͻ���ip��ַ�Ͷ˿ں�
		int* addrlen //�ṹ�Ĵ�С
	);*/

	//���صĿͻ���socket���Ǹ��ͻ��˿���ͨѶ��һ��socket
	// accept�������������ȴ��пͻ������ӽ����ͽ������ӣ�Ȼ�󷵻أ�����һֱ����
	while(1)
	{
		SOCKET client_socket = accept(listen_socket, NULL, NULL);
		if (INVALID_SOCKET == client_socket) //����ʧ��
		{
			continue;
		}
		int client_id = client_count; // ���䵱ǰ�ͻ��˵�ID

		SOCKET* sockfd = (SOCKET*)malloc(sizeof(SOCKET)*2);
		sockfd[0] = client_socket;
		sockfd[1] = client_id;

		// ���µĿͻ����׽�����ӵ�������
		if (client_count < MAX_CLIENTS)
		{
			clients[client_count].socket = client_socket;
			clients[client_count].id = client_id;
			++client_count;
		}

		// ���������߳�
		HANDLE hReceiveThread = CreateThread(NULL, 0, receive_thread_func, sockfd, 0, NULL);
		if (hReceiveThread == NULL)
		{
			printf("�����߳�ʧ�� %lu\n", GetLastError());
			free(sockfd);
			closesocket(client_socket);
			continue;
		}
	}
	closesocket(listen_socket);
	WSACleanup();
	return 0;
}