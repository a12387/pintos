# Project 3b: Virtual Memory

## Preliminaries

>Fill in your name and email address.

徐子扬 <2200013152@stu.pku.edu.cn>

>If you have any preliminary comments on your submission, notes for the TAs, please give them here.

本地测试得分：
```
SUMMARY BY TEST SET

Test Set                                      Pts Max  % Ttl  % Max
--------------------------------------------- --- --- ------ ------
tests/vm/Rubric.functionality                  55/ 55  50.0%/ 50.0%
tests/vm/Rubric.robustness                     28/ 28  15.0%/ 15.0%
tests/userprog/Rubric.functionality           108/108  10.0%/ 10.0%
tests/userprog/Rubric.robustness               88/ 88   5.0%/  5.0%
tests/filesys/base/Rubric                      30/ 30  20.0%/ 20.0%
--------------------------------------------- --- --- ------ ------
Total                                                 100.0%/100.0%
```

>Please cite any offline or online sources you consulted while preparing your submission, other than the Pintos documentation, course text, lecture notes, and course staff.

xv6-riscv <https://github.com/mit-pdos/xv6-riscv>

- 通过其Mmap Lab，对mmap()获得了进一步的理解

## Stack Growth

#### ALGORITHMS

>A1: Explain your heuristic for deciding whether a page fault for an
>invalid virtual address should cause the stack to be extended into
>the page that faulted.

1. 首先，检查页面是否是一个Lazy Alloc的页面，如果是就直接换入。
2. 其次，检查fault addr，如果在栈的上限之外(PHYSBASE- 8 * 1024 * 1024)，则直接中止。
3. 然后，通过传入的`intr_frame`中`esp`的值，确定fault addr是否属于栈（或栈指针下方32字节），如果属于栈则分配新的页面，否则中止。

## Memory Mapped Files

#### DATA STRUCTURES

>B1: Copy here the declaration of each new or changed struct or struct member, global or static variable, typedef, or enumeration.  Identify the purpose of each in 25 words or less.

在vm/spt.h中，为枚举`enum spt_type`添加新种类：
```c
enum spt_type {
    ...
    SPT_MMAP = 3
};
```
用于标识mmap映射的内存空间。

在`struct thread`中，修改打开文件表的数据结构：
```c
struct thread {
    ...

    /* old */
    struct file *open_file[NOFILE];

    /* new */
    struct {
        struct file *file;
        int refcnt;
    } open_file[NOFILE];
};
```
添加引用计数，便于在close后保持mmap

在`syscalls`函数指针数组中添加新元素：
```c
static int (*syscalls[])(struct intr_frame *) = {
    ...
    [SYS_MMAP]    sys_mmap,
    [SYS_MUNMAP]  sys_munmap,
};
```
用于统一调用新增的系统调用。

#### ALGORITHMS

>B2: Describe how memory mapped files integrate into your virtual
>memory subsystem.  Explain how the page fault and eviction
>processes differ between swap pages and other pages.

由于在设计时就考虑到mmap，或者说本身就是为了mmap设计的，因此并没有对vm子系统做过多的修改。少数修改的点包括：

1. 增加的spt表项的释放。由于旧系统中只有可执行文件会放入spt，直到进程结束才会和整个进程一起释放，而且也不会写入可执行文件，因此没有必要单独做释放spt时写回的操作；而加入mmap后才有了这个必要，因此加入了这一功能。
2. 对mmap页面和可执行文件页面采用了略有区别的换出方式。可执行文件页面尾部有不属于文件的部分，就把整个页面看作匿名页面，换出到swap space；而对于mmap，这种页面换出时只将属于文件的部分换出到文件，尾部直接丢弃。另外，还添加了保持文件位置的机制，防止换入换出对读写文件描述符造成影响。


对于swap pages，换出和换入都是对整个页面进行的，既不会多也不会少；直接对硬盘操作，也不需要使用文件系统的相关接口。对于其他页面，只有数据全部属于文件的页面才是对整体操作，否则对于可执行文件页面，直接将页面归为swap page，而对mmap页面，只对有效部分读写；并且读写都需要通过文件系统进行，需要通过锁、记录原状态等方式保持文件系统的不变量。

>B3: Explain how you determine whether a new file mapping overlaps
>any existing segment.

对于mmap映射的虚拟地址空间的每一个页面，尝试获取对应的PTE，如果能够获取到PTE并且不全为0，则说明该页面被占用了；如果不能获取到PTE（不存在对应二级页表）或获取到的PTE为0，则说明该页面没有被占用。

#### RATIONALE

>B4: Mappings created with "mmap" have similar semantics to those of
>data demand-paged from executables, except that "mmap" mappings are
>written back to their original files, not to swap.  This implies
>that much of their implementation can be shared.  Explain why your
>implementation either does or does not share much of the code for
>the two situations.

正如前面所述，在设计VM子系统之初就考虑了mmap的情况。我的想法是，无论是可执行文件、swap还是mmap文件，这些分配的内存区域都有着一个内存与文件之间的映射，区别只是文件的位置不同而已，所以我通过一个enum来区分分配的区域属于什么文件，并以此来决定换入换出时要进行的操作。根据这一思路，我搭建了一个框架，可执行文件或是swap都是向框架内添加的扩展（当然并不严格是这样，因为对于默认情况还需要采用swap来解决），而加入mmap只不过是添加新扩展罢了，至于其他不重合的部分，要么不是属于vm子系统，要么就是修补之前实现中并不robust的地方，所以大部分的代码都是共享的。