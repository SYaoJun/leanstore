# leanstore
## ubuntu-22.04
直接用gcc编译就行，不需要用clang
目前的commit-id是fbc05ea93370ee5404605a02a4c234b7870511e9
```c
apt update -y
apt-get install cmake libtbb2-dev libaio-dev libsnappy-dev zlib1g-dev libbz2-dev liblz4-dev libzstd-dev librocksdb-dev liblmdb-dev libwiredtiger-dev liburing-dev libgflags-dev -y
```
## 编译
```sh
git clone https://github.com/SYaoJun/leanstore.git
cd leanstore
git checkout ubuntu
mkdir build && cd build && cmake -DCMAKE_BUILD_TYPE=RelWithDebInfo .. && make -j `nproc`
```
# 2024.3.26
- 没有单元测试，代码质量不能保证。
- 依赖SSD，没有SSD甚至都不能运行。
- `KVInterface`提供CRUD接口。
# 2024.4.13
一直出现这个bug不知道怎么屏蔽。
gflags看起来还挺好用的，但是在命令行中一次性输入这么多命令非常不合理，最好写成脚本，让用户按需求修改最好。
这里有个比较复杂的调度线程，可以让多个worker并行运行，但是增加了代码的复杂性。
```
Error opening counter cycle
```
## 2024.4.17
用dockerfile创建镜像来编译代码
```sh
cd leanstore
sudo docker build . -t leanstore_img
sudo docker create --name leanstore_container -it leanstore_img /bin/bash
sudo docker start leanstore_container
sudo docker exec -it leanstore_container /bin/bash
```
## 关于docker问题
苹果M1芯片上如何用docker虚拟出x86_64的镜像？
## 其他
如果gflags设置的变量不通过命令传递，默认值是什么？
```
--wal = true
```
## 日志
似乎leanstore启动起来之后，没有专门的日志目录，对于记录执行路径的定位不是很方便