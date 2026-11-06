static void blockify_A_bytes(Blocks77 out, Fq f[p], const unsigned char *inbytes) {
    int i;
    int *dst = &out[0][0];
    blockify_clear_padding(out);
#if defined(__ARM_NEON) || defined(__ARM_NEON__)
    {
        /* A signed 16-bit input only spans x/q in [-7.09, 7.09], so the
         * 16-bit Shoup/Barratt pair with M = floor(2^15/q) = 7 satisfies
         * qhat = round(x*M/2^15) in {floor(x/q), floor(x/q)+1} over the
         * whole int16 range.  Then r = x - qhat*q is in (-q,q) and one
         * unsigned min canonicalises it, all on eight lanes at a time. */
        const uint16x8_t qv = vdupq_n_u16((uint16_t)q);
        const int16x8_t mu = vdupq_n_s16((int16_t)(((uint32_t)1 << 15) / (uint32_t)q));
        for (i = 0; i + 8 <= p; i += 8) {
            int16x8_t x = vreinterpretq_s16_u16(
                vreinterpretq_u16_u8(vld1q_u8(inbytes + 2 * i)));
            int16x8_t qhat = vqrdmulhq_s16(x, mu);
            uint16x8_t u = vreinterpretq_u16_s16(vmlsq_s16(x, qhat,
                                             vreinterpretq_s16_u16(qv)));
            int16x8_t r = vreinterpretq_s16_u16(vminq_u16(u, vaddq_u16(u, qv)));
            vst1q_s32(dst + i, vmovl_s16(vget_low_s16(r)));
            vst1q_s32(dst + i + 4, vmovl_high_s16(r));
            vst1q_s16(&f[i], r);
        }
    }
#endif
    for (; i < p; i++) {
        int v = (int16_t)(inbytes[2 * i] | ((unsigned)inbytes[2 * i + 1] << 8));
        int r = mod_q_i64_raw(v);
        dst[i] = r;
        f[i] = (Fq)r;
    }
}

int PQCLEAN_SNTRUP653_CLEAN_crypto_core_multsntrup653(unsigned char *outbytes, const unsigned char *inbytes, const unsigned char *kbytes) {
    Fq f[p], out[p];
    small g[p];
    Blocks77 Ablk SNTRUP_ALIGN64;
    Blocks77 Bblk SNTRUP_ALIGN64;
#if !SNTRUP653_USE_INPLACE_BUFFERS
    Blocks77 Ahat SNTRUP_ALIGN64;
    Blocks77 Bhat SNTRUP_ALIGN64;
    Blocks77 Hhat SNTRUP_ALIGN64;
    Blocks77 Cblk SNTRUP_ALIGN64;
#endif
    int clow[LOW_NP];
    int i, m;
#if SNTRUP653_FAST16
    Blocks16 A16 SNTRUP_ALIGN64;
    Blocks16 B16 SNTRUP_ALIGN64;
#endif

    init_tables();
#if SNTRUP653_FAST16
    SNTRUP_STAGE_MARK(blockify_A);
    blockify_A_bytes16(A16, f, inbytes);
#else
    blockify_A_bytes(Ablk, f, inbytes);
#endif
    /* The key decode runs here, so it belongs to the blockify_B row. */
    SNTRUP_STAGE_MARK(blockify_B);
    for (i = 0; i < p; ++i) {
        small gi = (small)kbytes[i];
        small gi0 = gi & 1;
        g[i] = (small)(gi0 - (gi & (gi0 << 1)));
    }

#if SNTRUP653_FAST16
    blockify_B16(B16, g);
    good_thomas_77_ntt_fwd16(A16, A16, SNTRUP653_PUBLIC_ZS_MODE);
    good_thomas_77_ntt_fwd16(B16, B16, SNTRUP653_PUBLIC_ZS_MODE);
    SNTRUP_STAGE_MARK(pointwise);
    for (m = 0; m + 3 < BLOCKS; m += 4) {
        weighted_mul16_scaled16(A16[m], A16[m], B16[m], m, 1);
        weighted_mul16_scaled16(A16[m + 1], A16[m + 1], B16[m + 1], m + 1, 1);
        weighted_mul16_scaled16(A16[m + 2], A16[m + 2], B16[m + 2], m + 2, 1);
        weighted_mul16_scaled16(A16[m + 3], A16[m + 3], B16[m + 3], m + 3, 1);
    }
    weighted_mul16_scaled16(A16[BLOCKS - 1], A16[BLOCKS - 1],
                            B16[BLOCKS - 1], BLOCKS - 1, 1);
    good_thomas_77_ntt_inv16(Ablk, A16);
#else
    blockify_B(Bblk, g);
#if SNTRUP653_USE_INPLACE_BUFFERS
    good_thomas_77_ntt_zs_neon(Ablk, Ablk, 0, SNTRUP653_PUBLIC_ZS_MODE, 0, 0, 1);
    good_thomas_77_ntt_zs_neon(Bblk, Bblk, 0, SNTRUP653_PUBLIC_ZS_MODE, 0, 0, 1);
    for (m = 0; m + 3 < BLOCKS; m += 4) {
        weighted_mul16_scaled(Ablk[m], Ablk[m], Bblk[m], m, 1);
        weighted_mul16_scaled(Ablk[m + 1], Ablk[m + 1], Bblk[m + 1], m + 1, 1);
        weighted_mul16_scaled(Ablk[m + 2], Ablk[m + 2], Bblk[m + 2], m + 2, 1);
        weighted_mul16_scaled(Ablk[m + 3], Ablk[m + 3], Bblk[m + 3], m + 3, 1);
    }
    weighted_mul16_scaled(Ablk[BLOCKS - 1], Ablk[BLOCKS - 1],
                          Bblk[BLOCKS - 1], BLOCKS - 1, 1);
    good_thomas_77_ntt_zs_neon(Ablk, Ablk, 1, 0, 0, SNTRUP653_FINAL_FOLD_NEON, 0);
#else
    good_thomas_77_ntt_zs_neon(Ahat, Ablk, 0, SNTRUP653_PUBLIC_ZS_MODE, 0, 0, 1);
    good_thomas_77_ntt_zs_neon(Bhat, Bblk, 0, SNTRUP653_PUBLIC_ZS_MODE, 0, 0, 1);
    for (m = 0; m < BLOCKS; m++) weighted_mul16_scaled(Hhat[m], Ahat[m], Bhat[m], m, 1);
    good_thomas_77_ntt_zs_neon(Cblk, Hhat, 1, 0, 0, SNTRUP653_FINAL_FOLD_NEON, 0);
#endif
#endif

    SNTRUP_STAGE_MARK(low73);
    low_part73_neon(clow, f, g);
    SNTRUP_STAGE_MARK(final_fold);
#if SNTRUP653_USE_INPLACE_BUFFERS
    final_folding_neon(out, Ablk, clow);
#else
    final_folding_neon(out, Cblk, clow);
#endif
    SNTRUP_STAGE_MARK(encode);
    crypto_encode_pxint16(outbytes, out);
    SNTRUP_STAGE_MARK(end);
    return 0;
}
