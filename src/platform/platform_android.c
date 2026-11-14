// The Android side of the platform: JNI calls into the Java FileManager (the files of the picked game folder through
// the Storage Access Framework) and TutankhamunActivity (the folder picker), after Julius'
// src/platform/android/android.c (https://github.com/bvschaik/julius, AGPL-3.0). The calls come from the SDL thread,
// a Java thread, so FindClass sees the app's classes. The picker blocks the game's thread until the activity answers.

#include "platform/platform_android.h"

#include "SDL.h"

#include <jni.h>
#include <stdlib.h>
#include <string.h>

static const char CLASS_FILE_MANAGER[] = "com/github/dalerank/tutankhamun/FileManager";
static const char CLASS_ACTIVITY[] = "com/github/dalerank/tutankhamun/TutankhamunActivity";
static const char SIGNATURE_ACTIVITY[] = "Landroid/app/Activity;";

enum android_wait {
    PICKER_POLL_MS = 100,
};

typedef struct {
    JNIEnv *env;
    jclass class;
    jobject activity;
    jmethodID method;
} java_call;

static volatile int picker_answered;

static bool call_begin(java_call *call, const char *class_name, const char *method, const char *signature, bool is_static)
{
    memset(call, 0, sizeof(*call));
    call->env = (JNIEnv *) SDL_AndroidGetJNIEnv();
    if (!call->env) {
        SDL_Log("android: no JNI environment");
        return false;
    }
    call->class = (*call->env)->FindClass(call->env, class_name);
    call->activity = (jobject) SDL_AndroidGetActivity();
    if (!call->class || !call->activity) {
        SDL_Log("android: class %s or the activity not found", class_name);
        return false;
    }
    call->method = is_static ? (*call->env)->GetStaticMethodID(call->env, call->class, method, signature)
                             : (*call->env)->GetMethodID(call->env, call->class, method, signature);
    if (!call->method) {
        SDL_Log("android: method %s.%s not found", class_name, method);
        return false;
    }
    return true;
}

static void call_end(java_call *call)
{
    if (!call->env) {
        return;
    }
    if ((*call->env)->ExceptionCheck(call->env)) {
        (*call->env)->ExceptionDescribe(call->env);
        (*call->env)->ExceptionClear(call->env);
    }
    if (call->activity) {
        (*call->env)->DeleteLocalRef(call->env, call->activity);
    }
    if (call->class) {
        (*call->env)->DeleteLocalRef(call->env, call->class);
    }
    memset(call, 0, sizeof(*call));
}

static bool file_manager_call(java_call *call, const char *method, const char *arguments, const char *result)
{
    char signature[160];
    SDL_snprintf(signature, sizeof(signature), "(%s%s)%s", SIGNATURE_ACTIVITY, arguments, result);
    return call_begin(call, CLASS_FILE_MANAGER, method, signature, true);
}

int32_t android_file_open(const char *path, const char *mode)
{
    java_call call;
    int32_t fd = -1;
    if (file_manager_call(&call, "openFileDescriptor", "Ljava/lang/String;Ljava/lang/String;", "I")) {
        jstring jpath = (*call.env)->NewStringUTF(call.env, path);
        jstring jmode = (*call.env)->NewStringUTF(call.env, mode);
        fd = (int32_t) (*call.env)->CallStaticIntMethod(call.env, call.class, call.method, call.activity, jpath, jmode);
        (*call.env)->DeleteLocalRef(call.env, jpath);
        (*call.env)->DeleteLocalRef(call.env, jmode);
    }
    call_end(&call);
    return fd;
}

int32_t android_path_kind(const char *path)
{
    java_call call;
    int32_t kind = ANDROID_PATH_NONE;
    if (file_manager_call(&call, "pathKind", "Ljava/lang/String;", "I")) {
        jstring jpath = (*call.env)->NewStringUTF(call.env, path);
        kind = (int32_t) (*call.env)->CallStaticIntMethod(call.env, call.class, call.method, call.activity, jpath);
        (*call.env)->DeleteLocalRef(call.env, jpath);
    }
    call_end(&call);
    return kind;
}

static bool path_call(const char *method, const char *path)
{
    java_call call;
    bool done = false;
    if (file_manager_call(&call, method, "Ljava/lang/String;", "Z")) {
        jstring jpath = (*call.env)->NewStringUTF(call.env, path);
        done = (*call.env)->CallStaticBooleanMethod(call.env, call.class, call.method, call.activity, jpath) == JNI_TRUE;
        (*call.env)->DeleteLocalRef(call.env, jpath);
    }
    call_end(&call);
    return done;
}

bool android_dir_create(const char *path)
{
    return path_call("createDirectory", path);
}

bool android_file_delete(const char *path)
{
    return path_call("deleteFile", path);
}

char **android_dir_list(const char *path, int32_t *count)
{
    java_call call;
    char **names = NULL;
    *count = 0;
    if (file_manager_call(&call, "listDirectory", "Ljava/lang/String;", "[Ljava/lang/String;")) {
        jstring jpath = (*call.env)->NewStringUTF(call.env, path);
        jobjectArray list = (jobjectArray) (*call.env)->CallStaticObjectMethod(call.env, call.class, call.method, call.activity, jpath);
        (*call.env)->DeleteLocalRef(call.env, jpath);
        jsize length = list ? (*call.env)->GetArrayLength(call.env, list) : 0;
        names = length > 0 ? calloc((size_t) length, sizeof(char *)) : NULL;
        for (jsize i = 0; names && i < length; i++) {
            jstring jname = (jstring) (*call.env)->GetObjectArrayElement(call.env, list, i);
            const char *name = (*call.env)->GetStringUTFChars(call.env, jname, NULL);
            names[*count] = SDL_strdup(name);
            (*count)++;
            (*call.env)->ReleaseStringUTFChars(call.env, jname, name);
            (*call.env)->DeleteLocalRef(call.env, jname);
        }
        if (list) {
            (*call.env)->DeleteLocalRef(call.env, list);
        }
    }
    call_end(&call);
    return names;
}

void android_dir_list_free(char **names, int32_t count)
{
    for (int32_t i = 0; i < count; i++) {
        SDL_free(names[i]);
    }
    free(names);
}

bool android_game_folder_set(const char *uri)
{
    java_call call;
    bool done = false;
    if (call_begin(&call, CLASS_FILE_MANAGER, "setBaseUri", "(Ljava/lang/String;)Z", true)) {
        jstring juri = (*call.env)->NewStringUTF(call.env, uri);
        done = (*call.env)->CallStaticBooleanMethod(call.env, call.class, call.method, juri) == JNI_TRUE;
        (*call.env)->DeleteLocalRef(call.env, juri);
    }
    call_end(&call);
    return done;
}

static void base_uri(char *uri, size_t size)
{
    java_call call;
    uri[0] = 0;
    if (call_begin(&call, CLASS_FILE_MANAGER, "getBaseUri", "()Ljava/lang/String;", true)) {
        jstring juri = (jstring) (*call.env)->CallStaticObjectMethod(call.env, call.class, call.method);
        if (juri) {
            const char *text = (*call.env)->GetStringUTFChars(call.env, juri, NULL);
            SDL_strlcpy(uri, text, size);
            (*call.env)->ReleaseStringUTFChars(call.env, juri, text);
            (*call.env)->DeleteLocalRef(call.env, juri);
        }
    }
    call_end(&call);
}

bool android_game_folder_pick(bool again, char *uri, size_t size)
{
    java_call call;
    picker_answered = 0;
    if (call_begin(&call, CLASS_ACTIVITY, "showDirectorySelection", "(Z)V", false)) {
        (*call.env)->CallVoidMethod(call.env, call.activity, call.method, again ? JNI_TRUE : JNI_FALSE);
    } else {
        picker_answered = 1;
    }
    call_end(&call);
    while (!picker_answered) {
        SDL_PumpEvents();
        SDL_Delay(PICKER_POLL_MS);
    }
    base_uri(uri, size);
    return uri[0] != 0;
}

const char *android_storage_path(void)
{
    return SDL_AndroidGetInternalStoragePath();
}

// The report's thread is not a Java one, and FindClass there sees only the system's classes: the activity's own class
// is taken from the activity object instead.
int32_t android_http_post_json(const char *url, const char *body, size_t size)
{
    JNIEnv *env = (JNIEnv *) SDL_AndroidGetJNIEnv();
    jobject activity = env ? (jobject) SDL_AndroidGetActivity() : NULL;
    if (!activity) {
        SDL_Log("android: no JNI environment or activity for the HTTP request");
        return 0;
    }
    int32_t status = 0;
    jclass class = (*env)->GetObjectClass(env, activity);
    jmethodID method = (*env)->GetStaticMethodID(env, class, "httpPostJson", "(Ljava/lang/String;[B)I");
    if (method) {
        jstring jurl = (*env)->NewStringUTF(env, url);
        jbyteArray jbody = (*env)->NewByteArray(env, (jsize) size);
        if (jurl && jbody) {
            (*env)->SetByteArrayRegion(env, jbody, 0, (jsize) size, (const jbyte *) body);
            status = (int32_t) (*env)->CallStaticIntMethod(env, class, method, jurl, jbody);
        }
        if (jurl) {
            (*env)->DeleteLocalRef(env, jurl);
        }
        if (jbody) {
            (*env)->DeleteLocalRef(env, jbody);
        }
    }
    if ((*env)->ExceptionCheck(env)) {
        (*env)->ExceptionDescribe(env);
        (*env)->ExceptionClear(env);
        status = 0;
    }
    (*env)->DeleteLocalRef(env, class);
    (*env)->DeleteLocalRef(env, activity);
    return status;
}

JNIEXPORT void JNICALL Java_com_github_dalerank_tutankhamun_TutankhamunActivity_gotDirectory(JNIEnv *env, jobject self)
{
    picker_answered = 1;
}
