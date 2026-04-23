# Roadmaps

`notes/roadmaps/` 现在只承担目录入口，不再平铺阶段文档。

当前分组：

- [Roadmap · 走向 AI-Native 小型游戏引擎](main-roadmap/README.md)
- [技术预研 (research/)](research/README.md)

站点导航会由 `scripts/_gen_notes_site.py` 自动按子目录展开：

- `Roadmap` 下的二级菜单显示各个子目录
- 每个子目录内部按文件名顺序显示页面
- 子目录内的 `README.md` 作为该组入口页

因此后续新增 roadmap 文档时，应优先放入某个明确的子目录，而不是继续平铺到 `notes/roadmaps/` 根目录。
