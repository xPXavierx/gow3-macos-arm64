# Community catalogue: submission, review and signed distribution

The launcher prepares new discoveries and opens the official GitHub contribution
form. The player signs in, attaches the selected `GOW3-<digest>.txt`, and submits.
This browser confirmation is intentional: the app stores no GitHub password or
token and never uploads gameplay information without a player's action.
The file contains catalogue descriptions, not dumps, saves, logs or GPU programs.

The normal user-facing download path accepts only the official signed catalogue.
Old manual-import files are retained but no longer loaded automatically. Local
discoveries and the bundled catalogue continue to work offline.

## Trust boundaries

- The issue attachment is **untrusted**. The validator accepts one bounded plain
  text file from GitHub's attachment service. It rejects foreign links, archives,
  executables, wrong game/renderer versions, duplicate JSON fields, non-finite
  values, unknown fields, oversized records and incomplete dependencies.
- The optional Actions job has read-only permissions, no signing key, no
  contributor checkout, and no GPU/native compilation of uploaded data. An
  accepted attachment is quarantined for review; it is never automatically merged.
- Schema validation does not establish correct rendering. A maintainer reviews
  the descriptions and tests new combinations against the verified game dump
  before merging them into a curated catalogue.
- Publication requires the maintainer's Ed25519 key, stored in macOS Keychain.
  `tools/CatalogSigner.swift` is a maintainer tool and must never be bundled.
  The player receives only the 32-byte `CommunityCatalogue.pub` verification key.
- The app checks the manifest signature before downloading the body. It then
  verifies game/version, renderer ABI, revision, expiry, byte count, SHA-256 and
  the catalogue schema. HTTPS uses macOS system trust roots; external origins
  and redirects are rejected. Failed updates preserve the previous generation.
- Cached official data is reverified when preparing a game launch. Revision
  history is stored outside the clearable shader cache to reject rollback.

These controls reduce malicious-file and supply-chain risks. They are not a
claim that parsers, GPU drivers, the maintainer's Mac, or GitHub are infallible.

## Official feed

The public `community-catalog` branch of `xPXavierx/gow3-macos-arm64` contains:

```
v1/<renderer-abi>/latest.json
v1/<renderer-abi>/latest.sig
v1/<renderer-abi>/<sha256>.jsonl
```

`latest.sig` is an Ed25519 signature of the exact `latest.json` bytes. The signed
manifest fixes the body digest and size; it does not supply an arbitrary URL.
Revision numbers strictly increase. Manifests expire after 90 days in the
maintainer packaging tool. Refresh before expiry, even if only renewing unchanged
descriptions; retain the same signing key or distribute a new app trust anchor.

Players can use **Update official catalogue**. Before the first Play in a launcher
session, an optional daily check runs; a network failure falls back to local data.
After a session, the launcher indicates newly discovered combinations and offers
**Contribute on GitHub**. No automatic uploads or user authentication are needed
for downloads.

## Maintainer procedure

1. Inspect the issue validation result. Run the same trusted parser locally:
   `python3 tools/review_community_catalog.py --file ATTACHMENT.txt --output REVIEW`.
2. Review/deduplicate the candidate and test its new shader/pipeline descriptions
   against the supported dump in an isolated profile. Do not run attachment code
   or install a contributor's compiled cache.
3. Merge only reviewed records into the canonical catalogue, retaining dependency
   identities. Review the diff; a valid schema alone does not warrant publication.
4. Compile `CatalogSigner.swift` locally. `CatalogSigner init PUBLIC_KEY` creates
   or reuses the Keychain key and writes only its public part. Initialize once;
   compare that public key with the application's pinned key.
5. Run `tools/sign_community_catalog.py --catalogue REVIEWED.jsonl --revision N
   --output FEED --signer /absolute/path/CatalogSigner`. It packages/signs locally
   and performs no upload. Verify using the app's `CatalogTrust verify` helper.
6. Publish the immutable body plus the two latest files to the feed branch using
   a normal reviewed commit. Do not put the private key in Git, app resources,
   environment variables, Actions secrets or an issue attachment.

The first feed is the existing, locally tested bundled seed; it is not a claim of
campaign-wide coverage. This service does not enable frame generation.
