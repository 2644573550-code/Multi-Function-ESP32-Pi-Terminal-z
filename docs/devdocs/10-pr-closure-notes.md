# PR 合并与关闭说明（2026-10-29）

> 本文回答一个问题：**哪些 PR 可以安全关闭**，依据是实际的分支包含关系，
> 不是"看起来像已经合了"。
>
> 唯一在收的 PR：**`feat/p0-software-complete`**
> https://github.com/Axe665/Multi-Function-ESP32-Pi-Terminal/pull/new/feat/p0-software-complete

---

## 结论速查

| PR 分支 | 能否关闭 | 依据 |
|---|---|---|
| `docs/pbr-secure-v1-rev3-tag-notation` | ✅ 可以 | 提交对象是 `feat/p0-software-complete` 的祖先 |
| `feat/pbr-t03-truststore` | ✅ 可以 | 同上 |
| `docs/pbr-t07-acceptance-tests` | ✅ 可以 | 同上 |
| `feat/z-p0-security` | ✅ 可以 | 同上 |
| `feat/pbr-p0-delivery` | ✅ 可以 | 同上 |
| `docs/pbr-d-signoff-rev3` | ✅ 可以 | 三个文件内容与 `feat/p0-software-complete` **逐字节相同** |
| `feat/flite-voice-engine` | ✅ 可以 | 与已合并的 `feat/flite-voicebank` 代码完全相同，仅 3 个文档措辞更旧 |

**七个都可以关。** 关闭不会丢任何代码或文档。

另外这三个也已被完全包含，如果它们有对应 PR 也可以一并关闭：

| 分支 | 状态 |
|---|---|
| `docs/d-pbr-secure-v1-decisions` | ✅ 已包含 |
| `feat/pbr-t03b-host-identity` | ✅ 已包含 |
| `feat/pbr-t05-slot-routing` | ✅ 已包含 |
| `feat/pbr-t08-uid-index-interface` | ✅ 已包含 |
| `docs/mr-z-todo` | ✅ 已包含 |
| `feat/p0-t05-control-schema` | ✅ 已包含 |

---

## 怎么验证的（可复现）

在仓库里跑，`feat/p0-software-complete` 是当前分支：

```bash
for b in d/docs/pbr-secure-v1-rev3-tag-notation \
         d/feat/pbr-t03-truststore \
         d/docs/pbr-t07-acceptance-tests \
         d/feat/pbr-p0-delivery \
         z/feat/z-p0-security ; do
  git merge-base --is-ancestor "$b" HEAD \
    && echo "OK   $b" || echo "MISS $b"
done
```

五个全部 `OK`。`--is-ancestor` 的含义是「该分支的每一个提交都已经在
`feat/p0-software-complete` 的历史里」，不是 cherry-pick 出来的等价物。

对 `docs/pbr-d-signoff-rev3`，因为它和主线的提交对象不同（GitHub 上
squash/merge 常见），改用**内容比对**：

```bash
for f in docs/pbr-secure-v1/D-SIGNOFF.md \
         docs/pbr-secure-v1/build_mbedtls_host.sh \
         docs/pbr-secure-v1/pbr-secure-v1.md ; do
  git diff d/docs/pbr-d-signoff-rev3 HEAD -- "$f" | wc -l   # 必须是 0
done
```

三个文件全部 `0`，即**无差异**。它相对 base 只新增/修改这三个文件。

对 `feat/flite-voice-engine`：

```bash
# 与已合入 origin/main 的 flite-voicebank 对比
git diff --stat d/feat/flite-voice-engine 331b06a -- pi_terminal/src pi_terminal/qml
# 输出为空 → 引擎代码完全一致
git diff --name-status 331b06a d/feat/flite-voice-engine
# 只有 3 个文档文件，且是更旧的版本
```

三个差异文件是：

```
M  pi_terminal/assets/voices/NOTICE.md      # 旧：units/*.wav 架构
M  pi_terminal/tts/tools/README.md          # 旧：缺 build_voicebank.py 行
M  third_party/flite/PROJECT-NOTES.md
```

`feat/flite-voicebank`（`8748c4f`，已在 main）是同一功能的更新版本：
声库从"每音节一个 wav"改成"单文件 `voice.zhvox`"，并补了打包脚本。
**保留新的那份是对的，旧的可以关。**

---

## 关闭后保留的信息

关闭 PR 不会删分支，也不会丢提交。以下仍然可用：

- `docs/pbr-secure-v1/D-SIGNOFF.md` —— D 对修订 3 的技术签认
- `docs/pbr-secure-v1/T-07-ACCEPTANCE-TESTS.md` —— T-07 验收规格
- `docs/pbr-secure-v1/vectors.json` + `gen_vectors.py` —— 官方向量
- `docs/pbr-secure-v1/build_mbedtls_host.sh` —— mbedtls 3.6.5 主机构建脚本
- `docs/devdocs/09-debug-hooks.md` —— 调试钩子清单
- `docs/devdocs/08-handoff-to-L.md` —— 给 L 的硬件交接

所有这些都在 `feat/p0-software-complete` 里。

---

## 需要在关 PR 时说明的事

关闭这七个 PR **不代表**它们对应的功能已验收通过。它们只是被并入了
更大的 `feat/p0-software-complete`。

`docs/devdocs/04-dev-tasks.md` §7 的 19 条 P0 完成条件**仍全部未勾选**，
原因是六层证据只完成了前两层（源码单测、主机模拟），目标 Pi 构建 /
Pi 实机 / ESP 板实机 / 环境实测都还是零。

建议在每个关闭的 PR 上贴一句：

> 内容已并入 #<feat/p0-software-complete 的 PR 编号>，功能与文档在那边继续跟踪。
> P0 验收状态以 `docs/devdocs/04-dev-tasks.md` §7 为准，本 PR 关闭不代表验收通过。

---

## 如果你要关的不是这七个

可能看错了分支名。`SomaRia-Meta:` 和 `2644573550-code:` 是 GitHub 上的
fork 所有者前缀，仓库里的 remote 名分别是 `d` 和 `z`。对应关系：

| GitHub 上的 | 本仓库 remote | 实际分支 |
|---|---|---|
| `SomaRia-Meta:docs/pbr-secure-v1-rev3-tag-notation` | `d` | `d/docs/pbr-secure-v1-rev3-tag-notation` |
| `SomaRia-Meta:feat/pbr-t03-truststore` | `d` | `d/feat/pbr-t03-truststore` |
| `SomaRia-Meta:docs/pbr-t07-acceptance-tests` | `d` | `d/docs/pbr-t07-acceptance-tests` |
| `2644573550-code:feat/z-p0-security` | `z` | `z/feat/z-p0-security` |
| `SomaRia-Meta:feat/pbr-p0-delivery` | `d` | `d/feat/pbr-p0-delivery` |
