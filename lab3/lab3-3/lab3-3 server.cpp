#include<WinSock2.h>
#include<Windows.h>
#include<iostream>
#include <stdint.h>
#include <cstring>
#include <cstdlib>
#include <thread>
#include <chrono>
#include <time.h>
#include <map>
#include <fstream>
#include <cstdio>
#include <ctime>
#include <algorithm>
#pragma comment(lib,"ws2_32.lib")
using namespace std;

//数据报头
struct Packethead {
    int seq;     // 序列号 16位
    uint16_t Check;   // 校验 16位
    uint16_t len;     // 数据部分总长度
    int ack;     //确认号
    unsigned char flags; // 标志位

    Packethead() : seq(0), Check(0), len(0), flags(0), ack(0) {}
};

//数据报
struct Packet {
    Packethead head;
    char data[2048]; // 数据部分

    Packet() : head(), data() {}
};

//差错检测
u_short packetcheck(u_short* packet, int packelength)
{
    //UDP检验和的计算方法是：
    //按每16位求和得出一个32位的数；
    //如果这个32位的数，高16位不为0，则高16位加低16位再得到一个32位的数；
    //重复第2步直到高16位为0，将低16位取反，得到校验和。
    //register关键字命令编译器尽可能的将变量存在CPU内部寄存器中
    //而不是通过内存寻址访问以提高效率。
    //u_long32位，不能使用unsigned int 只有16位
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
const int MAXSIZE = 10240;//传输缓冲区最大长度
const unsigned char OVER = 0x8;//结束标志
double MAX_TIME = 0.5 * CLOCKS_PER_SEC;

time_t now;
char curr_time[26];

int serverHandshake(SOCKET s, sockaddr_in& ServerAddr, int& sockLen) {
    //设置为非阻塞模式，避免卡在recvfrom
    u_long iMode = 1; //0：阻塞
    ioctlsocket(s, FIONBIO, &iMode);//非阻塞设置

    Packet packet1;//储存接收到的数据包
    int flag1 = 0;

    bool synReceived = false;//标记是否收到SYN包

    char* buffer1 = new char[sizeof(packet1)];
    // 等待SYN包
    while (1) {

        flag1 = recvfrom(s, buffer1, sizeof(packet1), 0, (sockaddr*)&ServerAddr, &sockLen);
        //没收到就一直循环
        if (flag1 <= 0)
        {
            //cout << "error" << endl;
            continue;
        }
        //如果接收成功
        memcpy(&(packet1), buffer1, sizeof(packet1.head));
        u_short res = packetcheck((u_short*)&packet1, sizeof(packet1));
        //cout << "success" << endl;
        //检查接收到的是否为SYN包
        //标志位为FLAG_SYN且序列号为1,且数据包正确
        if (packet1.head.flags == FLAG_SYN && packet1.head.seq == 0 && res == 0) {
            std::cout << "[Server]: Received SYN packet with seq=" << packet1.head.seq << std::endl;
            synReceived = true;
            break;
        }
    }

    //如果未收到SYN包，输出一条消息表示失败
    if (!synReceived) {
        std::cout << "[Server]: Failed to receive SYN packet." << std::endl;
        //return 0;
    }

    // 发送SYN_ACK包
    Packet packet2;
    packet2.head.seq = 1;
    packet2.head.flags = FLAG_SYN_ACK;
    packet2.head.Check = packetcheck((u_short*)&packet2, sizeof(packet2));

    char* buffer2 = new char[sizeof(packet2)];
    memcpy(buffer2, &packet2, sizeof(packet2));

    int flag2 = sendto(s, buffer2, sizeof(packet2), 0, (sockaddr*)&ServerAddr, sockLen);
    if (flag2 == -1) { // 发送失败
        std::cout << "[Server]: Failed to send SYN_ACK packet." << std::endl;
        return 0;
    }
    clock_t start = clock();//记录第二次握手发送时间
    std::cout << "[Server]: SYN_ACK packet sent successfully." << std::endl;


    bool ackReceived = false; // 标记是否收到ACK包

    // 等待ACK包
    Packet packet3;
    char* buffer3 = new char[sizeof(packet3)];
    int flag3 = 0;

    while (recvfrom(s, buffer3, sizeof(packet3), 0, (sockaddr*)&ServerAddr, &sockLen) <= 0) {
        if (clock() - start > MAX_TIME)//超时，重新传输第一次握手
        {
            return 1;
            /*std::cout << "[Server]:Timeout Retransmission,resending SYN_ACK packet..." << std::endl;
            sendto(s, buffer2, sizeof(packet2), 0, (sockaddr*)&ServerAddr, sockLen);
            start = clock();*/
        }
    }
    //如果接收成功
    memcpy(&(packet3), buffer3, sizeof(packet3.head));
    u_short res = packetcheck((u_short*)&packet3, sizeof(packet3));

    if (packet3.head.flags == FLAG_ACK && packet3.head.seq == 2 && res == 0) {
        std::cout << "[Server]: Received ACK packet with seq=" << packet3.head.seq << std::endl;
    }

    iMode = 0; //0：阻塞
    ioctlsocket(s, FIONBIO, &iMode);//恢复成阻塞模式
    return 1;
}

int serverCloseConnection(SOCKET s, sockaddr_in& ServerAddr, int& sockLen) {
    // 设置为非阻塞模式，避免卡在recvfrom
    u_long iMode = 1; // 0：阻塞
    ioctlsocket(s, FIONBIO, &iMode); // 非阻塞设置

    Packet packet1;
    char* buffer1 = new char[sizeof(packet1)];
    int flag = 0;

    // 等待客户端发送的FIN_ACK包
    bool finackReceived = false;

    while (1) {
        if (recvfrom(s, buffer1, sizeof(packet1), 0, (sockaddr*)&ServerAddr, &sockLen) > 0) {
            memcpy(&(packet1), buffer1, sizeof(packet1));
            u_short res = packetcheck((u_short*)&packet1, sizeof(packet1));
            if (packet1.head.flags == FLAG_FIN_ACK && packet1.head.seq == 0 && res == 0) {
                std::cout << "[Server]: Received FIN_ACK packet with seq=" << packet1.head.seq << std::endl;
                finackReceived = true;
                break;
            }
        }
        else {
            continue;
        }
    }

    if (!finackReceived) {
        std::cout << "[Server]: Failed to receive FIN_ACK packet." << std::endl;
        return 0;
    }

    // 发送ACK包
    Packet packet2;
    packet2.head.seq = 1;
    packet2.head.flags = FLAG_ACK;
    packet2.head.Check = packetcheck((u_short*)&packet2, sizeof(packet2));
    char* buffer2 = new char[sizeof(packet2)];
    memcpy(buffer2, &packet2, sizeof(packet2));

    flag = sendto(s, buffer2, sizeof(packet2), 0, (sockaddr*)&ServerAddr, sockLen);
    if (flag != -1) { // 发送成功
        std::cout << "[Server]: ACK packet sent successfully." << std::endl;
    }
    else {
        std::cout << "[Server]: Failed to send ACK packet." << std::endl;
    }

    // 发送FIN_ACK包
    Packet packet3;
    packet3.head.seq = 2;
    packet3.head.flags = FLAG_FIN_ACK;
    packet3.head.Check = packetcheck((u_short*)&packet3, sizeof(packet3));
    char* buffer3 = new char[sizeof(packet3)];
    memcpy(buffer3, &packet3, sizeof(packet3));

    bool finackSentSuccessfully = false;

    while (!finackSentSuccessfully) {
        flag = sendto(s, buffer3, sizeof(packet3), 0, (sockaddr*)&ServerAddr, sockLen);
        if (flag != -1) { // 发送成功
            std::cout << "[Server]: FIN_ACK packet sent successfully." << std::endl;
            finackSentSuccessfully = true;
        }
        else {
            std::cout << "[Server]: Failed to send FIN_ACK packet, retrying..." << std::endl;
            std::this_thread::sleep_for(std::chrono::seconds(1));
        }
    }
    clock_t start = clock();

    if (flag == -1) { // 发送失败
        std::cout << "[Server]: Failed to send FIN_ACK packet." << std::endl;
        return 0;
    }

    // 等待客户端发送的ACK包
    Packet packet4;
    char* buffer4 = new char[sizeof(packet4)];
    int flag4 = 0;

    while (recvfrom(s, buffer4, sizeof(packet4), 0, (sockaddr*)&ServerAddr, &sockLen) <= 0) {
        if (clock() - start > MAX_TIME)//超时
        {
            break;
            /*std::cout << "[Server]:Timeout Retransmission,resending ACK packet..." << std::endl;
            sendto(s, buffer3, sizeof(packet3), 0, (sockaddr*)&ServerAddr, sockLen);
            start = clock();*/
        }
    }
    //如果接收成功
    memcpy(&(packet4), buffer4, sizeof(packet4.head));
    u_short res = packetcheck((u_short*)&packet4, sizeof(packet4));

    if (packet4.head.flags == FLAG_ACK && packet4.head.seq == 3 && res == 0) {
        std::cout << "[Server]: Received ACK packet with seq=" << packet4.head.seq << std::endl;
    }

    iMode = 0; // 0：阻塞
    ioctlsocket(s, FIONBIO, &iMode); // 恢复成阻塞模式
    return 1;
}

int RecvMessage(SOCKET& sockServ, SOCKADDR_IN& ClientAddr, int& ClientAddrLen, char* message)
{
    u_long mode = 1;
    ioctlsocket(sockServ, FIONBIO, &mode);  // 非阻塞模式
    int filesize = 0;//文件长度

    int seq = 0;//序列号

    srand(time(0)); // 初始化随机数生成器

    while (1)
    {
        Packet packet1;
        char* Buffer1 = new char[MAXSIZE + sizeof(packet1)];

        while (recvfrom(sockServ, Buffer1, sizeof(packet1) + MAXSIZE, 0, (sockaddr*)&ClientAddr, &ClientAddrLen) <= 0);//接收报文长度

        memcpy(&packet1, Buffer1, sizeof(packet1));

        // 模拟丢包，假设丢包概率
        if (rand() % 100 < 5) {
            delete[] Buffer1;
            continue;
        }

        //判断是否是结束
        if (packet1.head.flags == OVER && packetcheck((u_short*)&packet1, sizeof(packet1)) == 0)
        {
            cout << "文件接收完毕" << endl;
            break;
        }
        //处理数据报文
        else if (packet1.head.flags == 0 && packetcheck((u_short*)&packet1, sizeof(packet1)) == 0)
        {
            //判断是否接受的是别的包
            if (seq != int(packet1.head.seq)-1)
            {
                Packet packet4;
                packet4.head.flags = FLAG_ACK;
                packet4.head.len = 0;
                packet4.head.seq = seq;
                packet4.head.Check = 0;
                packet4.head.ack = seq + 1;
                u_short temp = packetcheck((u_short*)&packet4, sizeof(packet4));
                packet4.head.Check = temp;
                char* buffer4 = new char[sizeof(packet4)];
                memcpy(buffer4, &packet4, sizeof(packet4));
                //重发该包的ACK
                sendto(sockServ, buffer4, sizeof(packet4), 0, (sockaddr*)&ClientAddr, ClientAddrLen);

                //cout << "重发ACK" << endl;
                continue;//丢弃该数据包
            }

            //cout << "seq: " << seq << "head.seq: " << packet1.head.seq << endl;
            //取出buffer中的内容
            int curr_size = packet1.head.len;

            cout << "接收到文件大小为 " << curr_size << " bytes! Flag:" << int(packet1.head.flags) << " SEQ : " << int(packet1.head.seq) << " ACK : " << int(packet1.head.ack) << " CHECK : " << int(packet1.head.Check) << endl;

            memcpy(message + filesize, Buffer1 + sizeof(packet1), curr_size);
            //cout << "size" << sizeof(message) << endl;
            filesize = filesize + curr_size;

            //返回ACK
            Packet packet2;
            char* Buffer2 = new char[sizeof(packet2)];

            packet2.head.flags = FLAG_ACK;
            seq = int(packet1.head.seq);
            //packet2.head.len = 0;
            packet2.head.seq = seq;
            packet2.head.ack = seq+1;
            packet2.head.Check = 0;
            packet2.head.Check = packetcheck((u_short*)&packet2, sizeof(packet2));
            memcpy(Buffer2, &packet2, sizeof(packet2));
            //重发该包的ACK
            sendto(sockServ, Buffer2, sizeof(packet2), 0, (sockaddr*)&ClientAddr, ClientAddrLen);

            cout << "发送ACK，确认数据包发送成功  Flag:" << int(packet2.head.flags) << " SEQ : " << int(packet2.head.seq) << " ACK : " << int(packet2.head.ack) << " CHECK : " << int(packet2.head.Check) << endl;
           
        }
        else {
            if (packetcheck((u_short*)&packet1, sizeof(packet1)) != 0) {
                cout << "error" << endl;
                system("pause");
            }
            cout << "错误" << endl;
        }
    }

    mode = 0;
    ioctlsocket(sockServ, FIONBIO, &mode);  // 阻塞模式

    return filesize;
}


int main() {
    //初始化Windows Sockets库
    WSADATA d;
    WORD w = MAKEWORD(2, 2);
    int flag = WSAStartup(w, &d);

    if (flag == 0)
    {
        now = time(nullptr);
        ctime_s(curr_time, sizeof(curr_time), &now);
        cout << curr_time << "[Server]:";
        cout << "套接字库加载成功" << endl;
    }
    else
    {
        now = time(nullptr);
        ctime_s(curr_time, sizeof(curr_time), &now);
        cout << curr_time << "[Server]:";
        cout << "套接字库加载失败" << endl;
    }

    //创建UDP套接字
    SOCKET s = socket(AF_INET, SOCK_DGRAM, 0);
    if (s == INVALID_SOCKET) {
        now = time(nullptr);
        ctime_s(curr_time, sizeof(curr_time), &now);
        cout << curr_time << "[Server]:";
        cout << "创建套接字失败" << endl;
        WSACleanup();
        return 0;
    }

    int bufferSize = 6553600;  // 设置为6400KB（可以根据需求调整）
    setsockopt(s, SOL_SOCKET, SO_RCVBUF, (char*)&bufferSize, sizeof(bufferSize));


    //设置服务器地址
    sockaddr_in addr;
    addr.sin_family = AF_INET;
    addr.sin_addr.S_un.S_addr = inet_addr("127.0.0.1"); // IP地址
    addr.sin_port = htons(9999);//端口号

    //绑定套接字
    flag = bind(s, (sockaddr*)&addr, sizeof(addr));
    if (flag == 0)
    {
        now = time(nullptr);
        ctime_s(curr_time, sizeof(curr_time), &now);
        cout << curr_time << "[Server]:";
        cout << "绑定成功" << endl;
    }
    else
    {
        now = time(nullptr);
        ctime_s(curr_time, sizeof(curr_time), &now);
        cout << curr_time << "[Server]:";
        cout << "绑定失败" << endl;
        closesocket(s);
        WSACleanup();
        return 0;
    }

    //三次握手
    int len = sizeof(addr);
    flag = serverHandshake(s, addr, len);
    if (flag == 1)
    {
        now = time(nullptr);
        ctime_s(curr_time, sizeof(curr_time), &now);
        cout << curr_time << "[Server]:";
        cout << "建立连接成功" << endl;
    }
    else
    {
        now = time(nullptr);
        ctime_s(curr_time, sizeof(curr_time), &now);
        cout << curr_time << "[Server]:";
        cout << "建立连接失败" << endl;
        closesocket(s);
        WSACleanup();
        return 0;
    }

    //接收文件
    int namelen = 0;
    int datalen = 0;
    char* name = new char[100];
    char* data = new char[100000000];
    namelen = RecvMessage(s, addr, len, name);
    datalen = RecvMessage(s, addr, len, data);
    name[namelen] = '\0';
    data[datalen] = '\0';
    string filename(name);
    ofstream fout(filename.c_str(), ofstream::binary);
    now = time(nullptr);
    ctime_s(curr_time, sizeof(curr_time), &now);
    cout << curr_time << "[Server]:";
    cout << "文件名接收成功" << endl;
    for (int i = 0; i < datalen; i++)
    {
        fout << data[i];
    }
    fout.close();
    now = time(nullptr);
    ctime_s(curr_time, sizeof(curr_time), &now);
    cout << curr_time << "[Server]:";
    cout << "文件内容接收成功" << endl;


    //挥手
    flag = serverCloseConnection(s, addr, len);
    if (flag != 0)//判断绑定
    {
        now = time(nullptr);
        ctime_s(curr_time, sizeof(curr_time), &now);
        cout << curr_time << "[Server]:";
        cout << "断开连接成功" << endl;
    }
    else
    {
        now = time(nullptr);
        ctime_s(curr_time, sizeof(curr_time), &now);
        cout << curr_time << "[Server]:";
        cout << "断开连接失败" << endl;
    }
    closesocket(s);
    WSACleanup();
    return 0;
}