/* Android: calls ro.docvex.mobilegame.GoogleAuth.start (Credential Manager)
 * and receives the result through GoogleAuth.nativeResult. */
#include "google_auth_internal.h"

#include <jni.h>
#include <stdint.h>

#include <SDL3/SDL_system.h>

#include "core/log.h"

/* GoogleAuth class through the activity's class loader (FindClass on a
 * native thread only sees system classes). Cached as a global ref. */
static jclass load_class(JNIEnv *env, jobject activity)
{
    jclass act_cls = (*env)->GetObjectClass(env, activity);
    jmethodID get_loader = (*env)->GetMethodID(env, act_cls, "getClassLoader", "()Ljava/lang/ClassLoader;");
    jobject loader = (*env)->CallObjectMethod(env, activity, get_loader);
    jclass loader_cls = (*env)->FindClass(env, "java/lang/ClassLoader");
    jmethodID load = (*env)->GetMethodID(env, loader_cls, "loadClass", "(Ljava/lang/String;)Ljava/lang/Class;");
    jstring name = (*env)->NewStringUTF(env, "ro.docvex.mobilegame.GoogleAuth");
    jclass cls = (jclass)(*env)->CallObjectMethod(env, loader, load, name);
    jclass global = NULL;
    if ((*env)->ExceptionCheck(env)) {
        (*env)->ExceptionClear(env);
    } else if (cls) {
        global = (jclass)(*env)->NewGlobalRef(env, cls);
        (*env)->DeleteLocalRef(env, cls);
    }
    (*env)->DeleteLocalRef(env, name);
    (*env)->DeleteLocalRef(env, loader);
    (*env)->DeleteLocalRef(env, loader_cls);
    (*env)->DeleteLocalRef(env, act_cls);
    return global;
}

bool google_auth_platform_available(const GoogleAuth *g) { return g->web_client_id[0] != 0; }

bool google_auth_platform_start(GoogleAuth *g, struct SDL_Window *window)
{
    (void)window;
    JNIEnv *env = (JNIEnv *)SDL_GetAndroidJNIEnv();
    jobject activity = (jobject)SDL_GetAndroidActivity();
    if (!env || !activity) {
        return false;
    }
    if (!g->platform) {
        g->platform = load_class(env, activity);
    }
    jclass cls = (jclass)g->platform;
    bool ok = false;
    if (cls) {
        jmethodID start = (*env)->GetStaticMethodID(env, cls, "start", "(Landroid/app/Activity;JLjava/lang/String;)V");
        jstring web = (*env)->NewStringUTF(env, g->web_client_id);
        if (start) {
            (*env)->CallStaticVoidMethod(env, cls, start, activity, (jlong)(intptr_t)g, web);
            ok = !(*env)->ExceptionCheck(env);
            if (!ok) {
                (*env)->ExceptionClear(env);
            }
        }
        (*env)->DeleteLocalRef(env, web);
    } else {
        mg_log_warn("google: GoogleAuth class not found");
    }
    (*env)->DeleteLocalRef(env, activity);
    return ok;
}

static void copy_jstring(JNIEnv *env, jstring s, char *out, size_t cap)
{
    out[0] = 0;
    if (s) {
        const char *c = (*env)->GetStringUTFChars(env, s, NULL);
        if (c) {
            SDL_strlcpy(out, c, cap);
            (*env)->ReleaseStringUTFChars(env, s, c);
        }
    }
}

JNIEXPORT void JNICALL Java_ro_docvex_mobilegame_GoogleAuth_nativeResult(JNIEnv *env, jclass cls, jlong handle,
                                                                         jstring token, jstring nonce, jstring error)
{
    (void)cls;
    GoogleAuth *g = (GoogleAuth *)(intptr_t)handle;
    if (!g) {
        return;
    }
    char tok[4096], non[96], err[160];
    copy_jstring(env, token, tok, sizeof(tok));
    copy_jstring(env, nonce, non, sizeof(non));
    copy_jstring(env, error, err, sizeof(err));
    google_auth_finish(g, tok[0] ? tok : NULL, non, err);
}
