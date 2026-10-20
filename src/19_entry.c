/* B19 对外接口函数
 *
 * crypto_core_multsntrup653.c 第 1387–1434 行，共 48 行。
 * 分块视图，不单独编译；去掉本注释后按 B01–B19 顺序拼接即为原文件，
 * tools/join.py 可校验 SHA-256。
 */
/* 以上为视图注释，以下为原文件内容 */
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
    int clow[LOW_N];
    int i, m;

    init_tables();
    crypto_decode_pxint16(f, inbytes);
    for (i = 0; i < p; ++i) f[i] = Fq_freeze(f[i]);
    for (i = 0; i < p; ++i) {
        small gi = (small)kbytes[i];
        small gi0 = gi & 1;
        g[i] = (small)(gi0 - (gi & (gi0 << 1)));
    }

    blockify_A(Ablk, f);
    blockify_B(Bblk, g);
#if SNTRUP653_USE_INPLACE_BUFFERS
    good_thomas_77_ntt_zs_neon(Ablk, Ablk, 0, SNTRUP653_PUBLIC_ZS_MODE);
    good_thomas_77_ntt_zs_neon(Bblk, Bblk, 0, SNTRUP653_PUBLIC_ZS_MODE);
    for (m = 0; m < BLOCKS; m++) weighted_mul16(Ablk[m], Ablk[m], Bblk[m], m);
    good_thomas_77_ntt_zs_neon(Ablk, Ablk, 1, 0);
#else
    good_thomas_77_ntt_zs_neon(Ahat, Ablk, 0, SNTRUP653_PUBLIC_ZS_MODE);
    good_thomas_77_ntt_zs_neon(Bhat, Bblk, 0, SNTRUP653_PUBLIC_ZS_MODE);
    for (m = 0; m < BLOCKS; m++) weighted_mul16(Hhat[m], Ahat[m], Bhat[m], m);
    good_thomas_77_ntt_zs_neon(Cblk, Hhat, 1, 0);
#endif

    low_part73_neon(clow, f, g);
#if SNTRUP653_USE_INPLACE_BUFFERS
    final_folding_neon(out, Ablk, clow);
#else
    final_folding_neon(out, Cblk, clow);
#endif

    crypto_encode_pxint16(outbytes, out);
    return 0;
}
