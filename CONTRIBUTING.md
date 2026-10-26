# 开发、提交与验证规范

- `v0` 之后的变更必须通过 pull request 合入，禁止直接 push `main`。
- 仓库必须能从已提交源码构建；不得依赖私有仓库、本机绝对路径或未提交文件。
- 导入第三方源码时保留 LICENSE、上游地址和固定版本，不提交嵌套 `.git`。
- 禁止提交 Wi-Fi 密码、token、私钥、`.env` 或本机生成的 SDK 配置。
- 文档和示例使用相对路径或 `<placeholder>`，不得写入本机绝对路径、用户名或设备序列号。
- 云端动作必须有本地白名单、参数限幅、单动作队列和停止路径；不能只靠提示词约束。
- 实际执行过的验证统一记录在 `docs/ARCHITECTURE.md`「实现状态」，计划中的能力必须标为待验证。

## Commit message

提交信息使用 Conventional Commits 格式：

```text
<type>(<scope>): <short summary>
```

- `type`：`feat`、`fix`、`docs`、`test`、`chore`、`refactor`、`build`、`ci`。
- `scope`：`firmware`、`linx`、`otto`、`product`、`hardware`、`architecture`、`validation`、`repo`。
- summary 使用英文祈使句，首字母小写，不加句号，尽量不超过 72 个字符。
- 一个 commit 只做一件事，不使用 `update`、`fix bug`、`changes` 这类空泛标题。
- 需要解释背景时，标题后空一行再写正文；验证结果可以放在正文。

示例：

```text
chore(firmware): import xiaozhi-esp32 baseline
docs(product): document target users and product scope
feat(linx): enable realtime voice and barge-in
feat(otto): add action allowlist and safety stop
test(validation): record hardware checks and demo
```

PR 标题使用主要 commit 的格式。合并使用普通 merge commit，不使用 squash，保证 PR 内提交历史仍然存在。

## 工作树与分支

一个任务只对应一个分支、一个工作树和一个 PR。分支名中的 `type` 和业务短名应与 commit、PR 保持一致。

同一个 PR 尚未完成时复用该工作树；PR 合并或任务取消后，该工作树立即清理。新任务从最新 `main` 新建工作树，不复用已合并的旧目录。

工作树放在仓库目录外，目录名按任务功能命名：

```text
<worktree-root>/
├── import-xiaozhi-baseline/
├── project-baseline/
├── linx-realtime/
├── otto-safety/
└── validation-demo/
```

分支与目录使用相同短名：

| 用途 | 分支 | 工作树目录 |
| --- | --- | --- |
| 导入上游基线 | `chore/import-xiaozhi-baseline` | `import-xiaozhi-baseline/` |
| 项目文档 | `docs/project-baseline` | `project-baseline/` |
| 灵矽音频 | `feat/linx-realtime` | `linx-realtime/` |
| 动作安全 | `feat/otto-safety` | `otto-safety/` |
| 验收与 Demo | `test/validation-demo` | `validation-demo/` |

创建前先更新 `main`：

```bash
export WORKTREE_ROOT=/path/to/worktrees
git fetch origin --prune
git switch main
git pull --ff-only
git worktree add -b <branch> "$WORKTREE_ROOT/<slug>" origin/main
```

PR 合并后先保存构建日志、固件和测试证据，再清理：

```bash
gh pr merge <PR号> --merge --delete-branch
git fetch origin --prune
git switch main
git pull --ff-only
git worktree remove "$WORKTREE_ROOT/<slug>"
git branch -d <branch>
git worktree list
```

使用普通 merge commit，不用 squash，便于保留 PR 内的开发过程。不要用 `--force` 删除仍有未提交修改的工作树。最多同时保留一个固件工作树和一个文档工作树；实体机器人烧录与验收必须串行。
