# Submitting to the Flipper Apps Catalog

The catalog does not watch this repo. Every version is a pull request against
[flipperdevices/flipper-application-catalog](https://github.com/flipperdevices/flipper-application-catalog),
reviewed by a human. Expect 1–2 business days. **The app in the catalog changes only when
that PR merges** — pushing here is not a release.

## Per-release checklist

1. **Bump `fap_version`** in `application.fam`. It must be `major.minor` and strictly
   greater than the last submitted version — the catalog rejects a repeat. CI enforces the
   format on every push so a bad version fails here, not weeks later at submission.
2. **Update `CHANGELOG.md`.** The manifest inlines it, so it is what users read in the app
   listing.
3. **Refresh `screenshots/`** if the UI moved. See `screenshots/README.md` — qFlipper
   captures, unmodified.
4. **Tag it.** `git tag v0.4 && git push origin v0.4`. The tag build publishes a permanent
   release; the rolling `latest-build` assets get overwritten on the next push and are not
   a citable artifact.
5. **Pin the commit.** Copy `catalog/manifest.yml` and replace `commit_sha` with the exact
   commit the tag points at:
   `git rev-parse v0.4`
6. **Validate before opening the PR**, in a clone of the catalog repo:
   ```sh
   python3 tools/bundle.py --nolint applications/Games/jokeri/manifest.yml bundle.zip
   ```
   This resolves the `@./` includes and the screenshot paths against the pinned commit, so
   it catches a missing file that a local `ls` would not.
7. **Open the PR** on branch `georgegach/jokeri_<version>` — e.g. `georgegach/jokeri_0.4` —
   with the manifest at `applications/Games/jokeri/manifest.yml`.

## Things that will bite

- **Never force-push `main` or delete a tag.** A submitted `commit_sha` must stay reachable
  forever; the catalog rebuilds published apps from it. Rewriting history breaks a live
  listing, not just a future one.
- **`appid` is permanent.** It is `jokeri`, it is the catalog's primary key, and it is also
  the app's data directory (`/ext/apps_data/jokeri/`). Changing it means a new listing and
  orphaned saves.
- **Compatibility is against the latest Release or Release Candidate firmware**, which is
  why CI builds the `release` channel and not just `dev`.
- **A change request left unanswered for 14 days gets the app removed.** Watch the PR.
- **Custom firmware is out of scope.** Momentum/Unleashed/RogueMaster build against
  different SDKs; neither published asset is guaranteed to launch there.
