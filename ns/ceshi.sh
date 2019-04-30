#!/bin/bash

./iget /home/xuq/100m /tmp/xuq/
./iget /home/xuq/test/100 /tmp/xuq/
./iput /tmp/xuq/600M-l /root/leaf/test/
./iput /tmp/xuq/200M-l /root/leaf/test/
./iget /root/leaf/pytoc/upload/400M   /tmp/xuq
./iget /root/leaf/pytoc/upload/600M /tmp/xuq
./iget /root/leaf/pytoc/upload/200M /tmp/xuq
./iput /tmp/xuq/100M-l /root/leaf/test/
./iget /root/leaf/pytoc/upload/100M /tmp/xuq
./iput /tmp/xuq/1M-l /root/leaf/test/
./iget  /eos/user/c/chyd/f1G  /tmp/xuq
./iput /tmp/xuq/f1G-l /root/leaf/test/
./iput /tmp/xuq/hosts-l /root/leaf/test/
