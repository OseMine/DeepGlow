// Deep Glow 2 — OFX port
// tests/hostcheck.cpp : a tiny in-process OFX host used to validate the
// plugin's *registration surface* without DaVinci Resolve.
//
// It implements just enough of the OFX host API (property, parameter, image
// effect, memory, multithread and message suites) to drive the real plugin
// entry points:
//
//     load -> describe -> describeInContext(filter) -> createInstance
//          -> getClipPreferences -> getRegionsOfInterest -> destroyInstance
//
// That catches the failure modes you would otherwise only find by opening
// Resolve: wrong clip names, duplicate/blank parameter names, missing
// defaults, out of range defaults, unparseable choices, and parameter lookups
// in the instance constructor that do not match what describeInContext created.

#include <cmath>
#include <cstdarg>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <list>
#include <map>
#include <memory>
#include <mutex>
#include <new>
#include <string>
#include <vector>

#if defined(_WIN32)
#  include <windows.h>
#endif

// The OFX headers only forward-declare their handle types:
//
//     typedef struct OfxPropertySetStruct *OfxPropertySetHandle;
//
// Completing those structs here lets the harness's own objects *be* the
// handles (a PropSet is-a property set, etc.) so no casting is needed at the
// call sites, while the suite function signatures stay exactly as OFX
// declares them.
struct OfxPropertySetStruct {};
struct OfxParamSetStruct {};
struct OfxParamStruct {};
struct OfxImageEffectStruct {};
struct OfxImageClipStruct {};
struct OfxImageMemoryStruct {};
struct OfxMutex {};

#include "ofxCore.h"
#include "ofxImageEffect.h"
#include "ofxMemory.h"
#include "ofxMessage.h"
#include "ofxMultiThread.h"
#include "ofxParam.h"
#include "ofxProperty.h"

namespace {

int gFailures = 0;

#if defined(_WIN32)
// A host that crashes inside the plugin is useless, so turn an access
// violation into a stack dump of exe-relative addresses (resolve against the
// .map file emitted by the /MAP linker option).
LONG WINAPI crashFilter(EXCEPTION_POINTERS* ep) {
    const uintptr_t base = reinterpret_cast<uintptr_t>(GetModuleHandleA(0));
    std::printf("\n*** CRASH: exception 0x%08lx at exe+0x%llx ***\n",
                static_cast<unsigned long>(ep->ExceptionRecord->ExceptionCode),
                static_cast<unsigned long long>(
                    reinterpret_cast<uintptr_t>(ep->ExceptionRecord->ExceptionAddress) - base));
    void* frames[48];
    const USHORT n = CaptureStackBackTrace(0, 48, frames, 0);
    for (USHORT i = 0; i < n; ++i) {
        std::printf("  #%-2u exe+0x%llx\n", i,
                    static_cast<unsigned long long>(
                        reinterpret_cast<uintptr_t>(frames[i]) - base));
    }
    std::fflush(stdout);
    return EXCEPTION_EXECUTE_HANDLER;
}
#endif

void check(bool cond, const std::string& what) {
    if (!cond) {
        std::printf("  FAIL: %s\n", what.c_str());
        ++gFailures;
    }
}

// ---------------------------------------------------------------------------
// property sets
// ---------------------------------------------------------------------------
// A host-side property bag.  It doubles as the param set and the effect
// instance, because a host is free to back all three with one object.
//
// NOTE: it derives from exactly one OFX handle type so the base subobject sits
// at offset 0.  A host hands the same address out as a param set / effect
// handle (see asHandle below) and the OFX plumbing round-trips it through
// `const void *`, so any base at a non-zero offset would corrupt the pointer.
struct PropSet : OfxPropertySetStruct {
    std::map<std::string, std::vector<std::string> > strings;
    std::map<std::string, std::vector<double> >      doubles;
    std::map<std::string, std::vector<int> >         ints;
    std::map<std::string, std::vector<void*> >       pointers;
};

// Re-tag the very same object as another kind of OFX handle (all handles here
// are opaque pointers to the one PropSet).
template <class HandleT>
HandleT asHandle(PropSet* p) {
    return reinterpret_cast<HandleT>(static_cast<OfxPropertySetHandle>(p));
}

std::list<std::unique_ptr<PropSet> > gStores;

PropSet* newPropSet() {
    gStores.push_back(std::unique_ptr<PropSet>(new PropSet()));
    return gStores.back().get();
}

PropSet* ps(OfxPropertySetHandle h) { return static_cast<PropSet*>(h); }   // base is at offset 0
template <class T>
void setValue(std::map<std::string, std::vector<T> >& m, const char* name, int index, T v) {
    std::vector<T>& vec = m[name];
    if (index < 0) index = 0;
    if (static_cast<int>(vec.size()) <= index) vec.resize(index + 1);
    vec[index] = v;
}

// ---------------------------------------------------------------------------
// parameter registry
// ---------------------------------------------------------------------------
struct ParamInfo : OfxParamStruct {
    std::string type;
    std::string name;
    PropSet*    props = nullptr;
};

std::vector<std::unique_ptr<ParamInfo> > gParams;
std::map<std::string, ParamInfo*>        gParamByName;

ParamInfo* findParam(const char* name) {
    std::map<std::string, ParamInfo*>::iterator it = gParamByName.find(name);
    return it == gParamByName.end() ? 0 : it->second;
}

// ---------------------------------------------------------------------------
// clips
// ---------------------------------------------------------------------------
struct ClipInfo : OfxImageClipStruct {
    std::string name;
    PropSet*    props = nullptr;
};

std::vector<std::unique_ptr<ClipInfo> > gClips;
std::map<std::string, ClipInfo*>        gClipByName;

// ---------------------------------------------------------------------------
// property suite
// ---------------------------------------------------------------------------
OfxStatus propSetPointer(OfxPropertySetHandle p, const char* n, int i, void* v) {
    if (!p) return kOfxStatErrBadHandle;
    setValue(ps(p)->pointers, n, i, v);
    return kOfxStatOK;
}
OfxStatus propSetString(OfxPropertySetHandle p, const char* n, int i, const char* v) {
    if (!p) return kOfxStatErrBadHandle;
    setValue(ps(p)->strings, n, i, std::string(v ? v : ""));
    return kOfxStatOK;
}
OfxStatus propSetDouble(OfxPropertySetHandle p, const char* n, int i, double v) {
    if (!p) return kOfxStatErrBadHandle;
    setValue(ps(p)->doubles, n, i, v);
    return kOfxStatOK;
}
OfxStatus propSetInt(OfxPropertySetHandle p, const char* n, int i, int v) {
    if (!p) return kOfxStatErrBadHandle;
    setValue(ps(p)->ints, n, i, v);
    return kOfxStatOK;
}
OfxStatus propSetStringN(OfxPropertySetHandle p, const char* n, int count, const char* const* v) {
    for (int i = 0; i < count; ++i) propSetString(p, n, i, v[i]);
    return kOfxStatOK;
}
OfxStatus propSetPointerN(OfxPropertySetHandle p, const char* n, int count, void* const* v) {
    for (int i = 0; i < count; ++i) propSetPointer(p, n, i, v[i]);
    return kOfxStatOK;
}
OfxStatus propSetDoubleN(OfxPropertySetHandle p, const char* n, int count, const double* v) {
    for (int i = 0; i < count; ++i) propSetDouble(p, n, i, v[i]);
    return kOfxStatOK;
}
OfxStatus propSetIntN(OfxPropertySetHandle p, const char* n, int count, const int* v) {
    for (int i = 0; i < count; ++i) propSetInt(p, n, i, v[i]);
    return kOfxStatOK;
}

// The getters always write a well defined value, even on failure: the OFX
// Support library reads several properties with `char *v; propGetString(...)`
// and would otherwise construct a std::string from an uninitialised pointer.
char* emptyString() {
    static char sEmpty[1] = { '\0' };
    return sEmpty;
}

OfxStatus propGetPointer(OfxPropertySetHandle p, const char* n, int i, void** v) {
    if (v) *v = 0;
    if (!p) return kOfxStatErrBadHandle;
    std::map<std::string, std::vector<void*> >::iterator it = ps(p)->pointers.find(n);
    if (it == ps(p)->pointers.end() || i < 0 || i >= static_cast<int>(it->second.size()))
        return kOfxStatErrValue;
    if (v) *v = it->second[i];
    return kOfxStatOK;
}
OfxStatus propGetString(OfxPropertySetHandle p, const char* n, int i, char** v) {
    if (v) *v = emptyString();
    if (!p) return kOfxStatErrBadHandle;
    std::map<std::string, std::vector<std::string> >::iterator it = ps(p)->strings.find(n);
    if (it == ps(p)->strings.end() || i < 0 || i >= static_cast<int>(it->second.size()))
        return kOfxStatErrValue;
    if (v) *v = const_cast<char*>(it->second[i].c_str());
    return kOfxStatOK;
}
OfxStatus propGetDouble(OfxPropertySetHandle p, const char* n, int i, double* v) {
    if (v) *v = 0.0;
    if (!p) return kOfxStatErrBadHandle;
    std::map<std::string, std::vector<double> >::iterator it = ps(p)->doubles.find(n);
    if (it == ps(p)->doubles.end() || i < 0 || i >= static_cast<int>(it->second.size()))
        return kOfxStatErrValue;
    if (v) *v = it->second[i];
    return kOfxStatOK;
}
OfxStatus propGetInt(OfxPropertySetHandle p, const char* n, int i, int* v) {
    if (v) *v = 0;
    if (!p) return kOfxStatErrBadHandle;
    std::map<std::string, std::vector<int> >::iterator it = ps(p)->ints.find(n);
    if (it == ps(p)->ints.end() || i < 0 || i >= static_cast<int>(it->second.size()))
        return kOfxStatErrValue;
    if (v) *v = it->second[i];
    return kOfxStatOK;
}
OfxStatus propGetStringN(OfxPropertySetHandle p, const char* n, int count, char** v) {
    for (int i = 0; i < count; ++i) {
        OfxStatus s = propGetString(p, n, i, &v[i]);
        if (s != kOfxStatOK) v[i] = const_cast<char*>("");
    }
    return kOfxStatOK;
}
OfxStatus propGetPointerN(OfxPropertySetHandle p, const char* n, int count, void** v) {
    for (int i = 0; i < count; ++i) propGetPointer(p, n, i, &v[i]);
    return kOfxStatOK;
}
OfxStatus propGetDoubleN(OfxPropertySetHandle p, const char* n, int count, double* v) {
    for (int i = 0; i < count; ++i) propGetDouble(p, n, i, &v[i]);
    return kOfxStatOK;
}
OfxStatus propGetIntN(OfxPropertySetHandle p, const char* n, int count, int* v) {
    for (int i = 0; i < count; ++i) propGetInt(p, n, i, &v[i]);
    return kOfxStatOK;
}
OfxStatus propReset(OfxPropertySetHandle p, const char* n) {
    if (!p) return kOfxStatErrBadHandle;
    ps(p)->strings.erase(n);
    ps(p)->doubles.erase(n);
    ps(p)->ints.erase(n);
    ps(p)->pointers.erase(n);
    return kOfxStatOK;
}
OfxStatus propGetDimension(OfxPropertySetHandle p, const char* n, int* count) {
    if (!p) return kOfxStatErrBadHandle;
    size_t dim = 0;
    if (ps(p)->strings.count(n)) dim = ps(p)->strings[n].size();
    if (ps(p)->doubles.count(n)) dim = std::max(dim, ps(p)->doubles[n].size());
    if (ps(p)->ints.count(n)) dim = std::max(dim, ps(p)->ints[n].size());
    if (ps(p)->pointers.count(n)) dim = std::max(dim, ps(p)->pointers[n].size());
    *count = static_cast<int>(dim);
    return kOfxStatOK;
}

OfxPropertySuiteV1 gPropSuite = {
    propSetPointer, propSetString, propSetDouble, propSetInt,
    propSetPointerN, propSetStringN, propSetDoubleN, propSetIntN,
    propGetPointer, propGetString, propGetDouble, propGetInt,
    propGetPointerN, propGetStringN, propGetDoubleN, propGetIntN,
    propReset, propGetDimension
};

// ---------------------------------------------------------------------------
// parameter suite
// ---------------------------------------------------------------------------
OfxStatus paramDefine(OfxParamSetHandle, const char* paramType, const char* name,
                      OfxPropertySetHandle* propertySet) {
    std::unique_ptr<ParamInfo> info(new ParamInfo());
    info->type = paramType;
    info->name = name;
    info->props = newPropSet();
    // A real host stamps the type onto the property set it hands back; the OFX
    // Support library reads it back to decide how to fetch the parameter.
    propSetString(info->props, kOfxParamPropType, 0, paramType);
    gParamByName[name] = info.get();
    gParams.push_back(std::move(info));
    *propertySet = gParams.back()->props;
    return kOfxStatOK;
}

OfxStatus paramGetHandle(OfxParamSetHandle, const char* name, OfxParamHandle* param,
                         OfxPropertySetHandle* propertySet) {
    ParamInfo* info = findParam(name);
    if (!info) {
        std::printf("  [host] paramGetHandle: unknown parameter '%s'\n", name);
        return kOfxStatErrBadHandle;
    }
    if (param) *param = info;
    if (propertySet) *propertySet = info->props;
    return kOfxStatOK;
}

OfxStatus paramSetGetPropertySet(OfxParamSetHandle handle, OfxPropertySetHandle* prop) {
    PropSet* self = reinterpret_cast<PropSet*>(handle);
    *prop = self;
    return kOfxStatOK;
}
OfxStatus paramGetPropertySet(OfxParamHandle param, OfxPropertySetHandle* prop) {
    *prop = static_cast<ParamInfo*>(param)->props;
    return kOfxStatOK;
}

// read the recorded default value, mirroring the real OFX varargs contract
void readDefault(ParamInfo* info, va_list& ap) {
    const std::string& t = info->type;
    PropSet* p = info->props;
    if (t == kOfxParamTypeInteger || t == kOfxParamTypeBoolean || t == kOfxParamTypeChoice) {
        int* out = va_arg(ap, int*);
        int v = 0;
        if (p->ints.count(kOfxParamPropDefault)) v = p->ints[kOfxParamPropDefault][0];
        *out = v;
    } else if (t == kOfxParamTypeDouble) {
        double* out = va_arg(ap, double*);
        double v = 0.0;
        if (p->doubles.count(kOfxParamPropDefault)) v = p->doubles[kOfxParamPropDefault][0];
        *out = v;
    } else if (t == kOfxParamTypeRGBA) {
        double* r = va_arg(ap, double*);
        double* g = va_arg(ap, double*);
        double* b = va_arg(ap, double*);
        double* a = va_arg(ap, double*);
        *r = *g = *b = *a = 0.0;
        if (p->doubles.count(kOfxParamPropDefault)) {
            const std::vector<double>& d = p->doubles[kOfxParamPropDefault];
            if (d.size() > 0) *r = d[0];
            if (d.size() > 1) *g = d[1];
            if (d.size() > 2) *b = d[2];
            if (d.size() > 3) *a = d[3];
        }
    } else if (t == kOfxParamTypeRGB) {
        double* r = va_arg(ap, double*);
        double* g = va_arg(ap, double*);
        double* b = va_arg(ap, double*);
        *r = *g = *b = 0.0;
    } else {
        // string / custom / push button: nothing to read
    }
}

OfxStatus paramGetValue(OfxParamHandle param, ...) {
    va_list ap;
    va_start(ap, param);
    readDefault(static_cast<ParamInfo*>(param), ap);
    va_end(ap);
    return kOfxStatOK;
}
OfxStatus paramGetValueAtTime(OfxParamHandle param, double, ...) {
    va_list ap;
    va_start(ap, param);
    readDefault(static_cast<ParamInfo*>(param), ap);
    va_end(ap);
    return kOfxStatOK;
}
OfxStatus paramGetDerivative(OfxParamHandle, double, ...) { return kOfxStatOK; }
OfxStatus paramGetIntegral(OfxParamHandle, double, double, ...) { return kOfxStatOK; }
OfxStatus paramSetValue(OfxParamHandle, ...) { return kOfxStatOK; }
OfxStatus paramSetValueAtTime(OfxParamHandle, double, ...) { return kOfxStatOK; }
OfxStatus paramGetNumKeys(OfxParamHandle, unsigned int* n) { *n = 0; return kOfxStatOK; }
OfxStatus paramGetKeyTime(OfxParamHandle, unsigned int, double* t) { *t = 0.0; return kOfxStatOK; }
OfxStatus paramGetKeyIndex(OfxParamHandle, double, int, int* i) { *i = -1; return kOfxStatOK; }
OfxStatus paramDeleteKey(OfxParamHandle, double) { return kOfxStatOK; }
OfxStatus paramDeleteAllKeys(OfxParamHandle) { return kOfxStatOK; }
OfxStatus paramCopy(OfxParamHandle, OfxParamHandle, OfxTime, const OfxRangeD*) {
    return kOfxStatOK;
}
OfxStatus paramEditBegin(OfxParamSetHandle, const char*) { return kOfxStatOK; }
OfxStatus paramEditEnd(OfxParamSetHandle) { return kOfxStatOK; }

OfxParameterSuiteV1 gParamSuite = {
    paramDefine, paramGetHandle, paramSetGetPropertySet, paramGetPropertySet,
    paramGetValue, paramGetValueAtTime, paramGetDerivative, paramGetIntegral,
    paramSetValue, paramSetValueAtTime, paramGetNumKeys, paramGetKeyTime,
    paramGetKeyIndex, paramDeleteKey, paramDeleteAllKeys, paramCopy,
    paramEditBegin, paramEditEnd
};

// ---------------------------------------------------------------------------
// image effect suite
// ---------------------------------------------------------------------------
OfxStatus clipDefine(OfxImageEffectHandle effect, const char* name,
                     OfxPropertySetHandle* propertySet) {
    std::unique_ptr<ClipInfo> info(new ClipInfo());
    info->name = name;
    info->props = newPropSet();
    gClipByName[name] = info.get();
    gClips.push_back(std::move(info));
    (void)effect;
    *propertySet = gClips.back()->props;
    return kOfxStatOK;
}

OfxStatus clipGetHandle(OfxImageEffectHandle, const char* name, OfxImageClipHandle* clip,
                        OfxPropertySetHandle* propertySet) {
    std::map<std::string, ClipInfo*>::iterator it = gClipByName.find(name);
    if (it == gClipByName.end()) return kOfxStatErrBadHandle;
    if (clip) *clip = it->second;
    if (propertySet) *propertySet = it->second->props;
    return kOfxStatOK;
}

OfxStatus clipGetPropertySet(OfxImageClipHandle clip, OfxPropertySetHandle* prop) {
    *prop = static_cast<ClipInfo*>(clip)->props;
    return kOfxStatOK;
}

OfxStatus getPropertySet(OfxImageEffectHandle effect, OfxPropertySetHandle* prop) {
    PropSet* self = reinterpret_cast<PropSet*>(effect);
    *prop = self;
    return kOfxStatOK;
}

OfxStatus getParamSet(OfxImageEffectHandle effect, OfxParamSetHandle* paramSet) {
    PropSet* self = reinterpret_cast<PropSet*>(effect);
    *paramSet = asHandle<OfxParamSetHandle>(self);
    return kOfxStatOK;
}

OfxStatus clipGetImage(OfxImageClipHandle, double, const OfxRectD*, OfxPropertySetHandle*) {
    return kOfxStatErrUnsupported;   // render is exercised by DGSmoke instead
}
OfxStatus clipReleaseImage(OfxPropertySetHandle) { return kOfxStatOK; }
OfxStatus clipGetRegionOfDefinition(OfxImageClipHandle, double, OfxRectD* rod) {
    rod->x1 = 0.0; rod->y1 = 0.0; rod->x2 = 1920.0; rod->y2 = 1080.0;
    return kOfxStatOK;
}
int abortFn(OfxImageEffectHandle) { return 0; }

// Image memory is a real object here so that lock/unlock round-trips an
// allocated buffer of the requested size.
struct ImageMem : OfxImageMemoryStruct {
    std::vector<unsigned char> bytes;
};
OfxStatus imageMemoryAlloc(OfxImageEffectHandle, size_t nBytes, OfxImageMemoryHandle* h) {
    ImageMem* mem = new (std::nothrow) ImageMem();
    if (!mem) return kOfxStatErrMemory;
    mem->bytes.resize(nBytes ? nBytes : 1);
    *h = mem;
    return kOfxStatOK;
}
OfxStatus imageMemoryFree(OfxImageMemoryHandle h) {
    delete static_cast<ImageMem*>(h);
    return kOfxStatOK;
}
OfxStatus imageMemoryLock(OfxImageMemoryHandle h, void** ptr) {
    *ptr = static_cast<ImageMem*>(h)->bytes.data();
    return kOfxStatOK;
}
OfxStatus imageMemoryUnlock(OfxImageMemoryHandle) { return kOfxStatOK; }

OfxImageEffectSuiteV1 gEffectSuite = {
    getPropertySet, getParamSet, clipDefine, clipGetHandle, clipGetPropertySet,
    clipGetImage, clipReleaseImage, clipGetRegionOfDefinition, abortFn,
    imageMemoryAlloc, imageMemoryFree, imageMemoryLock, imageMemoryUnlock
};

// ---------------------------------------------------------------------------
// memory / multithread / message suites
// ---------------------------------------------------------------------------
OfxStatus memoryAlloc(void* handle, size_t nBytes, void** allocated) {
    (void)handle;
    *allocated = std::malloc(nBytes);
    return *allocated ? kOfxStatOK : kOfxStatErrMemory;
}
OfxStatus memoryFree(void* allocated) { std::free(allocated); return kOfxStatOK; }
OfxMemorySuiteV1 gMemorySuite = {memoryAlloc, memoryFree};

OfxStatus multiThread(OfxThreadFunctionV1 func, unsigned int nThreads, void* arg) {
    (void)nThreads;
    if (func) func(0, 1, arg);
    return kOfxStatOK;
}
OfxStatus multiThreadNumCPUs(unsigned int* n) { *n = 1; return kOfxStatOK; }
OfxStatus multiThreadIndex(unsigned int* n) { *n = 0; return kOfxStatOK; }
int        multiThreadIsSpawnedThread(void) { return 0; }

struct MutexImpl : OfxMutex {
    std::mutex lock;
};
OfxStatus mutexCreate(OfxMutexHandle* mutex, int) {
    *mutex = new (std::nothrow) MutexImpl();
    return *mutex ? kOfxStatOK : kOfxStatErrMemory;
}
OfxStatus mutexDestroy(const OfxMutexHandle mutex) {
    delete static_cast<MutexImpl*>(mutex);
    return kOfxStatOK;
}
OfxStatus mutexLock(const OfxMutexHandle mutex) {
    if (mutex) static_cast<MutexImpl*>(mutex)->lock.lock();
    return kOfxStatOK;
}
OfxStatus mutexUnLock(const OfxMutexHandle mutex) {
    if (mutex) static_cast<MutexImpl*>(mutex)->lock.unlock();
    return kOfxStatOK;
}
OfxStatus mutexTryLock(const OfxMutexHandle mutex) {
    if (mutex && !static_cast<MutexImpl*>(mutex)->lock.try_lock()) return kOfxStatFailed;
    return kOfxStatOK;
}
OfxMultiThreadSuiteV1 gThreadSuite = {
    multiThread, multiThreadNumCPUs, multiThreadIndex, multiThreadIsSpawnedThread,
    mutexCreate, mutexDestroy, mutexLock, mutexUnLock, mutexTryLock
};

OfxStatus message(void*, const char* messageType, const char* /*messageId*/,
                  const char* format, ...) {
    va_list ap;
    va_start(ap, format);
    std::printf("  [%s] ", messageType ? messageType : "message");
    std::vprintf(format, ap);
    std::printf("\n");
    va_end(ap);
    return kOfxStatOK;
}
OfxMessageSuiteV1 gMessageSuite = {message};

// ---------------------------------------------------------------------------
// host object
// ---------------------------------------------------------------------------
const void* fetchSuite(OfxPropertySetHandle, const char* suiteName, int suiteVersion) {
    if (std::strcmp(suiteName, kOfxPropertySuite) == 0 && suiteVersion == 1) return &gPropSuite;
    if (std::strcmp(suiteName, kOfxParameterSuite) == 0 && suiteVersion == 1) return &gParamSuite;
    if (std::strcmp(suiteName, kOfxImageEffectSuite) == 0 && suiteVersion == 1) return &gEffectSuite;
    if (std::strcmp(suiteName, kOfxMemorySuite) == 0 && suiteVersion == 1) return &gMemorySuite;
    if (std::strcmp(suiteName, kOfxMultiThreadSuite) == 0 && suiteVersion == 1) return &gThreadSuite;
    if (std::strcmp(suiteName, kOfxMessageSuite) == 0 && suiteVersion == 1) return &gMessageSuite;
    return 0;   // unknown suite: the Support library degrades gracefully
}

} // namespace

int main() {
    std::setvbuf(stdout, 0, _IONBF, 0);
#if defined(_WIN32)
    SetUnhandledExceptionFilter(crashFilter);
#endif
    std::printf("Deep Glow 2 OFX registration check\n\n");

    // ---- plugin discovery ------------------------------------------------
    const int nPlugins = OfxGetNumberOfPlugins();
    std::printf("plugins: %d\n", nPlugins);
    check(nPlugins == 1, "exactly one plugin is exported");
    if (nPlugins < 1) return 1;

    OfxPlugin* plugin = OfxGetPlugin(0);
    check(plugin != 0, "OfxGetPlugin(0) is not null");
    if (!plugin) return 1;

    std::printf("identity: %s v%d.%d (%s)\n", plugin->pluginIdentifier, plugin->pluginVersionMajor,
                plugin->pluginVersionMinor, plugin->pluginApi);
    check(std::strcmp(plugin->pluginApi, kOfxImageEffectPluginApi) == 0, "plugin api is image effect");
    check(plugin->pluginVersionMajor >= 1, "plugin version major >= 1");

    // ---- host ------------------------------------------------------------
    PropSet* hostProps = newPropSet();
    propSetString(hostProps, kOfxPropName, 0, "org.openfx.hostcheck");
    propSetString(hostProps, kOfxPropLabel, 0, "OFX Host Check");
    propSetInt(hostProps, kOfxPropAPIVersion, 0, 1);
    propSetInt(hostProps, kOfxPropAPIVersion, 1, 4);
    propSetInt(hostProps, kOfxPropVersion, 0, 1);
    propSetInt(hostProps, kOfxPropVersion, 1, 0);
    propSetInt(hostProps, kOfxPropVersion, 2, 0);
    propSetString(hostProps, kOfxPropVersionLabel, 0, "1.0");
    propSetInt(hostProps, kOfxImageEffectHostPropIsBackground, 0, 0);
    propSetInt(hostProps, kOfxImageEffectPropSupportsOverlays, 0, 0);
    propSetInt(hostProps, kOfxImageEffectPropSupportsMultiResolution, 0, 1);
    propSetInt(hostProps, kOfxImageEffectPropSupportsTiles, 0, 1);
    propSetInt(hostProps, kOfxImageEffectPropTemporalClipAccess, 0, 1);
    propSetInt(hostProps, kOfxImageEffectPropSupportsMultipleClipDepths, 0, 0);
    propSetInt(hostProps, kOfxImageEffectPropSupportsMultipleClipPARs, 0, 0);
    propSetInt(hostProps, kOfxImageEffectPropSetableFrameRate, 0, 0);
    propSetInt(hostProps, kOfxImageEffectPropSetableFielding, 0, 0);
    propSetInt(hostProps, kOfxParamHostPropSupportsStringAnimation, 0, 0);
    propSetInt(hostProps, kOfxParamHostPropSupportsCustomInteract, 0, 0);
    propSetInt(hostProps, kOfxParamHostPropSupportsChoiceAnimation, 0, 0);
    propSetInt(hostProps, kOfxParamHostPropSupportsBooleanAnimation, 0, 1);
    propSetInt(hostProps, kOfxParamHostPropSupportsCustomAnimation, 0, 0);
    propSetInt(hostProps, kOfxParamHostPropMaxParameters, 0, 1000);
    propSetInt(hostProps, kOfxParamHostPropMaxPages, 0, 8);
    propSetInt(hostProps, kOfxParamHostPropPageRowColumnCount, 0, 20);
    propSetInt(hostProps, kOfxParamHostPropPageRowColumnCount, 1, 1);
    propSetString(hostProps, kOfxImageEffectPropSupportedComponents, 0, kOfxImageComponentRGBA);
    propSetString(hostProps, kOfxImageEffectPropSupportedContexts, 0, kOfxImageEffectContextFilter);
    propSetString(hostProps, kOfxImageEffectPropSupportedPixelDepths, 0, kOfxBitDepthFloat);
    propSetString(hostProps, kOfxImageEffectPropSupportedPixelDepths, 1, kOfxBitDepthHalf);

    OfxHost host;
    host.host = hostProps;
    host.fetchSuite = fetchSuite;

    plugin->setHost(&host);

    PropSet* inArgs = newPropSet();
    PropSet* outArgs = newPropSet();

    // ---- load ------------------------------------------------------------
    OfxStatus stat = plugin->mainEntry(kOfxActionLoad, 0, inArgs, outArgs);
    std::printf("load: status %d\n", stat);
    check(stat == kOfxStatOK, "load succeeded");

    // ---- describe --------------------------------------------------------
    PropSet* descProps = newPropSet();
    propSetString(descProps, kOfxPropType, 0, kOfxTypeImageEffect);
    propSetString(descProps, kOfxPropName, 0, plugin->pluginIdentifier);
    propSetString(descProps, kOfxPropLabel, 0, "Deep Glow 2");
    propSetString(descProps, kOfxPropShortLabel, 0, "Deep Glow 2");
    propSetString(descProps, kOfxPropLongLabel, 0, "Deep Glow 2");
    propSetString(descProps, kOfxPropPluginDescription, 0, "");
    propSetString(descProps, kOfxImageEffectPluginPropGrouping, 0, "DeepGlow");
    propSetString(descProps, kOfxImageEffectPropContext, 0, kOfxImageEffectContextFilter);

    stat = plugin->mainEntry(kOfxActionDescribe, descProps, inArgs, outArgs);
    std::printf("describe: status %d\n", stat);
    check(stat == kOfxStatOK, "describe succeeded");

    // ---- describe in context --------------------------------------------
    // Parameters are only defined during describeInContext, so start empty.
    gParams.clear();
    gParamByName.clear();
    gClips.clear();
    gClipByName.clear();

    PropSet* ctxArgs = newPropSet();
    propSetString(ctxArgs, kOfxImageEffectPropContext, 0, kOfxImageEffectContextFilter);

    stat = plugin->mainEntry(kOfxImageEffectActionDescribeInContext, descProps, ctxArgs, outArgs);
    std::printf("describeInContext(filter): status %d\n", stat);
    check(stat == kOfxStatOK, "describeInContext succeeded");

    // ---- clips -----------------------------------------------------------
    std::printf("\nclips: %d\n", static_cast<int>(gClips.size()));
    for (size_t i = 0; i < gClips.size(); ++i) {
        char* label = 0;
        propGetString(gClips[i]->props, kOfxPropLabel, 0, &label);
        std::printf("  %-10s label=%-10s %s\n", gClips[i]->name.c_str(), label ? label : "",
                    gClips[i]->props->ints.count(kOfxImageClipPropOptional) ? "(optional)" : "");
    }
    check(gClipByName.count(kOfxImageEffectSimpleSourceClipName) == 1, "Source clip defined");
    check(gClipByName.count(kOfxImageEffectOutputClipName) == 1, "Output clip defined");
    check(gClipByName.count("Mask") == 1, "Mask clip defined");
    check(gClipByName.count("LensDirt") == 1, "LensDirt clip defined");
    if (gClipByName.count("Mask")) {
        PropSet* p = gClipByName["Mask"]->props;
        check(p->ints.count(kOfxImageClipPropOptional) &&
                  p->ints[kOfxImageClipPropOptional][0] != 0,
              "Mask clip is optional");
    }

    // ---- parameters ------------------------------------------------------
    std::printf("\nparameters: %d\n", static_cast<int>(gParams.size()));
    check(gParams.size() >= 40, "a full parameter table was created");

    int nPages = 0, nGroups = 0, nChoices = 0;
    std::map<std::string, int> seen;
    for (size_t i = 0; i < gParams.size(); ++i) {
        ParamInfo* info = gParams[i].get();
        PropSet*    p = info->props;

        if (info->type == kOfxParamTypePage) ++nPages;
        if (info->type == kOfxParamTypeGroup) ++nGroups;

        char* label = 0;
        propGetString(p, kOfxPropLabel, 0, &label);
        char* script = 0;
        const bool hasScript = propGetString(p, kOfxParamPropScriptName, 0, &script) == kOfxStatOK;
        const std::string lbl = label ? label : "";
        const std::string scr = hasScript && script && *script ? script : info->name;

        check(!lbl.empty() || info->type == kOfxParamTypeGroup || info->type == kOfxParamTypePage,
              "param '" + info->name + "' has a label");

        if (info->type == kOfxParamTypeDouble) {
            check(p->doubles.count(kOfxParamPropMin) && p->doubles.count(kOfxParamPropMax),
                  "double param '" + info->name + "' has a range");
            check(p->doubles.count(kOfxParamPropDefault),
                  "double param '" + info->name + "' has a default");
            if (p->doubles.count(kOfxParamPropMin) && p->doubles.count(kOfxParamPropMax) &&
                p->doubles.count(kOfxParamPropDefault)) {
                const double lo = p->doubles[kOfxParamPropMin][0];
                const double hi = p->doubles[kOfxParamPropMax][0];
                const double def = p->doubles[kOfxParamPropDefault][0];
                check(lo <= hi, "double param '" + info->name + "' min <= max");
                check(def >= lo - 1e-9 && def <= hi + 1e-9,
                      "double param '" + info->name + "' default in range");
            }
        } else if (info->type == kOfxParamTypeInteger) {
            check(p->ints.count(kOfxParamPropDefault), "int param '" + info->name + "' has a default");
        } else if (info->type == kOfxParamTypeBoolean) {
            check(p->ints.count(kOfxParamPropDefault), "bool param '" + info->name + "' has a default");
        } else if (info->type == kOfxParamTypeChoice) {
            ++nChoices;
            const int n = p->strings.count(kOfxParamPropChoiceOption)
                              ? static_cast<int>(p->strings[kOfxParamPropChoiceOption].size())
                              : 0;
            check(n >= 2, "choice param '" + info->name + "' has >= 2 options");
            if (p->ints.count(kOfxParamPropDefault)) {
                const int def = p->ints[kOfxParamPropDefault][0];
                check(def >= 0 && def < n, "choice param '" + info->name + "' default in range");
            }
        } else if (info->type == kOfxParamTypeRGBA) {
            check(p->doubles.count(kOfxParamPropDefault) &&
                      p->doubles[kOfxParamPropDefault].size() == 4,
                  "rgba param '" + info->name + "' has a 4 component default");
        }

        check(seen[scr]++ == 0, "script name '" + scr + "' is unique");
        std::printf("  %-12s %-22s %s\n", info->type.c_str(), info->name.c_str(),
                    p->strings.count(kOfxParamPropChoiceOption)
                        ? ("[" + std::to_string(p->strings[kOfxParamPropChoiceOption].size()) +
                           " options]").c_str()
                        : "");
    }
    std::printf("\npages=%d groups=%d choices=%d\n", nPages, nGroups, nChoices);
    check(nPages == 1, "exactly one page");
    check(nGroups == 11, "eleven parameter groups");

    // ---- defaults that encode AE behaviour -------------------------------
    // These are the values the After Effects effect ships with; if a port
    // change moves one of them the look changes even at "reset".
    {
        struct ExpectDouble { const char* name; double value; };
        static const ExpectDouble doubles[] = {
            { "radius",          1000.0 },
            { "exposure",           1.0 },
            { "threshold",          0.0 },
            { "aspectRatio",        1.0 },
            { "pixelOffset",        1.0 },
            { "tonemapMix",       100.0 },
            { "resolution",        75.0 },
            { "customWeight",       0.9 },
            { "sourceOpacity",    100.0 },
            { "gamma",            100.0 },   // AE percent, 100 = 1.0
        };
        for (size_t i = 0; i < sizeof(doubles) / sizeof(doubles[0]); ++i) {
            ParamInfo* info = findParam(doubles[i].name);
            if (!info) { check(false, std::string("param '") + doubles[i].name + "' exists"); continue; }
            const std::vector<double>& d = info->props->doubles[kOfxParamPropDefault];
            check(!d.empty() && std::fabs(d[0] - doubles[i].value) < 1e-9,
                  std::string("default of '") + doubles[i].name + "' is " +
                      std::to_string(doubles[i].value));
        }

        struct ExpectChoice { const char* name; int index; int options; };
        static const ExpectChoice choices[] = {
            { "qualityPreset",      3, 7 },   // Standard
            { "glowMode",           0, 2 },   // Exponential
            { "blendMode",          0, 2 },   // Screen
            { "thresholdMode",      0, 2 },   // Luminance
            { "aberrationChannels", 0, 3 },   // Red & Green
            { "tintMode",           0, 3 },   // Off
            { "tonemapMode",        0, 7 },   // None
            { "view",               0, 5 },   // Final Result
            { "debugView",          2, 3 },   // None
        };
        for (size_t i = 0; i < sizeof(choices) / sizeof(choices[0]); ++i) {
            ParamInfo* info = findParam(choices[i].name);
            if (!info) { check(false, std::string("param '") + choices[i].name + "' exists"); continue; }
            PropSet* p = info->props;
            const int def = p->ints[kOfxParamPropDefault].empty() ? -1 : p->ints[kOfxParamPropDefault][0];
            const int n = p->strings.count(kOfxParamPropChoiceOption)
                              ? static_cast<int>(p->strings[kOfxParamPropChoiceOption].size()) : 0;
            check(n == choices[i].options, std::string("'") + choices[i].name + "' has " +
                                               std::to_string(choices[i].options) + " options");
            check(def == choices[i].index, std::string("default of '") + choices[i].name +
                                               "' is index " + std::to_string(choices[i].index));
        }

        // The Debug View order is load-bearing: buildParams maps choice index 0
        // to the X pass and 1 to the Y pass, with 2 = None (the default).  If
        // these labels are ever reordered, the default render starts dropping an
        // axis — which is exactly the bug this assertion exists to prevent.
        ParamInfo* dbg = findParam("debugView");
        if (dbg) {
            const std::vector<std::string>& opts =
                dbg->props->strings[kOfxParamPropChoiceOption];
            check(opts.size() == 3, "Debug View has three options");
            if (opts.size() == 3) {
                check(opts[0] == "Blur Width" && opts[1] == "Blur Height" && opts[2] == "None",
                      "Debug View options are {Blur Width, Blur Height, None}");
            }
        }

        ParamInfo* iter = findParam("glowIterations");
        if (iter) {
            const std::vector<int>& d = iter->props->ints[kOfxParamPropDefault];
            check(!d.empty() && d[0] == 8, "glowIterations default is 8");
            check(iter->props->ints[kOfxParamPropMin][0] == 6 &&
                      iter->props->ints[kOfxParamPropMax][0] == 10,
                  "glowIterations range is 6..10");
        } else {
            check(false, "param 'glowIterations' exists");
        }
    }

    // ---- instance --------------------------------------------------------
    // Instance clip state
    for (std::map<std::string, ClipInfo*>::iterator it = gClipByName.begin();
         it != gClipByName.end(); ++it) {
        propSetInt(it->second->props, kOfxImageClipPropConnected, 0, 1);
        propSetString(it->second->props, kOfxImageEffectPropComponents, 0, kOfxImageComponentRGBA);
    }

    PropSet* instProps = newPropSet();
    propSetString(instProps, kOfxPropType, 0, kOfxTypeImageEffectInstance);
    propSetString(instProps, kOfxImageEffectPropContext, 0, kOfxImageEffectContextFilter);
    propSetString(instProps, kOfxPropName, 0, "DeepGlow2_1");
    propSetString(instProps, kOfxPropLabel, 0, "Deep Glow 2");

    stat = plugin->mainEntry(kOfxActionCreateInstance, instProps, inArgs, outArgs);
    std::printf("createInstance: status %d\n", stat);
    check(stat == kOfxStatOK, "instance created");
    if (stat != kOfxStatOK) return 1;

    // ---- clip preferences ------------------------------------------------
    PropSet* prefsIn = newPropSet();
    PropSet* prefsOut = newPropSet();
    propSetDouble(prefsIn, kOfxImageEffectPropProjectSize, 0, 1920);
    propSetDouble(prefsIn, kOfxImageEffectPropProjectSize, 1, 1080);
    propSetDouble(prefsIn, kOfxImageEffectPropProjectPixelAspectRatio, 0, 1.0);
    propSetDouble(prefsIn, kOfxImageEffectPropFrameRate, 0, 25.0);
    propSetInt(prefsIn, kOfxImageEffectPropFrameRange, 0, 0);
    propSetInt(prefsIn, kOfxImageEffectPropFrameRange, 1, 100);
    stat = plugin->mainEntry(kOfxImageEffectActionGetClipPreferences, instProps, prefsIn, prefsOut);
    std::printf("getClipPreferences: status %d\n", stat);
    check(stat == kOfxStatOK, "clip preferences action succeeded");
    char* premult = 0;
    if (propGetString(prefsOut, kOfxImageEffectPropPreMultiplication, 0, &premult) == kOfxStatOK) {
        std::printf("  output premultiplication: %s\n", premult);
        check(std::strcmp(premult, kOfxImagePreMultiplied) == 0,
              "default output is premultiplied");
    } else {
        check(false, "clip preferences set the output premultiplication");
    }

    // ---- regions of interest --------------------------------------------
    PropSet* roiIn = newPropSet();
    PropSet* roiOut = newPropSet();
    propSetDouble(roiIn, kOfxImageEffectPropRenderScale, 0, 1.0);
    propSetDouble(roiIn, kOfxImageEffectPropRenderScale, 1, 1.0);
    propSetDouble(roiIn, kOfxImageEffectPropProjectSize, 0, 1920);
    propSetDouble(roiIn, kOfxImageEffectPropProjectSize, 1, 1080);
    propSetDouble(roiIn, kOfxImageEffectPropProjectOffset, 0, 0.0);
    propSetDouble(roiIn, kOfxImageEffectPropProjectOffset, 1, 0.0);
    propSetDouble(roiIn, kOfxImageEffectPropRegionOfInterest, 0, 0.0);
    propSetDouble(roiIn, kOfxImageEffectPropRegionOfInterest, 1, 0.0);
    propSetDouble(roiIn, kOfxImageEffectPropRegionOfInterest, 2, 1920.0);
    propSetDouble(roiIn, kOfxImageEffectPropRegionOfInterest, 3, 1080.0);
    propSetDouble(roiIn, kOfxPropTime, 0, 0.0);
    stat = plugin->mainEntry(kOfxImageEffectActionGetRegionsOfInterest, instProps, roiIn, roiOut);
    std::printf("getRegionsOfInterest: status %d\n", stat);
    check(stat == kOfxStatOK, "RoI action succeeded");
    int nRoi = 0;
    propGetDimension(roiOut, "OfxImageClipPropRoI_Source", &nRoi);
    check(nRoi == 4, "Source RoI is the full frame");
    if (nRoi == 4) {
        double rod[4];
        propGetDoubleN(roiOut, "OfxImageClipPropRoI_Source", 4, rod);
        std::printf("  source RoI: %.0f %.0f %.0f %.0f\n", rod[0], rod[1], rod[2], rod[3]);
        check(rod[2] > rod[0] && rod[3] > rod[1], "Source RoI is non-degenerate");
    }

    // ---- instance change notification ------------------------------------
    // kOfxStatReplyDefault is the normal "handled, host may do its default"
    // reply for this action, so accept it alongside kOfxStatOK.
    PropSet* chgIn = newPropSet();
    propSetString(chgIn, kOfxPropType, 0, kOfxTypeParameter);
    propSetString(chgIn, kOfxPropName, 0, "aberrationGlow");
    propSetString(chgIn, kOfxPropChangeReason, 0, kOfxChangeUserEdited);
    propSetDouble(chgIn, kOfxPropTime, 0, 0.0);
    propSetDouble(chgIn, kOfxImageEffectPropRenderScale, 0, 1.0);
    propSetDouble(chgIn, kOfxImageEffectPropRenderScale, 1, 1.0);
    stat = plugin->mainEntry(kOfxActionInstanceChanged, instProps, chgIn, outArgs);
    std::printf("instanceChanged(param): status %d\n", stat);
    check(stat == kOfxStatOK || stat == kOfxStatReplyDefault,
          "instanceChanged (parameter) succeeded");

    // ... and once for a clip, to exercise changedClip / refreshEnabled.
    propSetString(chgIn, kOfxPropType, 0, kOfxTypeClip);
    propSetString(chgIn, kOfxPropName, 0, "Mask");
    stat = plugin->mainEntry(kOfxActionInstanceChanged, instProps, chgIn, outArgs);
    std::printf("instanceChanged(clip): status %d\n", stat);
    check(stat == kOfxStatOK || stat == kOfxStatReplyDefault,
          "instanceChanged (clip) succeeded");

    // ---- teardown --------------------------------------------------------
    stat = plugin->mainEntry(kOfxActionDestroyInstance, instProps, inArgs, outArgs);
    std::printf("destroyInstance: status %d\n", stat);
    check(stat == kOfxStatOK, "instance destroyed");

    stat = plugin->mainEntry(kOfxActionUnload, 0, inArgs, outArgs);
    std::printf("unload: status %d\n", stat);
    check(stat == kOfxStatOK, "unload succeeded");

    std::printf("\n%s (%d failure%s)\n", gFailures == 0 ? "PASSED" : "FAILED", gFailures,
                gFailures == 1 ? "" : "s");
    return gFailures == 0 ? 0 : 1;
}
