# Governing-document inventory

Captured: 2026-07-29 UTC.

The documents below are authoritative for Gate Zero. Hashes are SHA-256 over
the exact bytes read before repository work began.

| ID | Document | Role | SHA-256 |
|---|---|---|---|
| GOV-001 | `/root/.codex/attachments/48b451ff-95d7-400b-ba0d-203f03b8a55c/pasted-text-1.txt` | Frontier Directorate master execution charter | `5235d46f948d8d233cecf61fdfb18622c02977935168578fbab46c6d10f21fb6` |
| GOV-002 | `/etc/vast-agents-guide.md` (also `/workspace/AGENTS.md` and `/workspace/CLAUDE.md`) | Instance operating guide | `06914e8fe301bee48b4c9731a35f963a51e7449e9bf3eabd1c761029b221db74` |
| GOV-003 | `/etc/vast_agents/base.md` | Base-image service, storage, GPU, and provisioning guide | `0ce37b2aceef111a8865fc4eb50b4df8b640f071b23b7cf25c86a9c03d159c6d` |

`GOV-002` is the concatenated instance guide; this image contains exactly one
component file, `GOV-003`. No repository-local project specification existed at
baseline. The charter is therefore copied by hash, not duplicated, so later
edits cannot masquerade as the input requirements.

## Precedence and change control

1. Platform and instance safety requirements govern operation of this machine.
2. The master charter governs the product and process.
3. Approved architecture decisions resolve underspecified choices without
   narrowing charter requirements.
4. Update specifications may add detail but may not conflict with items 1–3.

A changed governing hash invalidates Gate Zero approval until the Warden reviews
the diff and affected decisions, risks, traceability rows, and update states.

