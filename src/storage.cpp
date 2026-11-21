#include "storage.h"
#include <SDL.h>
#include <fstream>
#include <stdexcept>
#ifdef __ANDROID__
#include <SDL_system.h>
#include <jni.h>
namespace {
struct Java {
    JNIEnv* env = static_cast<JNIEnv*>(SDL_AndroidGetJNIEnv());
    jobject activity = static_cast<jobject>(SDL_AndroidGetActivity());
    jclass cls = env->GetObjectClass(activity);
    ~Java() { env->DeleteLocalRef(cls); env->DeleteLocalRef(activity); }
    void check() { if (env->ExceptionCheck()) { env->ExceptionClear(); throw std::runtime_error("Android storage access failed"); } }
};
std::string javaString(const char* method) {
    Java j;
    auto value = static_cast<jstring>(j.env->CallObjectMethod(j.activity,j.env->GetMethodID(j.cls,method,"()Ljava/lang/String;")));
    j.check();
    if (!value) return {};
    const char* utf = j.env->GetStringUTFChars(value,nullptr);
    std::string result(utf); j.env->ReleaseStringUTFChars(value,utf); j.env->DeleteLocalRef(value); return result;
}
void javaAction(const char* method) { Java j; j.env->CallVoidMethod(j.activity,j.env->GetMethodID(j.cls,method,"()V")); j.check(); }
}
#endif
namespace storage {
static std::filesystem::path testRoot;
void setTestRoot(const std::filesystem::path& path) { testRoot=path; std::filesystem::create_directories(path); }
std::filesystem::path root() {
    if(!testRoot.empty()) return testRoot;
#ifdef __ANDROID__
    return std::filesystem::u8path(SDL_AndroidGetInternalStoragePath());
#else
    char* base = SDL_GetPrefPath("SnesDQ5", "SnesDQ5");
    if (!base) throw std::runtime_error(SDL_GetError());
    auto result = std::filesystem::u8path(base); SDL_free(base);
    std::filesystem::create_directories(result / "rom"); return result;
#endif
}
std::vector<uint8_t> read(const std::string& name) {
#ifdef __ANDROID__
    Java j; auto key = j.env->NewStringUTF(name.c_str());
    auto bytes = static_cast<jbyteArray>(j.env->CallObjectMethod(j.activity,j.env->GetMethodID(j.cls,"readData","(Ljava/lang/String;)[B"),key));
    j.env->DeleteLocalRef(key); j.check();
    if (!bytes) return {};
    std::vector<uint8_t> result(j.env->GetArrayLength(bytes));
    j.env->GetByteArrayRegion(bytes,0,static_cast<jsize>(result.size()),reinterpret_cast<jbyte*>(result.data()));
    j.env->DeleteLocalRef(bytes); return result;
#else
    std::ifstream file(root()/std::filesystem::u8path(name),std::ios::binary);
    if (!file) return {};
    return {std::istreambuf_iterator<char>(file),std::istreambuf_iterator<char>()};
#endif
}
void write(const std::string& name, const void* data, size_t size) {
#ifdef __ANDROID__
    Java j; auto key = j.env->NewStringUTF(name.c_str()); auto bytes = j.env->NewByteArray(static_cast<jsize>(size));
    j.env->SetByteArrayRegion(bytes,0,static_cast<jsize>(size),static_cast<const jbyte*>(data));
    j.env->CallVoidMethod(j.activity,j.env->GetMethodID(j.cls,"writeData","(Ljava/lang/String;[B)V"),key,bytes);
    j.env->DeleteLocalRef(key); j.env->DeleteLocalRef(bytes); j.check();
#else
    auto dest = root()/std::filesystem::u8path(name); auto temp = dest; temp += ".tmp";
    { std::ofstream file(temp,std::ios::binary|std::ios::trunc); file.write(static_cast<const char*>(data),size); file.close();
      if (!file) throw std::runtime_error("Could not save data"); }
    // Keep the previous file intact until the new file is fully written.
    if (std::filesystem::exists(dest)) { auto backup = dest; backup += ".bak"; std::filesystem::copy_file(dest,backup,std::filesystem::copy_options::overwrite_existing); }
    std::error_code ec; std::filesystem::rename(temp,dest,ec);
    if (ec) { std::filesystem::copy_file(temp,dest,std::filesystem::copy_options::overwrite_existing); std::filesystem::remove(temp); }
#endif
}
std::vector<uint8_t> readRom(const std::string& path) {
    std::ifstream file(std::filesystem::u8path(path),std::ios::binary|std::ios::ate);
    if (!file) throw std::runtime_error("ROM could not be opened");
    const auto size = file.tellg();
    if (size < 0x8000 || size > 0x1000200) throw std::runtime_error("Invalid SNES ROM size");
    std::vector<uint8_t> data(static_cast<size_t>(size)); file.seekg(0); file.read(reinterpret_cast<char*>(data.data()),size);
    if (!file) throw std::runtime_error("ROM read failed"); return data;
}
std::string pickedRom() {
#ifdef __ANDROID__
    return javaString("consumePickedRom");
#else
    return {};
#endif
}
void pickRom() {
#ifdef __ANDROID__
    javaAction("chooseRom");
#endif
}
void pickDirectory() {
#ifdef __ANDROID__
    javaAction("chooseStorage");
#endif
}
std::array<int,4> safeInsets() {
    std::array<int,4> result{};
#ifdef __ANDROID__
    Java j; auto values=static_cast<jintArray>(j.env->CallObjectMethod(j.activity,j.env->GetMethodID(j.cls,"getSafeInsets","()[I"))); j.check();
    if(values) { if(j.env->GetArrayLength(values)==4) j.env->GetIntArrayRegion(values,0,4,result.data()); j.env->DeleteLocalRef(values); j.check(); }
#endif
    return result;
}
void rememberRom(const std::string&) {}
}
