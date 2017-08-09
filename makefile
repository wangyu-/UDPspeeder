cross_cc=mips-openwrt-linux-g++
FLAGS=-Wall -Wextra -Wno-unused-variable -Wno-unused-parameter -ggdb
FLAGS2= -O3
all:
	g++ main.cpp common.cpp log.cpp -I. -o speeder -static -lrt -std=c++11  ${FLAGS} ${FLAGS2}
release:
	g++ main.cpp common.cpp log.cpp -I. -o speeder_amd64 -static -lrt -std=c++11  ${FLAGS} ${FLAGS2} 
	g++ -m32 main.cpp common.cpp log.cpp -I. -o speeder_x86 -static -lrt -std=c++11  ${FLAGS} ${FLAGS2}
	${cross_cc} main.cpp common.cpp log.cpp -I. -o speeder_ar71xx -lrt -std=c++11  ${FLAGS} ${FLAGS2}
	tar -zcvf udp_speeder_binaries.tar.gz speeder_amd64  speeder_x86  speeder_ar71xx
cross:
	${cross_cc} main.cpp common.cpp log.cpp -I. -o speeder_cross -lrt -std=c++11  ${FLAGS} ${FLAGS2}

debug:
	g++ main.cpp common.cpp log.cpp -I. -o speeder -static -lrt -std=c++11  ${FLAGS} -Wformat-nonliteral -D MY_DEBUG

#g++ forward.cpp aes.c -o forward -static
#	${ccarm} forward.cpp aes.c  -o forwardarm   -static -lgcc_eh
