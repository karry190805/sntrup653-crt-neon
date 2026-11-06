#ifndef SNTRUP653_CORE_H
#define SNTRUP653_CORE_H

/* Public entry point of the delivered multiplication core. */
void PQCLEAN_SNTRUP653_CLEAN_crypto_core_multsntrup653(
    unsigned char *outbytes, const unsigned char *inbytes,
    const unsigned char *kbytes);

#endif
