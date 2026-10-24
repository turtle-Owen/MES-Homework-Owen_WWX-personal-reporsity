# Security review — 2026-10-17

Base: `d4e47ca8f3fdf6f0a3eb8903bbe2ebd2461c2292`. Scope: Express/SQLite API, React registration flow, file/model boundaries, Windows deployment console, dependency audit. This is a targeted source review and regression pass, not a guarantee that the application is production-safe.

## Findings and changes

| Severity | Confirmed problem | Remediation |
| --- | --- | --- |
| High | Any authenticated school member could read another student's submission; upload files were served without authentication | Scope reads to the submitting student, assignment teacher, or school administrator; authenticate and authorize legacy upload URLs; force attachment downloads and no-store |
| High | School code holders could self-register as teachers and gain school staff permissions | Public registration is student-only; administrators create teachers through the existing admin workflow; existing accounts are unchanged |
| High | An arbitrary model endpoint could trigger server-side requests, including internal addresses, with an API credential | Require selected vendor HTTPS origin or a deployment-configured approved origin, validate saved endpoints again at request time, reject redirects and embedded credentials, and time out requests |
| High | Deployment console accepted cross-site control and DNS-rebinding requests; a tunnel token was interpolated into `cmd.exe` | Exact loopback Host and same-origin POST checks; restricted token/domain/path arguments; remove APP_SECRET from command strings; constrain curl configuration inputs |
| Medium | Students could obtain reference answers, inspect assignments outside their class, or change their own class | Omit reference answers in student assignment responses; enforce class and owning-teacher checks before reads/uploads; deny student stage/class changes |
| Medium | Re-recognition could preserve teacher approval, including a recognize/review race | Invalidate approval and score; serialize recognition per submission; reject concurrent review/recognition; reset approval again when model output is saved |
| Medium | Upload MIME/extension were trusted, rejected submissions could leave files, and a tiny PDF could request a huge canvas | Check file signatures against claimed MIME; canonicalize saved extensions; clean rejected uploads and roll back failed writes; cap PDF dimensions/pixels/pages and destroy the loading task in finally |
| Medium | Arbitrary question/knowledge IDs could be persisted across assignment or school boundaries | Validate assignment knowledge against school; require exactly one review item for each real assignment question and derive mistake knowledge IDs from those questions; check focus students belong to the assignment class |
| Medium | Production accepted a default encryption secret; authentication lacked throttling | Refuse missing/short/template production secrets; bound password size/type; apply a bounded in-memory source-IP auth quota; block cross-origin state changes and avoid exposing provider error bodies |
| Reliability | Deployment busy flag never reset; stop could race with deployment; HTTP requests and recursive JSON were unbounded | RAII busy reset and exception handling, stop barrier, repeated-deployment cleanup, HTTP size/deadline limits, JSON depth limit, partial-send handling; Windows CSPRNG and complete BCrypt link settings |

PDF checks limit render surface allocation; file signatures are not full format validation or malware scanning. Model requests still require a supported provider protocol; the existing README caveat about non-OpenAI-compatible provider adapters remains applicable.

## Independent review

A backend reviewer independently reproduced the original recognition/approval race with a delayed mock model and temporary SQLite database. After the follow-up fix, the reviewer checked the lock lifecycle and independently reran the 23 then-current security tests successfully. Additional PDF and production-secret tests were added afterward.

A second agent reviewed the Windows deployer and implemented isolated fixes. The primary agent reviewed those changes and caught a missing BCrypt link flag in the direct PowerShell build path, which was then fixed. No paid model APIs, real student records, or live deployments were used.

## Verification

- `npm test`: 50 tests, including 26 security tests (unit and isolated HTTP regression coverage).
- `npm run build`: strict TypeScript and Vite production build.
- `npm run verify:teaching`: all 30 fixture API checks passed.
- Desktop deployer (`tools/deployer`): `mes-deployer-test` (CTest) and `mes-deployer --self-test` in Release passed on Windows MinGW, and the same target is built/tested with MSVC in GitHub Actions. Coverage includes SHA-256 vectors, ZIP extraction with zip-slip rejection, release manifest parsing, `.env`/service-file generation and demo/cancel deployment paths. The previous standalone console-deployer security test was retired together with the console deployer.
- `git diff --check`: passed.
- `npm audit --json`: initial seven package findings (including two critical and one high) reduced to three moderate package findings, all from one remaining transitive advisory below.

## Remaining limitations and deployment changes

- The remaining `sprintf-js` advisory (GHSA-hp3w-g68c-fv3c) propagates through `argparse` to `mammoth`. The installed registry offers no patched sprintf-js release; npm's suggested downgrade of mammoth to 0.3.29 is not an acceptable parser downgrade. Inspection places argparse's sprintf usage in CLI formatting; no exploit through this app's document conversion calls was demonstrated. This is a residual advisory, not a zero-vulnerability result.
- The new desktop deployer was built and exercised on Windows (MinGW locally, MSVC in CI) and built on Linux/macOS in CI; interactive GUI, system-level service installation and real HTTPS downloads still require manual validation on each target OS. Download verification trusts the release manifest served over HTTPS and verifies SHA-256 before extraction; ZIP extraction rejects absolute paths, `..` traversal and symlink entries. Administrator/root prompts for service installation remain explicit and are not silent-elevated.
- Do not treat these changes as document-parser isolation. Compressed DOCX/XLSX bombs, malicious images, expensive PDF content, total disk quotas, and worker CPU/memory limits still require additional controls before hostile public deployment.
- Login throttling is process-local, by direct peer IP, at 30 attempts per 10 minutes. Behind a proxy, users share the proxy's quota. Do not enable blanket trust of spoofable forwarding headers; use trusted-proxy infrastructure if changing this design. Recognition locks are also process-local, consistent with the current single-process SQLite deployment.
- HTTPS is required for production Secure cookies. For a Worker or reverse proxy, set the API server's `APP_ORIGIN` to the browser-facing exact origin. Set `PROVIDER_ALLOWED_ORIGINS` only for explicitly trusted HTTPS gateways; do not approve user-controlled or internal destinations.
- Existing accounts and data are not deleted or migrated. Audit historical teacher accounts because previously self-registered teachers remain valid. Public student registration is now disabled; staff provision student accounts and classes. Historical self-enrolled accounts still require identity/enrollment verification. Password recovery, malware scanning and data retention remain deployment work.
- Backend checks do not establish the accuracy of OCR/grading, provider compatibility, or real-world model quality. Teacher approval remains required.

## Workflow follow-up — 2026-10-17

Base: merged main `7ba5ee1f3d0c2a6c8b2b3e6728cd4858d4b9ff4d`. The second pass fixes the seven subsequently reproduced workflow failures:

- Review requests carry selection generations and per-submission operation ownership. A late response for A cannot overwrite B. Official question IDs/numbers, editable per-question scores and server-calculated totals replace positional grading and unchecked client totals. Correct means full points; incorrect allows partial points below the maximum; zero-point questions retain an independent verdict.
- Each new upload creates one submission with ordered `submission_files` attachments. Every attachment requires the same submission authorization. Recognition combines all attachments and applies the page limit to the whole answer sheet. Startup adds the table, and legacy records fall back to their original file path. Historical separately uploaded records are preserved because their intended page grouping cannot be inferred reliably.
- Practice grading requires actual JSON booleans, an exact result count and complete student answers. Malformed model verdicts fail without changing mastery. Updates check the question-set snapshot and current teacher approval so stale asynchronous results cannot mark replacement questions as mastered.
- Student notebook, generation and reviewed-submission responses strip exercise reference answers/explanations. Teacher corrections for the original reviewed homework remain available. Practice feedback is returned after submission.
- Revoked approval excludes old mistakes from notebooks, practice and paper generation until teacher review is completed again.
- Analytics initialize students from reviewed submissions and knowledge points from all official questions, retaining perfect-score students and 100% knowledge points. Same-name accounts use IDs for ownership, denominators and individual diagnosis; per-student question materials are isolated before constructing diagnosis prompts.

Independent follow-up review caught malformed OCR output with no retry path and individual question details that still contained another student's explanations. Both were corrected: invalid item shapes render a retry action; individual diagnosis rebuilds question materials from the selected submission owner. The reviewer independently passed 43 relevant tests and reproduced same-name isolation after the fix.

Validation: 91 Vitest tests passed, strict TypeScript/Vite build passed, all 30 teaching API checks passed, and `git diff --check` passed. HTTP regression tests cover startup without the new table, legacy reads, one submission with two correctly ordered images, every attachment's authorization, bounded scores/total calculation, practice answer stripping, strict model booleans, stale-result rejection, and unreviewed paper exclusion. Models are mocked and no paid endpoints were called.

Full browser interaction and a UI screenshot could not be completed: this container has no Chromium executable and its browser download returned invalid archives. Selection races, malformed OCR recovery and score/verdict edits are covered by the extracted production state helpers and automated tests; this does not replace a real browser visual review. Previously documented Windows and parser isolation limitations still apply.

## Third-round fixes and independent follow-up review — 2026-10-18

Reviewed base: main `57fe26721c618a5f593a714c249f7d4c8d2447a7` (PR #3 merged).

The six confirmed findings were addressed:

- Teaching analysis uses selection generations and per-operation sequence tickets. Changing assignments clears statistics, picked question numbers, focus entries and the student drawer immediately. Expired load/generate/refine/focus responses cannot populate the new selection; writes validate the displayed assignment identity before submission. Student detail/diagnosis callbacks are invalidated on close and selection changes.
- Public account enrollment is disabled; school identifiers cannot authorize joining a class. Existing staff creation/import remains available. Existing accounts are not deleted; administrators must verify historical enrollment.
- All provider calls reserve persistent SQLite usage before contacting the provider. A write transaction enforces per-user and per-school rolling quotas and active leases, including across database connections/restarts. Defaults: student 20/hour, staff 120/hour, school 600/hour and 3000/24 hours; one active call per user and four per school. Failed calls count. Leases expire after process failure and are checked/renewed before sending the provider request; a released/expired lease cannot renew. These are request quotas, not financial spending guarantees.
- A user's generation and grading cannot overlap, preventing older practice verdicts from overwriting later accepted attempts. Existing question-set and teacher-review checks still reject superseded results.
- Teaching analyses store a source fingerprint. Statistics and fingerprint share a read transaction; generation results are compared and saved atomically in a write transaction. Changed source data hides older reports, and an in-flight result for superseded grades returns 409. Legacy reports without fingerprints remain stored but require regeneration. Unlinked historical student diagnoses now use the same name key when saving and loading.
- Locally managed Cloudflare tunnels send a securely random secret and persist matching credentials before DNS changes. Token mode does not mix local credentials. Explicit configuration, successful-connection log checks, timeout failure, final-mode dependency detection, complete build inputs and incremental bounded logs correct related deployment failures.

The follow-up independent review found and corrected two further issues: quota denial could clear a completed submission's score/approval before any recognition started, and structurally invalid model JSON could persist and repeatedly crash the teaching screen. OCR now reserves capacity before state changes; denied calls preserve completed grades. Class/student output has recursive known-field/type validation before persistence, and malformed historical content is hidden. The teacher review screen now exposes authorized original attachments and official answers; student submission responses exclude the official-answer field.

Validation: 123 Vitest tests passed; all 30 teaching API checks passed; strict TypeScript/Vite production build passed. C++ security regressions, eight mocked tunnel provisioning cases, five connection-log cases, build freshness and log cursor regressions passed with `-Wall -Wextra -Werror -pedantic`. Tests cover quotas across restart, independent SQLite connections, rejected concurrent calls without additional provider calls, grade preservation on OCR throttling, snapshot invalidation, in-flight 409s, malformed current/historical reports, and authorized answer fields. All model/Cloudflare interactions used mocks; no paid API or live DNS changes were performed.

Limitations: full browser rendering and complete Windows compilation/installation/tunnel activation are unverified here. Parser process isolation, aggregate upload/disk quotas, historical member verification and the previously documented transitive sprintf-js advisory remain unresolved deployment concerns. Current recognition/review workflow locks still assume a single application process; shared model leases do not make every business operation cluster-safe. This review is scoped evidence, not a zero-vulnerability claim.
