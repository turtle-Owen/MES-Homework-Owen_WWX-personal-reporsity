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
- `g++ -std=c++17 -Wall -Wextra -Werror -pedantic tools/deployer/tests/security_test.cpp tools/deployer/src/json.cpp -o /tmp/deployer-security-test` and run: passed. The implementing reviewer also ran ASan/UBSan successfully, with leak detection disabled due to the container environment.
- `git diff --check`: passed.
- `npm audit --json`: initial seven package findings (including two critical and one high) reduced to three moderate package findings, all from one remaining transitive advisory below.

## Remaining limitations and deployment changes

- The remaining `sprintf-js` advisory (GHSA-hp3w-g68c-fv3c) propagates through `argparse` to `mammoth`. The installed registry offers no patched sprintf-js release; npm's suggested downgrade of mammoth to 0.3.29 is not an acceptable parser downgrade. Inspection places argparse's sprintf usage in CLI formatting; no exploit through this app's document conversion calls was demonstrated. This is a residual advisory, not a zero-vulnerability result.
- Windows SDK/MinGW is unavailable here. The portable C++ safety helpers and JSON depth guard were compiled and tested; full Win32 linking, socket parsing, thread scheduling, installer behavior, UI and Cloudflare tunnel flows require Windows validation. A connector token remains a validated process argument and may be visible to sufficiently privileged local users.
- Do not treat these changes as document-parser isolation. Compressed DOCX/XLSX bombs, malicious images, expensive PDF content, total disk quotas, and worker CPU/memory limits still require additional controls before hostile public deployment.
- Login throttling is process-local, by direct peer IP, at 30 attempts per 10 minutes. Behind a proxy, users share the proxy's quota. Do not enable blanket trust of spoofable forwarding headers; use trusted-proxy infrastructure if changing this design. Recognition locks are also process-local, consistent with the current single-process SQLite deployment.
- HTTPS is required for production Secure cookies. For a Worker or reverse proxy, set the API server's `APP_ORIGIN` to the browser-facing exact origin. Set `PROVIDER_ALLOWED_ORIGINS` only for explicitly trusted HTTPS gateways; do not approve user-controlled or internal destinations.
- Existing accounts and data are not deleted or migrated. Audit historical teacher accounts because previously self-registered teachers remain valid. Public student registration still relies on the school code and self-declared initial class; identity/enrollment verification, password recovery, malware scanning and data retention remain deployment work.
- Backend checks do not establish the accuracy of OCR/grading, provider compatibility, or real-world model quality. Teacher approval remains required.
