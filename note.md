  promise_base 存公共调度状态：parent、detached
  promise<T></t> 只负责返回值
  promise<void></void> 只负责 void 返回和异常
  task::await_suspend 记录 parent
  final_suspend 恢复 parent
  detach/clean 管协程帧所有权

  你现在最大的问题是把“返回值类型 T”和“promise 类型”混在了一起。协程调度逻辑不要依赖 return_type，应该放在 promise_base 里统一处理。

To continue this session, run codex resume 019fe68b-f7be-7290-834d-cd1afe5d85dc

› 为什么coroutine_handle<>是通用类型

• std::coroutine_handle<> 是通用类型，因为它其实是：

  std::coroutine_handle<void></void>

  它只保存一个东西：协程帧地址。

  它不关心这个协程的 promise 是什么类型，也不关心这个协程返回 int、void 还是 std::string。所以它可以代表任意协程句柄。

  对比一下：

  std::coroutine_handle<promise<int></int>>

  这是强类型句柄。它知道 promise 类型，所以可以：

  h.promise().result();

  但它只能表示 promise 是 promise<int></int> 的协程。

  而：

  std::coroutine_handle<>

  是类型擦除后的句柄。它能做的事情少一些，比如：

  h.resume();
  h.destroy();
  h.done();
  h.address();

  但不能直接：

  h.promise(); // 不行，因为不知道 promise 类型

  为什么父协程要用 coroutine_handle<>

  因为子协程不知道父协程的返回类型。

  例如：

  task<int></int> child()
  {
      co_return 1;
  }

  task<void></void> parent()
  {
      int x = co_await child();
  }

  在 child 的 promise 里，要记录“谁在等我”。这个等待者是 parent。

  但是 child 是：

  std::coroutine_handle<promise<int></int>>

  而 parent 是：

  std::coroutine_handle<promise<void></void>>

  它们不是同一个类型。

  如果你在 child 里这样存：

  std::coroutine_handle<promise<int></int>> parent;

  所以必须用：

  std::coroutine_handle<> parent;

  它可以接收任意 promise 类型的协程句柄。

  直观理解

  coroutine_handle<promise<T></t>>
  = 精确类型句柄，可以访问 promise<T></t>

  coroutine_handle<>
  = 通用句柄，只能 resume/destroy/done，但能指向任何协程

  在 final_suspend() 里，我们只需要恢复父协程：

  return parent;

  不需要访问父协程的 promise，所以用通用 coroutine_handle<> 正合适。

› clean函数中,对于一个coroutine_handle<> handle,怎么获取它的promise?因为detached标志是记录在promise中的

• 可以获取，但前提是：你必须把这个通用 coroutine_handle<> 转回具体 promise 类型的 handle。

  在这个项目里，所有 task<T></t> 的 promise 都继承自：

  detail::promise_base

  所以文件里已经给了一个别名：

  using coroutine_handle = std::coroutine_handle[detail::promise_base](detail::promise_base);

  见 include/coro/task.hpp:287。

  因此 clean(std::coroutine_handle<> handle) 里可以这样做：

  auto task_handle = coroutine_handle::from_address(handle.address());
  auto& promise = task_handle.promise();

  然后你就能访问 promise_base 里的字段：

  if (promise.detached && handle.done()) {
      handle.destroy();
  }

  完整方向大概是：

  [[CORO_TEST_USED(lab1)]] inline auto clean(std::coroutine_handle<> handle) noexcept -> void
  {
      auto task_handle = coroutine_handle::from_address(handle.address());
      if (task_handle.promise().detached && handle.done())
      {
          handle.destroy();
      }
  }

  这里关键点是：

  handle.address()

  拿到协程帧地址。

  std::coroutine_handle[detail::promise_base](detail::promise_base)::from_address(...)

  把同一个协程帧地址重新解释成“promise 是 promise_base”的句柄。

  为什么这成立？因为你的具体 promise 是：

  promise<T></t> : public promise_base
  promise<void></void> : public promise_base

  所以通过基类 promise handle 访问 promise_base 那部分状态是合理的。

1. detached 必须放在 promise_base，不能只放在 promise<T></t> 或 promise<void></void>，否则 clean() 无法统一访问。
2. clean() 只能访问 promise_base 里的公共状态，不能访问具体返回值，因为它不知道这是 promise<int></int> 还是 promise<void></void>。

  所以设计应该是：

  struct promise_base {
      std::coroutine_handle<> parent{nullptr};
      bool detached{false};
  };

  然后：

  task::detach()
  {
      m_coroutine.promise().detached = true;
      m_coroutine = nullptr;
  }

  clean() 再通过 promise_base 判断是否销毁。

› from_address的作用是什么,为什么能转换为具体的coroutine_handle? handle.address()获取的是什么?

• handle.address() 获取的是：协程帧的原始地址。

  可以把一个 coroutine handle 理解成“指向协程帧的轻量指针”。协程帧里面保存了：

  promise 对象
  局部变量
  挂起点状态
  恢复/销毁需要的运行时信息

  所以：

  void* p = handle.address();

  拿到的是这个协程帧的底层地址。

  ———

  from_address() 的作用是：根据一个协程帧地址，重新构造一个 coroutine_handle 对象。

  例如：

  auto raw = handle.address();
  auto h = std::coroutine_handle[detail::promise_base](detail::promise_base)::from_address(raw);

  意思是：

  我知道 raw 指向一个协程帧；
  请把它包装成 coroutine_handle[detail::promise_base](detail::promise_base)。

  它不会创建新协程，也不会拷贝协程帧，只是把同一个地址重新包装成另一种 handle 类型。

  ———

  为什么能这样转换？

  因为 std::coroutine_handle<> 是类型擦除句柄，它只知道地址，不知道 promise 类型：

  std::coroutine_handle<> h;

  它可以：

  h.resume();
  h.destroy();
  h.done();
  h.address();

  但不能：

  h.promise();

  因为它不知道 promise 类型。

  如果你确定这个协程来自 task<T></t>，那么它的 promise 一定继承自：

  detail::promise_base

  于是你可以从同一个地址恢复成：

  std::coroutine_handle[detail::promise_base](detail::promise_base)

  然后访问：

  h.promise()

  也就是访问 promise_base 那部分。

  ———

  注意：这不是 C++ 的安全动态转换。

  from_address() 不检查类型对不对。你必须自己保证：

  handle.address()

  确实来自一个 promise 兼容 detail::promise_base 的协程。

  在这个项目里，clean() 是给 tinyCoro 的 task 用的，而所有 task<T></t> 的 promise 都继承 promise_base，所以这样做合理。

  一句话总结：

  address()：从 handle 拿到底层协程帧地址
  from_address()：用底层地址重新包装出指定 promise 类型的 handle

  coroutine_handle<> 是“无类型句柄”，coroutine_handle<promise_base> 是“能访问 promise_base 的句柄”。

 我先看 uring_proxy.hpp 里 fixed fd 相关代码和它依赖的 marked_buffer/fixed_fds，这样能把 m_fds 的作用讲准确。

• uring_proxy.hpp 里的 m_fds 是为了支持 io_uring 的 fixed file / registered file 优化，也就是配合 IOSQE_FIXED_FILE 使用。

  默认配置里它是关闭的：

  constexpr bool kEnableFixfd = false;

  所以当前 Lab 阶段你可以先理解，不一定要用它。

  它解决什么问题

  普通 io_uring SQE 里写的是系统 fd：

  io_uring_prep_read(sqe, sockfd, buf, len, ...)

  内核每次处理 I/O 时都要根据这个 fd 去进程 fd 表里查找对应文件对象。

  fixed file 的思路是：提前把一批 fd 注册到 io_uring 里，之后 SQE 不再传真实 fd，而是传“注册表下标”：

  普通模式：
    SQE.fd = 真实 fd

  fixed file 模式：
    SQE.fd = 注册 fd 表里的 index
    SQE.flags |= IOSQE_FIXED_FILE

  这样可以减少 fd 查找和引用计数开销，适合高频 I/O。

  m_fds 是什么

  在 include/coro/uring_proxy.hpp:294：

  ::coro::detail::marked_buffer<int, config::kFixFdArraySize> m_fds;

  marked_buffer<int, N> 本质是一个固定长度数组加一个空闲下标队列：

  Type data[length];
  std::queue<int></int> que;

  见 include/coro/marked_buffer.hpp:79。

  所以 m_fds 可以理解成：

  m_fds.data: 当前注册到 io_uring 的 fd 数组
  m_fds.que : 哪些下标目前空闲

  它借出去的对象是：

  struct item {
      int idx;
      Type* ptr;
  };

  也就是：

  idx：注册 fd 表中的位置
  ptr：指向 m_fds.data[idx]

  初始化流程

  在 include/coro/uring_proxy.hpp:70：

  if constexpr (config::kEnableFixfd)
  {
      m_fds.init();

    m_null_fds = std::vector<int></int>{};
      for (int i = 0; i < config::kFixFdArraySize; i++)
      {
          auto fd = ::coro::utils::get_null_fd();
          m_null_fds.push_back(fd);
      }

    m_fds.set_data(m_null_fds);

    io_uring_register_files(&m_uring, m_fds.data, config::kFixFdArraySize);
  }

  含义是：

1. 准备 kFixFdArraySize 个 fd 槽位
2. 初始状态全部填 /dev/null fd
3. 把整个 fd 数组注册给 io_uring

  为什么要用 m_null_fds？
  因为注册表里的每个槽位都要有一个合法 fd。空闲槽位不能随便放垃圾值，所以用 /dev/null 的 fd 占位。

  get_fixed_fd()

  在 include/coro/uring_proxy.hpp:246：

  auto get_fixed_fd() noexcept -> uring_fds_item
  {
      if constexpr (!config::kEnableFixfd)
      {
          return invalid_fd_item;
      }
      return m_fds.borrow();
  }

  作用：从 m_fds 借一个空闲槽位。

  如果 fixed fd 没开，返回 invalid item。

  借到的 item 大概是：

  idx = 3
  ptr = &m_fds.data[3]

  fixed_fds::assign() 怎么用它

  看 include/coro/io/base_io_type.hpp:18：

  *(item.ptr) = fd;
  fd          = item.idx;
  flag |= IOSQE_FIXED_FILE;
  local_engine().get_uring().update_register_fixed_fds(item.idx);

  这段很关键。

  假设真实 socket fd 是 42，借到的 fixed slot 是 3：

  m_fds.data[3] = 42
  fd = 3
  flag |= IOSQE_FIXED_FILE
  更新 io_uring 注册 fd 表

  之后构造 SQE 时传进去的 fd 已经不是 42，而是 3。因为设置了 IOSQE_FIXED_FILE，内核会把 3 当作 registered file index 解释。

  update_register_fixed_fds()

  在 include/coro/uring_proxy.hpp:271：

  io_uring_register_files_update(&m_uring, 0, m_fds.data, config::kFixFdArraySize);

  作用：把用户态的 m_fds.data 更新到内核的 io_uring registered file table。

  代码里注释了一个 TODO：

  // TODO: Why local update is incorrect?
  // io_uring_register_files_update(&m_uring, index, m_fds.data, 1)

  也就是说作者本来想只更新一个槽位，但这里选择了从 0 开始更新整个数组。

  back_fixed_fd()

  在 include/coro/uring_proxy.hpp:260：

  m_fds.data[item.idx] = m_null_fds[item.idx];
  update_register_fixed_fds(item.idx);
  m_fds.return_back(item);

  作用：归还 fixed fd 槽位。

  流程是：

1. 把 m_fds.data[idx] 恢复成 /dev/null fd
2. 更新内核 registered file table
3. 把 idx 放回空闲队列

  对应 RAII 包装在 include/coro/io/base_io_type.hpp:30，fixed_fds 析构时会自动归还。

  整体流程

  uring_proxy::init()
    创建 fd 注册表
    全部填 null fd
    io_uring_register_files()

  tcp_server / tcp_connector 构造
    fixed_fds 从 local_engine().get_uring() 借一个槽位
    把真实 fd 写入槽位
    把 fd 变量改成槽位 idx
    SQE flag 加 IOSQE_FIXED_FILE
    更新 registered files

  发起 read/write/accept
    SQE.fd = idx
    SQE.flags 包含 IOSQE_FIXED_FILE

  连接关闭/对象析构
    槽位恢复 null fd
    归还给 m_fds

  一句话总结：m_fds 是 uring_proxy 内部维护的“固定 fd 注册表槽位池”，用于把真实 fd 临时注册进 io_uring，然后让 SQE 用注册表下标发起
  I/O，从而启用 IOSQE_FIXED_FILE 优化。

› IOSQE_FIXED_FILE feature指的是什么

• IOSQE_FIXED_FILE 是 io_uring 的一个 SQE flag，用来告诉内核：

  这个 SQE 里的 fd 字段不是普通文件描述符，
  而是 io_uring 已注册文件表里的下标。

  普通模式：

  io_uring_prep_read(sqe, fd, buf, len, offset);

  这里的 fd 是进程 fd 表里的真实 fd，比如 42。内核处理时要根据 42 去查 fd 表，拿到对应的 struct file。

  fixed file 模式：

  sqe->flags |= IOSQE_FIXED_FILE;
  io_uring_prep_read(sqe, fixed_index, buf, len, offset);

  这里的 fixed_index 不是系统 fd，而是你提前注册给 io_uring 的文件表下标，比如 3。

  注册过程类似：

  int fds[] = {fd1, fd2, fd3};
  io_uring_register_files(&ring, fds, 3);

  之后：

  sqe->fd = 0; // 表示使用 fds[0]
  sqe->flags |= IOSQE_FIXED_FILE;

  为什么要这样做

  主要是性能优化。
  对象，减少高频 I/O 的开销。

  在这个项目里的对应关系

  m_fds.data 是注册给 io_uring 的 fd 数组：

  io_uring_register_files(&m_uring, m_fds.data, config::kFixFdArraySize);

  fixed_fds::assign() 会把真实 fd 放进某个槽位，然后把用户代码里的 fd 改成槽位下标：

  *(item.ptr) = fd;      // 注册真实 fd
  fd = item.idx;         // 之后 fd 变量变成 fixed index
  flag |= IOSQE_FIXED_FILE;

  所以后面构造 SQE 时，如果 flag 带了 IOSQE_FIXED_FILE，内核就会把 fd 当作 registered file index，而不是普通 fd。

  一句话

  IOSQE_FIXED_FILE = “我这个 I/O 请求使用的是预注册 fd 表里的文件，请按下标解释 sqe->fd，不要按普通 fd 解释。”

› 普通 fd 每次 I/O 都要做 fd 查找、引用管理等操作。fixed file 提前把 fd 注册到 ring 里，后续 SQE 只传 index，内核可以更快找到对应文
  件对象，减少高频 I/O 的开销。我不理解这句话

• 可以把它理解成“每次都报身份证号办业务” vs “提前办会员卡，以后只报座位号”。

  普通 fd 模式

  比如你的进程里有一个 socket：

  fd = 42

  你提交一次 read：

  sqe->fd = 42;

  内核收到这个 I/O 请求后，并不能直接从 42 读数据。42 只是你这个进程 fd 表里的一个编号。内核还要做一步查找：

  当前进程 fd 表[42] -> struct file* -> 真正的 socket/file 对象

  而且为了保证这个 I/O 执行期间文件对象不会被关闭释放，内核还要做引用计数管理，大概类似：

  找到 file
  增加引用计数
  执行 I/O
  完成后减少引用计数

  一次 I/O 做一次，这些操作会有成本。单次看不明显，高 QPS 时就明显。

  fixed file 模式

  你提前告诉 io_uring：

  我把 fd=42 注册到 ring 的 fixed file 表第 3 号位置

  也就是：

  ring registered files[3] -> struct file*

  之后你提交 I/O 时不再传真实 fd 42，而是传：

  index = 3

  并设置：

  IOSQE_FIXED_FILE

  内核看到这个 flag 后就知道：

  不要去当前进程 fd 表里查 42；
  sqe->fd 是 registered files 的下标；
  直接去 ring 的 fixed file 表[3] 找 file。

  这张 fixed file 表是 io_uring 自己维护的，里面已经持有了文件对象引用，所以后续每次 I/O 不需要重复完整 fd 查找和普通 fd 引用获取流
  程。

  更具体一点

  普通模式：

  每次 I/O:
    sqe->fd = 42
    查当前进程 fd table
    检查 fd 是否有效
    拿到 struct file*
    完成后释放引用

    fd=42 -> 注册到 ring fixed table[3]
    ring 持有这个 file 引用

  每次 I/O:
    sqe->fd = 3
    sqe->flags |= IOSQE_FIXED_FILE
    直接从 ring fixed table[3] 拿 struct file*
    做 I/O

  为什么这能变快

  因为省掉了“从进程 fd 表查找真实 file 对象”以及部分重复引用管理成本。

  这不是改变了 I/O 本身的速度，磁盘/网络还是那个速度；它优化的是 每次提交 I/O 时内核定位文件对象的开销。在 TCP echo server 这种大量小
  I/O、高频 read/write 的场景下，这种开销会被放大，所以 fixed file 有意义。

# marked_buffer

• 我会按文件本身逐行解释 marked_buffer 的数据结构和它在 fixed fd 场景里的用法。

• Explored
  └ Read marked_buffer.hpp

• include/coro/marked_buffer.hpp:1 定义了一个很小的“固定容量资源池”。

  核心模板是：

  template<typename Type, size_t length>
  struct marked_buffer

  意思是：

  存 length 个 Type 类型对象；
  可以把其中某个槽位借出去；
  用完后再归还。

  整体结构

  Type data[length];
  std::queue<int></int> que;

  data 是真实存储区。

  que 保存“当前空闲的下标”。

  比如：

  marked_buffer<int, 4> buf;

  它内部类似：

  data[0]
  data[1]
  data[2]
  data[3]

  que = [0, 1, 2, 3]  // 表示这几个槽位都空闲

  item 是什么

  在 include/coro/marked_buffer.hpp:20：

  struct item
  {
      inline auto valid() -> bool { return idx >= 0; }

    inline auto set_invalid() -> void
      {
          idx = -1;
          ptr = nullptr;
      }

    int   idx;
      Type* ptr;
  };

  item 是借出去的“资源凭证”。

  它包含：

  idx：槽位下标
  ptr：指向 data[idx]

  如果：

  idx < 0

  表示这个 item 无效，没借到资源。

  init()

  void init() noexcept
  {
      std::queue<int></int> temp;
      que.swap(temp);
  }

  作用是清空空闲队列。

  注意：它只清空 que，不会清空 data。

  set_data()

  void set_data(const std::vector<Type></type>& values)
  {
      assert(data.size() == length && "");

    for (int i = 0; i < length; i++)
      {
          que.push(i);
      }

    for (int i = 0; i < length; i++)
      {
          data[i] = values[i];
      }
  }

  逻辑上是：

1. 把 0 到 length-1 全部加入空闲队列
2. 用 values 初始化 data

  但这里有个明显问题：

  assert(data.size() == length && "");

  data 是 C 数组：

  Type data[length];

  C 数组没有 .size() 成员。更合理的应该是检查：

  assert(values.size() == length);

  如果你现在编译没报这个问题，是因为 kEnableFixfd=false，这段模板代码可能没有被实例化到相关路径。

  borrow()

  在 include/coro/marked_buffer.hpp:56：

  item borrow() noexcept
  {
      if (que.empty())
      {
          return item{.idx = -1, .ptr = nullptr};
      }

    auto idx = que.front();
      que.pop();
      return item{.idx = idx, .ptr = &(data[idx])};
  }

  作用：借一个空闲槽位。

  比如当前：

  que = [0, 1, 2, 3]

  调用 borrow() 后：

  返回 item{idx=0, ptr=&data[0]}
  que = [1, 2, 3]

  如果没有空闲槽位，就返回 invalid item。

  return_back()

  在 include/coro/marked_buffer.hpp:68：

  void return_back(item it) noexcept
  {
      if (!it.valid())
      {
          return;
      }

    it.ptr = nullptr;
      que.push(it.idx);
  }

  作用：把槽位归还。

  比如归还 idx=0：

  que = [1, 2, 3]
  return_back(0)
  que = [1, 2, 3, 0]

  注意这里的：

  it.ptr = nullptr;

  没有影响调用者手里的 item，因为 it 是按值传参。真正起作用的是：

  que.push(it.idx);

  如果想让调用者的 item 也失效，调用者需要自己调用：

  item.set_invalid();

  项目里的 fixed_fds::return_back() 就这么做了：

  back_fixed_fd(item);
  item.set_invalid();

  在这个项目中的用途

  它主要被 uring_proxy 用作 fixed fd 槽位池：

  marked_buffer<int, config::kFixFdArraySize> m_fds;

  含义是：

  data[i] 存第 i 个 registered fd
  que 记录哪些 fd 槽位空闲
  borrow() 借一个 fixed file index
  return_back() 归还这个 index

  一句话总结：

  marked_buffer 是一个固定大小数组 + 空闲下标队列，用来管理“可借出、可归还”的槽位。它不负责内存分配，只负责标记哪些槽位当前可用。

# uring_proxy.hpp

• 我会按 uring_proxy.hpp 的成员和函数分组解释：eventfd、ring 初始化、SQE/CQE 操作、fixed fd 优化。

• include/coro/uring_proxy.hpp:1 是项目对 liburing 的轻量封装。它把底层 io_uring、eventfd、fixed fd 注册表包装成一个 uring_proxy 类，
  给 engine 使用。

  类型别名

  using ursptr = io_uring_sqe*;
  using urcptr = io_uring_cqe*;

- ursptr：指向 SQE，提交 I/O 请求用。
- urcptr：指向 CQE，读取 I/O 完成结果用。

  using urchandler = std::function<void(urcptr)>;

  用于遍历 CQE 时传 callback。

  using uring_fds_item = marked_buffer<int, config::kFixFdArraySize>::item;

  fixed fd 槽位的借用凭证。

  ———

  构造函数

  uring_proxy() noexcept
  {
  m_efd = eventfd(0, 0);
  }

  创建一个 eventfd。

  它的作用是“唤醒 worker 线程”。项目把 eventfd 注册给 io_uring，当 I/O 完成时，内核会写 eventfd，阻塞等待的线程就会醒来。

  ———

  init()

  auto init(unsigned int entry_length) noexcept -> void

  负责初始化 io_uring。

  主要流程：

1. 清空参数：

  memset(&m_para, 0, sizeof(m_para));

2. 可选开启 SQPOLL：

  m_para.flags |= IORING_SETUP_SQPOLL;

  这个默认没开。SQPOLL 是让内核线程帮忙轮询提交队列。

3. 初始化 ring：

  io_uring_queue_init_params(entry_length, &m_uring, &m_para);

  entry_length 是队列长度，项目里来自：

  config::kEntryLength

4. 注册 eventfd：

  io_uring_register_eventfd(&m_uring, m_efd);

  这样 io_uring 有 CQE 完成时，会通知 m_efd。

5. 如果开启 fixed fd，则注册文件表：

  io_uring_register_files(&m_uring, m_fds.data, config::kFixFdArraySize);

  默认 kEnableFixfd=false，所以 Lab2 可以先忽略这块。

  ———

  deinit()

  auto deinit() noexcept -> void

  释放资源：

  close(m_efd);
  io_uring_queue_exit(&m_uring);

  如果 fixed fd 开启，还会关闭用于占位的 /dev/null fd。

  ———

  peek_uring()

  auto peek_uring() noexcept -> bool

  非阻塞检查有没有 CQE：

  io_uring_peek_cqe(&m_uring, &cqe);
  return cqe != nullptr;

  它不等待，只看现在有没有完成结果。

  ———

  wait_uring()

  auto wait_uring(int num = 1) noexcept -> void

  阻塞等待 CQE：

  io_uring_wait_cqe(&m_uring, &cqe);

  或者等待至少 num 个：

  io_uring_wait_cqe_nr(&m_uring, &cqe, num);

  这个函数只负责等，不负责处理 CQE。

  ———

  seen_cqe_entry()

  io_uring_cqe_seen(&m_uring, cqe);

  告诉 io_uring：

  这个 CQE 我处理完了，可以从完成队列释放了。

  ———

  get_free_sqe()

  return io_uring_get_sqe(&m_uring);

  从提交队列拿一个空 SQE。awaiter 会用它填入具体 I/O 操作，比如 read、write、accept。

  如果提交队列满了，可能返回 nullptr。

  ———

  submit()

  return io_uring_submit(&m_uring);

  把已经填好的 SQE 提交给内核。

  在 Lab2a 里，engine::poll_submit() 会调用它。

  ———

  handle_for_each_cqe()

  auto handle_for_each_cqe(urchandler f, bool mark_finish = false) noexcept -> size_t

  遍历当前 CQ 中所有 CQE：

  io_uring_for_each_cqe(&m_uring, head, cqe)
  {
      f(cqe);
      i++;
  }

  如果 mark_finish=true，最后会：

  cq_advance(i);

  批量标记这些 CQE 已处理。

  ———

  wait_eventfd()

  auto wait_eventfd() noexcept -> uint64_t
  {
      eventfd_read(m_efd, &u);
      return u;
  }

  阻塞读 eventfd。

  用途：当 engine 没有普通任务可执行时，可以阻塞在 eventfd 上，避免 CPU 空转。I/O 完成或外部提交任务时，可以写 eventfd 唤醒它。

  ———

  peek_batch_cqe()

  io_uring_peek_batch_cqe(&m_uring, cqes, num);

  批量取 CQE，非阻塞。

  Lab2a 推荐用这个处理已完成 I/O：

  auto count = m_upxy.peek_batch_cqe(m_urc.data(), config::kQueCap);

  然后循环处理 m_urc[0..count)。

  ———

  write_eventfd()

  eventfd_write(m_efd, num);

  主动写 eventfd，唤醒可能正在 wait_eventfd() 的线程。

  Lab2a 很关键：submit_task() 时需要唤醒 engine，否则 worker 可能正在等 I/O，收不到新任务。

  ———

  cq_advance()

  io_uring_cq_advance(&m_uring, num);

  批量告诉 io_uring：

  前 num 个 CQE 已处理完。

  比对每个 CQE 单独调用 io_uring_cqe_seen() 更高效。

  ———

  fixed fd 相关

  这部分包括：

  m_null_fds
  m_fds
  get_fixed_fd()
  back_fixed_fd()
  update_register_fixed_fds()

  它们服务于 IOSQE_FIXED_FILE 优化。

  默认关闭：

  constexpr bool kEnableFixfd = false;

  如果开启，m_fds 管理一组注册到 io_uring 的 fd 槽位：

  marked_buffer<int, config::kFixFdArraySize> m_fds;

  get_fixed_fd() 借一个槽位：

  return m_fds.borrow();

  back_fixed_fd() 归还槽位，并恢复成 /dev/null fd：

  m_fds.data[item.idx] = m_null_fds[item.idx];
  update_register_fixed_fds(item.idx);
  m_fds.return_back(item);

  update_register_fixed_fds() 更新内核 registered file table：

  io_uring_register_files_update(&m_uring, 0, m_fds.data, config::kFixFdArraySize);

  这部分不是 Lab2a 的主线，先知道它是 fd 注册表优化即可。

  ———

  成员变量

  int m_efd;

  eventfd，用于通知/唤醒。

  io_uring_params m_para;

  ring 初始化参数，比如 SQPOLL。

  io_uring m_uring;

  真正的 io_uring 对象。

  std::vector<int></int> m_null_fds;
  marked_buffer<int, config::kFixFdArraySize> m_fds;

  fixed fd 功能用的 fd 槽位池。

  ———

  Lab2a 里最需要掌握的是这几个：

  get_free_sqe()
  submit()
  peek_batch_cqe()
  cq_advance()
  wait_eventfd()
  write_eventfd()

  它们对应 engine 的核心循环：

  awaiter 填 SQE
  engine submit SQE
  内核完成 I/O 产生 CQE
  engine 取 CQE
  engine 调 callback 恢复协程
  engine 标记 CQE 已处理

# lab2a

• 你现在不用急着实现，先把 Lab2a 的关系理顺。可以先只记住一句话：

  engine 就是一个循环：有普通任务就 resume；有 I/O 请求就 submit；有 I/O 完成就把等待的协程放回普通任务队列。

  先看整体关系

  task
    ↓ handle
  engine.m_task_queue
    ↓ schedule()
  engine.exec_one_task()
    ↓ resume()
  协程开始跑
    ↓ co_await I/O awaiter
  awaiter 从 engine 拿 SQE
  awaiter 填 SQE
  awaiter 调 engine.add_io_submit()
  协程暂停
    ↓
  engine.poll_submit()
    ↓ io_uring_submit()
  内核执行 I/O
    ↓ CQE
  engine.poll_submit()
    ↓ handle_cqe_entry()
  I/O callback
    ↓
  engine.submit_task(等待 I/O 的协程 handle)
    ↓
  协程回到 m_task_queue，之后继续 resume

  也就是说，engine 管两种任务：

1. 普通任务：coroutine_handle
2. I/O 任务：SQE/CQE

  你要做什么

  Lab2a 只做 src/engine.cpp:7 和可能在 include/coro/engine.hpp:168 里加几个成员变量。

  你要补这些函数：

  init()
  deinit()
  ready()
  get_free_urs()
  num_task_schedule()
  schedule()
  submit_task()
  poll_submit()
  add_io_submit()
  empty_io()

  别把它想复杂。逐个函数的职责如下。

1. init()

  初始化 engine。

  要做：

  初始化 io_uring
  把当前线程的 linfo.egn 指向 this
  初始化 I/O 计数

  大概是：

  m_upxy.init(config::kEntryLength);
  linfo.egn = this;

2. deinit()

  释放 engine。

  要做：

  释放 io_uring
  清掉 linfo.egn

3. submit_task(handle)

  别人提交一个协程给 engine。

  要做：

  把 handle 放进 m_task_queue
  任务数 +1
  写 eventfd 唤醒 poll_submit

  为什么要唤醒？
  因为 engine 可能睡在 wait_eventfd()，新任务来了要叫醒它。

4. ready()

  判断有没有普通任务。

  任务数 > 0

5. num_task_schedule()

  返回普通任务数量。

  return 任务数

6. schedule()

  取一个普通任务。

  从 m_task_queue pop 一个 handle
  任务数 -1
  return handle

7. get_free_urs()

  给 I/O awaiter 一个 SQE。

  return m_upxy.get_free_sqe()

8. add_io_submit()

  awaiter 填好 SQE 后调用。

  要做：

  待提交 I/O 数 +1
  写 eventfd 唤醒 poll_submit

9. empty_io()

  判断 engine 里有没有 I/O 相关事情没完成。

  待提交 I/O 数 == 0 && 正在运行 I/O 数 == 0

10. poll_submit()

  这是最重要的，但也可以拆开看。

  它要做：

  如果有待提交 I/O：
      io_uring_submit()
      待提交数减少
      正在运行数增加

  如果没有普通任务、但有正在运行 I/O：
      等 eventfd，避免空转

  批量取 CQE：
      对每个 CQE 调 handle_cqe_entry()
      正在运行数减少
      标记 CQE 已处理

  你最少需要加的成员变量

  在 engine.hpp 里加类似：

  std::atomic<size_t> m_task_num{0};
  std::atomic<size_t> m_submit_io{0};
  std::atomic<size_t> m_running_io{0};

  含义：

  m_task_num   普通任务队列里有多少 handle
  m_submit_io  已经填好 SQE，但还没 submit 的数量
  m_running_io 已经 submit 给内核，但还没收到 CQE 的数量

  Lab2a 的核心循环长什么样

  测试里会这么驱动 engine：

  while (...) {
      m_engine.poll_submit();

    while (m_engine.ready()) {
          m_engine.exec_one_task();
      }
  }

  所以你不用写完整 event loop，只要保证：

  poll_submit 处理 I/O 和唤醒
  ready/exec_one_task 处理普通任务

  最小理解模型

  把 engine 想成一个餐厅前台：

  m_task_queue：等着服务员处理的客人
  SQE：已经写好的外卖订单
  submit_io：订单写好了但还没发给厨房
  running_io：厨房正在做
  CQE：厨房做好了，通知前台
  callback：把等餐的人叫回来继续吃

  你现在真正要写的不是“协程系统”，而是：

  维护三个计数
  维护一个队列
  把 SQE 提交出去
  把 CQE 对应的协程放回队列

  建议你下一步先只实现前 7 个简单函数：init/deinit/ready/get_free_urs/num_task_schedule/schedule/submit_task，先让普通任务测试过；再
  做 add_io_submit/empty_io/poll_submit。

› poll_submit是只负责io任务吗

• 基本可以这么理解：poll_submit() 主要负责 I/O 任务，但它还要配合普通任务做“不要阻塞错时机”的判断。

  它不直接执行普通 task。普通 task 是：

  exec_one_task()

  负责的。

  poll_submit() 做的是：

1. 提交已经准备好的 SQE
2. 等待/检查 I/O 完成
3. 处理 CQE
4. 通过 callback 把等待 I/O 的协程重新放回任务队列

  所以它的主职责是 I/O。

  但为什么它还要看普通任务？

  因为如果它在不该阻塞的时候阻塞，就会卡死。

  比如现在队列里有普通任务：

  m_task_queue 非空
  没有 I/O 完成

  如果 poll_submit() 直接：

  wait_eventfd();

  那它会睡住，普通任务明明在队列里却没人执行。

  所以 poll_submit() 里要有类似判断：
      可以等 eventfd

  如果有普通任务：
      不要阻塞，赶紧返回，让外层去 exec_one_task()

  也就是说：

  poll_submit 不执行普通任务
  但它必须知道有没有普通任务，避免错误阻塞

  你可以按职责分：

  submit_task/schedule/exec_one_task:
    普通协程任务路径

  get_free_urs/add_io_submit/poll_submit/empty_io:
    I/O 路径

  poll_submit() 是 I/O 路径的核心，但会被普通任务状态影响。

› 文档上说engine 可能会收到别的线程投递的协程任务，因此对于任务队列的操作即存取任务必须是线程安全的。所以还需要加条件变量和互斥锁来
  保护任务队列是吗?

• 不需要为了 m_task_queue 再加 std::mutex + condition_variable，因为这里已经用了线程安全队列。

  在 include/coro/engine.hpp:55：

  template<typename T></typename>
  // multi producer and multi consumer queue
  using mpmc_queue = AtomicQueue<T></t>;

  然后 engine 里是：

  mpmc_queue<coroutine_handle<>> m_task_queue;

  mpmc_queue 的意思就是：

  multi-producer multi-consumer queue
  多个生产者、多个消费者队列

  也就是说，多个线程可以同时调用：

  submit_task()

  把任务塞进去；工作线程可以同时从里面取任务。这个队列本身就是为了并发场景准备的。

  你真正需要自己保证线程安全的是：

1. 任务数量计数
2. I/O 状态计数
3. eventfd 唤醒逻辑

  比如你可能会加：

  std::atomic<size_t> m_task_num{0};
  std::atomic<size_t> m_submit_io{0};
  std::atomic<size_t> m_running_io{0};

  因为 ready() / num_task_schedule() / empty_io() 都会读这些状态，而 submit_task()、schedule()、add_io_submit()、poll_submit() 会修改
  它们。

  为什么不用 condition_variable

  这个项目里唤醒 worker 的机制不是 std::condition_variable，而是 eventfd：

  m_upxy.write_eventfd(1);
  m_upxy.wait_eventfd();

  原因是 engine 既要等普通任务，也要等 io_uring 完成事件。io_uring 已经和 eventfd 绑定了：

  io_uring_register_eventfd(&m_uring, m_efd);

  所以 eventfd 可以统一承接：

  新普通任务来了 -> submit_task 写 eventfd
  新 I/O 待提交来了 -> add_io_submit 写 eventfd
  I/O 完成了 -> io_uring 写 eventfd

  如果你再用 condition_variable，会变成两套唤醒系统，容易写复杂，也不符合这个项目的设计。

  你需要注意的点

  如果 m_task_queue 满了，AtomicQueue 的 push 行为可能会阻塞或失败，取决于你用的接口。Lab2a 可以先按测试场景做：

  m_task_queue.push(handle);
  m_task_num.fetch_add(1);
  m_upxy.write_eventfd(1);

  取任务：

  auto handle = m_task_queue.pop();
  m_task_num.fetch_sub(1);
  return handle;

  总之：

  任务队列本身：AtomicQueue 负责线程安全
  任务数量/I/O 数量：你用 atomic 负责线程安全
  阻塞唤醒：eventfd 负责，不用 condition_variable




# Lab2b 要做的是：用 Lab2a 的 engine 包一层工作线程，再用 scheduler 管多个 context。

  Lab2a 只有一个 engine，你手动调用：

  poll_submit();
  exec_one_task();

  Lab2b 要变成：

  scheduler
    管多个 context

  context
    每个 context 有一个工作线程
    每个 context 有一个 engine
    工作线程不断驱动 engine

  整体关系：

  用户 submit_to_scheduler(task)
    ↓
  scheduler 用 dispatcher 选一个 context
    ↓
  context.submit_task(handle)
    ↓
  context 内部 engine.submit_task(handle)
    ↓
  context 的工作线程 run()
    ↓
  engine.poll_submit()
  engine.exec_one_task()

  ———

  context.hpp 要理解什么

  include/coro/context.hpp:61 定义一个 context。

  它的核心成员现在是：

  CORO_ALIGN engine   m_engine;
  unique_ptr<jthread></jthread> m_job;
  ctx_id              m_id;

  含义：

  m_engine：真正执行任务和 I/O 的 engine
  m_job：工作线程
  m_id：context id

  你还需要加一个引用计数，文档说的 register_wait/unregister_wait 就是为这个：

  std::atomic<int></int> m_wait_count{0};

  或者类似名字。

  为什么需要引用计数？
  后面 Lab4/Lab5 的协程同步组件会让协程 suspend，但这个协程不在 engine 的普通队列里，也不在 io_uring 里。此时如果 context 只看：

  任务队列空 && I/O 空

  它可能提前退出。引用计数就是告诉 context：

  还有协程挂在同步组件里，不能退出。

  ———

  context::init()

  src/context.cpp:11

  要做：

1. 初始化 m_engine
2. 绑定线程局部 linfo.ctx = this

  大概职责：

  linfo.ctx = this;
  m_engine.init();

  注意 m_engine.init() 会绑定 linfo.egn = &m_engine。

  ———

  context::deinit()

  src/context.cpp:16

  要做：

1. m_engine.deinit()
2. linfo.ctx = nullptr

  ———

  context::start()

  src/context.cpp:21

  框架已经给了：

  m_job = make_unique<jthread></jthread>(
      [this](stop_token token)
      {
          this->init();
          this->run(token);
          this->deinit();
      });

  意思是启动一个工作线程，这个线程里：

  初始化 context
  运行主循环
  退出时释放资源

  你一般不需要大改，只要需要时补一点状态即可。

  ———

  context::notify_stop()

  src/context.cpp:33

  它要给 jthread 发停止信号：

  m_job->request_stop();

  但只发 stop 不够，因为工作线程可能正阻塞在 engine.poll_submit() 的 wait_eventfd() 里。

  所以还要唤醒 engine：

  m_engine.get_uring().write_eventfd(1);

  否则线程可能收到了 stop 请求，但睡在 eventfd 上，醒不过来。

  ———

  context::submit_task(handle)

  src/context.cpp:38

  它就是转发给 engine：

  m_engine.submit_task(handle);

  因为真正任务队列在 engine 里。

  ———

  context::register_wait() / unregister_wait()

  src/context.cpp:43

  用于维护“挂起在同步组件里的协程数量”。

  register_wait(n):
      m_wait_count += n

  unregister_wait(n):
      m_wait_count -= n
      如果变成 0，可以唤醒 engine

  为什么 unregister_wait() 可能要唤醒？
  如果 context 正在等“所有任务结束”，引用计数归零后，它需要重新检查退出条件。

  ———

  context::run(stop_token token)

  src/context.cpp:53

  这是 Lab2b 的核心循环。

  它要不断驱动 engine：

1. poll_submit() 处理 I/O/等待事件
2. while engine.ready():
   exec_one_task()
3. 判断是否应该退出

  退出条件要考虑：

  收到 stop 请求
  engine 没有普通任务
  engine 没有 I/O
  wait_count == 0

  但测试里还有一个要求：单独使用 context 时，没有 scheduler 调 stop，也要能自动退出。所以通常还需要：

  如果没有 scheduler 管理，任务和 I/O 都空了，wait_count 也为 0，也可以退出

  从测试看，他们会这样：

  m_ctx.submit_task(...)
  m_ctx.start()
  m_ctx.join()

  中间没有 notify_stop()，所以 run() 不能永远等待 stop。

  ———

  scheduler.hpp 要理解什么

  include/coro/scheduler.hpp:22 是全局调度器，单例。

  用户主要用：

  scheduler::init()
  submit_to_scheduler(task)
  scheduler::loop()

  它内部有：

  size_t m_ctx_cnt;
  detail::ctx_container m_ctxs;
  detail::dispatcher[config::kDispatchStrategy](config::kDispatchStrategy) m_dispatcher;

  含义：

  m_ctx_cnt：context 数量
  m_ctxs：所有 context
  m_dispatcher：决定任务交给哪个 context

  ———

  scheduler::init_impl()

  src/scheduler.cpp:5

  这部分已经基本写好：

  detail::init_meta_info();
  m_ctx_cnt = ctx_cnt;
  m_ctxs.reserve(m_ctx_cnt);
  for (...) {
      m_ctxs.emplace_back(std::make_unique<context></context>());
  }
  m_dispatcher.init(m_ctx_cnt, &m_ctxs);

  它做的是：

  初始化全局信息
  创建 ctx_cnt 个 context
  初始化 dispatcher

  如果开了内存池，也在这里初始化。

  ———

  scheduler::submit_task_impl()

  src/scheduler.cpp:41

  现在也基本写好了：

  size_t ctx_id = m_dispatcher.dispatch();
  m_ctxs[ctx_id]->submit_task(handle);

  它用 dispatcher 选一个 context，然后把任务提交过去。

  ———

  dispatcher 是什么

  include/coro/dispatcher.hpp:60 里当前只有 round-robin：

  auto dispatch() noexcept -> size_t
  {
      return m_cur.fetch_add(1, std::memory_order_acq_rel) % m_ctx_cnt;
  }

  意思是任务轮流分配：

  第 1 个任务 -> context 0
  第 2 个任务 -> context 1
  第 3 个任务 -> context 2
  ...

  它用 atomic，所以多线程提交时也是线程安全的。

  ———

  scheduler::loop_impl()

  src/scheduler.cpp:25

  这是 scheduler 核心。

  文档要求 v1.1 之后：

  启动所有 context
  等待所有 context 完成任务
  再统一发送 stop 信号
  再 join 所有 context

  为什么不让空 context 立刻退出？
  因为运行中的协程可能之后还会：

  submit_to_scheduler(...)

  如果其他 context 已经退出，scheduler 可能把新任务派给一个死 context。

  所以 scheduler 管理下，context 应该在 loop() 期间保持活着，直到 scheduler 统一 stop。

  大概流程：

  for each ctx:
      ctx->start();

  等待所有任务自然跑完 / 或轮询状态

  stop_impl();

  for each ctx:
      ctx->join();

  这里“等待所有任务自然跑完”需要你设计一个判断方式。简单做法可以依赖 context 自己 run 的退出逻辑；但要小心 scheduler 场景不能让空
  context 过早退出。

  ———

  stop_impl()

  src/scheduler.cpp:30

  已经给了示例：

  for (int i = 0; i < m_ctx_cnt; i++)
  {
      m_ctxs[i]->notify_stop();
  }

  它就是通知所有 context 停止。

  ———

  Lab2b 最关键的状态

  你大概率需要在 context 里加：

  std::atomic<int></int> m_wait_count{0};

  可能还需要：

  std::atomic<bool></bool> m_running{false};

  或者类似状态，帮助 scheduler 判断 context 是否完成。

  但先别过度设计。先目标明确：

  context 单独运行：
    提交完任务 -> start -> 自动跑完 -> join 返回

  scheduler 运行：
    init -> submit tasks -> loop
    所有 context 启动
    运行中可以继续 submit_to_scheduler
    所有任务完成后统一停止

  ———

  一句话总结：

  Lab2a 的 engine 是发动机。
  Lab2b 的 context 是一台装了发动机的工作线程。
  scheduler 是管理多台工作线程的调度器。

  你现在要做的就是让 context::run() 不断调用 engine 的 poll_submit/exec_one_task，再让 scheduler::loop() 启动、停止、等待多个  context。





先别看代码细节，先看运行流程。Lab2b 只有两种使用方式：

1. 单个 context 自己跑
2. scheduler 管多个 context 跑

  ———

  一、单个 context 怎么跑

  测试里会这样用：

  context ctx;

  ctx.submit_task(task1);
  ctx.submit_task(task2);

  ctx.start();
  ctx.join();

  流程是：

  主线程
    |
    | submit_task()
    v
  context.m_engine.m_task_queue 里有任务
    |
    | start()
    v
  创建一个工作线程
    |
    v
  工作线程执行 context::run()
    |
    v
  循环驱动 engine:
      poll_submit()
      while engine.ready():
          exec_one_task()
    |
    v
  任务全跑完，没有 I/O，没有等待协程
    |
    v
  run() 退出
    |
    v
  join() 返回

  所以单个 context 的重点是：

  任务跑完后，工作线程要自己退出

  因为测试没有调用：

  ctx.notify_stop();

  ———

  二、scheduler 怎么跑

  用户一般这样写：

  scheduler::init(4);

  submit_to_scheduler(task1);
  submit_to_scheduler(task2);

  scheduler::loop();

  流程是：

  scheduler::init(4)
    |
    v
  创建 4 个 context
    context0
    context1
    context2
    context3

  submit_to_scheduler(task1)
    |
    v
  dispatcher 选一个 context，比如 context0
    |
    v
  context0.submit_task(task1)

  submit_to_scheduler(task2)
    |
    v
  dispatcher 选下一个 context，比如 context1
    |
    v
  context1.submit_task(task2)

  scheduler::loop()
    |
    v
  启动所有 context 的工作线程
    |
    v
  每个 context::run() 驱动自己的 engine

  ———

  关键区别

  单个 context：

  任务跑完就退出

  scheduler 管理多个 context：

  不能让空 context 太早退出

  为什么？

  假设有 4 个 context，但最开始只有 1 个任务：

  context0 有任务
  context1 空
  context2 空
  context3 空

  如果空 context 看到自己没任务就退出了：

  context1 退出
  context2 退出
  context3 退出

  然后 context0 里的任务运行中又调用：

  submit_to_scheduler(new_task);

  scheduler 可能把 new_task 派给 context1。
  但 context1 已经退出了，任务就没人执行。

  所以 scheduler 模式下：

  context 即使暂时没任务，也要先活着
  直到 scheduler 统一通知 stop

  ———

  你可以这样理解职责

  context 的职责：

  我有一个 engine 和一个工作线程；
  我负责在线程里不断驱动 engine；
  别人可以给我提交任务；
  收到停止信号且任务都做完后，我退出。

  scheduler 的职责：

  我有很多 context；
  我决定任务交给哪个 context；
  我启动所有 context；
  我等所有任务做完；
  我统一通知所有 context 停止；
  我 join 所有 context。

  ———

  context::run() 心智模型

  它就是 worker loop：

  while (true)
  {
      m_engine.poll_submit();

    while (m_engine.ready())
      {
          m_engine.exec_one_task();
      }

    if (可以退出)
      {
          break;
      }
  }

  退出条件大概是：

  engine 没普通任务
  engine 没 I/O
  没有挂起等待的协程
  并且：
      单 context 模式：可以自己退出
      scheduler 模式：收到 stop 信号后退出

  ———

  scheduler::loop() 心智模型

  for each context:
      context.start();

  等待整体任务完成;

  for each context:
      context.notify_stop();

  for each context:
      context.join();

  ———

  最容易混的点

  scheduler 不执行协程。
  真正执行协程的是：

  context 里的工作线程

  scheduler 只负责：

  创建 context
  分发任务
  控制启动和停止

  context 也不直接懂 io_uring。
  真正处理 I/O 的是：

  engine

  层级是：

  scheduler
    管多个 context

  context
    管一个线程 + 一个 engine

  engine
    管任务队列 + io_uring

  ———

  一句话总结：

  scheduler 是老板，context 是工人，engine 是工人手里的机器。
  老板分配任务，工人开线程干活，机器真正执行协程和 I/O。
