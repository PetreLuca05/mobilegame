/* Minimal test runner: each suite returns its failure count. */
#include <stdio.h>
#include <string.h>

int test_kinematic(void);
int test_replication(void);
int test_interact(void);
int test_stick(void);
int test_stick_record(const char *path);

int main(int argc, char **argv)
{
    if (argc == 3 && strcmp(argv[1], "--record-stick") == 0) {
        return test_stick_record(argv[2]);
    }
    int failures = 0;
    failures += test_kinematic();
    failures += test_replication();
    failures += test_interact();
    failures += test_stick();
    if (failures) {
        printf("\n%d check(s) FAILED\n", failures);
        return 1;
    }
    printf("\nall checks passed\n");
    return 0;
}
