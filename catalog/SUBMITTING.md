# Submitting to the Flipper Apps Catalog

The catalog does not watch this repo. Every version is a pull request against
[flipperdevices/flipper-application-catalog](https://github.com/flipperdevices/flipper-application-catalog),
reviewed by a human. Expect 1–2 business days. **The app in the catalog changes only when
that PR merges** — pushing here is not a release.

## Per-release checklist

0. **Run the preflight**: `python3 tools/catalog_preflight.py`. It mirrors every check in
   the catalog's `tools/bundle.py` that does not need the SDK, and CI runs it on every
   push. Green here means the submission fails only for reasons about the *code*.
1. **Bump `fap_version`** in `application.fam`. It must be `major.minor` and strictly
   greater than the last submitted version — the catalog's CI queries its backend and
   fails the PR if that version already exists.
2. **Update `CHANGELOG.md`.** The manifest inlines it, so it is what users read in the app
   listing. It must stay inside the allowed Markdown subset — see below.
3. **Refresh `screenshots/`** if the UI moved. See `screenshots/README.md` — qFlipper
   captures, unmodified, and the size rules there are not advisory.
4. **Tag it.** `git tag v0.4 && git push origin v0.4`. The tag build publishes a permanent
   release; the rolling `latest-build` assets get overwritten on the next push and are not
   a citable artifact.
5. **Pin the commit.** Copy `catalog/manifest.yml` and replace `commit_sha` with the
   **full 40-character** SHA the tag points at — `git rev-parse v0.4`. The bundler checks
   the length literally; an abbreviated SHA is rejected.
6. **Validate before opening the PR**, in a clone of the catalog repo:
   ```sh
   python3 -m venv venv && source venv/bin/activate
   pip install -r tools/requirements.txt
   python3 tools/bundle.py --nolint applications/Games/jokeri/manifest.yml bundle.zip
   ```
   This clones the pinned commit and resolves the `@./` includes and screenshot paths
   against it, so it catches a file that exists locally but was never pushed.
7. **Open the PR** on branch `georgegach/jokeri_<version>` — e.g. `georgegach/jokeri_0.4` —
   with the manifest at `applications/Games/jokeri/manifest.yml`. The path is load-bearing:
   the bundler asserts the category and appid in the path match `application.fam`.

## The Markdown subset (the non-obvious one)

`changelog` and `description` are run through a filter that **raises** on anything outside
headers 1–2, bold, italic, lists and links. It does not strip — it fails the submission.
Forbidden: backticks and code blocks, images, blockquotes, horizontal rules, setext
headers, reference-style links, raw HTML, HTML entities.

This is why the manifest points `description` at `catalog/description.md` rather than
`README.md`: the README is full of code fences and backticks and would fail. Keep the two
files apart — the README is for GitHub readers, `description.md` is for the catalog.

## Things that will bite

- **Never force-push `main` or delete a tag.** A submitted `commit_sha` must stay reachable
  forever; the catalog rebuilds published apps from it. Rewriting history breaks a live
  listing, not just a future one.
- **`appid` is permanent.** It is `jokeri`, it is the catalog's primary key, and it is also
  the app's data directory (`/ext/apps_data/jokeri/`). Changing it means a new listing and
  orphaned saves.
- **Compatibility is against the latest Release or Release Candidate firmware.** The
  catalog builds with the `release` SDK and retries with `rc` if that fails — which is why
  CI here builds the `release` channel and not just `dev`.
- **The catalog does not run `ufbt lint` on submissions** (its bundler action passes
  `skip-lint: true`), so clang-format drift will not bounce the PR. It *does* build the
  app, for every target.
- **A change request left unanswered for 14 days gets the app removed.** Watch the PR.
- **Custom firmware is out of scope.** Momentum/Unleashed/RogueMaster build against
  different SDKs; neither published asset is guaranteed to launch there.
