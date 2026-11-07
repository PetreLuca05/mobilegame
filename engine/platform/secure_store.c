#include "secure_store.h"

#include <SDL3/SDL.h>
#include <string.h>

#include "core/log.h"

#if defined(MG_PLATFORM_IOS)
/* ------------------------------------------------------------- Keychain */
#include <CoreFoundation/CoreFoundation.h>
#include <Security/Security.h>

#define KC_SERVICE "ro.docvex.mobilegame"

void secure_store_init(SecureStore *s, const char *profile)
{
    (void)s;
    (void)profile;
}

static CFMutableDictionaryRef kc_query(const char *key)
{
    CFMutableDictionaryRef q =
        CFDictionaryCreateMutable(NULL, 0, &kCFTypeDictionaryKeyCallBacks, &kCFTypeDictionaryValueCallBacks);
    CFStringRef service = CFStringCreateWithCString(NULL, KC_SERVICE, kCFStringEncodingUTF8);
    CFStringRef account = CFStringCreateWithCString(NULL, key, kCFStringEncodingUTF8);
    CFDictionarySetValue(q, kSecClass, kSecClassGenericPassword);
    CFDictionarySetValue(q, kSecAttrService, service);
    CFDictionarySetValue(q, kSecAttrAccount, account);
    CFRelease(service);
    CFRelease(account);
    return q;
}

bool secure_store_get(const SecureStore *s, const char *key, char *out, size_t cap)
{
    (void)s;
    CFMutableDictionaryRef q = kc_query(key);
    CFDictionarySetValue(q, kSecReturnData, kCFBooleanTrue);
    CFDictionarySetValue(q, kSecMatchLimit, kSecMatchLimitOne);
    CFTypeRef result = NULL;
    OSStatus st = SecItemCopyMatching(q, &result);
    CFRelease(q);
    if (st != errSecSuccess || !result) {
        return false;
    }
    CFDataRef data = (CFDataRef)result;
    size_t n = (size_t)CFDataGetLength(data);
    bool ok = n + 1 <= cap;
    if (ok) {
        memcpy(out, CFDataGetBytePtr(data), n);
        out[n] = 0;
    }
    CFRelease(result);
    return ok;
}

bool secure_store_set(const SecureStore *s, const char *key, const char *value)
{
    (void)s;
    CFDataRef data = CFDataCreate(NULL, (const UInt8 *)value, (CFIndex)strlen(value));
    CFMutableDictionaryRef q = kc_query(key);
    CFMutableDictionaryRef attrs =
        CFDictionaryCreateMutable(NULL, 0, &kCFTypeDictionaryKeyCallBacks, &kCFTypeDictionaryValueCallBacks);
    CFDictionarySetValue(attrs, kSecValueData, data);
    OSStatus st = SecItemUpdate(q, attrs);
    if (st == errSecItemNotFound) {
        CFDictionarySetValue(q, kSecValueData, data);
        CFDictionarySetValue(q, kSecAttrAccessible, kSecAttrAccessibleAfterFirstUnlockThisDeviceOnly);
        st = SecItemAdd(q, NULL);
    }
    CFRelease(attrs);
    CFRelease(q);
    CFRelease(data);
    if (st != errSecSuccess) {
        mg_log_warn("keychain: store failed (%d)", (int)st);
    }
    return st == errSecSuccess;
}

bool secure_store_remove(const SecureStore *s, const char *key)
{
    (void)s;
    CFMutableDictionaryRef q = kc_query(key);
    OSStatus st = SecItemDelete(q);
    CFRelease(q);
    return st == errSecSuccess || st == errSecItemNotFound;
}

#elif defined(MG_PLATFORM_ANDROID)
/* ------------------------------------------------------ Android Keystore */
#include <jni.h>

void secure_store_init(SecureStore *s, const char *profile)
{
    (void)profile;
    JNIEnv *env = (JNIEnv *)SDL_GetAndroidJNIEnv();
    jobject activity = (jobject)SDL_GetAndroidActivity();
    if (!env || !activity) {
        return;
    }
    /* Look the class up through the activity's class loader: FindClass on a
     * native thread only sees system classes. */
    jclass act_cls = (*env)->GetObjectClass(env, activity);
    jmethodID get_loader = (*env)->GetMethodID(env, act_cls, "getClassLoader", "()Ljava/lang/ClassLoader;");
    jobject loader = (*env)->CallObjectMethod(env, activity, get_loader);
    jclass loader_cls = (*env)->FindClass(env, "java/lang/ClassLoader");
    jmethodID load = (*env)->GetMethodID(env, loader_cls, "loadClass", "(Ljava/lang/String;)Ljava/lang/Class;");
    jstring name = (*env)->NewStringUTF(env, "ro.docvex.mobilegame.SecureStore");
    jclass cls = (jclass)(*env)->CallObjectMethod(env, loader, load, name);
    if ((*env)->ExceptionCheck(env)) {
        (*env)->ExceptionClear(env);
        cls = NULL;
    }
    if (cls) {
        jclass gcls = (jclass)(*env)->NewGlobalRef(env, cls);
        s->java_class = gcls;
        s->m_get = (*env)->GetStaticMethodID(env, gcls, "get",
                                             "(Landroid/content/Context;Ljava/lang/String;)Ljava/lang/String;");
        s->m_set = (*env)->GetStaticMethodID(env, gcls, "set",
                                             "(Landroid/content/Context;Ljava/lang/String;Ljava/lang/String;)Z");
        s->m_remove = (*env)->GetStaticMethodID(env, gcls, "remove",
                                                "(Landroid/content/Context;Ljava/lang/String;)Z");
        (*env)->DeleteLocalRef(env, cls);
    } else {
        mg_log_warn("secure_store: SecureStore class not found");
    }
    (*env)->DeleteLocalRef(env, name);
    (*env)->DeleteLocalRef(env, loader);
    (*env)->DeleteLocalRef(env, loader_cls);
    (*env)->DeleteLocalRef(env, act_cls);
    (*env)->DeleteLocalRef(env, activity);
}

bool secure_store_get(const SecureStore *s, const char *key, char *out, size_t cap)
{
    JNIEnv *env = (JNIEnv *)SDL_GetAndroidJNIEnv();
    jclass g_store_class = (jclass)s->java_class;
    if (!env || !g_store_class) {
        return false;
    }
    jobject activity = (jobject)SDL_GetAndroidActivity();
    jstring jkey = (*env)->NewStringUTF(env, key);
    jstring jval = (jstring)(*env)->CallStaticObjectMethod(env, g_store_class, (jmethodID)s->m_get, activity, jkey);
    bool ok = false;
    if ((*env)->ExceptionCheck(env)) {
        (*env)->ExceptionClear(env);
    } else if (jval) {
        const char *str = (*env)->GetStringUTFChars(env, jval, NULL);
        size_t n = strlen(str);
        if (n + 1 <= cap) {
            memcpy(out, str, n + 1);
            ok = true;
        }
        (*env)->ReleaseStringUTFChars(env, jval, str);
        (*env)->DeleteLocalRef(env, jval);
    }
    (*env)->DeleteLocalRef(env, jkey);
    (*env)->DeleteLocalRef(env, activity);
    return ok;
}

bool secure_store_set(const SecureStore *s, const char *key, const char *value)
{
    JNIEnv *env = (JNIEnv *)SDL_GetAndroidJNIEnv();
    jclass g_store_class = (jclass)s->java_class;
    if (!env || !g_store_class) {
        return false;
    }
    jobject activity = (jobject)SDL_GetAndroidActivity();
    jstring jkey = (*env)->NewStringUTF(env, key);
    jstring jval = (*env)->NewStringUTF(env, value);
    jboolean r = (*env)->CallStaticBooleanMethod(env, g_store_class, (jmethodID)s->m_set, activity, jkey, jval);
    if ((*env)->ExceptionCheck(env)) {
        (*env)->ExceptionClear(env);
        r = JNI_FALSE;
    }
    (*env)->DeleteLocalRef(env, jkey);
    (*env)->DeleteLocalRef(env, jval);
    (*env)->DeleteLocalRef(env, activity);
    return r == JNI_TRUE;
}

bool secure_store_remove(const SecureStore *s, const char *key)
{
    JNIEnv *env = (JNIEnv *)SDL_GetAndroidJNIEnv();
    jclass g_store_class = (jclass)s->java_class;
    if (!env || !g_store_class) {
        return false;
    }
    jobject activity = (jobject)SDL_GetAndroidActivity();
    jstring jkey = (*env)->NewStringUTF(env, key);
    jboolean r = (*env)->CallStaticBooleanMethod(env, g_store_class, (jmethodID)s->m_remove, activity, jkey);
    if ((*env)->ExceptionCheck(env)) {
        (*env)->ExceptionClear(env);
        r = JNI_FALSE;
    }
    (*env)->DeleteLocalRef(env, jkey);
    (*env)->DeleteLocalRef(env, activity);
    return r == JNI_TRUE;
}

#else
/* ---------------------------------------------------------------- desktop */
void secure_store_init(SecureStore *s, const char *profile)
{
    char *pref = SDL_GetPrefPath("docvex", "mobilegame");
    SDL_snprintf(s->dir, sizeof(s->dir), "%s", pref ? pref : "./");
    SDL_free(pref);
    SDL_snprintf(s->profile, sizeof(s->profile), "%s", profile && *profile ? profile : "default");
}

static void path_for(const SecureStore *s, const char *key, char *out, size_t cap)
{
    SDL_snprintf(out, cap, "%s%s.%s.txt", s->dir, key, s->profile);
}

bool secure_store_get(const SecureStore *s, const char *key, char *out, size_t cap)
{
    char path[640];
    path_for(s, key, path, sizeof(path));
    size_t n = 0;
    char *data = (char *)SDL_LoadFile(path, &n);
    if (!data) {
        return false;
    }
    bool ok = n + 1 <= cap;
    if (ok) {
        memcpy(out, data, n);
        out[n] = 0;
    }
    SDL_free(data);
    return ok;
}

bool secure_store_set(const SecureStore *s, const char *key, const char *value)
{
    char path[640];
    path_for(s, key, path, sizeof(path));
    return SDL_SaveFile(path, value, strlen(value));
}

bool secure_store_remove(const SecureStore *s, const char *key)
{
    char path[640];
    path_for(s, key, path, sizeof(path));
    return SDL_RemovePath(path);
}
#endif
