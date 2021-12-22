// 服务器IP
#define SERV_IP "47.114.189.50"
// 服务器端口
#define SERV_PORT 6005
// 服务器最大同时连接数
#define MAX_CLIENT_CNT 32

// 接收队列缓冲大小
#define RECV_BUF_SIZE 0x400
// 循环队列
#define RECV_NEXT(x) (((x) + 1) & (RECV_BUF_SIZE - 1))