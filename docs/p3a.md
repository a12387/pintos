# Project 3a: Virtual Memory

## Preliminaries

>Fill in your name and email address.

徐子扬 <2200013152@stu.pku.edu.cn>

>If you have any preliminary comments on your submission, notes for the TAs, please give them here.

本地测试得分（仅3a测试部分）：
```
Functionality of virtual memory subsystem (tests/vm/Rubric.functionality):
	- Test paging behavior.
	     3/ 3 tests/vm/page-linear
	     3/ 3 tests/vm/page-parallel
	     3/ 3 tests/vm/page-shuffle
	     4/ 4 tests/vm/page-merge-seq
	     4/ 4 tests/vm/page-merge-par

Robustness of virtual memory subsystem (tests/vm/Rubric.robustness):
	- Test robustness of page table support.
	     2/ 2 tests/vm/pt-bad-addr
	     3/ 3 tests/vm/pt-bad-read
	     2/ 2 tests/vm/pt-write-code
	     3/ 3 tests/vm/pt-write-code2
	     4/ 4 tests/vm/pt-grow-bad

Functionality of system calls (tests/userprog/Rubric.functionality):
    ...
    - Section summary.
	     28/ 28 tests passed
	    108/108 points subtotal

Robustness of system calls (tests/userprog/Rubric.robustness):
    ...
    - Section summary.
	     34/ 34 tests passed
	     88/ 88 points subtotal

Functionality of base file system (tests/filesys/base/Rubric):
    ...
    - Section summary.
	     13/ 13 tests passed
	     30/ 30 points subtotal
```


>Please cite any offline or online sources you consulted while preparing your submission, other than the Pintos documentation, course text, lecture notes, and course staff.

xv6-riscv <https://github.com/mit-pdos/xv6-riscv>

- 通过其Mmap Lab，获得了关于SPT的设计思路

Linux的Object-based reverse mapping

- 对于Frame Table的设计有一定启发

## Page Table Management

#### DATA STRUCTURES

>A1: Copy here the declaration of each new or changed struct or struct member, global or static variable, typedef, or enumeration.  Identify the purpose of each in 25 words or less.

在pte.h中新增宏`PTE_L`、`PTE_ZW`、`PTE_SPT`：
```c
/** If PTE not present - for demand paging */
#define PTE_L 0x2               /**< 1=lazy alloc, 0=not alloc */
#define PTE_ZW 0x4              /**< 1=writable for zero init page */
#define PTE_SPT 0xFFFFFFFC      /**< Spt item of the PTE. */
```
分别用于：标识页面是否是Lazy Alloc的；页面若为Zero init时标识是否可写；Mask PTE的值，获得对应SPT Entry的指针

在vm/spt.h中新增结构体`struct spt`与枚举`enum spt_type`:

```c
struct spt {
    uint32_t *start_uaddr;  /**< User addr from which the virtual memory area starts */
    uint32_t nbytes;        /**< Length of the vma */
    struct file *file;      /**< File backup, if vma loaded from file */
    uint32_t offset;        /**< File offset of the first byte in the vma */
    uint32_t pos;           /**< Swap sector [pos * 8, pos * 8 + 7] */
    enum spt_type type;     /**< Backup in file or swap space */
    bool writable;         
    struct list_elem elem; 
};

enum spt_type {
    SPT_FILE = 1,
    SPT_SWAP = 2,
};
```

前者用于管理存放在文件或Swap Space中的页面的相关信息；后者用于区分具体是来自文件还是Swap Space

在`struct thread`中新增成员变量：

```c
struct thread {
    ...
    struct list spt;
    ...
}
```

用于存放线程所有的SPT表项

在vm/frame.h中新增结构体`struct frame`，在vm/frame.c中新增静态变量`struct hash frametable`和`struct lock ft_lock`

```c
struct frame {
    struct hash_elem elem;
    uint32_t *ppage; /**< Physical page addr*/
    uint32_t *vpage; /**< Virtual page addr */
    struct thread *owner;
};

static struct hash frametable;
static struct lock ft_lock;
```

分别用于：快速由物理地址查找对应的用户虚拟地址；存放所有前述映射关系；防止关于frametable的同步问题

#### ALGORITHMS

>A2: In a few paragraphs, describe your code for accessing the data
>stored in the SPT about a given page.

给定虚拟地址vaddr，如果这个地址的页面已经被加载，那么可以通过遍历对应线程的spt列表，找到`start_uaddr <= vaddr < start_uaddr + nbytes`的SPT表项，即可获得对应的数据。

如果没有被加载，那么也可以获得这一地址的PTE，PTE的第0位(PTE_P)为0且第1位(PTE_L)为1如果PTE前29位全为0，则说明这一虚拟地址的页面应当被初始化为0，而是否可写的信息则存放在第2位(PTE_ZW)。

如果满足P位为0而L位为1，但是不满足前29位全为0，则说明前30位构成了指向对应SPT表项的指针。由于指针要对齐4字节，因此最后两位不会造成影响，直接设为0就能得到对应的SPT表项的地址。

>A3: How does your code coordinate accessed and dirty bits between
>kernel and user virtual addresses that alias a single frame, or
>alternatively how do you avoid the issue?

尽量避免使用内核的地址对相应Frame读写。内核地址只用于索引物理地址，对具体数据的读写全部使用用户虚拟地址进行。例如，Syscall中需要对用户内存进行读写时，直接使用传入的指针，而不是翻译为物理地址后再使用。

只有页面换入时，使用内核地址进行操作，因为如果使用用户地址，那么刚换入的页面也会被标为脏，增加IO的开销。


#### SYNCHRONIZATION

>A4: When two user processes both need a new frame at the same time,
>how are races avoided?

首先，在修改完palloc.c的相关代码后，`palloc_get_page()`和`palloc_get_multiple()`等函数只会分配内核页面，不存在使用PAL_USER的对用户页面的分配。

对用户页面的分配全部集中到了Page Fault中。具体来说，只有在Page Fault Handler中，才会调用分配用户内存的函数`palloc_get_page_for_page_fault()`。而这个函数采用的clock算法需要一个全局的clock hand变量，只需要保证两个进程不会同时获取到相同的clock hand值即可保证分配的Frame位置不同，因此使用一把锁来保护，使得获取clock hand的值和更新clock hand的值合成一个原子操作，确保进程之间不会同时访问到clock hand。

#### RATIONALE

>A5: Why did you choose the data structure(s) that you did for
>representing virtual-to-physical mappings?

在TA Session中了解到P位为0的PTE也可以使用之后，就想要尝试通过这种方法来进行翻译。在需要换入对应页面时，这种方法查找比较迅速且占用较低，但是需要换出时就需要遍历SPT，所以整体上来看，就我个人的实现是不如使用哈希表的，但是改动成本比较高所以保留了。

在吸取教训之后，Frametable就老老实实采用哈希表了，对于这种映射关系，我认为哈希表无疑是十分适合的数据结构


## Paging To And From Disk

#### DATA STRUCTURES

>B1: Copy here the declaration of each new or changed struct or struct member, global or static variable, typedef, or enumeration.  Identify the purpose of each in 25 words or less.

在vm/swap.h中新增结构体`struct swaptable`：
```c
struct swaptable {
    struct bitmap *used_map;
    struct block *swap_block;
    struct lock block_lock;
    struct lock bm_lock;
};
```
用于标识Swap Block的使用情况和换入换出

在vm/swap.c中增加静态变量`struct swaptable sw`
```c
static struct swaptable sw;   /**< Global swaptable */
```
是上述数据结构的实例，为整个内核管理Swap Table

在palloc.c中新增静态变量`int clock`和`struct lock clock_lock`:
```c
static int clock;
static struct lock clock_lock;
```
前者用于记录clock算法的时针位置，后者用于避免同时请求frame时获取到相同clock值，进而造成并发问题

#### ALGORITHMS

>B2: When a frame is required but none is free, some frame must be
>evicted.  Describe your code for choosing a frame to evict.

维护一个全局的clock hand，每次发生page fault时，以clock hand为指针，以环形的方式遍历所有用户Frame，如果所指的Frame对应PTE中，A位被标记，则清空这一标记，如果没有被标记，则选择这一Frame进行驱逐。

>B3: When a process P obtains a frame that was previously used by a
>process Q, how do you adjust the page table (and any other data
>structures) to reflect the frame Q no longer has?

1. 修改进程Q的页表，为被驱逐的页面寻找或创建SPT表项并储存，并将PTE设为对应的值。这里PTE的P位必定是0，L位必定是1，表示这一页面并非不存在
2. 从Frametable中移除当前frame的记录
3. 在Frametable中确立物理地址到新进程的虚拟地址的映射

#### SYNCHRONIZATION

>B5: Explain the basics of your VM synchronization design.  In
>particular, explain how it prevents deadlock.  (Refer to the
>textbook for an explanation of the necessary conditions for
>deadlock.)

1. 只对公共资源加锁
2. 避免锁的嵌套（避免循环等待）
3. 尽量减小Critical Section的长度（减少出现Hold and wait的可能）

>B6: A page fault in process P can cause another process Q's frame
>to be evicted.  How do you ensure that Q cannot access or modify
>the page during the eviction process?  How do you avoid a race
>between P evicting Q's frame and Q faulting the page back in?

在选定要驱逐的页面之后，会立即设置Q的PTE，使得Q在对应页面的地址为无效。在设置PTE之前的访问可以正常进行，也可以正常设置标志位；一旦设置PTE之后，Q访问对应页面就会发生Page Fault，不会影响到之前标志位的设置情况，所以也能够正常地通过这些标志位来进行换入换出、设置新PTE等等。

P驱逐Q（设置Q的PTE无效）之后，Q访问相同的页面，只会开始新一次的驱逐，并且由于clock hand是受保护的，P和Q几乎不可能使用相同的clock hand，即几乎不可能使用同一个Frame。即使由于环形的特性导致P和Q取得了相同的clock hand值，如果P没有完成Swap，那么frametable中就查找该Frame只会返回NULL，因而跳过这一页；如果P已经完成Swap，那么再换出去也无妨，因为P这一次Swap已经结束了，不会对Q的Swap造成影响，也不会有并发问题。


>B7: Suppose a page fault in process P causes a page to be read from
>the file system or swap.  How do you ensure that a second process Q
>cannot interfere by e.g. attempting to evict the frame while it is
>still being read in?

同上，只有在页面内容被完全读入之后，才会在frametable中建立frame和page之间的映射，因此Q在想要换出该frame时，查找frametable只会获得NULL，并且跳过这一个Frame。如果Q能够换出此frame，说明这个frame一定已经建立了和虚拟页面的映射，即内燃已经被读取完成了。

>B8: Explain how you handle access to paged-out pages that occur
>during system calls.  Do you use page faults to bring in pages (as
>in user programs), or do you have a mechanism for "locking" frames
>into physical memory, or do you use some other design?  How do you
>gracefully handle attempted accesses to invalid virtual addresses?

我采用的方法是使用page fault换回页面的方式。

具体方式是，在系统调用函数中只检查地址是否为用户地址，剩下的事情全部丢给page fault来检查。如果传入的地址真的是合法有效的，那么Page fault handler自然会把正确的内容换回来；如果传入的是无效的地址，发生page fault之后不能正常换入页面就会让进程中止。

由于文件读写时若发生swap会导致同时进行两个读写操作，造成对同一把锁的二次获取，因此需要在内核中开辟一块缓冲区，先读入数据再写文件（写）或先读取文件再写入用户内存（读）。这就引入了新的问题：如果Page fault时进程中止，那么缓冲区的内存就无法释放了。不过由于只有这两个操作会出现这种问题，并且同一个线程只能同时进行其中之一，因此我在`struct thread`中加入一个缓冲区指针的备份，如果当调用`thread_exit()`退出时这个备份不为NULL就释放资源即可。

#### RATIONALE

>B9: A single lock for the whole VM system would make
>synchronization easy, but limit parallelism.  On the other hand,
>using many locks complicates synchronization and raises the
>possibility for deadlock but allows for high parallelism.  Explain
>where your design falls along this continuum and why you chose to
>design it this way.

我的实现偏向细粒度的锁、高并发。

对于Frametable、Swaptable等全局数据结构，基本上每个都需要一到两个锁，以防止在这些数据结构上发生竞争。此处完全没有必要使用共用的锁，因为本身关联就并不紧密，同时访问不同数据结构的两个进程是允许存在的。

而在Swap过程中，我所用到的锁只有保护clock hand的一个，只要保护好它，Swap中就很难出现race，在此之上再安排各个操作的顺序，后续即使不显式地使用锁也能起到保护的作用。