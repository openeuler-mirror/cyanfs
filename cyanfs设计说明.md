# 设计方案

## 需求

专用于存储虚拟机镜像文件的文件系统

* 不需要支持目录
* 文件个数很少（小于4096）
* 文件尺寸大
* IO粒度为扇区（512字节）

## 磁盘结构

### 超级块

超级块采用A/B备份模式，从磁盘开头连续分布
* A块位于[0, 4095]的位置
* B块位于[4096, 8191]的位置

超级块内部数据分布为：
```c
struct cyanfs_super_header {
	uint32_t uuid[4];
	uint32_t magic;
	uint32_t crc32; // 超级块数据的校验码
	uint64_t version; // 超级块的更新版本号
	uint32_t journal_id; // 起始日志块的Extent ID
	uint64_t journal_seq; // 起始journal的序号
};

struct cyanfs_super_block {
    struct cyanfs_super_header header;
    char padding[4096 - sizeof(struct cyanfs_super_header)];
};
```

### 数据块

数据块位于超级块之后的位置，每连续1MB数据对应一个Extent，其ID从0开始计算

|Extent ID|磁盘起点|磁盘终点|
|---|---|---|
|0|8192|1056767|
|1|1056768|2105343|
|...|...|...|

### 日志块

当数据块被用于存放文件系统日志时，被称为日志块

日志块的格式是：|journal|journal|journal|...

每条`journal`的格式为：|header|entry 1|entry 2|entry 3|...|entry N|
```c
struct cyanfs_journal_header {
	uint32_t crc32; // journal数据的校验码
	uint64_t seq;  // journal的序号
	uint64_t size; // journal的数据长度
	uint16_t count; // entry的个数
};
```

每条entry的格式为：
```c
typedef enum {
	CYANFS_JOURNAL_NEXT,
	CYANFS_JOURNAL_CREATE,
	CYANFS_JOURNAL_TRUNCATE,
	CYANFS_JOURNAL_FORK,
	CYANFS_JOURNAL_DELETE,
	CYANFS_JOURNAL_BIND,
	CYANFS_JOURNAL_UNBIND,
	CYANFS_JOURNAL_RENAME,
} cyanfs_journal_entry_type;

struct cyanfs_journal_entry {
    uint8_t type;
	union {
		struct {
			uint32_t backend;
		} next;
		struct {
			char name[128];
			uint64_t id;
		} create;
		struct {
			uint64_t id;
			uint64_t size;
		} truncate;
		struct {
			char name[128];
			uint64_t id;
			uint64_t pid;
		} fork;
		struct {
			uint64_t id;
		} delete;
		struct {
			uint64_t id;
			uint32_t backend, file;
		} bind;
		struct {
			uint64_t id;
			uint32_t file;
		} unbind;
		struct {
			char name[128];
			uint64_t id;
		} rename;
	};
};
```

## 内存数据结构

### Extent

磁盘上的每个数据块和日志块，都对应内存中的如下数据结构：
```c
struct cyanfs_extent {
	uint32_t backend : 30; // 磁盘上的Extent ID
	int map : 1; // 是否对应底层磁盘已分配的extent
	int pending : 1; // 该extent正在写入，需挂起新的IO
	uint32_t file : 30; // 文件中的Extent ID
	int error : 1; // 该extent存在IO问题
};

struct cyanfs_extent_node {
	struct cyanfs_extent v;
    CYANFS_RB_ENTRY(cyanfs_extent_node) node; // 红黑树存储节点
};
```

文件Extent ID与文件数据区的对应逻辑如下：

|Extent ID|文件起点|文件终点|
|---|---|---|
|0|0|1048575|
|1|1048576|2097151|
|...|...|...|

所有的Extent在内存中以红黑树的形式维护

* 若Extent未分配给文件，则存储于超级块的红黑树中
* 若Extent已分配给文件，则存储于文件对应的红黑树中

### 超级块

内存中的超级块结构包含如下内容：

```c
struct cyanfs_super {
	struct cyanfs_files_id_rb files_by_id; // 文件列表，按文件ID维护的红黑树
	struct cyanfs_files_name_rb files_by_name; // 文件列表，按文件名维护的红黑树

	struct cyanfs_backend_extents_rb free_extents; // 存放未分配的Extent的红黑树
	struct cyanfs_backend_extents_rb journal_extents; // 存放用于存储日志块的Extent的红黑树

    uint64_t journal_id; // 当前在写入的日志块的Extent ID
    uint64_t journal_seq; // 当前journal的序号
};
```

### 文件

内存中的文件结构包含如下内容：

```c
struct cyanfs_file {
	struct cyanfs_super *super;
	CYANFS_RB_ENTRY(cyanfs_file) id_node; // 红黑树节点，按文件ID维护
	CYANFS_RB_ENTRY(cyanfs_file) name_node; // 红黑树节点，按文件名维护

	struct cyanfs_file_extents_rb extents; // 存放文件的Extent的红黑树
};
```

## 运行逻辑

### 操作日志

* 每一条操作日志对应磁盘上的一个`struct cyanfs_journal_entry`数据
* 如若操作日志不需要马上写入磁盘，可以在内存中按顺序暂存
* 将操作日志写入磁盘时，将生成journal数据
    * 将内存中所有的操作日志按顺序排列
    * 在头部拼接`struct cyanfs_journal_header`数据
    * 填写journal数据的校验码
    * 累加super的journal_seq，并复制到journal数据中
* 将journal数据写入磁盘

### 元数据管理

* 元数据全部存放在内存中
* 文件系统启动时，读取Super A/B，以其中version较大的为准，获取起始日志块Extent ID
    * 读取日志块，校验并分析里面所有的journal数据
    * 根据journal中的`struct cyanfs_journal_entry`数据，还原操作日志
    * 根据操作日志，在内存中修改状态
    * 通过遍历所有日志块，完成整个操作日志的重放，还原文件系统的最终状态
* 对于元数据的修改，先修改内存状态，再将操作日志追加写入磁盘的日志块
* 一个日志块A快写满前，将分配一个新的日志块B用于后续写入，此分配操作将追加记录在日志块A中

### 日志块管理

由于元数据的修改会一直追加日志块，因此需要定期合并日志块中的冗余信息，以释放磁盘空间

举例说明：

1. 起始状态：SuperA->Journal1->Journal2->Journal3->Journal Current，Current为当前正在用于追加的日志块
1. 划定合并范围：Journal1->Journal2->Journal3
1. 执行合并，分配并生成Journal4，并在其最后，追加一个分配Journal Current日志块的记录
1. 修改SuperB，将其指向Journal4，并标记version为SuperA的version + 1

所有已分配的日志块，都需要将对应extent从super的free_extents移动至journal_extents

### 文件操作

#### 创建

1. 创建一个file结构体，生成一个文件ID，填写文件名
1. 将file插入到super的files_by_id和files_by_name红黑树中
1. 执行元数据修改流程，操作日志的内容为：文件创建

#### 块分配

1. 根据文件名或文件ID，查找file
1. 从super的free_extents中按一定算法选择一个extent
    * 优先保障文件的Extent在磁盘上连续分布
1. 将此extent从super的free_extents移动至file的extents
1. 在extent上记录文件的Extent ID
1. 执行元数据修改流程，操作日志的内容为：数据块分配

#### 读数据

1. 根据文件名或文件ID，查找file
1. 根据读请求的偏移与长度，计算出对应的文件Extent ID
1. 根据文件Extent ID，从file的extents中查找extent
1. 若extent不存在，则返回全零
1. 从extent的backend字段，获得磁盘的Extent ID，并计算出对应的磁盘偏移
1. 将读请求转发至磁盘

#### 写数据

1. 根据文件名或文件ID，查找file
1. 根据读请求的偏移与长度，计算出对应的文件Extent ID
1. 根据文件Extent ID，从file的extents中查找extent
1. 若extent不存在，则执行块分配，获得一个extent
1. 从extent的backend字段，获得磁盘的Extent ID，并计算出对应的磁盘偏移
1. 将写请求转发至磁盘

#### 修改大小

1. 根据文件名或文件ID，查找file
1. 更新file的size字段
1. 根据size，计算最大的Extent ID
1. 将file的extents中大于此Extent ID的extent都移动到super的free_extents中
1. 执行元数据修改流程，操作日志的内容为：修改文件大小

#### 删除

1. 根据文件名或文件ID，查找file
1. 将file的extents中所有的extent都移动到super的free_extents中
1. 从super的files_by_id和files_by_name红黑树中删除file
1. 释放file结构
1. 执行元数据修改流程，操作日志的内容为：删除文件

## 代码树结构

* core：文件系统核心代码
    * 具备跨平台特性，支持Linux、Windows、UEFI
    * 只依赖C库与平台提供的同步机制
* utils：文件系统配套工具
* linux：Linux平台的专用代码
    * 基于通用块层的接口，实现将文件映射为虚拟磁盘的能力
	* 为core提供向物理磁盘发起BIO的能力
* uefi：UEFI平台的专用代码
    * 基于EDK2框架，提供访问cyanfs的驱动程序
