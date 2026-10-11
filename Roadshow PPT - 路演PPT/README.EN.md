# Smart Campus Homework Analyzer · Roadshow Deck

**2026 Meisha Youth Hackathon Invitational · Team B3 · v0.2.0-rc1**

English | [中文](README.md)

---

## Which file to open

| File | Purpose | Notes |
|---|---|---|
| **`index.html`** | **Live presenting (recommended, Chinese)** | Double-click to open in a browser, press `F` for full screen. Animations, staggered reveals, overview grid, and speaker notes all live here. This is the design source — edit text here. |
| **`index.EN.html`** | **Live presenting (English)** | The English design source. Layout, colors, and motion are identical to the Chinese deck; only the text differs. |
| **`校园作业分析器-路演.pptx`** | **Submission / backup (Chinese)** | 15 slides, standard 16:9. Every slide is a 3840×2160 finished image **with full speaker notes** (view via PowerPoint's "View → Notes Page" or Presenter View). |
| **`校园作业分析器-路演.EN.pptx`** | **Submission / backup (English)** | The 15-slide English deck, identical in specification to the Chinese PPTX. |
| **`校园作业分析器-路演.pdf`** | **Submission / printing (Chinese)** | 15 pages, each 20×11.25 in (strictly 16:9), vector — no distortion. |
| **`校园作业分析器-路演.EN.pdf`** | **Submission / printing (English)** | The 15-page English PDF. |

> The PPTX uses a "full-page high-resolution image + native notes" approach: it looks exactly like the design source on any computer and any projector (no font-substitution drift). **To change text, edit `index.html` (or `index.EN.html` for English) and rebuild** (see below).
>
> Note: the app screenshots embedded in the deck were captured from the real, running Chinese-language application, so UI text inside those screenshots remains Chinese. Slide headlines, body copy, and speaker notes are in the language of the deck.

---

## Presenting shortcuts

| Key | Action |
|---|---|
| `→` `↓` `Space` `Enter` | Next slide |
| `←` `↑` | Previous slide |
| `Home` / `End` | First / last slide |
| `N` | Show / hide speaker notes |
| `O` | Slide overview (click any tile to jump) |
| `F` | Full screen |
| `Esc` | Exit overview / notes |
| Mouse click | Right 60% advances, left 40% goes back |

---

## The 15 slides

| # | Slide | Role |
|---|---|---|
| 01 | Cover | Project name · team · event identity |
| 02 | Problem | Grading is slow / statistics are manual / review is guesswork |
| 03 | Insight | "Grading produces data, but data never becomes a lesson" |
| 04 | Product | One-liner + real teacher-dashboard screenshot |
| 05 | Flow | The five-step loop |
| 06 | AI Teaching Analysis | The core v0.2.0 module |
| 07 | Design Principle | Deterministic statistics vs. AI transcription |
| 08 | Knowledge × Error Causes | Real-data screenshot |
| 09 | Review Script | Explain to students / board demo / check right after |
| 10 | Lesson Plan | 45-minute timeline + tiered support |
| 11 | Student Side | Mistake notebook + mastery check via varied practice |
| 12 | Real Case | Real statistics and AI conclusions from Class 808's physics paper |
| 13 | Engineering | Tech stack / verification / security |
| 14 | **Open Source & Stack** | MIT · repository QR code · technology choices |
| 15 | Closing | Vision + repository + team |

---

## What to change

The **team name** defaults to **Team B3** in the English deck and **B3组** in the Chinese deck (taken from the repository commit author). Replace across the whole file:

```powershell
cd "Roadshow PPT - 路演PPT"
(Get-Content index.html -Raw) -replace 'B3组', '你的团队名' | Set-Content index.html -Encoding utf8
(Get-Content index.EN.html -Raw) -replace 'Team B3', 'Your Team Name' | Set-Content index.EN.html -Encoding utf8
```

Other replaceable items: `v0.2.0-rc1` on the cover, and the repository URL and QR code on the closing slide.

---

## Rebuilding PPTX / PDF

```powershell
cd "Roadshow PPT - 路演PPT\tools"
npm install        # first time only (pre-installed in this checkout, can be skipped)

node build.mjs            # Chinese: render 15 4K images → rebuild .pptx and .pdf
node build.mjs --png      # Chinese: re-render images only

node build.mjs --lang=en  # English: reads ../index.EN.html → writes .EN.pptx and .EN.pdf
```

Requires Chrome or Edge installed locally (the script locates it automatically). Output is written back into the `Roadshow PPT - 路演PPT/` directory, overwriting the same-named files.

> The two pipelines never collide: the Chinese deck reads `index.html` and writes to `.build/`; the English deck reads `index.EN.html` and writes to `.build-en/`.

---

## Design notes

The typography, type scale, colors, corner radii, and motion parameters were implemented **point by point against Apple's shipped production CSS** (`apple.com/os`, `apple.com/iphone-duo`) — not imitated by feel:

- **Type scale**: Apple's `12/14/17/19/21/24/28/32/40/48/56/64/80`, each size with strictly `+4px` leading; shifted up one tier for the 1920×1080 canvas (hero 112px / headline 64px / body 21px). Only the weights Apple actually ships are used — **400 / 500 / 600**, no 700, no italics.
- **Letter spacing**: Apple applies negative tracking at large sizes but **forces `letter-spacing: 0` for Chinese** (Apple itself ships 34 `:lang(zh)` overrides). This deck follows the same rule: zero tracking for CJK, negative tracking retained for Latin and numerals.
- **Colors**: `#000` dark base / `rgb(29,29,31)` body black / `rgb(245,245,247)` light-grey base / `#0071e3` button blue / `#0066cc` link blue — all original Apple tokens, with a single accent family across the whole deck.
- **Cards**: flat fill, 28px radius, **no border, no shadow**. The only shadow in Apple's entire CSS bundle is the focus ring, so none is used here.
- **Buttons / tags**: `border-radius: 980px` full capsule.
- **Motion**: reproduces Apple's `StaggeredFadeIn` — 30px offset / 0.7s, opacity 0.9s, 0.15s per-item stagger, `easeInOutQuad` easing.
- **Fonts**: **Inter** (Latin and numerals) + **Noto Sans SC** (Chinese) bundled locally, 4.4 MB total, so **nothing shifts offline**. A second Inter declaration with a narrowed `unicode-range` lets Chinese curly quotes (「“”」) and the ellipsis render with the CJK face, preventing Latin punctuation from breaking the Chinese typographic rhythm.
- The ambient glow on dark slides is deliberately held to 0.16–0.34 opacity — atmosphere only, never competing with the subject. Apple's dark sections are carried by product imagery, not gradient blocks.

---

## Asset sources

Every app screenshot comes from the **real running interface and real data** of this repository at `v0.2.0-rc1` (Middle School Physics · Chapter 15 Stage Assessment · Class 808) — not design mockups:

- Teacher dashboard, AI teaching analysis, knowledge mastery and error cause attribution, high-frequency mistake review sheet, classroom lesson plan, and the student mistake notebook
- The AI teaching conclusion quoted on slide 12 is an exact excerpt from the system (87.5% unanswered, 18% scoring rate, 5 questions at 100% error rate)

**Inter** and **Noto Sans SC** are both licensed under the **SIL Open Font License 1.1** and may be freely redistributed.
