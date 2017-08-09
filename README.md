# UDPspeeder
![image0](images/Capture.PNG)
UDP加速器，降低UDP传输的丢包率。尤其适用于游戏和语音。

# 简明操作说明

### 环境要求
Linux主机，可以使是openwrt路由器，也可以是树莓派。在windows和mac上可以开虚拟机（桥接模式测试可用）。

### 安装
下载编译好的二进制文件，解压到任意目录。

https://github.com/wangyu-/UDPspeeder/releases

### 运行
假设你有一个server，ip为44.55.66.77，有一个服务监听在udp 7777端口。 假设你需要加速本地到44.55.66.77:7777的流量

```
在client端运行:
./speeder_ar71xx -l0.0.0.0:3323 -r 44.55.66.77:8855 -c  -d2

在server端运行:
./speeder_amd64 -l0.0.0.0:8855 -r127.0.0.1:7777 -s -d2
```

现在client和server之间建立起了tunnel。想要连接44.55.66.77:7777，只需要连接 127.0.0.1:3333。来回的所有的udp流量会被加速。

### 效果


# 进阶操作说明

### 命令选项
```
UDPspeeder
version: Aug  9 2017 18:13:09
repository: https://github.com/wangyu-/UDPspeeder

usage:
    run as client : ./this_program -c -l local_listen_ip:local_port -r server_ip:server_port  [options]
    run as server : ./this_program -s -l server_listen_ip:server_port -r remote_ip:remote_port  [options]

common option,must be same on both sides:
    -k,--key              <string>        key for simple xor encryption,default:"secret key"
main options:
    -d                    <number>        duplicated packet number, -d 0 means no duplicate. default value:0
    -t                    <number>        duplicated packet delay time, unit: 0.1ms,default value:20(2ms)
    -j                    <number>        simulated jitter.randomly delay first packet for 0~jitter_value*0.1 ms,to
                                          create simulated jitter.default value:0.do not use if you dont
                                          know what it means
    --report              <number>        turn on udp send/recv report,and set a time interval for reporting,unit:s
advanced options:
    -t                    tmin:tmax       simliar to -t above,but delay randomly between tmin and tmax
    -j                    jmin:jmax       simliar to -j above,but create jitter randomly between jmin and jmax
    --random-drop         <number>        simulate packet loss ,unit 0.01%
    -m                    <number>        max pending packets,to prevent the program from eating up all your memory.
other options:
    --log-level           <number>        0:never    1:fatal   2:error   3:warn 
                                          4:info (default)     5:debug   6:trace
    --log-position                        enable file name,function name,line number in log
    --disable-color                       disable log color
    --sock-buf            <number>        buf size for socket,>=10 and <=10240,unit:kbyte,default:512
    -h,--help                             print this help message

```

# 应用

#### UDPspeeder + openvpn加速任何流量
![image0](images/Capture2.PNG)
#### UDPspeeder + kcptun/finalspeed同时加速tcp和udp流量
这样对tcp而言，加速效果更好。
![image0](images/Capture3.PNG)

