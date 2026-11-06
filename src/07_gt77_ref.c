static SNTRUP_MAYBE_UNUSED void good_thomas_77_ntt_zs(Blocks77 out, const Blocks77 in, int invert, int public_zs_forward) {
    const int (*P7tab)[7] = invert ? POW7_I : POW7_F;
    const int (*P11tab)[11] = invert ? POW11_I : POW11_F;
    MatrixGT tmp SNTRUP_ALIGN64;
    int s, u, v, r, c, t, k;

    if (public_zs_forward) {
        for (r = 0; r < 7; r++) {
            int ns = ACTIVE_COLS_PER_R[r];
            for (v = 0; v < 11; v++) {
                int64_t acc[BS] = {0};
                for (t = 0; t < ns; t++) {
                    int ss = ACTIVE_COL_LIST[r][t];
                    int idx = GT_IDX_RS[ss][r];
                    int coeff = P11tab[v][ss];
                    for (c = 0; c < BS; c++) acc[c] += (int64_t)in[idx][c] * coeff;
                }
                for (c = 0; c < BS; c++) tmp[r][v][c] = mod_q_i64_raw(acc[c]);
            }
        }
    } else {
        for (r = 0; r < 7; r++) {
            for (v = 0; v < 11; v++) {
                int64_t acc[BS] = {0};
                for (s = 0; s < 11; s++) {
                    int idx = GT_IDX_RS[s][r];
                    int coeff = P11tab[v][s];
                    for (c = 0; c < BS; c++) acc[c] += (int64_t)in[idx][c] * coeff;
                }
                for (c = 0; c < BS; c++) tmp[r][v][c] = mod_q_i64_raw(acc[c]);
            }
        }
    }

    /* Second layer: 11 independent 7-point direct DFTs. */
    for (u = 0; u < 7; u++) {
        for (v = 0; v < 11; v++) {
            int64_t acc[BS] = {0};
            k = GT_K_UV[u][v];
            for (r = 0; r < 7; r++) {
                int coeff = P7tab[u][r];
                for (c = 0; c < BS; c++) acc[c] += (int64_t)tmp[r][v][c] * coeff;
            }
            for (c = 0; c < BS; c++) {
                int val = mod_q_i64_raw(acc[c]);
                if (invert) val = mul_q_raw(val, INV77);
                out[k][c] = val;
            }
        }
    }
}


/* NEON Good--Thomas helpers vectorize across the 16 lanes in each block. */

#if defined(__ARM_NEON) || defined(__ARM_NEON__)
