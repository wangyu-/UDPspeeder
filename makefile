ccarm=mips-openwrt-linux-g++
all:
	g++ main.cpp -o dupd -static -lrt
release:
	g++ main.cpp -o dupd_amd64 -static -lrt	
	g++ main.cpp -o dupd_x86 -static -lrt -m32
	${ccarm} main.cpp -o dupd_ar71xx  -static -lgcc_eh -lrt

#g++ forward.cpp aes.c -o forward -static
#	${ccarm} forward.cpp aes.c  -o forwardarm   -static -lgcc_eh
