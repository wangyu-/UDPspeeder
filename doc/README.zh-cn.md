# UDPspeeder
![image0](/images/cn/speedercn.PNG)

双边网络加速工具，软件本身的功能是加速UDP；不过，配合vpn可以加速全流量(包括TCP/UDP/ICMP)。通过合理配置，可以加速游戏，降低游戏的丢包和延迟；也可以加速下载和看视频这种大流量的应用。用1.5倍的流量，就可以把10%的丢包率降低到万分之一以下。跟 kcptun/finalspeed/BBR 等现有方案比，主要优势是可以加速 UDP 和 ICMP，现有方案几乎都只能加速 TCP。

我自己稳定用了几个月，用来加速美服的Brawl Stars和亚服的Mobile Legend，效果不错，加速前卡得几乎没法玩，加速后就没怎么卡过了。用来看视频也基本满速。

最新的版本是v2版，在v1版的基础上增加了FEC功能，更省流量。如果你用的是v1版（路由器固件里自带的集成版很可能是v1版的），请看[v1版主页](/doc/README.zh-cn.v1.md)

配合vpn加速全流量的原理图(已测试支持VPN的有OpenVPN、L2TP、$\*\*\*VPN):

![image0](/images/Capture2.PNG)

对于某些运营商，UDPspeeder跟udp2raw配合可以达到更好的速度，udp2raw负责把UDP伪装成TCP，来绕过运营商的UDP限速。

udp2raw的repo:

https://github.com/wangyu-/udp2raw-tunnel

如果你嫌UDPspeeder+OpenVPN麻烦，你可以尝试tinyFecVPN，一个集成了UDPspeeder功能的VPN：

tinyFecVPN的repo:

https://github.com/wangyu-/tinyFecVPN


#### 效果
![image0](/images/cn/ping_compare_cn.PNG)

![image0](/images/cn/scp_compare.PNG)
#### 原理简介
主要原理是通过冗余数据来对抗网络的丢包，发送冗余数据的方式支持FEC(Forward Error Correction)和多倍发包，其中FEC算法是Reed-Solomon。

FEC方式的原理图:

![image0](/images/en/fec.PNG)

#### 其他功能
对包的内容和长度做随机化（可以理解为混淆），从抓包看不出你发送了冗余数据，不用担心vps被封。

在多个冗余包之间引入延迟（时间可配）来对抗突发性的丢包，避开中间路由器因为瞬时buffer长度过长而连续丢掉所有副本。

模拟一定的延迟抖动（时间可配），这样上层应用计算出来的RTT方差会更大，以等待后续冗余包的到达，不至于发生在冗余包到达之前就触发重传的尴尬。

输出UDP收发情况报告，可以看出丢包率。

模拟丢包，模拟延迟，模拟jitter。便于通过实验找出应用卡顿的原因。

client支持多个udp连接，server也支持多个client

# 简明操作说明

### 环境要求
Linux主机，可以是桌面版，可以是android手机/平板，可以是openwrt路由器，也可以是树莓派。在windows和mac上配合虚拟机可以稳定使用（speeder跑在Linux里，其他应用照常跑在window里，桥接模式测试可用），可以使用[这个](https://github.com/wangyu-/udp2raw-tunnel/releases/download/20171108.0/lede-17.01.2-x86_virtual_machine_image.zip)虚拟机镜像，大小只有7.5mb，免去在虚拟机里装系统的麻烦；虚拟机自带ssh server，可以scp拷贝文件，可以ssh进去，可以复制粘贴，root密码123456。

android版需要通过terminal运行。

###### 注意
在使用虚拟机时，建议手动指定桥接到哪个网卡，不要设置成自动。否则可能会桥接到错误的网卡。

### 安装
下载编译好的二进制文件，解压到本地和服务器的任意目录。

https://github.com/wangyu-/UDPspeeder/releases

### 运行
假设你有一个server，ip为44.55.66.77，有一个服务监听在udp 7777端口。 假设你需要加速本地到44.55.66.77:7777的流量。
```
在server端运行:
./speederv2 -s -l0.0.0.0:4096 -r127.0.0.1:7777  -f20:10 -k "passwd"

在client端运行:
./speederv2 -c -l0.0.0.0:3333 -r44.55.66.77:4096 -f20:10 -k "passwd"
```

现在client和server之间建立起了tunnel。想要连接44.55.66.77:7777，只需要连接 127.0.0.1:3333。来回的所有的udp流量会被加速。

###### 备注:

`-f20:10` 表示对每20个原始数据发送10个冗余包。`-f20:10` 和`-f 20:10`都是可以的，空格可以省略，对于所有的单字节option都是如此。对于双字节option，例如后面的`--mode 0`和`--mtu 1200`，空格不可以省略。

`-k` 指定一个字符串，开启简单的异或加密

如果需要更省流量，请加上`--mode 0`，`--mode 0`模式会牺牲一点点的延迟(默认最多8ms，可调)换取更低的流量消耗。

###### 注意

要为UDPspeeder加速的应用设置好MTU(不是在UDPspeeder中，是在被加速的应用中)，建议设置为1200。 另外，如果被加速的应用不能调整MTU，也可以在UDPspeeder中通过`--mode 0 --mtu 1200`设置MTU。关于`--mode 0`和`--mtu`的具体解释请看下文。

# 进阶操作说明

### 命令选项
```
UDPspeeder V2
git version:8e7a8aed92    build date:Oct 25 2017 02:00:54
repository: https://github.com/wangyu-/UDPspeeder

usage:
    run as client : ./this_program -c -l local_listen_ip:local_port -r server_ip:server_port  [options]
    run as server : ./this_program -s -l server_listen_ip:server_port -r remote_ip:remote_port  [options]

common option,must be same on both sides:
    -k,--key              <string>        key for simple xor encryption. if not set,xor is disabled
main options:
    -f,--fec              x:y             forward error correction,send y redundant packets for every x packets
    --timeout             <number>        how long could a packet be held in queue before doing fec,unit: ms,default :8ms
    --mode                <number>        fec-mode,available values: 0,1 ; 0 cost less bandwidth,1 cost less latency(default)
    --report              <number>        turn on send/recv report,and set a period for reporting,unit:s
advanced options:
    --mtu                 <number>        mtu. for mode 0,the program will split packet to segment smaller than mtu_value.
                                          for mode 1,no packet will be split,the program just check if the mtu is exceed.
                                          default value:1250
    -j,--jitter           <number>        simulated jitter.randomly delay first packet for 0~<number> ms,default value:0.
                                          do not use if you dont know what it means.
    -i,--interval         <number>        scatter each fec group to a interval of <number> ms,to protect burst packet loss.
                                          default value:0.do not use if you dont know what it means.
    --random-drop         <number>        simulate packet loss ,unit:0.01%. default value: 0
    --disable-obscure     <number>        disable obscure,to save a bit bandwidth and cpu
developer options:
    --fifo                <string>        use a fifo(named pipe) for sending commands to the running program,so that you
                                          can change fec encode parameters dynamically,check readme.md in repository for
                                          supported commands.
    -j ,--jitter          jmin:jmax       similiar to -j above,but create jitter randomly between jmin and jmax
    -i,--interval         imin:imax       similiar to -i above,but scatter randomly between imin and imax
    -q,--queue-len        <number>        max fec queue len,only for mode 0
    --decode-buf          <number>        size of buffer of fec decoder,unit:packet,default:2000
    --fix-latency         <number>        try to stabilize latency,only for mode 0
    --delay-capacity      <number>        max number of delayed packets
    --disable-fec         <number>        completely disable fec,turn the program into a normal udp tunnel
    --sock-buf            <number>        buf size for socket,>=10 and <=10240,unit:kbyte,default:1024
log and help options:
    --log-level           <number>        0:never    1:fatal   2:error   3:warn
                                          4:info (default)     5:debug   6:trace
    --log-position                        enable file name,function name,line number in log
    --disable-color                       disable log color
    -h,--help                             print this help message

```
### 包发送选项，两端设置可以不同。 只影响本地包发送。
##### `-f` 选项
设置fec参数，影响数据的冗余度。
##### `--timeout` 选项
指定fec编码器在编码时候最多可以引入多大的延迟。越高fec越有效率，加速游戏时调低可以降低延迟。

#####  `--mode` 选项 和 `--mtu`选项
fec编码器的工作模式。对于mode 0，编码器会积攒一定数量的packet，然后把他们合并再切成等长的片段（切分长度由--mtu指定）。对于mode 1，编码器不会做任何切分，而是会把packet按最大长度对齐，fec冗余包的长度为对齐后的长度（最大长度）。

mode 0更省流量，在丢包率正常的情况下效果和mode 1是一样的；mode 1延迟更低，在极高丢包的情况下表现更好。

mode 0使用起来可以不用关注mtu，因为fec编码器会帮你把包切分到合理的大小；用mode 1时必须合理设置上层应用的mtu。mode 0模式中--mtu选项决定切分的片段的长度，mode 1模式中--mtu选项只起检查作用，如果超过了--mtu指定的值，数据包会被丢弃。

mode 0模式的流量消耗基本完全透明。mode 1因为涉及到数据按最大长度对齐，所以流量消耗不是完全可预期。<del>不过就实际使用来看，mode 1消耗的额外流量不多。</del> mode 1一般会比mode 0多消耗零点几倍的流量，对于在意流量的人，推荐用mode 0。

mode 0模式数据包一般不会乱序，除非网络本身有严重乱序；mode 1模式被恢复的数据包可能会乱序，不过UDP本来就允许乱序，对绝大多数应用没有影响。mode 0模式反而可以纠正一些乱序情况。

mode 0模式允许你发送的数据包大小超过物理接口的MTU而几乎不引起性能损失（而普通的ip分片做不到这点），目前最高支持到2000字节，2000字节已经可以应对任何应用了，因为一般网络的MTU只有1400多。之所以支持到2000字节是为了省程序内部开的静态buff(静态buff避免malloc提高性能)，如果你是开发者，通过重新编译，支持到UDP协议的极限(
65507字节)也没问题。

##### `--report`  选项
数据发送和接受报告。开启后可以根据此数据推测出包速和丢包率等特征。

##### `-i` 选项
指定一个时间窗口，长度为n毫秒。同一个fec分组的数据在发送时候会被均匀分散到这n毫秒中。可以对抗突发性的丢包。默认值是0，因为这个功能需要用到时钟，在某些虚拟机里时钟不稳定，可能会导致个别包出现非常大的延迟，所以默认关掉了。这个功能很有用，默认参数效果不理想时可以尝试打开。这个选项的跟通信原理上常说的`交错fec` `交织fec`的原理是差不多的。

##### `-j` 选项
为原始数据的发送，增加一个延迟抖动值。这样上层应用计算出来的RTT方差会更大，以等待后续冗余包的到达，不至于发生在冗余包到达之前就触发重传的尴尬。配合-t选项使用。正常情况下跨国网络本身的延迟抖动就很大，可以不用设-j。这个功能也需要时钟，默认关掉了，不过一般情况应该不需要这个功能。

-j选项不但可以模拟延迟抖动，也可以模拟延迟。

##### `--random-drop` 选项
随机丢包。模拟高丢包的网络环境时使用。 `--random-drop`和`-j`选项一起用，可以模拟高延迟（或者高延迟抖动）高丢包的网络，可用于测试FEC参数在各种网络环境下的表现。

##### `-k`选项
指定一个字符串，server/client间所有收发的包都会被异或，改变协议特征，防止UDPspeeder的协议被运营商针对。

##### `--disable-obscure`
UDPspeeder默认情况下会对每个发出的数据包随机填充和异或一些字节(4~32字节)，这样通过抓包难以发现你发了冗余数据，防止VPS被封。这个功能只是为了小心谨慎，即使你关掉这个功能，基本上也没问题，关掉可以省一些带宽和CPU。

#### `--fifo` option
用fifo(命名管道)向运行中的程序发送command。例如`--fifo fifo.file`，可用的command有：
```
echo fec 19:9 > fifo.file
echo mtu 1100 > fifo.file
echo timeout 5 > fifo.file
echo queue-len 100 > fifo.file
echo mode 0 > fifo.file
```
可以动态改变fec编码器参数。可以从程序的log里看到command是否发送成功。

# 使用经验

### 在FEC和多倍发包之间如何选择

对于游戏，游戏的流量本身不大，延迟很重要，多倍发包是最佳解决方案，多倍发包不会引入额外的延迟。FEC编码器需要先积攒一些数据，才可以做FEC，延迟无法避免；对于多倍发包，没有这个问题，所以没有延迟。

对于其他日常应用（延迟要求一般），在合理配置的情况下，FEC的效果肯定好过多倍发包。不过需要根据网络的最大丢包来配置FEC参数，才能有稳定的效果。如果配置不当，对于--mode 1可能会完全没有效果；对于--mode 0，可能效果会比不用UDPspeeder还差。

对于游戏以外的应用，推荐使用FEC。但是，如果FEC版的默认参数在你那边效果很差，而你又不会调，可以先用多倍发包。

### V2版如何多倍发包

只要在设置-f参数时把x设置为1，fec算法就退化为多倍发包了。例如-f1:1，表示2倍发包，-f1:2表示3倍发包，以此类推。另外建议加上"--mode 1"参数，防止fec编码器试图积攒和合并数据，获得最低的延迟。

2倍发包的完整参数：

```
./speederv2 -s -l0.0.0.0:4096 -r127.0.0.1:7777  -f1:1 -k "passwd" --mode 1
./speederv2 -c -l0.0.0.0:3333 -r44.55.66.77:4096 -f1:1 -k "passwd" --mode 1
```

使用了`--mode 1`以后，`--timeout`选项不起作用，所以不用调。

如果你只需要多倍发包，可以直接用回V1版，V1版配置更简单，占用内存更小，而且经过了几个月的考验，很稳定。

### 根据网络丢包合理设置FEC参数

默认的FEC参数为-f20:10，对每20个包，额外发送10个冗余包，也就是1.5倍发包。已经可以适应绝大多数的网络情况了，对于10%的网络丢包，可以降低到0.01%以下；对于20%的网络丢包，可以降低到2.5%。

如果你的网络丢包很低，比如在3%以下，可以尝试调低参数。比如-f20:5，也就是1.2倍发包，这个参数已经足够把3%的丢包降低到0.01%以下了。

如果网络丢包超过20%，需要把-f20:10调大。

如果你实在不会配，那么也可以用回V1版。

### 根据CPU处理能力来调整FEC参数

FEC算法很吃CPU，初次使用建议关注UDPspeeder的CPU占用。如果CPU被打满，可以在冗余度不变的情况下把FEC分组大小调小，否则的话效果可能很差。

比如-f20:10和-f10:5，都是1.5倍的冗余度，而-f20:10的FEC分组大小是30个包，-f10:5的FEC分组大小是15个包。-f20:10更费CPU，但是在一般情况下效果更稳定。把分组调小可以节省CPU。

另外，fec分组大小不宜过大，否则不但很耗CPU，还有其他副作用，建议x+y<50。

### 改变FEC参数而不断线

`--fifo`选项可以在运行时改变FEC参数，无需重启程序，也不会断线。如果你在使用过程中发现网络丢包突然变高，可以动态地把冗余度调大；反之也一样，如果网络变好了，把冗余度调小节省流量。一切都是无缝进行，不会断线，也不会因为改FEC参数导致额外的丢包。

### 为什么使用之后效果反而变差了？

有可能是你用了`--mode 0`参数，而又没调好参数。

如果你没有使用`--mode 0`，而确实效果变差了，那很可能是因为你的运营商对UDP有限制。一般看视频和下载都是TCP流量，而用UDPspeeder中转后流量变成了UDP流量，如果运营商对UDP做了限制，就可能导致效果比不用还差。用udp2raw可以解决，udp2raw: https://github.com/wangyu-/udp2raw-tunnel


### UDPspeeder和BBR/锐速配合

UDPspeeder和BBR/锐速可以配合使用，UDPspeeder工作在IP层负责降低丢包率，BBR/锐速工作在TCP层负责优化拥塞和重传。这种情况下，可以调低UDPspeeder的冗余度，能把丢包率降低到5%以内就可以了，剩下的交给BBR/锐速解决，这样预计可以节省一些流量。如果是UDPspeeder跟Linux默认的Cubic一起用，最少也要把丢包率降低到1%以下才能流畅使用TCP。

对下文的`UDPspeeder + openvpn`和`UDPspeeder + openvpn + $***`方法有效。不过有一点区别，具体见下文。

### UDPspeeder和Kcptun配合

UDPspeeder和Kcptun配合，UDPspeeder和Kcptun可以并联也可以串联。

并联的情况下，让kcptun负责加速TCP，UDPspeeder负责加速UDP。见下文的`UDPspeeder + kcptun + $*** 同时加速tcp和udp流量`。

串联的情况。UDPspeeder的FEC跟Kcptun自带的相比：可以对两个方向设置不同的FEC参数、有一个更省流量的mode 0模式、可以动态改变FEC参数；但是UDPspeeder本身不优化拥塞和重传算法。所以UDPspeeder和Kcptun也可以配合使用，结合两者的优点。

串联时可以关掉Kcptun的FEC，让UDPspeeder接管FEC功能。这样UDPspeeder工作在UDP层负责降低丢包率，Kcptun工作在应用层用kcp算法负责优化拥塞和重传，能起到和`UDPspeeder+BBR/锐速`类似的效果。

如果发Issue问Kcptun+UDPspeeder相关的问题，一定要说明是并联还是串联。

# 应用

#### UDPspeeder + OpenVPN加速任何流量，也适用于其他VPN
![image0](/images/Capture2.PNG)

可以和BBR/锐速叠加，不过BBR/锐速部署在VPS上只对从本地到VPS的流量有效，对从本地到第三方服务器的流量无效。

需要在服务端开启ipforward和NAT。在客户端改路由表（可以手动修改，也可以由OpenVPN的redirect-gateway选项自动加好）。

Linux具体配置: [UDPspeeder + openvpn config guide](/doc/udpspeeder_openvpn.md).

Windows具体配置: [win10系统UDPspeeder+OpenVPN的完整设置](https://github.com/wangyu-/UDPspeeder/wiki/win10系统UDPspeeder-OpenVPN的完整设置)

如果UDPspeeder + OpenVPN对你来说显得太麻烦了，你可以尝试一下tinyFecVPN,一个集成了UDPspeeder功能的VPN:

https://github.com/wangyu-/tinyFecVPN/

#### UDPspeeder + kcptun/finalspeed + $*** 同时加速tcp和udp流量
如果你需要用加速的tcp看视频和下载文件，这样效果可能比没有BBR的UDPspeeder+vpn方案更好。另外，如果你需要玩游戏，但是嫌配VPN麻烦，也可以用这种方案。
![image0](/images/cn/speeder_kcptun.PNG)

具体配置方法简介:

假设$\*\*\*  server监听在在44.55.66.77的443端口(tcp和udp同时)。用kcptun把tcp 443映射到本地的tcp 1234；用UDPspeeder把udp 443的映射到本地的udp 1234。
然后让$\*\*\* client 去连127.0.0.1:1234就可以了，tcp和udp都被加速了。完整命令：
```
run at server side:
./kcp_server  -l ":4000" -t "127.0.0.1:443" -mode fast2
./speederv2 -s -l0.0.0.0:4001 -r127.0.0.1:443  -f20:10 -k "passwd"

run at client side:
./kcp_client  -l ":1234" -r "44.55.66.77:4000" -mode fast2
./speederv2 -c -l0.0.0.0:1234 -r44.55.66.77:4001 -f20:10 -k "passwd"
```

这就是全部的命令了。issue里有很多人困惑于怎么把tcp和udp流量"分开"，其实很简单就可以做到。

如果只需要加速UDP，不需要加速TCP，可以把kcptun换成其他的任意端口转发方式，比如ncat/socat/ssh tunnel/iptables/[tinyPortMapper](https://github.com/wangyu-/tinyPortMapper/releases)。

另外，如果没有kcptun只有BBR/锐速的话，也可以把kcptun换成ncat/socat/ssh tunnel/iptables/[tinyPortMapper](https://github.com/wangyu-/tinyPortMapper/releases)。这样，TCP流量由锐速/BBR加速，UDP由UDPspeeder加速。

#### UDPspeeder + openvpn + $*** 混合方案，也适用于其他VPN
也是我正在用的方案。优点是可以随时在vpn和$\*\*\*方案间快速切换。
实际部署起来比图中看起来的还要简单。不需要改路由表，不需要写iptables规则和开启NAT，需要做的只是用openvpn分配的ip访问$*** server。

![image0](/images/cn/speeder_vpn_s.PNG)

(也可以把图中的$*** server换成其他的socks5 server，这样就不需要$*** client了)

可以和BBR/锐速叠加，BBR/锐速只要部署在VPS上就有效。

也可以用[tinyFecVPN](https://github.com/wangyu-/tinyFecVPN/) + $\*\*\* ，配置起来更简单。

# 应用实例

#### win10系统UDPspeeder+OpenVPN的完整设置

https://github.com/wangyu-/UDPspeeder/wiki/win10系统UDPspeeder-OpenVPN的完整设置


#### 用树莓派做路由器，搭建透明代理，加速游戏主机的网络

https://github.com/wangyu-/UDPspeeder/wiki/用树莓派做路由器，搭建透明代理，加速游戏主机的网络


# 编译教程
暂时先参考udp2raw的这篇教程，几乎一样的过程。

https://github.com/wangyu-/udp2raw-tunnel/blob/master/doc/build_guide.zh-cn.md
