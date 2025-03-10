Testing UDP speeder
---

### Build
```
export UDP_SPEEDER_REPO=<path to UPDspeeder repo>
make -C ${UDP_SPEEDER_REPO}
```

### Simple test
```
# Start server
python3 ${UDP_SPEEDER_REPO}/test/udp_speeder_test.py    \
    --speederv2-path ${UDP_SPEEDER_REPO}/speederv2      \
    --mode server                                       \
    --extra-args "--conn-timeout 20 --conv-timeout 10"  \
    --log-level info

# Start client
python3 ${UDP_SPEEDER_REPO}/test/udp_speeder_test.py            \
    --speederv2-path ${UDP_SPEEDER_REPO}/UDPspeeder/speederv2   \
    --mode client                                               \
    --log-level info
```

### Test with systemd-socket-activate
```
# Start server with --socket-activate
python3 ${UDP_SPEEDER_REPO}/test/udp_speeder_test.py    \
    --speederv2-path ${UDP_SPEEDER_REPO}/speederv2      \
    --mode server                                       \
    --socket-activate                                   \
    --extra-args "--conn-timeout 20 --conv-timeout 10"  \
    --log-level info

#Start client
```

### Test with real systemd units
```
# Add unit files
sudo ln -s ${UDP_SPEEDER_REPO}/test/udp_speeder.socket /etc/systemd/system/
sudo ln -s ${UDP_SPEEDER_REPO}/test/udp_speeder.service /etc/systemd/system/

# Enable them to systemd
sudo systemctl daemon-reload
sudo systemctl enable udp_speeder.socket
sudo systemctl enable udp_speeder.socket

# Observe status and logs
sudo systemctl status udp_speeder.socket
journalctl -f -u udp_speeder.socket -u udp_speeder.service

# Start test server without speederv2 (will be started by udp_speeder.socket)
python3 ${UDP_SPEEDER_REPO}/test/udp_speeder_test.py    \
    --mode server                                       \
    --no-udpspeeder                                     \
    --log-level info

# Start client
```

