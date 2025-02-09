#include<WinSock2.h>
#include<Windows.h>
#include<iostream>
#include <stdint.h>
#include <cstring>
#include <cstdlib>
#include <thread>
#include <chrono>
#include <time.h>
#include <fstream>
#include <thread>
#include <mutex>
#include <algorithm>
#include <condition_variable>
#pragma comment(lib,"ws2_32.lib")
using namespace std;
std::mutex windowMutex;
std::mutex coutMutex;
std::condition_variable ackCv;
std::mutex ackMutex;

struct Packethead {
    int seq;     // 序列号 16位
    uint16_t Check;   // 校验 16位
    uint16_t len;     // 数据部分总长度
    int ack;     //确认号
    unsigned char flags; // 标志位

    Packethead() : seq(0), Check(0), len(0), flags(0), ack(0) {}
};

struct Packet {
    Packethead head;
    char data[2048]; //数据部分

    Packet() : head(), data() {}
};

// 标志位常量
const uint8_t FLAG_SYN = 0x01;
const uint8_t FLAG_ACK = 0x02;
const uint8_t FLAG_SYN_ACK = FLAG_SYN | FLAG_ACK;
const uint8_t FLAG_FIN = 0x04;
const uint8_t FLAG_FIN_ACK = 0x06;
const unsigned char OVER = 0x08;//结束标志
const int MAXSIZE = 10240;//传输缓冲区最大长度
double MAX_TIME = 0.5 * CLOCKS_PER_SEC;
const uint8_t SLOW_START = 0x09;//慢启动
const uint8_t CON_AVOID = 0x0A;//拥塞控制
const uint8_t FAST_RECOVERY = 0x0B;//快速恢复

struct sendwindow {
    int size;
    int start;
    int end;
    int maxseq;//已发送但未确认的最大seq
    int ssthresh;//慢开始门限
    int curacq;//重复的ack序号
    int ackcount;//重复的ack个数
    char state;//当前状态
    int resend;

    sendwindow() :size(1), start(1), end(1) , maxseq(0),ssthresh(14), curacq(0), ackcount(0),state(SLOW_START),resend(0){}
};
sendwindow window;

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

    while (recvfrom(s, buffer2, sizeof(packet2), 0, (sockaddr*)&clientAddr, &sockLen) <= 0) {
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
    //std::cout << "[Client]: FIN_ACK packet sent successfully." << std::endl;
    // 等待服务器发送的ACK包
    Packet packet2;
    char* buffer2 = new char[sizeof(packet2)];

    bool ackReceived = false;

    while (recvfrom(s, buffer2, sizeof(packet2), 0, (sockaddr*)&ClientAddr, &sockLen) <= 0) {
        if (clock() - start > MAX_TIME)//超时，重新传输第一次握手
        {
            return 1;
            //std::cout << "[Client]:Timeout Retransmission,resending FIN_ACK packet..." << std::endl;
            //sendto(s, buffer1, sizeof(packet1), 0, (sockaddr*)&ClientAddr, sockLen);
            //start = clock();
        }
    }
    memcpy(&packet2, buffer2, sizeof(packet2));
    u_short res = packetcheck((u_short*)&packet2, sizeof(packet2));

    if (packet2.head.flags == FLAG_ACK && packet2.head.seq == 1 && res == 0) {
        std::cout << "[Client]: Received ACK packet with seq=" << packet2.head.seq << std::endl;
        ackReceived = true;
    }
    else {
        std::cout << "[Client]: Failed to receive ACK packet." << std::endl;
        //return 0;
    }

    // 等待服务器发送的FIN_ACK包
    bool finackReceived = false;
    Packet packet3;
    char* buffer3 = new char[sizeof(packet3)];

    while (1) {
        u_short res = packetcheck((u_short*)&packet3, sizeof(packet3));

        if (recvfrom(s, buffer3, sizeof(packet3), 0, (sockaddr*)&ClientAddr, &sockLen) <= 0) {
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

void handleAckReception(SOCKET& socketClient, SOCKADDR_IN& servAddr, int& servAddrlen, int packagenum)
{
    Packet packet2;
    char* buffer2 = new char[sizeof(packet2)];
    int num = 0;
    while (window.start <= packagenum)
    {
        if (recvfrom(socketClient, buffer2, sizeof(packet2), 0, (sockaddr*)&servAddr, &servAddrlen) > 0)
        {
            memcpy(&packet2, buffer2, sizeof(packet2));
            u_short check2 = packetcheck((u_short*)&packet2, sizeof(packet2));

            std::lock_guard<std::mutex> lock(windowMutex); // 加锁
            if (int(check2) != 0)
            {
                //window.end = window.start + 1;

                //std::lock_guard<std::mutex> guarderror(coutMutex);
                cout << "接收到错误的ACK" << endl;
                continue;
            }
            else
            {
                if (int(packet2.head.seq) >= window.start)
                {
                    //如果为慢启动状态，cwnd翻倍
                    if (window.state == SLOW_START) 
                    {
                        int nowsize = window.size;
                        num = 0;
                        window.start = int(packet2.head.seq) + 1;
                        window.size += 1;
                        window.end = window.start + window.size -1;
                        if (window.end >= packagenum) 
                        {
                            window.end = packagenum;
                        }
                        window.curacq = packet2.head.seq;
                        window.ackcount = 0;
                        std::lock_guard<std::mutex> guardack(coutMutex);
                        cout << endl;
                        cout << "慢启动阶段，窗口大小为: " << window.size << " 窗口大小变化为: " << window.size - nowsize << endl;
                        cout << "接收到ACK，确认数据包发送成功  Flag:" << int(packet2.head.flags) << " SEQ:" << int(packet2.head.seq) << " ACK:" << int(packet2.head.ack) << " CHECK:" << int(packet2.head.Check) << endl;
                        if (window.size >= window.ssthresh) 
                        {
                            //进入拥塞避免阶段
                            window.state = CON_AVOID;
                            cout << "慢启动阶段进入拥塞避免阶段" << endl;
                        }
                    }
                    //拥塞避免阶段收到ACK线性增长
                    if (window.state == CON_AVOID)
                    {
                        int nowsize = window.size;
                        num += 1;
                        window.start = int(packet2.head.seq) + 1;
                        if (num == window.size)
                        {
                            //cout << "拥塞避免增加窗口大小" << endl;
                            window.size += 1;
                            num = 0;
                        }
                        window.end = window.start + window.size - 1;
                        if (window.end >= packagenum)
                        {
                            window.end = packagenum;
                        }
                        window.curacq = packet2.head.seq;
                        window.ackcount = 0;
                        std::lock_guard<std::mutex> guardack(coutMutex);
                        cout << endl;
                        cout << "拥塞避免阶段，窗口大小为: " << window.size << " 窗口大小变化为: " << window.size - nowsize << endl;
                        cout << "接收到ACK，确认数据包发送成功  Flag:" << int(packet2.head.flags) << " SEQ:" << int(packet2.head.seq) << " ACK:" << int(packet2.head.ack) << " CHECK:" << int(packet2.head.Check) << endl;
                    }
                    //快速恢复阶段
                    if (window.state == FAST_RECOVERY)
                    {
                        int nowsize = window.size;
                        num = 0;
                        window.state = CON_AVOID;
                        window.start = int(packet2.head.seq) + 1;
                        window.size = window.ssthresh;
                        window.end = window.start + window.size - 1;
                        if (window.end >= packagenum)
                        {
                            window.end = packagenum;
                        }
                        window.curacq = packet2.head.seq;
                        window.ackcount = 0;
                        std::lock_guard<std::mutex> guardack(coutMutex);
                        cout << endl;
                        cout << "当前为快速恢复阶段，窗口大小为: " << window.size << " 窗口大小变化为: " << window.size - nowsize << endl;
                        cout << "接收到ACK，确认数据包发送成功  Flag:" << int(packet2.head.flags) << " SEQ:" << int(packet2.head.seq) << " ACK:" << int(packet2.head.ack) << " CHECK:" << int(packet2.head.Check) << endl;
                        cout << "快速恢复阶段进入拥塞避免阶段" << endl;
                    }
                    //std::lock_guard<std::mutex> guardack(coutMutex);
                    //cout << "接收到ACK，确认数据包发送成功  Flag:" << int(packet2.head.flags) << " SEQ:" << int(packet2.head.seq) << " ACK:" << int(packet2.head.ack) << " CHECK:" << int(packet2.head.Check) << " 窗口起始：" << window.start << " 窗口结束：" << window.end << " 窗口大小：" << window.size << endl;

                }
                else
                {
                    if (packet2.head.seq == window.curacq)
                    {
                        int nowsize = window.size;
                        window.ackcount += 1;
                        if (window.ackcount > 3)
                        {
                            window.size += 1;
                        }
                        std::lock_guard<std::mutex> guardack(coutMutex);
                        cout << "收到重复的ack，序列号为："<< packet2.head.seq << " 此时ackcount：" << window.ackcount <<" 窗口大小：" << window.size << " 窗口大小变化为: " << window.size - nowsize << endl;
                    }
                    //一旦受到重复的3个ACK就进入快速恢复状态
                    if (window.ackcount == 3)
                    {
                        int nowsize = window.size;
                        window.state = FAST_RECOVERY;
                        window.ssthresh = window.size / 2;
                        window.size = window.ssthresh + 3;
                        window.end = window.start + window.size - 1;
                        if (window.end >= packagenum)
                        {
                            window.end = packagenum;
                        }
                        window.resend = 1;
                        std::lock_guard<std::mutex> guardack(coutMutex);
                        cout << endl;
                        cout << "收到3个重复ACK，序列号为：" << packet2.head.seq << " 重传数据包，进入快速恢复阶段，窗口大小为: " << window.size << " 窗口大小变化为: " << window.size - nowsize << endl;
                    }
                    continue;
                }
            }
            //锁自动释放
        }
    }
    delete[] buffer2;
}

void send(SOCKET& socketClient, SOCKADDR_IN& servAddr, int& servAddrlen, char* message, int messagelen)
{
    u_long mode = 1;
    ioctlsocket(socketClient, FIONBIO, &mode);
    int seqnum = 1;//记录下一个要发送的数据包序列号
    int packagenum = (messagelen / MAXSIZE) + (messagelen % MAXSIZE != 0);
    cout << packagenum << endl;
    int len = 0;
    //sendwindow window;
    window.start = 1;
    window.size = 1;
    window.end = 1;
    window.state = SLOW_START;
    window.ssthresh = 14;
    window.maxseq = 0;
    window.curacq = 0;
    window.ackcount = 0;
    window.resend = 0;
    clock_t start = clock();

    // 启动ACK接收线程
    std::thread ackThread(handleAckReception, std::ref(socketClient), std::ref(servAddr), std::ref(servAddrlen), std::ref(packagenum));
    //ackThread.detach();

    //std::lock_guard<std::mutex> lock(windowMutex);
    while (seqnum <= packagenum+1)
    {
        if (window.maxseq < window.end)
        {
            for (int i = window.maxseq +1 ; i <= window.end;i++)
            {
                len = (i == packagenum ? (messagelen - ((packagenum - 1) * MAXSIZE)) : MAXSIZE);
                Packet packet1;
                char* buffer1 = new char[len + sizeof(packet1)];
                packet1.head.len = len;
                packet1.head.seq = seqnum;//序列号
                packet1.head.Check = 0;
                u_short check = packetcheck((u_short*)&packet1, sizeof(packet1));//计算校验和
                packet1.head.Check = check;
                packet1.head.flags = 0;
                packet1.head.ack = 0;
                memcpy(buffer1, &packet1, sizeof(packet1));
                char* mes = message + (i - 1) * MAXSIZE;
                memcpy(buffer1 + sizeof(packet1), mes, len);//将数据复制到缓冲区的后部分
                sendto(socketClient, buffer1, len + sizeof(packet1), 0, (sockaddr*)&servAddr, servAddrlen);//发送
                std::lock_guard<std::mutex> guardsend(coutMutex);
                cout << "发送文件大小为 " << len << " bytes!" << " Flag:" << int(packet1.head.flags) << " SEQ:" << int(packet1.head.seq) << " ACK:" << int(packet1.head.ack) << " CHECK:" << int(packet1.head.Check) << endl;
                start = clock();
                seqnum = i+1;
                window.maxseq = i;
                if (window.resend == 1)
                {
                    break;
                }
            }
        }
        if (window.resend == 1)
        {
            for (int j = window.start; j <= window.end; j++)
            {
                len = (j == packagenum ? (messagelen - ((packagenum - 1) * MAXSIZE)) : MAXSIZE);
                Packet packet2;
                char* buffer2 = new char[len + sizeof(packet2)];
                packet2.head.len = len;
                packet2.head.seq = j;//序列号
                packet2.head.Check = 0;
                u_short check = packetcheck((u_short*)&packet2, sizeof(packet2));//计算校验和
                packet2.head.Check = check;
                packet2.head.flags = 0;
                packet2.head.ack = 0;
                memcpy(buffer2, &packet2, sizeof(packet2));
                char* mes = message + (j - 1) * MAXSIZE;
                memcpy(buffer2 + sizeof(packet2), mes, len);//将数据复制到缓冲区的后部分
                sendto(socketClient, buffer2, len + sizeof(packet2), 0, (sockaddr*)&servAddr, servAddrlen);//发送

                std::lock_guard<std::mutex> guardsend(coutMutex);
                cout << "发送文件大小为 " << len << " bytes!" << " Flag:" << int(packet2.head.flags) << " SEQ:" << int(packet2.head.seq) << " ACK:" << int(packet2.head.ack) << " CHECK:" << int(packet2.head.Check) << endl;
                start = clock();
            }
            window.resend = 0;
            window.maxseq = window.end;
            seqnum = window.end + 1;
        }
        if (clock() - start > MAX_TIME)
        {
            //超时进入慢启动状态
            window.ssthresh = window.size / 2;
            window.size = 1;
            window.end = window.start + window.size - 1;
            if (window.end >= packagenum)
            {
                window.end = packagenum;
            }
            window.state = SLOW_START;
            window.ackcount = 0;
            cout << endl;
            cout << "超时重传，进入慢启动状态，当前ssthresh值为：" << window.ssthresh << endl;
            for (int j = window.start; j <= window.end; j++)
            {
                len = (j == packagenum ? (messagelen - ((packagenum - 1) * MAXSIZE)) : MAXSIZE);
                Packet packet2;
                char* buffer2 = new char[len + sizeof(packet2)];
                packet2.head.len = len;
                packet2.head.seq = j;//序列号
                packet2.head.Check = 0;
                u_short check = packetcheck((u_short*)&packet2, sizeof(packet2));//计算校验和
                packet2.head.Check = check;
                packet2.head.flags = 0;
                packet2.head.ack = 0;
                memcpy(buffer2, &packet2, sizeof(packet2));
                char* mes = message + (j-1) * MAXSIZE;
                memcpy(buffer2 + sizeof(packet2), mes, len);//将数据复制到缓冲区的后部分
                sendto(socketClient, buffer2, len + sizeof(packet2), 0, (sockaddr*)&servAddr, servAddrlen);//发送

                std::lock_guard<std::mutex> guardsend(coutMutex);
                cout << "发送文件大小为 " << len << " bytes!" << " Flag:" << int(packet2.head.flags) << " SEQ:" << int(packet2.head.seq) << " ACK:" << int(packet2.head.ack) << " CHECK:" << int(packet2.head.Check) << endl;
                start = clock();
            }
            window.maxseq = window.end;
            seqnum = window.end +1;
        }
        if (window.start > packagenum)
        {
            break;
        }
        else 
        {
            continue;
        }
    }
    ackThread.join();

    // 等待所有ACK
    /*std::unique_lock<std::mutex> lock(ackMutex);
    std::this_thread::sleep_for(std::chrono::milliseconds(100));*/

    //发送结束信息
    Packet packet5;
    char* Buffer5 = new char[sizeof(packet5)];
    packet5.head.flags = OVER;//结束信息
    packet5.head.Check = 0;
    u_short temp = packetcheck((u_short*)&packet5, sizeof(packet5));
    packet5.head.Check = temp;

    memcpy(Buffer5, &packet5, sizeof(packet5));
    sendto(socketClient, Buffer5, sizeof(packet5), 0, (sockaddr*)&servAddr, servAddrlen);
    //cout << "发送文件完毕！" << endl;
    start = clock();

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