# AIMer v3 AVX2/AVX-512 LNCS paper draft

이 폴더는 기존 `AIMer_on_AVX512_LNCS`의 LNCS 구성만 참고하여 새로 작성한
AIMer v3 원고다. 기존 원고의 문장을 복사하지 않았고, 논문의 중심 주장을
“AIMer v2의 SIMD 최적화 원리를 AIM3 식을 보존하면서 AIMer v3에 이식”으로
재구성했다.

## 빌드

```bash
pdflatex samplepaper.tex
bibtex samplepaper
pdflatex samplepaper.tex
pdflatex samplepaper.tex
```

## 사용한 결과

- 최종 end-to-end 표:
  `../AIMer_v3/benchmarks/results/paper-final-locked-isolated-20260830-v2-tuned`
- kernel characterization:
  `../AIMer_v3/benchmarks/results/kernel-paper-final-irq-exclusive-20260901-v10-tuned`
- 정확성 근거:
  `../AIMer_v3/MATHEMATICAL_EQUIVALENCE_AVX512.md`

## 제출 전 확인사항

1. 저자·소속·이메일을 확정한다.
2. 이전 AIM2 AVX-512 원고의 최종 게재정보/DOI로 BibTeX를 교체한다.
3. kernel 데이터는 사전 기준을 아주 근소하게 통과하지 못했다(CV 5.03%,
   device IRQ 32회/2,970 cells). 재측정 후 Table 6을 교체하거나 부록의 예비
   결과로 유지한다.
4. 제출 직전에 AIMer v3 공식 저장소와 IACR에서 새 AVX2/AVX-512 선행연구를
   다시 검색한다.
