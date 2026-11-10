# Integration checks

The validation reference for this public source distribution is [guide/VALIDATION.md](guide/VALIDATION.md). The package status is [PACKAGE_STATUS.md](../PACKAGE_STATUS.md).

## Release pipeline checks

The workflow builds Release packages for Windows and Linux. The publication stage requires every selected platform to succeed. It collects only expected binary packages, archives the exact repository commit, records build metadata and generates SHA-256 checksums. Files are uploaded before a draft is published.

Local preparation checks workflow structure, shell syntax, documentation links, required sources and archive integrity. A local rehearsal uses artificial package fixtures to check release collection and failure handling without publishing.

## Full integration

Successful source checks do not prove a full engine build. Inspect the platform jobs, installed payload checks and portable-runtime checks in Actions. Then exercise the compiled launcher, updater, local server and two-client gameplay. Record the commit, content set and result for each scenario.
