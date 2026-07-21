# Test Plan — 스펙 추적표

기준 문서: **SK hynix DDR5 SDRAM 3DS RDIMM datasheet**
(HMCT04MEERAxxxN / HMCT14MEERAxxxN, Based on 16Gb M-die, Rev 1.0, Aug. 2021)

> On-Die ECC의 내부 구조(128 데이터 + 8 패리티, SEC)와 ECS의 세부 동작은
> 모듈 datasheet에는 feature 항목으로만 존재하며, JEDEC JESD79-5 정의를 따랐다.

## 시나리오 ↔ 스펙 추적표

| TC | 스펙 근거 | 검증 내용 | 구현 |
|---|---|---|---|
| tc1 | p.4 Address Table (BG0~BG2, BA0~BA1, R0~R15, C0~C9, 1KB page) | 비트필드 주소 인코드/디코드 라운드트립 32조합 | `core/dram_model.c`, `host/main.c` |
| tc2 | p.4 Address Table | BG/BA 위치별 고유 패턴으로 주소 aliasing 검사 | `core/memory_test.c` |
| tc3 | — | 32비트 read/write 기본 동작 | `host/main.c` |
| tc4 | — | 무결함 베이스라인 패턴 테스트 (PASS + 정정 0회) | `core/memory_test.c` |
| tc5 | p.3 "On-Die ECC" (+ JESD79-5) | **escape 재현**: 1비트 셀 결함이 정정되어 테스트 통과, 흔적은 정정 카운터뿐 | `core/odecc.c`, `core/dram_model.c` |
| tc6 | p.3 "On-Die ECC" | stuck-at(hard fault)도 escape됨을 확인 | `core/dram_model.c` |
| tc7 | p.3 "ECC Transparency and Error Scrub" (JESD79-5의 ECS) | **scrub 스크린**: 반복 scrub으로 soft(치유됨)/hard(재발) 분류 | `dram_scrub_range()`, `host/main.c` |
| tc8 | JESD79-5 SEC 한계 | 2비트 결함 → 오정정(제3비트 오염, 카운터가 정정으로 오집계) 또는 정정 불가 검출 | `core/odecc.c` |
| tc9 | van de Goor 표준 | March C- 6요소 {up(w0); up(r0,w1); up(r1,w0); down(r0,w1); down(r1,w0); up(r0)} 전 영역 실행 | `core/memory_test.c` |
| tc10 | 결함 모델 이론 (SAF/TF/CF) | **커버리지 매트릭스** 자동 생성: 5개 결함 모델 × 2개 알고리즘 검출표. ODECC를 끄고 순수 알고리즘 검출력을 측정 — constant는 SA0/TF↓/CF를 놓치고 March C-는 전부 검출 | `core/dram_model.c` (쓰기 경로 결함), `host/main.c` |
| tc11 | p.23 §1.3 (tREFI 3.9us×8192=tREF 32ms, Table 2: 85<Tcase≤95°C 시 tREFI/2) | **retention/refresh**: refresh를 놓치면 약한 셀(40ms)이 방전(1→0). 25°C에선 tREFW 주기로 유지되지만 95°C에선 버티는 시간이 절반(20ms)이라 2배 refresh(16ms)여야 유지. ODECC off | `core/dram_model.c`, `host/main.c` |
| shmoo (`--test shmoo`) | p.23 Table 2 | **shmoo 스윕**: 온도(25~95°C) × refresh 주기(4~44ms) 88칸 PASS/FAIL 지도. 경계가 40ms에 서 있다가 85°C를 넘는 순간 20ms로 계단 하강 — Table 2의 규칙이 지도에 그대로 보임. 이론 대조 self-check 포함 | `host/main.c` |

## CSV 로그 스키마 (`dram_test_results.csv`)

```
test_id,result,start_address,length_bytes,pattern,
words_tested,error_count,first_fail_address,first_expected,first_actual,
ecc_corr,ecc_uncorr,note
```

- `ecc_corr` / `ecc_uncorr`: 해당 행 기록 시점의 ECC 정정/정정불가 카운터.
  **result=PASS인데 ecc_corr>0인 행이 escape** — GUI가 이 조합을 강조해야 한다.
- scrub 행은 `error_count`에 이벤트 수, `first_fail_address`에 첫 이벤트 주소를 기록.
- retention 행(tc11)은 `note`에 조건을 남긴다. `result=FAIL`은 **의도된 데이터 손실 검출**(방전이 나야 정상), `result=PASS`는 refresh가 데이터를 지킨 경우다.
- 16진수 필드의 `="0x..."` 래핑은 엑셀 자동 변환 방지용. 파서는 벗겨서 읽는다.

## 모델링 범위

실물 DRAM을 두 층으로 나눴을 때, 이 시뮬레이터는 **셀/데이터 층**(전하 누설,
ECC, 결함이 데이터를 깨는 세계)만 모델링한다. **명령/프로토콜 층**(명령 타이밍,
MR 협상)은 테스트 알고리즘의 검출력 검증에 불필요해서 뺐다. p.23 기준 예시:

| 스펙 개념 | 코드 | 반영 |
|---|---|---|
| tREF=32ms (전 row 한 바퀴) | `DRAM_TREFW_NS`, tc11 | O |
| Tcase>85°C → refresh 2배 | `dram_advance_time()`의 실효 retention 절반 | O |
| REFab 명령 | `dram_refresh()` | 효과만 — 1회 호출 = 8,192회 REFab의 결과 압축 |
| Tcase | `dram_set_temperature()` | 함수로 대체 (실물은 MR 읽기) |
| tREFI 3.9us / tRFC 295ns | — | X 명령 스케줄링 층 |
| FGR, MR 공간, CA8 indicator | — | X 프로토콜 협상 층 |

## 예정 (로드맵)

| 항목 | 스펙 근거 | 내용 |
|---|---|---|
| UEFI 포팅 | — | 동일 core를 EDK2 앱으로 빌드, QEMU/OVMF 부팅 실행 |
| 모듈 레벨 ECC | p.7 CB0~CB7 (DIMM ECC check bits) | 칩 내부 ODECC와 별개의 2계층 ECC |
| 컨트롤러 주소 계층 | p.7 CS0~CS1 (Rank Select), p.6 3DS 2Hi/4Hi | 물리 주소 → CS/CID/BG/BA/ROW/COL 변환을 매핑 프로파일 플러그인으로: linear(현행) / 뱅크 XOR 해시(Intel풍, DRAMA 논문 역공학 기반) / 레지스터 파라미터형(AMD PPR풍). 같은 결함이 프로파일·interleave에 따라 흩어져 보이거나 한 칩으로 모임을 재현 — 테스트 BIOS가 interleave를 끄는 이유 |
| DQ 매핑 | p.14~15 모듈 구성도 | fail 주소+비트 → rank(CS)/층(CID)/불량 칩 위치 역산 (GUI) |
