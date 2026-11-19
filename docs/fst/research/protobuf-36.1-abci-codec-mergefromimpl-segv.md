# Research Report: protobuf 36.1.0 table-dispatch SEGV in abci_codec.cpp MergeFromImpl

**Deliverable ID:** RQ-PROTOBUF-36-SEGV-2026-11-19  
**Author:** fst-research (Research Agent)  
**Date:** 2026-11-19  
**Classification:** Research report — recommendation proposed, NOT owner-approved. Does not change any APPROVED/PROVISIONAL/OPEN RESEARCH decision.  
**Edit scope:** `docs/fst/research/**` only. No source code modified. No push.

---

## 1. Required FST capabilities / problem statement

- FST frames ABCI messages (`tendermint::abci::Request`/`Response`) over a socket using `read_framed_message<T>()` in `src/consensus/abci/abci_codec.cpp`.
- Line 705 (current tree): `msg.ParseFromArray(msg_bytes.data(), static_cast<int>(length))`.
- `ParseFromArray` → `MessageLite::ParseFrom<kParse>` → `internal::MergeFromImpl<false>(absl::string_view, ...)` → `internal::TcParser::ParseLoop` → `TcParser::TagDispatch` (table-driven "tail-call" parser).
- Symptom: AddressSanitizer SEGV at `abci_codec.cpp:705` inside protobuf `MergeFromImpl` during CometBFT integration, on a **valid 21-byte `Request.info` frame**.
- Fix commit `ad9bf01` ("fix(abci): bounds-checked frame length; serialize socket-server lifecycle") added frame-length validation (zero, `max_message_size`, 64 MiB cap, int32 range) and `kParseSlackBytes = 16` zero-initialized parse slack. It does **not** prevent the SEGV (reproduced under ASan at `team/integration` commit `bdb07fd`: "14 passed then SEGV abci_codec.cpp:674 (VDEF-001 pre-existing/unrelated)"; line 674 = line 705 after the `ad9bf01` edit shifted the file).
- `integration-status.md` (VDEF-001 entry) already characterizes the residual risk as: "ASan aborts on protobuf table-dispatch indirect call for valid Info frame — documented as protobuf-internal limitation, does not affect Debug/TSan paths."
- Working hypothesis (from the task): protobuf 36.x table-dispatch performs an **indirect call through a data-derived index** (`fast_entry->target()` indexed by `coded_tag & fast_idx_mask`) that can fault on valid input.

---

## 2. Candidate technologies / alternatives considered

| Option | Description |
|--------|-------------|
| A. Keep protobuf 36.1.0 (system-installed), accept SEGV | Status quo; ASan abort in Debug/ASan presets on valid Info frames. Not acceptable for a sanitized build. |
| B. Rebuild protobuf from source with the **same compiler/flags as FST** | Documented upstream workaround for the same crash mechanism (#21447/#20482). |
| C. Pin a single consistent toolchain (one compiler + version + flags for protoc, libprotobuf, and all FST TUs) | Prevents ABI/mixed-define mismatch that drives null `func_`/kFunc dispatch. |
| D. Downgrade protobuf to ≤28.x (pre-table-dispatch parser) | #21447 reporter: "Clang-compiled binary worked with version 28." Old/EOL, codegen/runtime must match; significant regression & supply-chain concern. Not recommended. |
| E. Upgrade to protobuf 36.2 / latest | Release notes contain no C++ parse-table crash fix. Unlikely to resolve; not a fix. |
| F. Disable table-driven parsing at runtime | Not exposed in protobuf 36.x; generated `.pb.cc` calls `GetTcParseTable()` unconditionally. Requires legacy codegen / fork. Not practical. |
| G. FST-side frame/content validation | Cannot help: a **valid**, self-consistently serialized `Request.info` frame crashes inside libprotobuf; the fault is not in the frame bytes FST can validate. |

---

## 3. Technical comparison — the crash mechanism

### 3.1 protobuf table-dispatch (confirmed mechanism in v36.1 source)

Source: `src/google/protobuf/generated_message_tctable_impl.h` at tag `v36.1` (commit `f377bfe`, released 2026-08-31).

`TcParser::TagDispatch` (PROTOBUF_ALWAYS_INLINE):
```cpp
const auto coded_tag = UnalignedLoad<uint16_t>(ptr);
const size_t idx = coded_tag & table->fast_idx_mask;
PROTOBUF_ASSUME((idx & 7) == 0);
auto* fast_entry = table->fast_entry(idx >> 3);
TcFieldData data = fast_entry->bits;
data.data ^= coded_tag;
PROTOBUF_MUSTTAIL return fast_entry->target()(PROTOBUF_TC_PARAM_PASS);
```
- `target` is a `TailCallParseFunc` (function pointer) taken from `table->fast_entries[]`, a jump/dispatch table.
- The **index into that table is derived directly from the input bytes** (`coded_tag & fast_idx_mask`), i.e. a data-derived indirect call.
- If the selected `fast_entry->target()` is null or invalid, the CPU performs an indirect call through a bad pointer → SEGV at address `0x0`.

### 3.2 Upstream crash reports matching this mechanism

| Source | Version(s) | Crash site | Root cause as documented | Resolution / workaround |
|--------|-----------|------------|-------------------------|-------------------------|
| **protobuf issue #21447** "[C++] Parsing crashes with segmentation fault when clang is used, since protobuf >= 29" | 29.x, 31.x (repro on 29.3, 29.4, 30.2); mechanism present in v36.1 source | `0x0` → `TcParser::FastMtS1`/`FastMtR1` → `MergeFromImpl<false>` → `TagDispatch` → `ParseLoop` (`generated_message_tctable_impl.h:1097`/`:1139`, `message_lite.cc:243`) | **ABI/compiler mismatch**: protobuf library built with GCC, consuming app with Clang (or Clang N vs Clang N+1). `as_tag == kFunc` but `MessageCreator::func_ == null` due to mismatched `kMustBeFunc` defines across TUs. | **Build protobuf from source with the same compiler as the app**; do not mix compilers. `brew reinstall --build-from-source protobuf` fixed macOS Homebrew users. Closed 2025-06-05 (no code fix; build-consistency requirement). |
| **protobuf issue #20482** "Crash on deserialization of repeated field when using Clang 19 on Windows" | 5.29.2/5.29.3 | `TcParser::ParseLoop` → `MergeFromImpl<0>` → `ParseFrom` | Build-config mismatch: MSVC-built protobuf + Clang-built app → mismatched defines in protobuf headers. | Reporter's own configuration issue; consistent toolchain resolved it. |
| Stack Overflow (2025-04-16) "Segmentation fault during Serialization by using protobuf in C++17" | protobuf 29.3 (macOS) | `FastMtR1` → `MergeFromImpl` under ASan | Same `ParseLoop`/`MergeFromImpl` table-dispatch path; ASan SEGV. | Not resolved in thread; consistent with #21447 family. |

- protobuf issue #21447: https://github.com/protocolbuffers/protobuf/issues/21447 (created 2025-04-24, closed 2025-06-05; labels `29.x`, `31.x`, `c++`, `investigation`, `packaging & distribution`, `untriaged`; assignee `sbenzaquen`).
- protobuf issue #20482: https://github.com/protocolbuffers/protobuf/issues/20482 (created/closed 2025-02-26/27).

### 3.3 protobuf documented read-past-end contract (slack addresses only this part)

Source: `src/google/protobuf/parse_context.h` (v36.1 and main). The parser intentionally requires `kSlopBytes` past the end of each buffer chunk: "it is still legal to read them (ie. should not segfault)... Reading past the end should be detected by the user and indicated as an error." Reading a key/value pair is always < 16 bytes. `ParseFromArray` (`message_lite.cc`, v36.1) sets the parse limit = `size`; lookahead reads past `size` are expected and must land in allocated, readable memory.

- `ad9bf01`'s `kParseSlackBytes = 16` zero-initialized slack satisfies this contract for lookahead reads past the buffer end.
- It does **not** satisfy the `TagDispatch` indirect call: `fast_entry->target()` is a function-pointer dispatch indexed by data, and a null/invalid `target()` faults regardless of how much byte-slack is provided. This explains why the slack fix does not prevent the SEGV.

---

## 4. Security and intellectual-property assessment

- Crash is a denial-of-service (process abort under ASan; undefined behavior in non-sanitized builds where an indirect call through a bad pointer may continue to arbitrary code). It is **protobuf-internal**, triggered during parsing of a *valid* frame — i.e. an attacker who can make FST parse attacker-controlled protobuf bytes could potentially influence the `coded_tag` index. However FST's framing already bounds length (64 MiB cap, int32 range, max_message_size), so the input is length-bounded; the residual risk is the dispatch target itself.
- Not a known CVE; no protobuf security advisory found for 36.x table-dispatch SEGV in this research. Upstream treats #21447 as a toolchain/ABI consistency issue, not a vulnerability.
- Patent: protobuf implementation is not known to assert enforced patents over standard use; Google's protobuf README/patent grant applies. No FST-specific patent concern identified in scope of this report.

---

## 5. License and dependency-tree assessment

- protobuf is **BSD-3-Clause** (SPDX: `BSD-3-Clause`). Permissive, no copyleft, no notice beyond retention of copyright/license in distributions.
- FST already depends on protobuf (`find_package(Protobuf REQUIRED)`, links `protobuf` + Abseil into `feinstatic_core`). No *new* dependency is being introduced by this research; the question is version/toolchain consistency of an existing dependency.
- Installed toolchain (this environment): `libprotobuf.so.36.1.0`, `libprotoc 36.1`, `/usr/lib/libprotobuf.so`. CMake comment in `CMakeLists.txt:20` still references "Ubuntu 24.04 provides protobuf 3.21.12" — the actual environment is protobuf 36.1, i.e. **toolchain drift vs. the CMake assumption**. Codegen (protoc 36.1) and runtime (libprotobuf 36.1) are version-matched here, which is correct; the risk is compiler/flag mismatch across TUs, not protobuf-version mismatch.
- Transitive inventory: protobuf 36.x depends on Abseil (abseil-cpp), zlib (optional). Already in FST link line (`absl_strings absl_strerror absl_base absl_synchronization absl_stacktrace absl_symbolize absl_time absl_civil_time absl_time_zone`). No new transitive obligations.

**SPDX/attribution note:** verify the installed package's `LICENSE` file (`/usr/share/doc/libprotobuf-dev` or `dpkg -L libprotobuf-dev | grep -i license`) and retain the BSD-3-Clause copyright/notice text in any redistribution, per FST's dependency policy (`docs/fst/policies/technology-and-dependency-acceptance.md`). *(Note: that policy file was not found at the expected path during this session; confirm its location before formal acceptance.)*

---

## 6. Patent analysis

- protobuf (Google) is distributed under BSD-3-Clause. Google's protobuf repository does not assert royalty-bearing patents over conforming implementations of the protobuf wire format/runtime. No FST-specific patent exposure identified from this crash research. Patent assessment for the *dependency* is limited to protobuf's own license; no additional patent search was performed as part of this report (out of scope; owner may commission).

---

## 7. Performance and scalability evidence

- No performance numbers collected for this crash; the issue is correctness (SEGV), not throughput.
- Table-driven parsing was introduced (protobuf ~29.x) specifically for parse performance; reverting to ≤28.x (Option D) would lose that optimization and is not justified solely to work around a toolchain-triggered crash.
- The 21-byte `Request.info` frame is trivially small; frame size is not a factor.

---

## 8. Maturity and maintenance evidence

- protobuf 36.1 released 2026-08-31 (commit `f377bfe`); 36.2 released 2026-09-17 (commit `2c74169`, latest in the 36.x line as of research date). 37.0-rc1 exists (2026-10-15).
- 36.1 release notes: C# enum mapping, Rust README/version, Python JSON enum only — **no C++ parse-table or MergeFromImpl fix**.
- 36.2 release notes: compiler/crate-root change only — **no C++ parse-table fix**.
- 36.0 release notes: descriptor limits, EDITION_2026 enforcement, etc. — **no C++ parse-table crash fix**.
- Upstream position on #21447: closed as completed without a code fix; the accepted resolution is **consistent toolchain / build-from-source-with-matching-compiler**. The maintainers do not consider it a protobuf source bug to fix in a release.
- Conclusion: protobuf 36.x is mature and actively maintained, but the table-dispatch crash mechanism is a **latent toolchain-sensitivity**, and there is no published 36.x release fix. Version change alone (36.1 → 36.2) is not expected to resolve it.

---

## 9. C++20 integration implications

- FST compiles with C++20 (`CMAKE_CXX_STANDARD 20`), links protobuf + Abseil. The crash occurs inside `libprotobuf.so` (`MergeFromImpl`), outside FST's C++20 code; FST cannot patch protobuf internals.
- `abci_codec.cpp` is generated/hand-maintained (not protobuf-generated); `read_framed_message` is an FST wrapper around `ParseFromArray`. FST-side hardening options are limited to what the frame wrapper can control (length, slack) — already done in `ad9bf01` and insufficient for the dispatch fault.
- CMake uses `protobuf_generate()` at build time so generated `.pb.cc`/`.pb.h` match the installed protoc (36.1). Codegen/runtime version match is satisfied; **compiler/flag consistency across all translation units** is the actionable integration variable.

---

## 10. Deployment implications

- Debug and ASan presets abort on CometBFT single-node genesis/commit when parsing a valid `Request.info` (reproduced at `team/integration::bdb07fd` under ASan). Release and TSan presets do not reproduce (per integration ledger: "does not affect Debug/TSan paths" — actually Debug 343/343 passes except the targeted ASan cometbft test; TSan passes). Deployment of a sanitized build is blocked until resolved or worked around.
- Production (non-sanitized) behavior: the same indirect call is UB; may not abort but is unreliable. Must not ship with this latent fault.
- The fix is at the **build/toolchain layer**, not the FST source layer: build protobuf (and Abseil) from source with the exact compiler/flags used for FST, or pin one consistent toolchain across protoc, libprotobuf, and all FST TUs. No source change required in `src/`.

---

## 11. Future-phase compatibility

- CometBFT consensus integration (D-017/ADR-0004) relies on ABCI protobuf framing. A parser SEGV on valid `Request.info` blocks Phase 3–4 integration testing under ASan.
- Any future protobuf version bump must re-validate: (a) codegen/runtime version match, (b) compiler/flag consistency, (c) absence of table-dispatch SEGV on ABCI frames under ASan.
- Keep protobuf version pinned and recorded in the dependency inventory; treat protobuf as a **toolchain-sensitive dependency** requiring consistent rebuild.

---

## 12. Reproducible prototype plan (read-only, no source change)

Do not modify source. To confirm the hypothesis in this environment:

1. Build a minimal C++ program that serializes a `Request.Info` (version/block_version/p2p_version/abci_version), writes a 21-byte frame (varint length + payload), and calls `ParseFromArray`, compiled with the same compiler/flags as FST against the installed `libprotobuf.so.36.1.0`. Run under ASan. Expect reproduction of SEGV at `MergeFromImpl`/`TagDispatch` if the toolchain is inconsistent.
2. Rebuild `protobuf` from source (36.1) with the **same compiler/version/flags** as FST, reinstall `libprotobuf.so`, recompile the minimal program against it, rerun under ASan. Expect the SEGV to disappear if root cause is compiler/ABI mismatch (#21447 workaround).
3. As a negative control, build protobuf with GCC and the program with Clang (mixed) and confirm SEGV returns, reproducing #21447 exactly.
4. Record exact compiler versions (`clang --version` / `g++ --version`), flags, protobuf commit, ASan options, and pass/fail.

All experiments are local, read-only with respect to FST source; they build outside `src/` (e.g. `/tmp/kilo/protobuf-repro/`).

---

## 13. Recommendation and rejected alternatives

### Recommendation
**Treat this as a protobuf-internal, toolchain-sensitive table-dispatch fault (not an FST framing bug).** The FST frame-length validation + parse-slack added in `ad9bf01` correctly implements protobuf's documented read-past-end contract but cannot prevent a null/invalid `fast_entry->target()` indirect call. The documented upstream resolution for the identical crash (#21447, #20482) is **build-toolchain consistency**:

1. Build protobuf 36.1 (and Abseil) **from source with the same compiler/version/flags** used to compile FST (do not mix a system/GCC-built libprotobuf with a Clang-built FST, or Clang N with Clang N+1).
2. Pin the toolchain and record protobuf version/commit + compiler + flags in the dependency inventory.
3. Re-verify the ASan CometBFT single-node genesis/commit test passes after the consistent rebuild.
4. Do **not** downgrade to ≤28.x (Option D) or upgrade to 36.2 expecting a fix (Options D/E rejected); do **not** attempt to disable table-driven parsing (Option F rejected); FST-side content validation (Option G) cannot help because a valid frame crashes.

### Rejected alternatives and rationale
- **Downgrade protobuf ≤28.x** — pre-table-dispatch parser avoids the mechanism but loses the parse-performance optimization, is EOL, and forces codegen/runtime mismatch; excessive regression for a toolchain-fixable fault.
- **Upgrade to 36.2/37.0** — release notes show no C++ parse-table crash fix; version change alone is not expected to resolve.
- **Disable table-driven parsing** — not exposed in protobuf 36.x; would require legacy codegen/fork; not practical.
- **FST-side frame/content validation** — a valid `Request.info` frame crashes inside libprotobuf; nothing FST validates in the frame prevents the dispatch fault.

---

## 14. Source links, versions, publication dates, research verification date

| # | Source | Title | URL | Version / edition | Access / publication date | Verified by |
|---|--------|-------|-----|-------------------|--------------------------|-------------|
| S1 | protocolbuffers/protobuf | Release Protocol Buffers v36.1 | https://github.com/protocolbuffers/protobuf/releases/tag/v36.1 | v36.1, commit `f377bfe`, published 2026-08-31T22:10:23Z | accessed 2026-11-19 | fst-research |
| S2 | protocolbuffers/protobuf | Release Protocol Buffers v36.2 | https://github.com/protocolbuffers/protobuf/releases/tag/v36.2 | v36.2, commit `2c74169`, published 2026-09-17T17:33:01Z | accessed 2026-11-19 | fst-research |
| S3 | protocolbuffers/protobuf | Release Protocol Buffers v36.0 | https://github.com/protocolbuffers/protobuf/releases/tag/v36.0 | v36.0, commit `3f17acb`, published 2026-08-20 | accessed 2026-11-19 | fst-research |
| S4 | protocolbuffers/protobuf | Issue #21447 — SEGV in clang, protobuf >= 29 | https://github.com/protocolbuffers/protobuf/issues/21447 | protobuf 29.x/31.x; created 2025-04-24, closed 2025-06-05 | accessed 2026-11-19 | fst-research |
| S5 | protocolbuffers/protobuf | Issue #20482 — Clang 19 Windows deserialization crash | https://github.com/protocolbuffers/protobuf/issues/20482 | protobuf 5.29.2/5.29.3; created/closed 2025-02-26/27 | accessed 2026-11-19 | fst-research |
| S6 | protocolbuffers/protobuf | `src/google/protobuf/generated_message_tctable_impl.h` (TagDispatch) | https://raw.githubusercontent.com/protocolbuffers/protobuf/v36.1/src/google/protobuf/generated_message_tctable_impl.h | v36.1 | accessed 2026-11-19 | fst-research |
| S7 | protocolbuffers/protobuf | `src/google/protobuf/parse_context.h` (kSlopBytes read-past-end contract) | https://github.com/protocolbuffers/protobuf/blob/main/src/google/protobuf/parse_context.h | main (reflects v36.1 contract) | accessed 2026-11-19 | fst-research |
| S8 | protocolbuffers/protobuf | `src/google/protobuf/message_lite.cc` (ParseFromArray → MergeFromImpl → ParseLoop) | https://raw.githubusercontent.com/protocolbuffers/protobuf/v36.1/src/google/protobuf/message_lite.cc | v36.1 | accessed 2026-11-19 | fst-research |
| S9 | Local FST tree | `src/consensus/abci/abci_codec.cpp` line 705 (`ParseFromArray`) | internal (local file) | current tree, commit `9f72f8e` HEAD `team/research` | 2026-11-19 | fst-research |
| S10 | Local FST tree | `docs/fst/operations/integration-status.md` (VDEF-001/FST-BUG-0018, ad9bf01, ASan SEGV reproduction at `bdb07fd`) | internal (local file) | current tree | 2026-11-19 | fst-research |
| S11 | Local environment | `protoc --version` = 36.1; `libprotobuf.so.36.1.0` at `/usr/lib/` | internal (local system) | protobuf 36.1 | 2026-11-19 | fst-research |

**Research verification date:** 2026-11-19.  
**Local protobuf installed:** `libprotobuf.so.36.1.0`, `libprotoc 36.1` (verified via `ldconfig -p`, `protoc --version`).

---

## Open questions / next steps

1. Confirm the FST build toolchain: which compiler/version/flags built the installed `libprotobuf.so.36.1.0` vs. which compiler/version/flags compile `abci_codec.cpp` and the generated `.pb.cc`. A mismatch reproduces #21447 exactly.
2. Execute the reproducible prototype plan (§12) to confirm or reject the compiler-mismatch hypothesis in this environment.
3. If confirmed, the fix is a build/toolchain change (rebuild protobuf+Abseil from source with FST's compiler) owned by fst-integration/fst-implementation, coordinated through `integration-status.md`; no FST source change.
4. License: confirm installed `libprotobuf-dev` LICENSE file text (expect BSD-3-Clause) and record SPDX `BSD-3-Clause` in the dependency inventory; confirm location of `docs/fst/policies/technology-and-dependency-acceptance.md` (not found at the expected path this session).

This report is a research recommendation only; it does not approve any dependency change and does not modify source. Owner decision and fst-integration coordination required before any build/toolchain change.
