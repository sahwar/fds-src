/* DO NOT EDIT THIS FILE - it is machine generated */
#include <jni.h>
/* Header for class com_formationds_nativeapi_NativeApi */

#ifndef _Included_com_formationds_nativeapi_NativeApi
#define _Included_com_formationds_nativeapi_NativeApi
#ifdef __cplusplus
extern "C" {
#endif
/*
 * Class:     com_formationds_nativeapi_NativeApi
 * Method:    init
 * Signature: ()V
 */
JNIEXPORT void JNICALL Java_com_formationds_nativeapi_NativeApi_init
  (JNIEnv *, jclass);

/*
 * Class:     com_formationds_nativeapi_NativeApi
 * Method:    getBucketsStats
 * Signature: (Ljava/util/function/Consumer;)V
 */
JNIEXPORT void JNICALL Java_com_formationds_nativeapi_NativeApi_getBucketsStats
  (JNIEnv *, jclass, jobject);

/*
 * Class:     com_formationds_nativeapi_NativeApi
 * Method:    createBucket
 * Signature: (Ljava/lang/String;Ljava/util/function/Consumer;)V
 */
JNIEXPORT void JNICALL Java_com_formationds_nativeapi_NativeApi_createBucket
  (JNIEnv *, jclass, jstring, jobject);

#ifdef __cplusplus
}
#endif
#endif
