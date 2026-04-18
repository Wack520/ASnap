# AI Screenshot Modernization Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Rebuild the current PyQt6 screenshot assistant into a modular app with multi-provider AI support, PixPin-style capture UX, recrop-before-send flow, themed floating chat, and smart on-screen placement.

**Architecture:** Replace the single-file implementation with a small package that separates provider integrations, screenshot/session logic, placement logic, and PyQt widgets. Preserve the tray/hotkey workflow while adding provider profiles, chat sessions, recrop flow, and multi-screen aware capture.

**Tech Stack:** Python 3.14, PyQt6, requests, markdown, keyboard, unittest

---

### Task 1: Add regression tests for core non-UI logic

**Files:**
- Create: `tests/test_config.py`
- Create: `tests/test_placement.py`
- Create: `tests/test_providers.py`
- Create: `tests/test_session.py`

- [ ] Step 1: Write failing tests for config/profile roundtrip, placement, provider registry, and chat session behavior.
- [ ] Step 2: Run `python -m unittest discover -s tests -v` and confirm failures due to missing modules.
- [ ] Step 3: Implement the minimal modules needed to satisfy the tests.
- [ ] Step 4: Re-run `python -m unittest discover -s tests -v` and confirm green.

### Task 2: Split the monolith into package modules

**Files:**
- Create: `ai_screenshot/__init__.py`
- Create: `ai_screenshot/config.py`
- Create: `ai_screenshot/providers.py`
- Create: `ai_screenshot/session.py`
- Create: `ai_screenshot/placement.py`
- Create: `ai_screenshot/screen_service.py`
- Modify: `screenshot_ai.py`

- [ ] Step 1: Move config, provider abstraction, placement, and session logic into dedicated modules.
- [ ] Step 2: Replace `screenshot_ai.py` with a tiny entrypoint.
- [ ] Step 3: Run unit tests and import smoke checks.

### Task 3: Implement new capture flow and themed floating chat

**Files:**
- Create: `ai_screenshot/ui_capture.py`
- Create: `ai_screenshot/ui_chat.py`
- Create: `ai_screenshot/app.py`

- [ ] Step 1: Implement virtual-desktop screenshot overlay with PixPin-like visuals.
- [ ] Step 2: Implement crop editor dialog for recrop-before-send.
- [ ] Step 3: Implement floating chat panel with image preview, follow-up input, re-crop, theme, and topmost behavior.
- [ ] Step 4: Wire the controller, tray menu, and hotkey flow.
- [ ] Step 5: Run app import smoke checks and unit tests.

### Task 4: End-to-end verification

**Files:**
- Verify only

- [ ] Step 1: Run `python -m unittest discover -s tests -v`.
- [ ] Step 2: Run `@'\nfrom ai_screenshot.config import AppConfig\nfrom ai_screenshot.providers import ProviderRegistry\nfrom ai_screenshot.app import create_application\nprint('imports-ok', len(ProviderRegistry().providers))\n'@ | python -`.
- [ ] Step 3: Fix any failures and rerun.
