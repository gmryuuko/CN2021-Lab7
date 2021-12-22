#define isdigit(x) ('0' <= (x) && (x) <= '9')

/*
数据包格式：
字符'{'标记数据包开始
字符'}'标记数据包结束
直接禁止数据包中的数据出现这两个字符，防止干扰解析。
*/

/* 第一个字节标识指令类型 */
// 指令
#define CMD_GETTIME 0x33    // 获取服务器时间
#define CMD_GETNAME 0x34    // 获取服务器名称
#define CMD_CONNLIST 0x35   // 获取连接列表
#define CMD_SEND 0x36       // 发送消息

// 响应
#define RET_TIME 0x43       // 响应获取服务器时间请求
#define RET_NAME 0x44       // 响应获取服务器名称请求
#define RET_CONNLIST 0x45   // 响应获取连接列表请求
#define RET_SEND 0x46       // 响应发送请求
#define RET_BADCMD 0x4f     // 服务器无法解析上一个数据包

// 指示
#define SERV_FORWARD 0x50   // 服务器转发的来自其他客户端的消息
/* 剩下的数据根据指令类型选择解析方式 */

// 数据包
struct data_pkt {
    u_char command;
    char* str;
};

// #define DEBUG