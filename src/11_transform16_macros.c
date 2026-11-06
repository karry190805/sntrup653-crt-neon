static inline void store_lazy_s16(int16_t *dst, int32x4_t lo, int32x4_t hi) {
#if USE_BARRETT_REDUCE
    const int32x4_t mu = vdupq_n_s32(BARRETT_MU_Q31);
    uint32x4_t rlo = vmlsq_n_u32(vreinterpretq_u32_s32(lo),
                                 vreinterpretq_u32_s32(vqdmulhq_s32(lo, mu)),
                                 (uint32_t)q);
    uint32x4_t rhi = vmlsq_n_u32(vreinterpretq_u32_s32(hi),
                                 vreinterpretq_u32_s32(vqdmulhq_s32(hi, mu)),
                                 (uint32_t)q);
#else
    uint32x4_t rlo = vmod_q_u32_barrett(vreinterpretq_u32_s32(lo));
    uint32x4_t rhi = vmod_q_u32_barrett(vreinterpretq_u32_s32(hi));
#endif
    vst1q_s16(dst, vcombine_s16(vmovn_s32(vreinterpretq_s32_u32(rlo)),
                                vmovn_s32(vreinterpretq_s32_u32(rhi))));
}

/* acc += x * c[lane], with c a table row replicated over eight lanes. */
#define SNTRUP_ACC16(ELO, EHI, OLO, OHI, SJ, DJ, PV, MV, LANE)                \
    do {                                                                      \
        ELO = vmlal_laneq_s16(ELO, vget_low_s16(SJ), PV, LANE);               \
        EHI = vmlal_high_laneq_s16(EHI, SJ, PV, LANE);                        \
        OLO = vmlal_laneq_s16(OLO, vget_low_s16(DJ), MV, LANE);               \
        OHI = vmlal_high_laneq_s16(OHI, DJ, MV, LANE);                        \
    } while (0)

#define SNTRUP_L1_LOAD16(RR, SS, VAR)                                         \
    int16x8_t SNTRUP_MAYBE_UNUSED VAR = SNTRUP_GT_ACT(RR, SS)                 \
        ? vld1q_s16(&in[SNTRUP_GT_IDX(RR, SS)][cbase])                        \
        : vdupq_n_s16(0);

#define SNTRUP_L1_DC16(RR, SS, VAR)                                           \
    if (SNTRUP_GT_ACT(RR, SS)) {                                              \
        acc0lo_ = vaddq_s32(acc0lo_, vmovl_s16(vget_low_s16(VAR)));            \
        acc0hi_ = vaddq_s32(acc0hi_, vmovl_high_s16(VAR));                     \
    }

/* One (j, 11-j) pair contribution to one output pair. */
#define SNTRUP_L1_PAIR16(RR, JJ, AJ, AM, PV, MV)                              \
    do {                                                                      \
        if (SNTRUP_GT_ACT(RR, JJ)) {                                          \
            if (SNTRUP_GT_ACT(RR, 11 - (JJ))) {                               \
                int16x8_t sj_ = vaddq_s16(AJ, AM);                            \
                int16x8_t dj_ = vreinterpretq_s16_u16(vminq_u16(              \
                    vreinterpretq_u16_s16(vsubq_s16(AJ, AM)),                 \
                    vaddq_u16(vreinterpretq_u16_s16(vsubq_s16(AJ, AM)),       \
                              vreinterpretq_u16_s16(qv_))));                  \
                SNTRUP_ACC16(elo_, ehi_, olo_, ohi_, sj_, dj_, PV, MV,        \
                             (JJ) - 1);                                       \
            } else {                                                          \
                SNTRUP_ACC16(elo_, ehi_, olo_, ohi_, AJ, AJ, PV, MV,          \
                             (JJ) - 1);                                       \
            }                                                                 \
        } else if (SNTRUP_GT_ACT(RR, 11 - (JJ))) {                            \
            SNTRUP_ACC16(elo_, ehi_, olo_, ohi_, AM, vsubq_s16(qv_, AM),      \
                         PV, MV, (JJ) - 1);                                   \
        }                                                                     \
    } while (0)

#define SNTRUP_L1_K16(RR, KV, PV, MV)                                         \
    do {                                                                      \
        int32x4_t elo_, ehi_, olo_, ohi_;                                     \
        elo_ = vmovl_s16(vget_low_s16(c0));                                   \
        ehi_ = vmovl_high_s16(c0);                                            \
        olo_ = vdupq_n_s32(0);                                                \
        ohi_ = vdupq_n_s32(0);                                                \
        SNTRUP_L1_PAIR16(RR, 1, p1, m1, PV, MV);                              \
        SNTRUP_L1_PAIR16(RR, 2, p2, m2, PV, MV);                              \
        SNTRUP_L1_PAIR16(RR, 3, p3, m3, PV, MV);                              \
        SNTRUP_L1_PAIR16(RR, 4, p4, m4, PV, MV);                              \
        SNTRUP_L1_PAIR16(RR, 5, p5, m5, PV, MV);                              \
        store_lazy_s16(&tmp[RR][KV][cbase], vaddq_s32(elo_, olo_),            \
                       vaddq_s32(ehi_, ohi_));                                \
        store_lazy_s16(&tmp[RR][11 - (KV)][cbase],                            \
                       vaddq_s32(vsubq_s32(elo_, olo_), qoff_),               \
                       vaddq_s32(vsubq_s32(ehi_, ohi_), qoff_));              \
    } while (0)

#define SNTRUP_L1_ROW16(RR)                                                   \
    do {                                                                      \
        for (cbase = 0; cbase < BS; cbase += 8) {                             \
            int32x4_t acc0lo_, acc0hi_;                                       \
            const int16x8_t qv_ = vdupq_n_s16((int16_t)q);                    \
            const int32x4_t qoff_ = vdupq_n_s32(q * PAIRWISE_QOFF_MULT);      \
            SNTRUP_L1_LOAD16(RR, 0, c0)                                       \
            SNTRUP_L1_LOAD16(RR, 1, p1)  SNTRUP_L1_LOAD16(RR, 10, m1)         \
            SNTRUP_L1_LOAD16(RR, 2, p2)  SNTRUP_L1_LOAD16(RR, 9, m2)          \
            SNTRUP_L1_LOAD16(RR, 3, p3)  SNTRUP_L1_LOAD16(RR, 8, m3)          \
            SNTRUP_L1_LOAD16(RR, 4, p4)  SNTRUP_L1_LOAD16(RR, 7, m4)          \
            SNTRUP_L1_LOAD16(RR, 5, p5)  SNTRUP_L1_LOAD16(RR, 6, m5)          \
            acc0lo_ = vdupq_n_s32(0);                                         \
            acc0hi_ = vdupq_n_s32(0);                                         \
            SNTRUP_L1_DC16(RR, 0, c0)                                         \
            SNTRUP_L1_DC16(RR, 1, p1)   SNTRUP_L1_DC16(RR, 2, p2)             \
            SNTRUP_L1_DC16(RR, 3, p3)   SNTRUP_L1_DC16(RR, 4, p4)             \
            SNTRUP_L1_DC16(RR, 5, p5)   SNTRUP_L1_DC16(RR, 6, m5)             \
            SNTRUP_L1_DC16(RR, 7, m4)   SNTRUP_L1_DC16(RR, 8, m3)             \
            SNTRUP_L1_DC16(RR, 9, m2)   SNTRUP_L1_DC16(RR, 10, m1)            \
            store_lazy_s16(&tmp[RR][0][cbase], acc0lo_, acc0hi_);             \
            {                                                                 \
                const int16x8_t pv1 = vld1q_s16(P11Q_F[1]);                   \
                const int16x8_t mv1 = vld1q_s16(M11Q_F[1]);                   \
                SNTRUP_L1_K16(RR, 1, pv1, mv1);                               \
            }                                                                 \
            {                                                                 \
                const int16x8_t pv2 = vld1q_s16(P11Q_F[2]);                   \
                const int16x8_t mv2 = vld1q_s16(M11Q_F[2]);                   \
                SNTRUP_L1_K16(RR, 2, pv2, mv2);                               \
            }                                                                 \
            {                                                                 \
                const int16x8_t pv3 = vld1q_s16(P11Q_F[3]);                   \
                const int16x8_t mv3 = vld1q_s16(M11Q_F[3]);                   \
                SNTRUP_L1_K16(RR, 3, pv3, mv3);                               \
            }                                                                 \
            {                                                                 \
                const int16x8_t pv4 = vld1q_s16(P11Q_F[4]);                   \
                const int16x8_t mv4 = vld1q_s16(M11Q_F[4]);                   \
                SNTRUP_L1_K16(RR, 4, pv4, mv4);                               \
            }                                                                 \
            {                                                                 \
                const int16x8_t pv5 = vld1q_s16(P11Q_F[5]);                   \
                const int16x8_t mv5 = vld1q_s16(M11Q_F[5]);                   \
                SNTRUP_L1_K16(RR, 5, pv5, mv5);                               \
            }                                                                 \
        }                                                                     \
    } while (0)

/* Dense first layer: every column present (used by the dense self test). */
