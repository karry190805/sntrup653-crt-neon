#include <stdio.h>
#include <stdlib.h>

 int sntrup653_internal_selftest(unsigned random_tests, unsigned seed);

int main(int argc, char **argv) {
    unsigned random_tests = 1000;
    unsigned seed = 1;
    int rc;
    if (argc >= 2) {
        char *end = NULL;
        unsigned long value = strtoul(argv[1], &end, 10);
        if (!argv[1][0] || *end != '\0' || value > 1000000UL) {
            fprintf(stderr, "usage: %s [random_tests<=1000000]\n", argv[0]);
            return 2;
        }
        random_tests = (unsigned)value;
    }
    if (argc >= 3) {
        char *end = NULL;
        unsigned long value = strtoul(argv[2], &end, 10);
        if (!argv[2][0] || *end != '\0' || value > 0xffffffffUL) {
            fprintf(stderr, "usage: %s [random_tests<=1000000] [seed]\n", argv[0]);
            return 2;
        }
        seed = (unsigned)value;
    }
    rc = sntrup653_internal_selftest(random_tests, seed);
    if (rc != 0) {
        fprintf(stderr, "internal construction selftest: FAIL code=%d\n", rc);
        return 1;
    }
    printf("internal construction selftest: PASS random_tests=%u seed=%u\n",
           random_tests, seed);
    return 0;
}
