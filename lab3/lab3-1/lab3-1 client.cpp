#include<WinSock2.h>
#include<Windows.h>
#include <iostream>
#include <stdint.h>
#include <cstring>
#include <cstdlib>
#include <thread>
#include <chrono>
#include <time.h>
#include <fstream>
#include <algorithm>
#pragma comment(lib,"ws2_32.lib")
using namespace std;

struct Packethead {
	uint16_t seq;     // 序列号 16位
	uint16_t Check;   // 校验 16位
	uint16_t len;     // 数据部分总长度
    uint16_t ack;     //确认号
	unsigned char flags; // 标志位

	Packethead() : seq(0), Check(0), len(0), flags(0),ack(0) {}
};

struct Packet {
	Packethead head;
	char data[2048]; //数据部分

	Packet() : head(), data() {}
};

//差错检测
u_short packetcheck(u_short* packet, int packelength)
{
    register u_long sum = 0;
    int count = (packelength + 1) / 2;//两个字节的计算
    //u_short* buf = (u_short*)malloc(packelength + 1);
    u_short* buf = new u_short[packelength + 1];
    memset(buf, 0, packelength + 1);
    memcpy(buf, packet, packelength);
    //cout << "count=" << packelength << endl;
    while (count--)
    {
        sum += *buf++;
        if (sum & 0xFFFF0000)
        {
            sum &= 0xFFFF;
            sum++;
        }
    }
    return ~(sum & 0xFFFF);
}

// 标志位常量
const uint8_t FLAG_SYN = 0x01;
const uint8_t FLAG_ACK = 0x02;
const uint8_t FLAG_SYN_ACK = FLAG_SYN | FLAG_ACK;
const uint8_t FLAG_FIN = 0x04;
const uint8_t FLAG_FIN_ACK = 0x06;
const unsigned char OVER = 0x8;//结束标志
const int MAXSIZE = 10240;//传输缓冲区最大长度
double MAX_TIME = 0.5 * CLOCKS_PER_SEC;

time_t now;
char curr_time[26];

int clientHandshake(SOCKET s, sockaddr_in& clientAddr, int& sockLen) {
    // 设置为非阻塞模式，避免卡在recvfrom
    u_long iMode = 1; // 0：阻塞
    ioctlsocket(s, FIONBIO, &iMode); // 非阻塞设置

    Packet packet1;

    // 发送SYN包
    packet1.head.seq = 0;
    packet1.head.flags = FLAG_SYN;
    packet1.head.Check = packetcheck((u_short*)&packet1, sizeof(packet1));
    int flag1 = 0;

    //发送缓冲区
    char* buffer1 = new char[sizeof(packet1)];
    memcpy(buffer1, &packet1, sizeof(packet1));
    flag1 = sendto(s, buffer1, sizeof(packet1), 0, (sockaddr*)&clientAddr, sockLen);

    if (flag1 == -1) { // 发送失败
        std::cout << "[Client]: Failed to send SYN packet." << std::endl;
        return 0;
    }
    clock_t start = clock(); //记录发送第一次握手时间
    std::cout << "[Client]: SYN packet sent successfully." << std::endl;
       
    // 等待SYN_ACK包
    Packet packet2;

    //缓冲区
    char* buffer2 = new char[sizeof(packet2)];

    bool synAckReceived = false;

    while (recvfrom(s, buffer2, sizeof(packet2), 0, (sockaddr*)&clientAddr, &sockLen)<=0) {
        if (clock() - start > MAX_TIME)//超时，重新传输第一次握手
        {
            std::cout << "[Client]:Timeout Retransmission,resending SYN packet..." << std::endl;
            sendto(s, buffer1, sizeof(packet1), 0, (sockaddr*)&clientAddr, sockLen);
            start = clock();
        }
    }
    memcpy(&packet2, buffer2, sizeof(packet2));
    u_short res = packetcheck((u_short*)&packet2, sizeof(packet2));
    if (packet2.head.flags == FLAG_SYN_ACK && packet2.head.seq == 1 && res == 0) {
        std::cout << "[Client]: Received SYN_ACK packet with seq=" << packet2.head.seq << std::endl;
        synAckReceived = true;
    }
    else {
        std::cout << "[Client]: Failed to receive SYN_ACK packet." << std::endl;
        return 0;
    }
    start = clock();

    // 发送ACK包
    Packet packet3;

    packet3.head.seq = 2;
    packet3.head.flags = FLAG_ACK;
    packet3.head.Check = packetcheck((u_short*)&packet3, sizeof(packet3));
    int flag3 = 0;

    //发送缓冲区
    char* buffer3 = new char[sizeof(packet3)];
    memcpy(buffer3, &packet3, sizeof(packet3));

    bool ackSentSuccessfully = false;

    flag3 = sendto(s, buffer3, sizeof(packet3), 0, (sockaddr*)&clientAddr, sockLen);

    if (flag3 == -1) { // 发送失败
        std::cout << "[Client]: Failed to send ACK packet." << std::endl;
        return 0;
    }
    
    std::cout << "[Client]: ACK packet sent successfully." << std::endl;

    iMode = 0; //0：阻塞
    ioctlsocket(s, FIONBIO, &iMode);//恢复成阻塞模式
    return 1;
}

int clientCloseConnection(SOCKET s, sockaddr_in& ClientAddr, int& sockLen) {
    // 设置为非阻塞模式，避免卡在recvfrom
    u_long iMode = 1; // 0：阻塞
    ioctlsocket(s, FIONBIO, &iMode); // 非阻塞设置

    Packet packet1;
    int flag1 = 0;

    // 发送FIN_ACK包
    packet1.head.seq = 0;
    packet1.head.flags = FLAG_FIN_ACK;
    packet1.head.Check = packetcheck((u_short*)&packet1, sizeof(packet1));
    char* buffer1 = new char[sizeof(packet1)];
    memcpy(buffer1, &packet1, sizeof(packet1));

    flag1 = sendto(s, buffer1, sizeof(packet1), 0, (sockaddr*)&ClientAddr, sockLen);
    if (flag1 == -1) {
        std::cout << "[Client]: Failed to send FIN_ACK packet." << std::endl;
        return 0;
    }
    clock_t start = clock();
    std::cout << "[Client]: FIN_ACK packet sent successfully." << std::endl;

    // 等待服务器发送的ACK包
    Packet packet2;
    char* buffer2 = new char[sizeof(packet2)];

    bool ackReceived = false;

    while (recvfrom(s, buffer2, sizeof(packet2), 0, (sockaddr*)&ClientAddr, &sockLen)<=0) {
        if (clock() - start > MAX_TIME)//超时，重新传输第一次握手
        {
            std::cout << "[Client]:Timeout Retransmission,resending FIN_ACK packet..." << std::endl;
            sendto(s, buffer1, sizeof(packet1), 0, (sockaddr*)&ClientAddr, sockLen);
            start = clock();
        }
    }
    memcpy(&packet2, buffer2, sizeof(packet2));
    u_short res = packetcheck((u_short*)&packet2, sizeof(packet2));

    if (packet2.head.flags == FLAG_ACK && packet2.head.seq == 1 && res == 0) {
        std::cout << "[Client]: Received ACK packet with seq=" << packet2.head.seq << std::endl;
        ackReceived = true;
    }
    else{
        std::cout << "[Client]: Failed to receive ACK packet." << std::endl;
        //return 0;
    }

    // 等待服务器发送的FIN_ACK包
    bool finackReceived = false;
    Packet packet3;
    char* buffer3 = new char[sizeof(packet3)];

    while(1) { 
        u_short res = packetcheck((u_short*)&packet3, sizeof(packet3));
        
        if (recvfrom(s, buffer3, sizeof(packet3), 0, (sockaddr*)&ClientAddr, &sockLen)<=0) {
            //cout << "未收到FIN_ACK包" << endl;
        }
        memcpy(&packet3, buffer3, sizeof(packet3));
        if (packet3.head.flags == FLAG_FIN_ACK && packet3.head.seq == 2 && res == 0) {
            std::cout << "[Client]: Received FIN_ACK packet with seq=" << packet3.head.seq << std::endl;
            finackReceived = true;
            break;
        }
             
    }
    start = clock();

    if (!finackReceived) {
        std::cout << "[Client]: Failed to receive FIN_ACK packet." << std::endl;
        return 0;
    }

    // 发送ACK包
    Packet packet4;
    packet4.head.seq = 3;
    packet4.head.flags = FLAG_ACK;
    packet4.head.Check = packetcheck((u_short*)&packet4, sizeof(packet4));
    char* buffer4 = new char[sizeof(packet4)];
    memcpy(buffer4, &packet4, sizeof(packet4));
    int flag4 = 0;

    bool ackSentSuccessfully = false;

    while (!ackSentSuccessfully) {
        flag4 = sendto(s, buffer4, sizeof(packet4), 0, (sockaddr*)&ClientAddr, sockLen);
        if (flag4 != -1) { // 发送成功
            std::cout << "[Client]: ACK packet sent successfully." << std::endl;
            ackSentSuccessfully = true;
        }
        else {
            std::cout << "[Client]: Failed to send ACK packet, retrying..." << std::endl;
            return 0;
        }
    }

    iMode = 0; // 0：阻塞
    ioctlsocket(s, FIONBIO, &iMode); // 恢复成阻塞模式
    return 1;
}

void send(SOCKET& socketClient, SOCKADDR_IN& servAddr, int& servAddrlen, char* message, int messagelen)
{
    u_long mode = 1;
    ioctlsocket(socketClient, FIONBIO, &mode);
    int seqnum = 0;
    int packagenum = (messagelen / MAXSIZE)+ (messagelen % MAXSIZE != 0);
    cout << packagenum << endl;
    int len = 0;

    for (int i = 0; i < packagenum; i++){
        Packet packet1;
        
        len = ((i == (packagenum - 1)) ? (messagelen - ((packagenum - 1) * MAXSIZE)) : MAXSIZE);
        //cout << len << endl;
        //cout << len - ((packagenum - 1) * MAXSIZE) << endl;
        char* buffer1 = new char[len + sizeof(packet1)];
        packet1.head.len = len;
        packet1.head.seq = seqnum;//序列号
        packet1.head.Check = 0;

        //计算校验和
        u_short check = packetcheck((u_short*)&packet1, sizeof(packet1));//计算校验和
        packet1.head.Check = check;
        packet1.head.flags = 0;
        packet1.head.ack = 0;
        memcpy(buffer1, &packet1, sizeof(packet1));
        char* mes = message + i * MAXSIZE;
        memcpy(buffer1 + sizeof(packet1), mes, len);//将数据复制到缓冲区的后部分
        sendto(socketClient, buffer1, len + sizeof(packet1), 0, (sockaddr*)&servAddr, servAddrlen);//发送
        cout << "发送文件大小为 " << len << " bytes!" << " Flag:" << int(packet1.head.flags) << " SEQ:" << int(packet1.head.seq) << " ACK:" << int(packet1.head.ack) << " CHECK:" << int(packet1.head.Check) << endl;
        clock_t start = clock();//记录发送时间

        //接收ACK
        Packet packet2;
        char* buffer2 = new char[sizeof(packet2)];
        //如果接受失败则继续等待
        while (recvfrom(socketClient, buffer2, sizeof(packet2), 0, (sockaddr*)&servAddr, &servAddrlen) <= 0)
        {
            //cout << "error" << endl;
            if (clock() - start > MAX_TIME)
            {
                //memcpy(buffer1, &packet1, sizeof(packet1));
                sendto(socketClient, buffer1, len + sizeof(packet1), 0, (sockaddr*)&servAddr, servAddrlen);//发送
                cout << "发送超时，重传文件大小为 " << len << " bytes! " << endl;
                cout << "发送文件大小为 " << len << " bytes!" << " Flag:" << int(packet1.head.flags) << " SEQ:" << int(packet1.head.seq) << " ACK:" << int(packet1.head.ack) << " CHECK:" << int(packet1.head.Check) << endl;
                //system("pause");
                start = clock();//记录发送时间
            } 
        }
        memcpy(&packet2, buffer2, sizeof(packet2));//缓冲区接收到信息，读取
        u_short check2 = packetcheck((u_short*)&packet2, sizeof(packet2));
        //cout << "packet2.head.ack: " << packet2.head.ack << endl;
        //cout << "seqnum: " << seqnum << endl;
        if ((packet2.head.ack == (seqnum + 1) % 256) && packet2.head.flags == FLAG_ACK && check2 == 0)
        {
            cout << "接收到ACK，确认数据包发送成功  Flag:" << int(packet2.head.flags) << " SEQ:" << int(packet2.head.seq) << " ACK:" << int(packet2.head.ack) << " CHECK:" << int(packet2.head.Check) << endl;
        }
        else
        {//重传
            if (packet2.head.ack != seqnum + 1)
                cout << "1";
            if (packet2.head.flags != FLAG_ACK)
                cout << "2";
            if (check2 != 0)
                cout << "3";
            
            i--;
            cout << "error";
            //system("pause");
            continue;
        }
        seqnum = (seqnum + 1) % 256;
    }
    
    //发送结束信息
    Packet packet3;
    char* Buffer3 = new char[sizeof(packet3)];
    packet3.head.flags = OVER;//结束信息
    packet3.head.Check = 0;
    u_short temp = packetcheck((u_short*)&packet3, sizeof(packet3));
    packet3.head.Check = temp;
    //发送结束信息
    memcpy(Buffer3, &packet3, sizeof(packet3));
    sendto(socketClient, Buffer3, sizeof(packet3), 0, (sockaddr*)&servAddr, servAddrlen);
    cout << "接收文件完毕！" << endl;
    clock_t start = clock();

    Packet packet4;
    char* Buffer4 = new char[sizeof(packet4)];

    //接收结束确认
    //如果接收失败
    while (recvfrom(socketClient, Buffer4, sizeof(packet4), 0, (sockaddr*)&servAddr, &servAddrlen) <= 0)
    {
        if (clock() - start > MAX_TIME)
        { 
            sendto(socketClient, Buffer3, sizeof(packet3), 0, (sockaddr*)&servAddr, servAddrlen);
            cout << "结束信息超时重传" << endl;
            start = clock();
        }
    }
    memcpy(&packet4, Buffer4, sizeof(packet4));//缓冲区接收到信息，读取
    u_short check4 = packetcheck((u_short*)&packet4, sizeof(packet4));
    if (packet4.head.flags == OVER && check4 == 0)
    {
        cout << "对方已成功接收文件!" << endl;
    }
    else
    {
        cout << "未成功接收文件!" << endl;
    }
        
    
    mode = 0;
    ioctlsocket(socketClient, FIONBIO, &mode);//改回阻塞模式
    return; 
}

int main() {
    WSADATA d;
    WORD w = MAKEWORD(2, 2);
    int flag = WSAStartup(w, &d);

    if (flag == 0)
    {
        now = time(nullptr);
        ctime_s(curr_time, sizeof(curr_time), &now);
        cout << curr_time << "[Client]:";
        cout << "套接字库加载成功" << endl;
    }
    else
    {
        now = time(nullptr);
        ctime_s(curr_time, sizeof(curr_time), &now);
        cout << curr_time << "[Client]:";
        cout << "套接字库加载失败" << endl;
    }

    sockaddr_in Addr;
    Addr.sin_family = AF_INET;
    Addr.sin_addr.S_un.S_addr = inet_addr("127.0.0.1");
    Addr.sin_port = htons(9999);

    int sockLen = sizeof(Addr);

    SOCKET client = socket(AF_INET, SOCK_DGRAM, 0);
    //进行连接
    int len = sizeof(Addr);
    flag = clientHandshake(client, Addr, sockLen);
    if (flag != 0)//判断绑定
    {
        now = time(nullptr);
        ctime_s(curr_time, sizeof(curr_time), &now);
        cout << curr_time << "[Client]:";
        cout << "建立连接成功" << endl;
    }
    else
    {
        now = time(nullptr);
        ctime_s(curr_time, sizeof(curr_time), &now);
        cout << curr_time << "[Client]:";
        cout << "建立连接失败" << endl;
        closesocket(client);
        WSACleanup();
        return 0;
    }

    //数据传输
    string filename;
    now = time(nullptr);
    ctime_s(curr_time, sizeof(curr_time), &now);
    cout << curr_time << "[Client]:";
    cout << "请输入文件名称" << endl;
    cin >> filename;
    //filename = "资源文件\\" + filename;
    ifstream fin(filename.c_str(), ifstream::binary);//以二进制方式打开文件
    char* buffer = new char[100000000];
    int index = 0;
    unsigned char temp = fin.get();
    while (fin)
    {
        buffer[index++] = temp;
        temp = fin.get();
    }
    fin.close();
    send(client, Addr, len, (char*)(filename.c_str()), filename.length());
    now = time(nullptr);
    ctime_s(curr_time, sizeof(curr_time), &now);
    cout << curr_time << "[Client]:";
    cout << "文件名传输成功" << endl;
    clock_t start = clock();
    send(client, Addr, len, buffer, index);
    clock_t end = clock();
    now = time(nullptr);
    ctime_s(curr_time, sizeof(curr_time), &now);
    cout << curr_time << "[Client]:";
    cout << "文件内容传输成功" << endl;
    cout << "传输总时间为:" << (end - start) / CLOCKS_PER_SEC << "s" << endl;
    cout << "吞吐率为:" << ((float)index) / ((end - start) / CLOCKS_PER_SEC) << " byte/s" << endl;
    

    //断开连接
    clock_t starttime = clock();
    flag = clientCloseConnection(client, Addr, sockLen);
    if (flag != 0)//判断绑定
    {
        now = time(nullptr);
        ctime_s(curr_time, sizeof(curr_time), &now);
        cout << curr_time << "[System]:";
        cout << "断开连接成功" << endl;
    }
    else
    {
        now = time(nullptr);
        ctime_s(curr_time, sizeof(curr_time), &now);
        cout << curr_time << "[System]:";
        cout << "断开连接失败" << endl;
    }

    closesocket(client);
    WSACleanup();
    return 0;
}