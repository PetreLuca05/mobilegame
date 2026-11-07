/* Minimal test runner: each suite returns its failure count. */
#include <stdio.h>

int test_kinematic(void);

int main(void)
{
    int failures = 0;
    failures += test_kinematic();
    if (failures) {
        printf("\n%d check(s) FAILED\n", failures);
        return 1;
    }
    printf("\nall checks passed\n");
    return 0;
}
