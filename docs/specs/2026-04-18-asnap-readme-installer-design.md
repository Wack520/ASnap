# ASnap README / Installer Polish Design

**Date:** 2026-04-18

## Goal

将 ASnap 的公开门面整理成更像正式产品的状态：安装器欢迎页更有品牌感，README 首页更像真实开源项目首页，并补上可直接展示的 Demo 图片与 GIF 资源。

## Scope

### 1. 安装器欢迎页
- 保留 Inno Setup 现有安装逻辑。
- 升级品牌视觉资源：侧边欢迎图、小图、中文文案。
- 文案风格更像正式桌面软件，而不是工具脚本。
- 不引入复杂自定义安装逻辑，不修改权限模型。

### 2. README 首页
- 顶部改成产品首页结构：标题、简介、下载入口、演示区、核心特性。
- 删除“当前状态”“更适合这样定位”这类总结汇报口吻内容。
- 文案偏真实产品介绍，不使用自我解释型或 AI 口吻句子。
- 保留必要的构建说明、隐私说明与 License。

### 3. Demo 资源
- 在仓库中新增可被 README 直接引用的媒体资源目录。
- 至少包含：Hero 图、演示截图、演示 GIF。
- 如果暂时无法生成高质量真实录屏，则优先提供风格统一、可直接展示的静态图与轻量 GIF。

## File Plan

- Modify: `README.md`
- Modify: `packaging/windows/asnap.iss`
- Create/Modify: `packaging/windows/assets/*`
- Create: `packaging/windows/languages/*`（保留中文语言资源）
- Create: `docs/media/*`
- Create: `docs/specs/2026-04-18-asnap-readme-installer-design.md`
- Create: `docs/plans/2026-04-18-asnap-readme-installer-plan.md`

## Visual Direction

### Installer
- 深色、极简、干净。
- 以 ASnap 图标为中心，辅以轻微光晕、层次和产品副标题。
- 避免廉价渐变和“模板味”太重的元素。

### README
- 首屏优先回答三个问题：是什么、能做什么、去哪里下载。
- 主视觉尽量像正式产品页，不堆砌说明文。
- 演示区优先展示：框选截图、悬浮问答、设置页。

## Acceptance Criteria

- GitHub 仓库首页不再出现明显“总结汇报式”段落。
- README 首屏可直接看到下载入口和演示资源。
- 安装器欢迎页视觉明显比当前版本更完整、更像产品。
- 本地打包脚本仍能成功生成安装器。
