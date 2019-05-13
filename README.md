README
===========================
该文件用于描述本系统的基本架构、功能特性、部署安装等。

****
	
|Author|xuq|
|---|---
|E-mail|xuq@ihep.ac.cn


****
## 目录
* [基本信息](#基本信息)
* [项目特性](#项目特性)
* [基本架构](#基本架构)
    * [传输服务模块](#传输服务模块)
    * [缓存服务模块](#缓存服务模块)
    * [客户端接口模块](#客户端接口模块)
* [安装部署](#安装部署)
    * [数据源服务器端](#数据源服务器端)
    * [缓存服务器端](#缓存服务器端)
	* [客户端](#客户端)
	* [可能出现的问题及解决方法](#可能出现的问题及解决方法)
* [使用方法](#使用方法)
    * [文件上传--iput](#文件上传--iput)
	* [文件下载--iget](#文件下载--iget)
	* [文件列表--ilist](#文件列表--ilist)
	* [文件删除--idelete](#文件删除--idelete)
    * [如何添加新功能](#如何添加新功能)
* [联系我们](#联系我们)
****

基本信息
------
该项目是C语言编写的，用于分布式异构站点间文件的跨域传输，旨在为高能物理计算提供一种本地化的分布式数据管理及访问体验。

项目特性
------
1、提供分布式站点间的全局统一命名空间
2、提供统一存储接口，实现异构存储系统的数据访问
3、提供一种高效、灵活且健壮的数据访问与传输机制，实现数据按需传输
4、提供分布式缓存，减少相同数据的二次传输，实现高效的跨域数据访问
5、提供高并发访问服务，实现高并发请求的快速响应
6、提供系统数据安全性保障
7、提供元数据管理功能
8、提供数据实体管理功能
9、提供可移植性用户访问接口，实现便捷的数据访问模式
10、具有良好的可扩展性

基本架构
------
CDAS 系统架构主要由传输服务(TransferD)、缓存服务(CacheD)、客户端接口(File Plugin API, FPAPI)三大模块组成，并且缓存服务又分为跨域文件名字服务(MetaD)简称元数据服务、守护进程(Daemon Process)以及缓存资源管理服务(DataD)。
### 传输服务模块
-该模块基于XrootD API实现。
-客户端实现了基于数据块大小的子块划分、
多流传输、断点重传和数据校验功能。
-服务端等同于xrootd服务端。
### 缓存服务模块
-该模块包括：守护进程DP，跨域文件名字服务（MetaD），缓存资源管理服务（DataD）。
-该模块主要负责系统的整体资源调度与数据处理，三个部分分别负责请求处理，元数据管理和缓存文件管理。
-DP：系统的多线程消息处理模块，用于监听客户端请求消息、提供消息处理队列和数据操作原子化。
MetaD：元数据服务，主要作用包括提供分布式站点统一命名空间、缓存跨域文件元数据信息、支撑跨域文件的按需访问、维护跨域文件与缓存文件的映射关系以及提供跨域文件访问控制等。
元数据信息表：MetaD通过TransferD获取数据源目录下的相关元数据信息，为每个文件添加新属性（父目录标记，逻辑标记，位图字段），将所有属性存储在表中。
当查询数据源目录结构时，MetaD根据父目录标记和逻辑标记重构树形逻辑命名空间。
缓存文件散列信息表：记录已缓存文件的散列情况，即物理位置和数据源文件的映射。
DataD：缓存文件的预开辟，散列存储以及缓存置换。
采用hash散列算法，hash对象是文件名，通过uuid为缓存文件重命名，缓存文件与数据文件一一映射。缓存文件路径仅与原文件名有关。

### 客户端接口模块：
实现了CLI和FUSE接口。

安装部署
------
### 数据源服务器端
1、安装xrootd
```Bash
yum install xrootd xrootd-devel xrootd-client-devel
```
2、修改暴露的目录，假设为/xrootdD
```Bash
vi /etc/xrootd/xrootd-standalone.cfg
all.export=/xrootdD
```
3、修改文件夹属性
```Bash
chmod xrootd:xrootd /xrootdD
```
4、启动xrootd服务
```Bash
systemctl start xrootd@standalone.service
```
### 缓存服务器端
1、安装mysql
A、创建mysql-cmmunity.repo
```Bash
vi /etc/yum.repos.d/mysql-cmmunity.repo
```
内容如下：
```Bash
[mysql57-community]
name=MySQL 5.7 Community Server
baseurl=http://repo.mysql.com/yum/mysql-5.7-community/el/7/$basearch/
enabled=1
gpgcheck=0
gpgkey=file:///etc/pki/rpm-gpg/RPM-GPG-KEY-mysq
```
B、下载mysql
```Bash
yum install mysql-community-server.x86_64  mysql-community-devel.x86_64
```
C、初始化mysql
```Bash
mysql initialization
```
D、获取初次安装后的密码
```Bash
grep "temporary password" /var/log/mysqld.log
```
E、设置新密码
```Bash
set global validate_password_policy=0;
set password for root@localhost = password('123456ok');
```
F、重新生成mysql.sock文件，提供正常服务
```Bash
/opt/lampp/lampp restart
```
PS：如果没有lampp
使用root用户，在https://www.apachefriends.org/zh_cn/index.html 上找到合适的版本下载地址，使用wget -c进行下载，得到.run格式的文件，这里以xampp-linux-1.8.3-5-installer.run为例，安装并运行。
```Bash
chmod +x xampp-linux-1.8.3-5-installer.run
./xampp-linux-1.8.3-5-installer.run
/opt/lampp/lampp start
```
2、安装MyRocksDB
A、安装依赖
```Bash
yum install cmake gcc-c++ bzip2-devel libaio-devel bison \
zlib-devel snappy-devel boost-devel
yum install gflags-devel readline-devel ncurses-devel \
openssl-devel lz4-devel gdb git
yum install libzstd-devel
yum install nss curl libcurl
```
B、安装MyRocksDB
```Bash
git clone https://github.com/facebook/mysql-5.6.git
git submodule init
git submodule update
cmake . -DCMAKE_BUILD_TYPE=RelWithDebInfo -DWITH_SSL=system \
-DWITH_ZLIB=bundled -DMYSQL_MAINTAINER_MODE=0 -DENABLED_LOCAL_INFILE=1 \
-DENABLE_DTRACE=0 -DCMAKE_CXX_FLAGS="-march=native" \
-DWITH_ZSTD=/usr
make -j8    
make install
```
C、修改my.cnf
内容如下：
```Bash
[mysqld]
datadir=/var/lib/mysql/
socket=/tmp/mysql.sock  #最好定义为/tmp之后连接客户端时不需要再次声明-S，cpp访问时不是tmp下也需要代码内指定
symbolic-links=0

rocksdb
default-storage-engine=rocksdb
skip-innodb
default-tmp-storage-engine=MyISAM
collation-server=latin1_bin #(or utf8_bin, binary)
log-bin
binlog-format=ROW

sql_mode=NO_ENGINE_SUBSTITUTION,STRICT_TRANS_TABLES

[mysqld_safe]
log-error=/var/log/mysql/mysql.log
pid-file=/var/run/mysql/mysql.pid
```
D、有关mysql的其他设置
```Bash
useradd mysql
chown -R mysql:mysql /var/lib/mysql //数据文件
chown -R mysql:mysql /usr/local/mysql //安装文件
chown -R mysql:mysql /var/log/mysql //日志文件
chown -R mysql:mysql /var/run/mysql //进程文件
cd /usr/local/mysql
/usr/local/mysql/scripts/mysql_install_db --defaults-file=/usr/local/mysql/my.cnf
/usr/local/mysql/bin/mysqld_safe --defaults-file=/etc/my.cnf &
/usr/local/mysql/bin/mysqladmin -u root password '123456' 
/usr/local/mysql/bin/mysql -p
在/etc/ld.so.conf中添加
/usr/local/mysql/lib
/sbin/ldconfig
```
E、其他依赖库
```Bash
yum install curl-devel  json-c-devel  libuuid-devel  zlib-devel
```

3、安装python tornado传输模块
```Bash
yum install openssl-devel
/安装Python3.5/
./configure --enable-shared
make 
make install
ln -s  /usr/local/lib/libpython3.5m.so.1.0  /usr/lib/libpython3.5m.so.1.0
/sbin/ldconfig -v
/安装tornado/
python3   setup.py   build
python3   setup.py   install

# 安装XROOTD PROTOCAL 传输模块(缓存既是xrootd客户端也是服务器)：
1 yum install epel.release
2 yum install xrootd-devel xrootd-client-devel xrootd 
3  vi /etc/xrootd/xrootd-standalone.cfg #修改all.export 服务端暴露目录为/cdfs_data 缓存数据目录
4 chmod xrootd:xrootd  /cdfs_data #文件夹属性修改，不然客户端只有读、创建权限没有写权限
3 systemctl start xrootd@standalone.service #启动xrootd服务
```
访问方式     ./xrootds  root://192.168.83.173//xrootdD/ceshi

4、缓存服务配置
/etc/NSCONFIG填写数据库用户和密码
```Bash
./initdb.sh #创建数据库 Cns_mysql_tbl.sql创建数据表(只需要file-transfer seg-transfer uniqueid-transfer 三个表)
cp nsdaemon.scripts /etc/init.d/nsdaemon #添加服务启动脚本         
ln -s /usr/bin/nsdaemon /usr/local/bin/nsdaemon    
ln -s /usr/bin/nsshutdown /usr/local/bin/nsshutdown
```
填写配置文件   /etc/cdfs/cdfs.cf
【配置文件详解】
5、编译安装
```Bash
make
make install
```

### 客户端
1、客户端必须配置CNS_HOST 
```Bash
export CNS_HOST="vm083170.v.ihep.ac.cn" 
 #写入/etc/profile长期生效   
 #hostname必须是a.b.c格式
 #vi /etc/sysconfig/network 并使用hostname命令
 #同时添加/etc/hosts 主机名用于域名解析
```
2、scp  缓存服务器的client/ 到客户端本地

### 可能出现的问题及解决方法
1、Cns_mysql_tbl.sql  数据库操作脚本，插入爆出异常
```Bash
[Err] 1055 - Expression #1 of ORDER BY clause is not in GROUP BY clause and contains nonaggregated column 'information_schema.PROFILING.SEQ' which is not functionally dependent on columns in GROUP BY clause; this is incompatible with sql_mode=only_full_group_by
```Bash
这是由于only_full_group_by造成的，删除该默认属性即可
```Bash
select @@global.sql_mode
set @@global.sql_mode ='STRICT_TRANS_TABLES,NO_ZERO_IN_DATE,NO_ZERO_DATE,ERROR_FOR_DIVISION_BY_ZERO,NO_AUTO_CREATE_USER,NO_ENGINE_SUBSTITUTION';
···

2、无法使用IP 进行mysql_real_connect，原因是数据库的访问权限默认在localhost
```Bash
grant all privileges on cns_db.* to'leaf'@'%' identified by 'leaf_cache_pq2';
flush privileges;
```

使用方法
------
### 文件上传--iput
```Bash
usage: ./iput [-r] [--comment] files/directories r_directory...
```
### 文件下载--iget
```Bash
usage: ./iget [-hvfmRrV] [--help --verbose --force --mthread --refreash] files/direcories l_directory...
```
### 文件列表--ilist
```Bash
usage: ./iget [-hvfmRrV] [--help --verbose --force --mthread --refreash] files/direcories l_directory...
```
### 文件删除--idelete
```Bash
usage: ./idelete [-chrV] [--help version] path...
```
### 如何添加新功能
1.编写nssetactualpath.c、nsdelactualpath.c、CNS_setactualpath.c、CNS_delactualpath.c文件
2.修改Cns.h、Cns_api.h Cns_main.c Cns_procreq.c Cns_mysql_ifce.c  Makefile
3.数据库添加表Cns_file_actualpath
4.make clean  make   make install
5.重启nsdaemon服务

联系我们
------
if this project helps you a lot, and you would like to support this project's further development and the continuous maintenance of this project, you can contect xuq@ihep.ac.cn. Your donation is highly appreciated. Thank you!
