mkdir UDPspeeder
cd UDPspeeder
wget https://github.com/wangyu-/UDPspeeder/releases/download/20170806.0/speederv2_linux.tar.gz
tar zxf speederv2_linux.tar.gz
mv speederv2_amd64 speederv2
nohup ./speederv2 -s -l 0.0.0.0:20000 -r 127.0.0.1:8833  -f20:10 &
