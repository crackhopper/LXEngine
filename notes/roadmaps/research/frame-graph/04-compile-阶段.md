# 04 · compile 阶段

> 阅读前提：[03](03-实现层数据结构.md) 已经讲清数据结构。本文展开 compile 阶段做的三件事：构边、拓扑排序、aliasing。

compile 是 frame graph 里技术含量最高的部分，也是大部分实现风险所在。

## 4.1 构边（compute edges）

### 目标

每个节点的 `edges` 字段（"必须在我之前完成的节点列表"）原本是空的。这一步把它填好。

### 算法

```cpp
for ( u32 r = 0; r < node->inputs.size; ++r ) {
  // 1. 拿到当前 input resource
  FrameGraphResource* resource = frame_graph->get_resource( node->inputs[r].index );

  // 2. 按名字查"哪个 output 同名"
  u32 output_index = frame_graph->find_resource( hash_calculate(resource->name) );
  FrameGraphResource* output_resource = frame_graph->get_resource( output_index );

  // 3. 把 output 的元数据回填到 input 上
  resource->producer = output_resource->producer;
  resource->resource_info = output_resource->resource_info;
  resource->output_handle = output_resource->output_handle;

  // 4. 在 producer node 的 edges 里加上当前 node
  FrameGraphNode* parent_node = frame_graph->get_node( resource->producer.index );
  parent_node->edges.push( frame_graph->nodes[node_index] );
}
```

### 关键认知

- **graph 内部用一个 map 把所有 output 资源按名字索引**。input 不进 map（同名 input 会有多个，同名 output 只有一个）
- **input 端会拿到 output 的 `resource_info` 副本**。从此 input 端可独立回答 format / size 问题，不需要每次都查 map
- **`output_handle` 是 reference**（不是 copy），让 input 永远能找回 output 端的规范实例
- **edges 方向**：`parent_node.edges` 含"依赖 parent 的 node 列表"，即 *从 producer 指向 consumer*

### 一个潜在优化（书里提到没做）

如果某个 pass 的输出没人消费（也不是最终 present），那这个 pass 是 dead code，可以剔除。叫 **dead pass elimination**。

## 4.2 拓扑排序（DFS + 三态 visited）

### 目标

把所有 node 排成一个序列，使得"任何 node 的所有依赖都排在它前面"。如果 graph 里有循环依赖（cycle），拓扑排序会失败 —— 这本身就是 cycle 检测的一种方式。

### 两种主流算法

| 算法 | 思路 | 实现风格 |
|------|------|---------|
| Kahn | 每次找入度为 0 的 node 移出 | 队列 + in-degree 计数 |
| DFS | 深度优先遍历，访问完所有 children 才把当前 node 加入结果 | 递归或显式栈 |

**书里选了 DFS + 显式栈**（不用递归）。

### 数据结构

```cpp
Array<FrameGraphNodeHandle> sorted_nodes;  // 结果（反序）
Array<u8> visited;                          // 0 = 未访问, 1 = 访问中, 2 = 已加入结果
Array<FrameGraphNodeHandle> stack;          // 待处理栈
```

**`visited` 用三态而不是布尔**，关键设计：

- **0 (未访问)**：从没碰过
- **1 (visited / 访问中)**：已把它的 children 入栈，但还没把自己加入 sorted_nodes
- **2 (added / 已加入)**：自己和所有 children 都已在 sorted_nodes 里

为什么三态？因为 DFS 拓扑排序的本质是 **后序遍历**：要等所有 children 处理完，才能处理自己。三态让我们能区分"我刚到这个 node"（要先处理 children）和"children 处理完了，回到我"（现在可以加入结果）。

### 主循环

```cpp
for ( u32 n = 0; n < nodes.size; ++n ) {
  stack.push( nodes[n] );

  while ( stack.size > 0 ) {
    FrameGraphNodeHandle node_handle = stack.back();

    // 情况 A: 已加入结果，直接弹出
    if (visited[node_handle.index] == 2) {
      stack.pop();
      continue;
    }

    // 情况 B: children 已处理（visited == 1），现在轮到自己
    if (visited[node_handle.index] == 1) {
      visited[node_handle.index] = 2;
      sorted_nodes.push(node_handle);
      stack.pop();
      continue;
    }

    // 情况 C: 第一次访问，入栈所有 children
    visited[node_handle.index] = 1;
    if (node->edges.size == 0) {
      continue;  // 叶子节点，下次循环走情况 B
    }
    for ( u32 r = 0; r < node->edges.size; ++r ) {
      FrameGraphNodeHandle child_handle = node->edges[r];
      if ( !visited[child_handle.index] ) {
        stack.push(child_handle);
      }
    }
  }
}
```

### 走一个具体例子

graph：

```
A → B → D
A → C → D
```

（A 的 edges = [B, C]，B 的 edges = [D]，C 的 edges = [D]，D 的 edges = []）

外层从 A 开始：

| iter | 栈 | 触发情况 | 动作 | sorted_nodes |
|------|-----|---------|------|--------------|
| 1 | [A] | C | visited[A]=1，入栈 B、C | [] |
| 2 | [A,B,C] | C | visited[C]=1，入栈 D | [] |
| 3 | [A,B,C,D] | C | visited[D]=1，叶子节点 | [] |
| 4 | [A,B,C,D] | B | visited[D]=2，sorted += D | [D] |
| 5 | [A,B,C] | B | visited[C]=2，sorted += C | [D,C] |
| 6 | [A,B] | C | visited[B]=1，尝试入栈 D 但被 `!visited` 过滤 | [D,C] |
| 7 | [A,B] | B | visited[B]=2，sorted += B | [D,C,B] |
| 8 | [A] | B | visited[A]=2，sorted += A | [D,C,B,A] |

最终 `sorted_nodes = [D, C, B, A]`。反过来填回 `nodes` → `[A, B, C, D]`，正确执行顺序。

### 反序输出

```cpp
for (i32 i = sorted_nodes.size - 1; i >= 0; --i) {
  nodes.push( sorted_nodes[i] );
}
```

为什么 sorted_nodes 是反的？DFS 后序遍历自然得到"叶子在前、根在后"。我们要的是"先执行 producer，再执行 consumer"，所以反过来。

### 三态 visited 解决的具体问题

> 书里："Traditional graph processing implementations don't have this step. We had to add it as a node might produce multiple outputs."

意思是：在标准 DFS 拓扑排序里，一个 node 只会被访问一次。但这里因为 *栈里可能重复出现同一个 node*（多个 parent 都依赖同一个 child），需要 visited=2 跳过已加入结果的。

例：上面 D 在 iter 2 被 C 入栈，在 iter 6 被 B 尝试入栈但被 `!visited` 过滤。这是为多 outputs / 多 parent 场景设计的健壮性。

### cycle 检测（书里漏的）

代码里没显式做 cycle 检测，但有暗含条件：cycle 会导致 visited[X]=1 的 node 再次入栈，进入无限循环。

完整实现应在情况 C 入栈 children 时，如果 `visited[child] == 1`（访问中但未完成），立即报错。书里没做，是个已知 gap。

## 4.3 Resource Aliasing

### 算法整体思路

**两遍扫**：

1. 第一遍：算每个资源的 `ref_count`（被多少个 input 引用）
2. 第二遍：按拓扑序遍历 nodes，处理 outputs（分配 / 复用）和 inputs（递减 ref_count，归零放回 free_list）

经典的 *引用计数 + 自由列表* 模式。

### 数据结构

```cpp
Array<FrameGraphNodeHandle> allocations;    // 资源在哪个 node 被分配
Array<FrameGraphNodeHandle> deallocations;  // 资源在哪个 node 被回收
Array<TextureHandle> free_list;             // 当前可复用的 texture handle 列表
```

`allocations` 和 `deallocations` 主要用于调试和验证。`free_list` 是真正驱动 aliasing 的数据结构。

### 第一遍：算 ref_count

```cpp
for ( u32 i = 0; i < nodes.size; ++i ) {
  FrameGraphNode* node = ...;
  for ( u32 j = 0; j < node->inputs.size; ++j ) {
    FrameGraphResource* input_resource = ...;
    FrameGraphResource* resource = builder->get(input_resource->output_handle.index);
    resource->ref_count++;
  }
}
```

走每个 node 的每个 input。input 通过 `output_handle` 找到 *规范资源*（output 端那个），把它的 `ref_count` 加 1。

这就是为什么前面强调 input/output 是 *独立 resource 实例* + `output_handle` 回指 —— ref_count 必须挂在唯一的规范实例上，否则同一资源在多个 input 上各算各的。

### 第二遍：按拓扑序处理 outputs（分配或复用）

```cpp
for ( u32 i = 0; i < nodes.size; ++i ) {
  FrameGraphNode* node = ...;

  for ( u32 j = 0; j < node->outputs.size; ++j ) {
    FrameGraphResource* resource = ...;

    if ( !resource->resource_info.external &&
         allocations[resource_index].index == k_invalid_index ) {
      allocations[resource_index] = nodes[i];

      if ( resource->type == FrameGraphResourceType_Attachment ) {
        if ( free_list.size > 0 ) {
          // 复用
          TextureHandle alias_texture = free_list.back();
          free_list.pop();
          // 用 alias_texture 的 memory 创建新 texture
          TextureHandle handle = builder->device->create_texture(texture_creation);
          info.texture.texture = handle;
        } else {
          // 新分配
          TextureHandle handle = builder->device->create_texture(texture_creation);
          info.texture.texture = handle;
        }
      }
    }
  }
```

要点：

- **`external` 资源跳过**（swapchain image 不在 graph 内分配）
- **重复 output 跳过**（同一资源可能在多个 node 的 outputs 出现，比如 reference 类型）
- **free_list 非空 → greedy 复用**：拿到的不一定是最优匹配。书末尾承认这会导致内存碎片，留给读者优化

### 第二遍：处理 inputs（递减 + 回收）

```cpp
  for ( u32 j = 0; j < node->inputs.size; ++j ) {
    FrameGraphResource* input_resource = ...;
    FrameGraphResource* resource = ...;  // 通过 output_handle 拿规范实例
    resource->ref_count--;

    if ( !resource->resource_info.external && resource->ref_count == 0 ) {
      deallocations[resource_index] = nodes[i];
      if ( resource->type == FrameGraphResourceType_Attachment ||
           resource->type == FrameGraphResourceType_Texture ) {
        free_list.push( resource->resource_info.texture.texture );
      }
    }
  }
}
```

每处理完一个 input，给规范资源 ref_count 减 1。归零 → 资源之后没人用 → 把它的 texture handle 放回 `free_list`，供后续 nodes 复用。

### 一个完整 walk-through

拓扑序：`gbuffer_pass → light_pass → tonemap_pass`

资源：

- `gbuffer_colour`：gbuffer 写、light 读 → ref_count = 1
- `lighting`：light 写、tonemap 读 → ref_count = 1
- `final`：tonemap 写

| iter | node | outputs | inputs | free_list |
|------|------|---------|--------|-----------|
| 1 | gbuffer_pass | 处理 gbuffer_colour → free_list 空 → 新分配 texA | 无 | [] |
| 2 | light_pass | 处理 lighting → 新分配 texB | gbuffer_colour ref 1→0 → += texA | [texA] |
| 3 | tonemap_pass | 处理 final → 复用 texA 的 memory，建 texC | lighting ref 1→0 → += texB | [texB] |

总分配 texA + texB + texC，但 texC 复用 texA → **实际只占 2 个 attachment 的显存**（不做 aliasing 要 3 个）。

### aliasing 的安全性条件

**关键不变量**：当资源被回收（放进 free_list），必须 *物理上不再被任何 GPU 操作引用*。

ref_count 归零保证了 *逻辑上* 没人引用，但 *物理上* GPU 是否还在用？这是 barrier 系统的责任 —— 在新资源开始写之前，必须等旧资源的所有读完成。

**另一个不变量**：复用时格式必须兼容。书里的简化代码省略了这一检查。

### 书承认的局限

> "We use a greedy approach and simply pick the first free resource that can accommodate a new resource. This can lead to fragmentation and suboptimal use of memory."

更优的算法：

- *Best fit*：选最匹配的而不是第一个
- *Memory pool + offset allocation*：把所有 attachment 当作一片大 memory，按 lifetime 区间做 1D bin packing
- *Constraint solver*：把 aliasing 当作图染色问题求解

工业引擎（Frostbite、Anvil）做得更精细。教科书版是最简实现。

## 4.4 接下来读什么

- [05 LX 当前状态对照](05-LX当前状态对照.md) — 字段级 / 能力级 gap 分析
