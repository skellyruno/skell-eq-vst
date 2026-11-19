# Windows VST3 preview provenance

The Windows workflow records two distinct revisions:

- source_commit: the exact event source. On pull_request runs the workflow uses `github.event.pull_request.head.sha`; on push or `workflow_dispatch` it uses `github.sha`.
- ci_checkout_commit: the exact commit checked out by the workflow (the PR merge commit for pull_request validation).

The package build-info.json and Skelly-EQ-Windows-VST3-manifest.txt record these values alongside the workflow run ID, Windows build result, CTest result, pluginval strictness/result, VST3 binary SHA-256 and moduleinfo SHA-256.

The SHA-256 manifests detect accidental corruption and enforce package completeness. They are not digital signatures and do not establish publisher identity. This package remains a private, unsigned preview until host, audio, licensing, vendor-contact, signing and owner-approval gates pass.
