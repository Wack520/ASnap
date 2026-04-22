# ASnap README / Installer Polish Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Upgrade the installer welcome experience and rewrite the public README into a cleaner, product-style project homepage with demo media.

**Architecture:** Keep the existing Qt/CMake app and Inno Setup packaging flow unchanged, but replace branding assets, installer copy, and README structure. Media assets will live under `docs/media/` so the repository homepage can reference them directly.

**Tech Stack:** Markdown, Inno Setup, PowerShell packaging script, Python (Pillow) for media asset generation.

---

### Task 1: Add polished media and installer assets

**Files:**
- Modify: `packaging/windows/asnap.iss`
- Modify/Create: `packaging/windows/assets/*`
- Create: `docs/media/hero.png`
- Create: `docs/media/capture.png`
- Create: `docs/media/chat.png`
- Create: `docs/media/settings.png`
- Create: `docs/media/demo.gif`

- [ ] Generate upgraded installer visual assets and README media assets with a unified dark product look.
- [ ] Update `asnap.iss` to use the upgraded assets and polished Chinese copy.
- [ ] Keep packaging compatible with the current `scripts/package-windows.ps1` flow.

### Task 2: Rewrite README homepage

**Files:**
- Modify: `README.md`

- [ ] Replace the current intro with a product-style hero section.
- [ ] Add direct download / release links near the top.
- [ ] Add demo media section referencing files in `docs/media/`.
- [ ] Rewrite feature, install, build, and privacy sections in concise human product language.
- [ ] Remove the AI-sounding “current status / positioning / open preview” sections.

### Task 3: Verify packaging and homepage output

**Files:**
- Verify: `README.md`
- Verify: `packaging/windows/asnap.iss`
- Verify: `docs/media/*`

- [ ] Run the packaging script with installer generation enabled.
- [ ] Confirm the installer EXE is generated successfully.
- [ ] Confirm README media paths render locally in markdown and are committed.
- [ ] Commit and push the polished changes.
