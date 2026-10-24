# Smart Campus Homework Analyzer · 校园作业分析

**v0.2.0-rc1** | MIT License

[中文](README.md) | English

Smart Campus Homework Analyzer: upload homework, AI grades it; teacher review turns it into class insight — mastery rates, error cause statistics, and lesson plans. All numbers come from reviewed data; AI only writes them up for class.

---

This is a full-stack version that can run on school or personal servers. Accounts, schools, students, knowledge bases, homework, OCR, teacher review, mistake notebooks, exam papers, and AI teaching analysis all use persistent data.

## What's New in v0.2.0-rc1: AI Teaching Analysis

A new module for classroom review sessions that directly consumes teacher-reviewed mistakes and turns grading results into teachable content.

- **Class Learning Profile**: Number of reviewed submissions, class scoring rate, mistake counts and error cause breakdowns, with separate alerts for unrecognizable questions.
- **Knowledge Point Mastery Rates**: Aggregated by question number to school knowledge points, sorted from weakest first to highlight gaps to address in class.
- **Error Cause Attribution**: Built-in 12 error categories (concept confusion, misreading the question, careless calculation, missing steps, weak formula recall, omitted conditions, etc.). Keyword rules provide deterministic classification first; teachers can click "AI Re-attribution" for model-assisted review combining question stems and knowledge points. Error causes are strictly divided by question number, and percentages always sum to 100%.
- **High-Frequency Mistake Review Sheets**: Sorted by number of students who made each mistake. Expand to see affected students, reference answers, step-by-step solutions, similar practice problems, and AI-generated prompts for "explain to students / board demonstration / quick check after explanation."
- **Lesson Plan Generator**: Topic, duration, objectives, timeline (what to teach, what the teacher does, what students do), and ready-to-use board design.
- **Differentiated Intervention**: Each error cause comes with a 5-minute actionable classroom activity and a named list of students for targeted support.
- **Follow-Up Homework**: Targets only the knowledge points where mistakes occurred. One-click generation of new practice assignments from selected high-frequency mistakes.
- **Student Follow-Up List**: Tag students as "Observe / Follow Up / Priority." One-click generation of individualized diagnostics (weak knowledge points, error cause analysis, remediation actions, targeted exercises, and a note for parents).
- **Export**: One-click export of Markdown review sheets, or print directly as A4 handouts for class.

**Statistical Note**: Only mistakes confirmed by teachers in "Recognition & Review" enter statistics. Questions marked "Unrecognizable" are listed separately and excluded from student error cause percentages to prevent misjudgment due to poor scan quality.

-  Knowledge point mastery rate: Aggregate the school-based knowledge points by question number, sort them from the weakest, and directly point out the loopholes that need to be filled first in this lesson.
-   - Error attribution: Built-in 12 types of error causes (conceptual confusion, question review deviation, careless calculation, missing steps, poor memory of formulas, omission of conditions, etc.). First use keyword rules to categorically classify. The teacher can then click "AI Re-Attribution" to let the model combine the question stem and knowledge points to review; the error causes are strictly divided according to question numbers, and the sum of the proportions is always 100%.
    -  - Review sheets for frequently mistaken questions: Sort by the number of people who made mistakes, expand to see the students who made the mistakes, reference answers, step-by-step analysis, similar exercises, and the AI-generated "tell to students/demonstrate on the blackboard/check immediately after speaking".
       -  - Classroom lecture and evaluation lesson plan: theme, lesson time, goals, timeline (what to talk about, what the teacher does, what students do) and a blackboard design that can be copied and written on.
          -  - Hierarchical coaching and error cause intervention: Each error cause is assigned a classroom action that can be performed within 5 minutes, and a layered coaching list is given that can be directly named.
             - - After-class homework: Only for the knowledge points that were wrong this time, you can generate new re-practice homework for the selected frequently wrong questions with one click. - Student follow-up list: Mark "Observation/Follow-up/Key Points" to generate an individualized diagnosis of the student (weak knowledge points, analysis of error causes, reinforcement actions, targeted exercises, and an explanation for parents) with one click.
               - - Export: Export Markdown lecture notes with one click, or print them directly into A4 lecture notes and bring them to class.

## One-Click Deployment (Windows)

The repository includes a C++ console deployer (`tools/deployer/`) that can automatically detect and install dependencies, prepare source code, build, and start the service on any Windows computer, with optional LAN or public network access:

```powershell
# Build the deployer (requires MinGW-w64 or MSVC + CMake, output ~3 MB single file)
powershell -ExecutionPolicy Bypass -File tools/deployer/build.ps1

# Usage
dist-deployer\campus-deploy.exe                 # Interactive menu
dist-deployer\campus-deploy.exe --web           # Browser-based visual deployment console
dist-deployer\campus-deploy.exe --mode lan      # LAN: accessible to all devices on the same network
dist-deployer\campus-deploy.exe --mode tunnel   # Cloudflare temporary public URL (HTTPS)
dist-deployer\campus-deploy.exe --mode domain --domain hw.example.com --api-token cf_xxx
```

It automatically detects Node.js / cloudflared / curl / git and installs whatever is missing (preferring winget, falling back to official downloads). Source code is fetched from GitHub if not present locally. See `tools/deployer/README.md` for details.

## Local or Server Run

Requires Node.js 24+.

```powershell
npm install
$env:APP_SECRET="replace-with-at-least-32-char-random-string"
npm run build
npm start
```

For development, run `npm run dev:server` and `npm run dev` separately, then visit `http://127.0.0.1:5173`. Production server listens on `127.0.0.1:8787` by default; use `HOST=0.0.0.0` to allow LAN access. Data is stored in `data/smart-campus.sqlite`, with original files in `data/uploads/`.

## Security update (2026-10-17)

School administrators must create teacher accounts; public registration is student-only. Students cannot change their own stage/class. Submission files and answers require the submitting student, owning teacher, or school administrator. Student assignment responses omit reference answers. Re-recognition requires fresh teacher approval.

Model endpoints are restricted to the selected vendor's HTTPS origin. Deployment administrators may approve exact custom origins in comma-separated `PROVIDER_ALLOWED_ORIGINS`; existing stored endpoints are also checked on every call. Do not approve user-controlled domains or private-network addresses. Worker/reverse-proxy deployments must set `APP_ORIGIN=https://your-frontend-domain` on the API server, without a path or trailing slash. Client-supplied forwarding headers are not trusted. Production requires a valid `APP_SECRET` and HTTPS (Secure session cookies).

Environment variables must be passed to the process; start with `node --env-file=.env server/index.mjs` when using `.env`. See [SECURITY_REVIEW.md](SECURITY_REVIEW.md) for findings, regression coverage, and remaining limitations.

## Recommended Workflow

1. Select "Register New School" and fill in school and administrator details.
2. Download the template from "Student Management" and import students, or create accounts individually.
3. Maintain your school's actual knowledge points in "Knowledge Base."
4. Upload images, PDFs, or Word files in "New Homework" to generate drafts; complete answers, scores, and knowledge points before publishing.
5. Teachers or students select the official assignment and upload student answer sheets.
6. Teachers start page-by-page OCR in "Recognition & Review" and confirm each question.
7. Confirmed mistakes enter the corresponding student accounts and the mistake-based exam paper pool.
8. In "AI Teaching Analysis," select this assignment to generate review plans, error cause statistics, and student follow-up lists.

## Submission and grading fixes (2026-10-17)

Files selected in one upload now form a single student's answer sheet and are recognized in selection order. Do not mix different students in one upload. Startup adds the `submission_files` table automatically; legacy single-file submissions remain readable. Previously split submissions cannot be reliably regrouped and are left unchanged; re-upload complete answer sheets and review historical statistics where needed.

Review uses canonical question numbers, prompts, maximum points and editable scores; verdicts and scores stay consistent, and totals are calculated on the server. Switching students cannot mix recognition results, and malformed model output can be retried. Student practice APIs omit reference answers and require strict JSON boolean grades. Re-recognized mistakes are excluded from notebooks, practice and paper generation until approved again. Analytics retain perfect-score students and error-free knowledge points and isolate same-name accounts by ID.

## Testing & Verification

```powershell
npm test            # Vitest: error cause classification, class statistics, data isolation, AI attribution fusion
npm run build       # Strict TS checks + production build
npm run verify:teaching   # End-to-end verification of teaching analysis APIs (uses fixture data, no paid model calls)
```

`npm run verify:teaching` generates a fixture database under `.tmp-teaching-verify/` and starts a temporary port, verifying read/write interfaces, statistical standards, and permission boundaries item by item, then cleans up automatically. It only validates deterministic parts; AI generation endpoints return clear error messages when no API key is configured.

## Cloudflare Free Tier

Cloudflare Worker hosts only the static frontend and proxies `/api/*` to your origin server, without depending on D1 or R2. Start the main service on your server first, then use cloudflared to expose port 8787, and write the HTTPS origin URL to `ORIGIN_URL` in `wrangler.jsonc`.

```powershell
cloudflared tunnel --url http://127.0.0.1:8787
npm run build
npx wrangler deploy
```

## Pre-Launch Checklist

- Set up a fixed HTTPS domain, a strong random `APP_SECRET`, and daily offline backups.
- Add email verification, password recovery, and enrollment verification. Login rate limits currently use the source IP; users behind a reverse proxy may share a quota.
- Add virus scanning for uploaded files and define retention/deletion policies for originals, recognized text, and account data.
- Confirm minor data processing terms with OCR providers and evaluate recognition rates with real school samples.
- SQLite suits single-machine or single-writer setups; multi-node deployments require migration to an external database and object storage.

Model capabilities and API formats change over time. Current OCR uses OpenAI-compatible vision message format; providers incompatible with this format require a separate adapter.

## Directory Structure

| Directory | Description |
|-----------|-------------|
| `src/` | Frontend source (React + TypeScript) |
| `server/` | Backend server (Express + SQLite) |
| `public/` | Static assets |
| `tools/` | Deployment tools and scripts |
| `Roadshow PPT - 路演PPT/` | Roadshow deck and assets (Chinese and English PPTX / PDF / HTML sources) |

| Document | Description |
|----------|-------------|
| `README.md` / `README.EN.md` | Project overview (中文 / English) |
| `SAFETY.zh.md` / `SAFETY.md` | Safety statement and responsible use guidelines (中文 / English) |
| `AGENTS.md` | Repository collaboration and coding conventions |
| `Roadshow PPT - 路演PPT/README.md` / `README.EN.md` | Roadshow deck usage guide (中文 / English) |

> **Note**: `src`, `server`, `public`, and `tools` are standard code directories kept with their original names to ensure builds work correctly. Non-code directories use bilingual naming.

## Safety

This project is assisted by DeepSeek V4.1-Flash and Qwen 3.8 Max (running on DeepSeek Harness) and GPT 6 Sol (running on Codex). All AI-generated output has not been carefully reviewed by the developers, who are not responsible for the final code base and documentation.

See [SAFETY.md](SAFETY.md) ([中文](SAFETY.zh.md)) for detailed safety information, responsible use guidelines, and data privacy considerations.

## Background tasks and operations

See [OPERATIONS.md](OPERATIONS.md) for durable OCR jobs, limits, review auditing, backup/restore, and capacity/evaluation tools (Chinese).
