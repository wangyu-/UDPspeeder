ccarm=mips-openwrt-linux-g++
FLAGS=-Wall -Wextra -Wno-unused-variable -Wno-unused-parameter
FLAGS2= -O3
all:
	g++ main.cpp common.cpp log.cpp -I. -o dupd -static -lrt -std=c++11  ${FLAGS} ${FLAGS2}
release:
	g++ main.cpp -o dupd_amd64 -static -lrt	  ${FLAGS} ${FLAGS2}
	g++ main.cpp -o dupd_x86 -static -lrt -m32  ${FLAGS} ${FLAGS2}
	${ccarm} main.cpp -o dupd_ar71xx  -lrt
debug:
	g++ main.cpp common.cpp log.cpp -I. -o dupd -static -lrt -std=c++11  ${FLAGS} -Wformat-nonliteral -D MY_DEBUG

#g++ forward.cpp aes.c -o forward -static
#	${ccarm} forward.cpp aes.c  -o forwardarm   -static -lgcc_eh
