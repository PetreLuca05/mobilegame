/* Tiny secure key/value store for the auth refresh token.
 *   iOS:     Keychain (kSecClassGenericPassword, this device only)
 *   Android: AES-GCM with a key held in the Android Keystore, stored in
 *            SharedPreferences (ro.docvex.mobilegame.SecureStore, via JNI)
 *   desktop: a file in the SDL pref path (per --profile)
 * Thread-safe; called from the network thread. */
#ifndef MG_SECURE_STORE_H
#define MG_SECURE_STORE_H

#include <stdbool.h>
#include <stddef.h>

typedef struct SecureStore {
    char dir[512];     /* desktop */
    char profile[32];  /* desktop */
    void *java_class;  /* Android: global ref to ro.docvex.mobilegame.SecureStore */
    void *m_get, *m_set, *m_remove;
} SecureStore;

/* Call once on the main thread at startup (Android caches JNI refs). */
void secure_store_init(SecureStore *s, const char *profile);
bool secure_store_get(const SecureStore *s, const char *key, char *out, size_t cap);
bool secure_store_set(const SecureStore *s, const char *key, const char *value);
bool secure_store_remove(const SecureStore *s, const char *key);

#endif
