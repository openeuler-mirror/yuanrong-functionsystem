# Tasks: Generic LRU Cache Module

**Input**: Design documents from `/specs/001-generic-lru-module/`
**Prerequisites**: plan.md (required), spec.md (required), research.md, data-model.md, contracts/

**Tests**: Included — spec success criteria explicitly require unit tests (SC-002: 10+ eviction scenarios, SC-003: TSan verification).

**Organization**: Tasks grouped by user story for independent implementation and testing.

## Format: `[ID] [P?] [Story] Description`

- **[P]**: Can run in parallel (different files, no dependencies)
- **[Story]**: Which user story this task belongs to (e.g., US1, US2, US3)
- Include exact file paths in descriptions

## Path Conventions

- **Source**: `functionsystem/src/common/lru/`
- **Tests**: `functionsystem/tests/unit/common/lru/`
- **Specs**: `specs/001-generic-lru-module/`

---

## Phase 1: Setup (Shared Infrastructure)

**Purpose**: Create directory structure, CMake config, and header scaffolding

- [x] T001 Create directory `functionsystem/src/common/lru/` and add `CMakeLists.txt` as INTERFACE library (no .cpp sources, header-only); register with `add_subdirectory(lru)` in `functionsystem/src/common/CMakeLists.txt`
- [x] T002 Create header guard scaffold for `functionsystem/src/common/lru/lru_cache.h` with `namespace functionsystem`, includes (`<list>`, `<unordered_map>`, `<optional>`, `<functional>`, `<cstddef>`, `<utility>`), and empty `LruCache<K, V>` class template declaration
- [x] T003 [P] Create header guard scaffold for `functionsystem/src/common/lru/thread_safe_lru_cache.h` with `namespace functionsystem`, include `<mutex>` and `"lru_cache.h"`, and empty `ThreadSafeLruCache<K, V>` class template declaration
- [x] T004 [P] Create directory `functionsystem/tests/unit/common/lru/` and empty `lru_cache_test.cpp` with GTest include and `namespace functionsystem::test`; register test sources in `functionsystem/tests/unit/common/CMakeLists.txt` via `aux_source_directory`

**Checkpoint**: Project compiles with empty LRU module; `bash run.sh build -j 4` succeeds

---

## Phase 2: Foundational (Blocking Prerequisites)

**Purpose**: Core data structure and private members that all user stories depend on

**⚠️ CRITICAL**: No user story work can begin until this phase is complete

- [x] T005 Define private type aliases (`ListType`, `ListIterator`, `EvictionCallback`) and private members (`member_capacity`, `member_list`, `member_map`, `member_onEvict`) in `LruCache<K, V>` in `functionsystem/src/common/lru/lru_cache.h` per data-model.md
- [x] T006 Implement constructor `LruCache(size_t capacity, EvictionCallback onEvict = nullptr)` with capacity > 0 assertion in `functionsystem/src/common/lru/lru_cache.h`; delete copy constructor/assignment; default move constructor/assignment
- [x] T007 Implement private helper method `EvictLru()` that removes the back entry from `member_list`, erases from `member_map`, and invokes `member_onEvict` with `(key, std::move(value))` wrapped in try-catch for exception safety, in `functionsystem/src/common/lru/lru_cache.h`
- [x] T008 Implement `Size()` and `Capacity()` const query methods in `functionsystem/src/common/lru/lru_cache.h`

**Checkpoint**: Foundation ready — `LruCache` has internal structure, constructor, and eviction helper

---

## Phase 3: User Story 1 — Typed LRU Cache (Priority: P1) 🎯 MVP

**Goal**: Generic template LRU cache with Put/Get/Contains/Peek/Remove/Clear and correct LRU eviction

**Independent Test**: Create `LruCache<std::string, int>`, insert items beyond capacity, verify LRU eviction order

### Tests for User Story 1

> **NOTE: Write these tests FIRST, ensure they FAIL before implementation**

- [x] T009 [US1] Write GTest `PutAndGet_BasicInsertRetrieve` in `functionsystem/tests/unit/common/lru/lru_cache_test.cpp`: insert 3 items into `LruCache<std::string, int>(5)`, verify Get returns correct values
- [x] T010 [P] [US1] Write GTest `Put_EvictsLruWhenCapacityExceeded` in `functionsystem/tests/unit/common/lru/lru_cache_test.cpp`: insert N+1 items into cache of capacity N, verify oldest item evicted
- [x] T011 [P] [US1] Write GTest `Get_PromotesToMru` in `functionsystem/tests/unit/common/lru/lru_cache_test.cpp`: insert A, B, C; Get(A); insert D (capacity 3); verify B evicted (not A)
- [x] T012 [P] [US1] Write GTest `Get_MissReturnsNullopt` in `functionsystem/tests/unit/common/lru/lru_cache_test.cpp`: verify Get on non-existent key returns std::nullopt
- [x] T013 [P] [US1] Write GTest `Contains_DoesNotPromote` in `functionsystem/tests/unit/common/lru/lru_cache_test.cpp`: insert A, B, C; Contains(A); insert D (capacity 3); verify A evicted (Contains did not promote)
- [x] T014 [P] [US1] Write GTest `Peek_DoesNotPromote` in `functionsystem/tests/unit/common/lru/lru_cache_test.cpp`: insert A, B, C; Peek(A); insert D (capacity 3); verify A evicted
- [x] T015 [P] [US1] Write GTest `Put_UpdatesExistingKey` in `functionsystem/tests/unit/common/lru/lru_cache_test.cpp`: Put same key twice with different values; verify Get returns new value and size unchanged
- [x] T016 [P] [US1] Write GTest `Remove_DeletesEntry` in `functionsystem/tests/unit/common/lru/lru_cache_test.cpp`: insert items, Remove one, verify Contains returns false and Size decremented
- [x] T017 [P] [US1] Write GTest `Clear_RemovesAllEntries` in `functionsystem/tests/unit/common/lru/lru_cache_test.cpp`: insert items, Clear, verify Size is 0
- [x] T018 [P] [US1] Write GTest `Constructor_RejectsZeroCapacity` in `functionsystem/tests/unit/common/lru/lru_cache_test.cpp`: verify constructing with capacity 0 triggers assertion/exception
- [x] T019 [P] [US1] Write GTest `MoveOnlyValues` in `functionsystem/tests/unit/common/lru/lru_cache_test.cpp`: use `LruCache<int, std::unique_ptr<std::string>>`, verify Put/Get/eviction work with move-only types

### Implementation for User Story 1

- [x] T020 [US1] Implement `Put(const K& key, V value)` in `functionsystem/src/common/lru/lru_cache.h`: check existing key (update + splice to front), or insert new (evict if full, emplace_front, add to map); return true if new insert, false if update
- [x] T021 [US1] Implement `Get(const K& key)` in `functionsystem/src/common/lru/lru_cache.h`: lookup in map, splice to front if found, return `std::optional<std::reference_wrapper<V>>`
- [x] T022 [P] [US1] Implement `Contains(const K& key) const` in `functionsystem/src/common/lru/lru_cache.h`: return `member_map.count(key) > 0`
- [x] T023 [P] [US1] Implement `Peek(const K& key) const` in `functionsystem/src/common/lru/lru_cache.h`: lookup without splice, return `std::optional<std::reference_wrapper<const V>>`
- [x] T024 [US1] Implement `Remove(const K& key)` in `functionsystem/src/common/lru/lru_cache.h`: erase from map and list; return true if found; do NOT invoke eviction callback
- [x] T025 [US1] Implement `Clear()` in `functionsystem/src/common/lru/lru_cache.h`: clear both map and list; do NOT invoke eviction callback
- [x] T026 [US1] Run all US1 tests and verify they pass: `bash run.sh test` (or run specific lru tests)

**Checkpoint**: `LruCache<K,V>` is fully functional with all core operations and 11 passing unit tests

---

## Phase 4: User Story 2 — Thread-Safe Access (Priority: P2)

**Goal**: `ThreadSafeLruCache<K,V>` wrapper with mutex protection for concurrent access

**Independent Test**: Launch multiple threads doing concurrent Put/Get on a shared `ThreadSafeLruCache`, verify no crashes or TSan violations

### Tests for User Story 2

- [x] T027 [US2] Write GTest `ThreadSafe_BasicOperations` in `functionsystem/tests/unit/common/lru/lru_cache_test.cpp`: verify `ThreadSafeLruCache` Put/Get/Remove/Clear work correctly in single-threaded context (same as US1 basics but through wrapper)
- [x] T028 [P] [US2] Write GTest `ThreadSafe_ConcurrentPutGet` in `functionsystem/tests/unit/common/lru/lru_cache_test.cpp`: spawn 4 threads doing 1000 Put+Get operations each on shared `ThreadSafeLruCache<int, int>(100)`, verify no crashes
- [x] T029 [P] [US2] Write GTest `ThreadSafe_SizeNeverExceedsCapacity` in `functionsystem/tests/unit/common/lru/lru_cache_test.cpp`: concurrent writers inserting items, periodically check `Size() <= Capacity()`

### Implementation for User Story 2

- [x] T030 [US2] Implement `ThreadSafeLruCache<K,V>` in `functionsystem/src/common/lru/thread_safe_lru_cache.h`: private members `member_cache` (`LruCache<K,V>`) and `member_mutex` (`mutable std::mutex`); constructor delegates to `LruCache`; delete copy/move
- [x] T031 [US2] Implement all public methods (`Put`, `Get`, `Contains`, `Peek`, `Remove`, `Clear`, `Size`, `Capacity`) in `ThreadSafeLruCache`, each acquiring `std::lock_guard<std::mutex>` then delegating to `member_cache`; note `Get`/`Peek` return `std::optional<V>` (copy) instead of reference for thread safety
- [x] T032 [US2] Run all US2 tests and verify they pass including TSan: compile with `-fsanitize=thread` or run under thread sanitizer

**Checkpoint**: `ThreadSafeLruCache<K,V>` works correctly under concurrent access with no data races

---

## Phase 5: User Story 3 — Custom Eviction Callback (Priority: P3)

**Goal**: Optional eviction callback invoked with evicted key and moved value for async cleanup

**Independent Test**: Register a callback that records evicted entries, insert items beyond capacity, verify callback invoked with correct key-value pairs

### Tests for User Story 3

- [x] T033 [US3] Write GTest `EvictionCallback_InvokedOnCapacityOverflow` in `functionsystem/tests/unit/common/lru/lru_cache_test.cpp`: register callback that pushes evicted pairs to a vector, insert N+1 items, verify callback received the correct LRU entry
- [x] T034 [P] [US3] Write GTest `EvictionCallback_ReceivesMovedValue` in `functionsystem/tests/unit/common/lru/lru_cache_test.cpp`: use `LruCache<int, std::unique_ptr<std::string>>` with callback, verify callback receives valid moved unique_ptr
- [x] T035 [P] [US3] Write GTest `EvictionCallback_NotInvokedOnExplicitRemove` in `functionsystem/tests/unit/common/lru/lru_cache_test.cpp`: register callback, call Remove(key), verify callback NOT invoked
- [x] T036 [P] [US3] Write GTest `EvictionCallback_NotInvokedOnClear` in `functionsystem/tests/unit/common/lru/lru_cache_test.cpp`: register callback, call Clear(), verify callback NOT invoked
- [x] T037 [P] [US3] Write GTest `EvictionCallback_ExceptionDoesNotCorruptState` in `functionsystem/tests/unit/common/lru/lru_cache_test.cpp`: register callback that throws, verify cache state remains consistent after eviction (size correct, other entries accessible)
- [x] T038 [P] [US3] Write GTest `NoCallback_EvictionWorksNormally` in `functionsystem/tests/unit/common/lru/lru_cache_test.cpp`: create cache with no callback (nullptr), insert beyond capacity, verify eviction occurs without error

### Implementation for User Story 3

- [x] T039 [US3] Verify eviction callback integration in `EvictLru()` (T007) correctly invokes `member_onEvict` with `(key, std::move(value))` and handles exceptions; adjust if tests reveal issues in `functionsystem/src/common/lru/lru_cache.h`
- [x] T040 [US3] Verify `ThreadSafeLruCache` propagates eviction callback through to underlying `LruCache` in `functionsystem/src/common/lru/thread_safe_lru_cache.h`
- [x] T041 [US3] Run all US3 tests and verify they pass

**Checkpoint**: Eviction callbacks work correctly, including exception safety and move-only values

---

## Phase 6: Polish & Cross-Cutting Concerns

**Purpose**: Final validation, edge case coverage, build integration

- [x] T042 [P] Verify `bash run.sh build -j 4` compiles cleanly with no warnings for LRU module
- [x] T043 [P] Run `.clang-format` on all LRU header files in `functionsystem/src/common/lru/`
- [x] T044 Run full test suite `bash run.sh test` and verify all LRU tests pass (14+ tests total)
- [x] T045 Validate quickstart.md examples compile and work by writing a brief integration snippet in test file

---

## Dependencies & Execution Order

### Phase Dependencies

- **Setup (Phase 1)**: No dependencies — can start immediately
- **Foundational (Phase 2)**: Depends on Setup completion — BLOCKS all user stories
- **US1 (Phase 3)**: Depends on Foundational (Phase 2)
- **US2 (Phase 4)**: Depends on US1 completion (needs working `LruCache` to wrap)
- **US3 (Phase 5)**: Depends on Foundational (Phase 2); can run in parallel with US1 since callback mechanism is in `EvictLru()` (T007)
- **Polish (Phase 6)**: Depends on all user stories being complete

### User Story Dependencies

- **US1 (P1)**: Depends on Phase 2 only — no other story dependencies
- **US2 (P2)**: Depends on US1 — wraps `LruCache` which must be implemented first
- **US3 (P3)**: Depends on Phase 2 — eviction callback is implemented in foundational `EvictLru()` helper; tests verify it works end-to-end

### Within Each User Story

- Tests MUST be written and FAIL before implementation
- Implementation tasks in listed order (some marked [P] for parallel)
- Run verification task at end of each story

### Parallel Opportunities

- T002 and T003 can run in parallel (different header files)
- T003 and T004 can run in parallel (different directories)
- All US1 test tasks (T009–T019) can run in parallel (same file but independent test cases)
- T022 and T023 can run in parallel (independent const methods)
- US3 tests (T033–T038) can run in parallel
- Phase 6 tasks T042 and T043 can run in parallel

---

## Parallel Example: User Story 1

```text
# Write all US1 tests in parallel (same file, independent test cases):
T009: PutAndGet_BasicInsertRetrieve
T010: Put_EvictsLruWhenCapacityExceeded
T011: Get_PromotesToMru
T012: Get_MissReturnsNullopt
T013: Contains_DoesNotPromote
T014: Peek_DoesNotPromote
T015: Put_UpdatesExistingKey
T016: Remove_DeletesEntry
T017: Clear_RemovesAllEntries
T018: Constructor_RejectsZeroCapacity
T019: MoveOnlyValues

# Then implement core methods (some in parallel):
T020: Put  →  T021: Get  (sequential, Get depends on list structure from Put)
T022: Contains  ||  T023: Peek  (parallel, independent const methods)
T024: Remove  →  T025: Clear  (sequential)
T026: Run all tests
```

---

## Implementation Strategy

### MVP First (User Story 1 Only)

1. Complete Phase 1: Setup (T001–T004)
2. Complete Phase 2: Foundational (T005–T008)
3. Complete Phase 3: User Story 1 (T009–T026)
4. **STOP and VALIDATE**: All 11 US1 tests pass; `LruCache<K,V>` works
5. Can be used immediately by callers who don't need thread safety or callbacks

### Incremental Delivery

1. Setup + Foundational → Build compiles
2. US1 → `LruCache<K,V>` works → MVP usable by single-threaded actors
3. US2 → `ThreadSafeLruCache<K,V>` works → Usable in multi-threaded contexts
4. US3 → Eviction callbacks work → Async cleanup pattern enabled
5. Polish → Production-ready

---

## Notes

- All source is header-only (`.h` files only, no `.cpp` in `src/common/lru/`)
- Tests use GTest/GMock framework already configured in the project
- Follow C++ naming conventions: small camelCase, `member_` prefix, ALL_CAPS constants
- Commit after each phase checkpoint
- Total: 45 tasks (4 setup, 4 foundational, 18 US1, 6 US2, 9 US3, 4 polish)
