#include <rc_hash.h>
#include <rc_consoles.h>
#include <stdio.h>
#include <string.h>

/* Simulate StormLib's exported name. A full link must not alias or collide
 * with the independent implementation used by rcheevos hashing. */
static int other_md5_called;
void md5_init(void* unused) {
    (void)unused;
    other_md5_called = 1;
}

int main(void) {
    const unsigned char data[] = { 'a', 'b', 'c' };
    char hash[33] = { 0 };
    rc_hash_iterator_t iterator;
    rc_hash_initialize_iterator(&iterator, "synthetic.gb", data, sizeof(data));
    const int result = rc_hash_generate(hash, RC_CONSOLE_GAMEBOY, &iterator);
    rc_hash_destroy_iterator(&iterator);
    if (!result || strcmp(hash, "900150983cd24fb0d6963f7d28e17f72") != 0 || other_md5_called) {
        fputs("rcheevos MD5 isolation/hash regression\n", stderr);
        return 1;
    }
    md5_init(NULL);
    if (!other_md5_called) {
        return 1;
    }
    puts("MD5 link isolation and known digest passed");
    return 0;
}
