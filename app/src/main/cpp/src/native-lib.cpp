#include <jni.h>
#include <string>
#include <thread>
#include <chrono>
#include <vector>
#include <cmath>
#include <cstring>
#include <cfloat>
#include <algorithm>
#include <map>

#include "BNM/Loading.hpp"
#include "BNMIncludes.hpp"

#include "imgui.h"
#include "imgui_internal.h"
#include "czrender.hpp"
#include "XRInput.hpp"
#include "geokar.h"

#define MINIMP3_IMPLEMENTATION
#define MINIMP3_NO_STDIO
#define MINIMP3_FLOAT_OUTPUT
#include "../extern/minimp3.h"
#include "../extern/czsounds.h"

// cz was here

static constexpr int kfbwidth  = 540;
static constexpr int kfbheight = 360;

static constexpr int kfbkbw    = 540;
static constexpr int kfbkbh    = 220;

static constexpr float kpanelmetersw = 0.40f;
static constexpr float kpanelmetersh = 0.27f;

static constexpr float kkbmetersw = 0.55f;
static constexpr float kkbmetersh = 0.22f;

static constexpr float khoverdist     = 0.40f;
static constexpr float ktriggerthresh = 0.55f;

struct czstate {
    bool initialized = false;

    ImGuiContext*    ctx        = nullptr;
    cz::framebuffer  framebuf {};
    cz::fontatlas    atlas {};

    GameObject* quad = nullptr;
    Texture2D*  tex  = nullptr;
    Material*   mat  = nullptr;

    ImGuiContext*    kbctx     = nullptr;
    cz::framebuffer  kbfb {};
    GameObject*      kbquad    = nullptr;
    Texture2D*       kbtex     = nullptr;
    Material*        kbmat     = nullptr;
    bool             kbprevclicked = false;

    bool toggleprev = false;
    bool isfrozen   = false;
    Transform* hostparent = nullptr;

    Vector3    localoffset    {0.0f, 0.10f, 0.04f};
    Quaternion localrotoffset = Quaternion::FromEuler(65.0f, 0.0f, 0.0f);

    bool menuopen       = false;
    bool prevshowheld   = false;

    bool movemode       = false;
    bool dragging       = false;
    Vector3 draggrablocal {};
    Vector3 dragstartoffset {};

    bool  speed     = false;
    bool  longarms  = false;
    float speedmul  = 1.5f;
    float armreach  = 4.0f;

    bool prevclicked = false;
    float animprogress = 0.0f;
    bool  prevmenuopen = false;

    AudioClip* openclip   = nullptr;
    AudioClip* closeclip  = nullptr;
    AudioClip* selectclip = nullptr;
    AudioClip* hoverclip  = nullptr;

    ImGuiID lasthoverid   = 0;
    ImGuiID kblasthoverid = 0;

    BNM::Class playercls;

    BNM::FieldBase jumpmultiplierfield;
    BNM::FieldBase maxarmlengthfield;
    bool  origcached         = false;
    float origjumpmultiplier = 0.0f;
    float origmaxarmlength   = 1.5f;
} gcz;

namespace gtag {
    static BNM::Class playercls() {
        static BNM::Class c = BNM::Class("GorillaLocomotion", "Player");
        return c;
    }
    static void* player() {
        if (!playercls()) return nullptr;
        static Method<void*> m = playercls().GetMethod("get_Instance");
        return m.IsValid() ? m() : nullptr;
    }

    struct fcacheentry { const char* name; BNM::FieldBase fb; };
    static fcacheentry fcache[32];
    static int fcache_n = 0;

    static BNM::FieldBase cachedfield(const char* name) {
        for (int i = 0; i < fcache_n; ++i) {
            if (fcache[i].name == name) return fcache[i].fb;
        }
        for (int i = 0; i < fcache_n; ++i) {
            if (std::strcmp(fcache[i].name, name) == 0) return fcache[i].fb;
        }
        BNM::FieldBase fb = playercls().GetField(name);
        if (fcache_n < 32) fcache[fcache_n++] = { name, fb };
        return fb;
    }

    static void setpf(const char* name, float val) {
        void* p = player();
        if (!p) return;
        auto fb = cachedfield(name);
        if (!fb.IsValid()) return;
        BNM::Field<float> f = fb;
        f[p].Set(val);
    }
    static float getpf(const char* name) {
        void* p = player();
        if (!p) return 0;
        auto fb = cachedfield(name);
        if (!fb.IsValid()) return 0;
        BNM::Field<float> f = fb;
        return f[p].Get();
    }
    static void setpb(const char* name, bool val) {
        void* p = player();
        if (!p) return;
        auto fb = cachedfield(name);
        if (!fb.IsValid()) return;
        BNM::Field<bool> f = fb;
        f[p].Set(val);
    }
    static void setpv3(const char* name, Vector3 val) {
        void* p = player();
        if (!p) return;
        auto fb = cachedfield(name);
        if (!fb.IsValid()) return;
        BNM::Field<Vector3> f = fb;
        f[p].Set(val);
    }
    static Transform* head() {
        void* p = player();
        if (!p) return nullptr;
        static BNM::FieldBase fb = cachedfield("headCollider");
        if (!fb.IsValid()) return nullptr;
        BNM::Field<Component*> f = fb;
        Component* c = f[p].Get();
        return c ? c->GetTransform() : nullptr;
    }
    static Transform* body() {
        void* p = player();
        if (!p) return nullptr;
        static BNM::FieldBase fb = cachedfield("bodyCollider");
        if (!fb.IsValid()) return nullptr;
        BNM::Field<Component*> f = fb;
        Component* c = f[p].Get();
        return c ? c->GetTransform() : nullptr;
    }
    static Rigidbody* rb() {
        void* p = player();
        if (!p) return nullptr;
        static BNM::FieldBase fb = cachedfield("playerRigidBody");
        if (!fb.IsValid()) return nullptr;
        BNM::Field<Rigidbody*> f = fb;
        return f[p].Get();
    }
    static GameObject* rootobj_cached = nullptr;
    static int          rootobj_cd     = 0;
    static Transform* roottrans() {
        if (!rootobj_cached || rootobj_cd <= 0) {
            rootobj_cached = GameObject::Find("GorillaPlayer");
            rootobj_cd = 600;
        }
        rootobj_cd--;
        return rootobj_cached ? rootobj_cached->GetTransform() : nullptr;
    }
}

struct cmod {
    const char* category;
    const char* name;
    bool        enabled    = false;
    bool        wasenabled = false;
    bool        oneshot    = false;
    void      (*tick)()    = nullptr;
    const char* desc       = nullptr;
};
static std::vector<cmod> g_mods;

static void addtick(const char* cat, const char* name, void(*fn)()) {
    g_mods.push_back({cat, name, false, false, false, fn, nullptr});
}
static void addtick(const char* cat, const char* name, const char* desc, void(*fn)()) {
    g_mods.push_back({cat, name, false, false, false, fn, desc});
}
static void addoneshot(const char* cat, const char* name, void(*fn)()) {
    g_mods.push_back({cat, name, false, false, true, fn, nullptr});
}
static void addoneshot(const char* cat, const char* name, const char* desc, void(*fn)()) {
    g_mods.push_back({cat, name, false, false, true, fn, desc});
}

struct pickedshader {
    Shader* shader = nullptr;
    bool    isurp  = false;
    const char* name = "";
    bool    intrinsictransparent = false;
};

static pickedshader findbestshader() {
    struct c { const char* name; bool urp; bool intrinsic; };
    c candidates[] = {
        {"Universal Render Pipeline/Unlit",      true,  false},
        {"Universal Render Pipeline/Simple Lit", true,  false},
        {"Universal Render Pipeline/Lit",        true,  false},
        {"Unlit/Transparent",                    false, true },
        {"Sprites/Default",                      false, true },
        {"UI/Default",                           false, true },
        {"Mobile/Particles/Alpha Blended",       false, true },
        {"Particles/Alpha Blended",              false, true },
        {"Hidden/Internal-Colored",              false, false},
    };
    for (auto& cand : candidates) {
        Shader* s = Shader::Find(cand.name);
        if (s) return {s, cand.urp, cand.name, cand.intrinsic};
    }
    return {nullptr, false, "", false};
}

static void matsetfloat(Material* mat, const char* name, float v) {
    static Method<void> m = Material::GetClass().GetMethod("SetFloat", {"name","value"});
    if (m.IsValid()) m[mat](BNM::CreateMonoString(name), v);
}

static void matenablekeyword(Material* mat, const char* name) {
    static Method<void> m = Material::GetClass().GetMethod("EnableKeyword", {"keyword"});
    if (m.IsValid()) m[mat](BNM::CreateMonoString(name));
}

static void matdisablekeyword(Material* mat, const char* name) {
    static Method<void> m = Material::GetClass().GetMethod("DisableKeyword", {"keyword"});
    if (m.IsValid()) m[mat](BNM::CreateMonoString(name));
}

static void matsetoverridetag(Material* mat, const char* tag, const char* val) {
    static Method<void> m = Material::GetClass().GetMethod("SetOverrideTag", {"tag","val"});
    if (m.IsValid()) m[mat](BNM::CreateMonoString(tag), BNM::CreateMonoString(val));
}

static void matsetrenderqueue(Material* mat, int q) {
    static Method<void> m = Material::GetClass().GetMethod("set_renderQueue", 1);
    if (m.IsValid()) m[mat](q);
}

static void matsettexturebyname(Material* mat, const char* name, void* tex) {
    static Method<void> m = Material::GetClass().GetMethod("SetTexture", {"name","value"});
    if (m.IsValid()) m[mat](BNM::CreateMonoString(name), tex);
}

static void matsettexturescalebyname(Material* mat, const char* name, Vector2 s) {
    static Method<void> m = Material::GetClass().GetMethod("SetTextureScale", {"name","value"});
    if (m.IsValid()) m[mat](BNM::CreateMonoString(name), s);
}

static void matsettextureoffsetbyname(Material* mat, const char* name, Vector2 o) {
    static Method<void> m = Material::GetClass().GetMethod("SetTextureOffset", {"name","value"});
    if (m.IsValid()) m[mat](BNM::CreateMonoString(name), o);
}

static void matsetmaintexture(Material* mat, Texture2D* tex) {
    static Method<void> m = Material::GetClass().GetMethod("set_mainTexture", 1);
    if (m.IsValid()) m[mat]((Object*)tex);
    matsettexturebyname(mat, "_MainTex", tex);
    matsettexturebyname(mat, "_BaseMap", tex);
}

static void matsetscaleandoffset(Material* mat, float sx, float sy, float ox, float oy) {
    Vector2 s(sx, sy), o(ox, oy);
    static Method<void> setscale  = Material::GetClass().GetMethod("set_mainTextureScale",  1);
    static Method<void> setoffset = Material::GetClass().GetMethod("set_mainTextureOffset", 1);
    if (setscale.IsValid())  setscale[mat](s);
    if (setoffset.IsValid()) setoffset[mat](o);
    matsettexturescalebyname (mat, "_MainTex", s);
    matsettextureoffsetbyname(mat, "_MainTex", o);
    matsettexturescalebyname (mat, "_BaseMap", s);
    matsettextureoffsetbyname(mat, "_BaseMap", o);
}

static void configurematerialtransparent(Material* mat, const pickedshader& sh) {
    matsetoverridetag(mat, "RenderType", "Transparent");
    matsetrenderqueue(mat, 3000);
    if (sh.isurp) {
        matsetfloat(mat, "_Surface",   1.0f);
        matsetfloat(mat, "_Blend",     0.0f);
        matsetfloat(mat, "_ZWrite",    0.0f);
        matsetfloat(mat, "_AlphaClip", 0.0f);
        matsetfloat(mat, "_SrcBlend",  5.0f);
        matsetfloat(mat, "_DstBlend", 10.0f);
        matsetfloat(mat, "_Cull",      0.0f);
        matdisablekeyword(mat, "_SURFACE_TYPE_OPAQUE");
        matenablekeyword (mat, "_SURFACE_TYPE_TRANSPARENT");
        matdisablekeyword(mat, "_ALPHATEST_ON");
        matdisablekeyword(mat, "_ALPHAPREMULTIPLY_ON");
        matenablekeyword (mat, "_ALPHABLEND_ON");
        return;
    }
    if (sh.intrinsictransparent) return;
    matsetfloat(mat, "_Mode",     3.0f);
    matsetfloat(mat, "_SrcBlend", 5.0f);
    matsetfloat(mat, "_DstBlend",10.0f);
    matsetfloat(mat, "_ZWrite",   0.0f);
    matsetfloat(mat, "_Cull",     0.0f);
    matdisablekeyword(mat, "_ALPHATEST_ON");
    matdisablekeyword(mat, "_ALPHAPREMULTIPLY_ON");
    matenablekeyword (mat, "_ALPHABLEND_ON");
}

static Material* creatematerial(const pickedshader& sh) {
    if (!sh.shader) return nullptr;
    Material* m = (Material*)Material::GetClass().CreateNewObjectParameters(sh.shader);
    if (!m) return nullptr;
    GameObject::DontDestroyOnLoad((Object*)m);
    return m;
}

static Texture2D* creatergba32texture(int w, int h) {
    Texture2D* tex = (Texture2D*)Texture2D::GetClass().CreateNewObjectParameters(w, h);
    if (!tex) { BNM_LOG_INFO("cz imgui: tex null"); return nullptr; }

    static Method<bool> reinit_named = Texture2D::GetClass().GetMethod("Reinitialize",
        {"width","height","textureFormat","hasMipMap"});
    static Method<bool> reinit_count = Texture2D::GetClass().GetMethod("Reinitialize", 4);
    static Method<bool> resize_named = Texture2D::GetClass().GetMethod("Resize",
        {"width","height","format","hasMipMap"});
    static Method<bool> resize_count = Texture2D::GetClass().GetMethod("Resize", 4);
    if (reinit_named.IsValid()) {
        reinit_named[tex](w, h, (int)TextureFormat::RGBA32, false);
        BNM_LOG_INFO("cz imgui: tex Reinitialize (named) ok");
    } else if (reinit_count.IsValid()) {
        reinit_count[tex](w, h, (int)TextureFormat::RGBA32, false);
        BNM_LOG_INFO("cz imgui: tex Reinitialize (count) ok");
    } else if (resize_named.IsValid()) {
        resize_named[tex](w, h, (int)TextureFormat::RGBA32, false);
        BNM_LOG_INFO("cz imgui: tex Resize (named) ok");
    } else if (resize_count.IsValid()) {
        resize_count[tex](w, h, (int)TextureFormat::RGBA32, false);
        BNM_LOG_INFO("cz imgui: tex Resize (count) ok");
    } else {
        BNM_LOG_INFO("cz imgui: NO reinit/resize available");
    }

    static Method<void> set_filtermode = BNM::Class("UnityEngine","Texture").GetMethod("set_filterMode", 1);
    if (set_filtermode.IsValid()) set_filtermode[tex](0);

    GameObject::DontDestroyOnLoad((Object*)tex);
    return tex;
}

static void textureloadrawandapply(Texture2D* tex, const uint8_t* src, int w, int h) {
    if (!tex || !src || w <= 0 || h <= 0) return;
    size_t rowbytes = (size_t)w * 4;
    size_t total    = rowbytes * (size_t)h;

    static Method<void> loadraw_ptr = Texture2D::GetClass().GetMethod("LoadRawTextureData", {"data","size"});
    static Method<void> loadraw_arr = Texture2D::GetClass().GetMethod("LoadRawTextureData", {"data"});
    static Method<void> apply2      = Texture2D::GetClass().GetMethod("Apply", {"updateMipmaps","makeNoLongerReadable"});
    static Method<void> apply0      = Texture2D::GetClass().GetMethod("Apply", 0);
    try {
        if (loadraw_ptr.IsValid()) {
            loadraw_ptr[tex]((void*)src, (int)total);
        } else if (loadraw_arr.IsValid()) {
            auto* arr = Array<uint8_t>::Create(total);
            if (arr) {
                uint8_t* dst = arr->GetData();
                if (dst) {
                    std::memcpy(dst, src, total);
                    loadraw_arr[tex](arr);
                }
            }
        }
        if (apply2.IsValid())      apply2[tex](false, false);
        else if (apply0.IsValid()) apply0[tex]();
    } catch (...) {
        BNM_LOG_INFO("cz imgui: upload threw");
    }
}

namespace czstyle {
    static ImGuiStyle& s() { return ImGui::GetStyle(); }
    static ImVec4*    cols() { return s().Colors; }

    static void background  (float r, float g, float b, float a = 1.0f) { cols()[ImGuiCol_WindowBg]       = ImVec4(r,g,b,a); }
    static void titlecolor  (float r, float g, float b, float a = 1.0f) { cols()[ImGuiCol_TitleBg]       = ImVec4(r,g,b,a*0.9f); cols()[ImGuiCol_TitleBgActive] = ImVec4(r,g,b,a); }
    static void accent      (float r, float g, float b, float a = 1.0f) { cols()[ImGuiCol_ButtonHovered] = ImVec4(r,g,b,a); cols()[ImGuiCol_HeaderHovered] = ImVec4(r,g,b,a); cols()[ImGuiCol_FrameBgHovered] = ImVec4(r,g,b,a); cols()[ImGuiCol_SliderGrab] = ImVec4(r,g,b,a); }
    static void buttoncolor (float r, float g, float b, float a = 1.0f) { cols()[ImGuiCol_Button]        = ImVec4(r,g,b,a); }
    static void framecolor  (float r, float g, float b, float a = 1.0f) { cols()[ImGuiCol_FrameBg]       = ImVec4(r,g,b,a); }
    static void headercolor (float r, float g, float b, float a = 1.0f) { cols()[ImGuiCol_Header]        = ImVec4(r,g,b,a); }
    static void textcolor   (float r, float g, float b, float a = 1.0f) { cols()[ImGuiCol_Text]          = ImVec4(r,g,b,a); }
    static void bordercolor (float r, float g, float b, float a = 1.0f) { cols()[ImGuiCol_Border]        = ImVec4(r,g,b,a); cols()[ImGuiCol_Separator] = ImVec4(r,g,b,a*0.8f); }
    static void checkmark   (float r, float g, float b, float a = 1.0f) { cols()[ImGuiCol_CheckMark]     = ImVec4(r,g,b,a); }

    static void rounding    (float r)        { s().WindowRounding = r; s().ChildRounding = r * 0.85f; s().FrameRounding = r * 0.75f; s().GrabRounding = r * 0.75f; s().PopupRounding = r * 0.85f; s().ScrollbarRounding = r * 0.75f; s().TabRounding = r * 0.7f; }
    static void bordersize  (float px)       { s().WindowBorderSize = px; }
    static void padding     (float x, float y) { s().WindowPadding = ImVec2(x,y); s().FramePadding = ImVec2(x * 0.85f, y * 0.6f); }
    static void spacing     (float x, float y) { s().ItemSpacing = ImVec2(x,y); }
    static void fontscale   (float scale)    { ImGui::GetIO().FontGlobalScale = scale; }
}

static void registermods() {
    if (!g_mods.empty()) return;

    addoneshot("Movement", "Speed Boost", "2.25x jump multiplier; you launch much faster on swings", []() {
        gtag::setpf("jumpMultiplier", 2.25f);
        gtag::setpf("maxJumpSpeed", 999.0f);
    });
    addoneshot("Movement", "BIG Speed Boost", "7.5x jump multiplier; full sendmode", []() {
        gtag::setpf("jumpMultiplier", 7.5f);
        gtag::setpf("maxJumpSpeed", 999.0f);
    });
    addoneshot("Movement", "Fix Speed", "reset speed back to default values", []() {
        gtag::setpf("jumpMultiplier", 1.1f);
        gtag::setpf("maxJumpSpeed", 6.5f);
    });
    addoneshot("Movement", "Long Arms", "extends max arm length so you can grab from far away", []() {
        gtag::setpf("maxArmLength", 999.9f);
    });
    addoneshot("Movement", "Fix Arms", "reset arm reach to the default 1.5m", []() {
        gtag::setpf("maxArmLength", 1.5f);
    });

    addtick("Movement", "Fly (R-Trigger)", "hold right trigger to glide forward following your head direction", []() {
        float t = XRInput::GetFloatFeature(Trigger, Right);
        if (t < 0.5f) return;
        Transform* h = gtag::head();
        Transform* r = gtag::roottrans();
        Rigidbody* body = gtag::rb();
        if (!h || !r) return;
        Vector3 step = h->GetForward() * Time::GetDeltaTime() * 12.0f;
        r->SetPosition(r->GetPosition() + step);
        if (body) body->SetVelocity(Vector3(0,0,0));
    });
    addtick("Movement", "Bark Fly (L-Trigger)", "hold left trigger to dash forward along your facing", []() {
        if (!XRInput::GetBoolFeature(TriggerButton, Left)) return;
        Rigidbody* body = gtag::rb();
        Transform* h = gtag::head();
        if (!body || !h) return;
        Vector3 fwd = h->GetForward();
        fwd.y = 0; fwd = Vector3::Normalize(fwd);
        body->AddForce(fwd * 7.0f * Time::GetDeltaTime() * 35.0f, ForceMode::VelocityChange);
    });
    addtick("Movement", "Bounce (L-Trigger)", "left trigger gives you an upward velocity kick", []() {
        if (!XRInput::GetBoolFeature(TriggerButton, Left)) return;
        Rigidbody* body = gtag::rb();
        if (body) body->AddForce(Vector3(0, 3.5f, 0), ForceMode::VelocityChange);
    });
    addtick("Movement", "Up + Down (Triggers)", "right trigger pushes up, left trigger pushes down", []() {
        Rigidbody* body = gtag::rb();
        if (!body) return;
        float dt = Time::GetDeltaTime();
        if (XRInput::GetFloatFeature(Trigger, Right) > 0.5f)
            body->SetVelocity(body->GetVelocity() + Vector3(0, 1, 0) * dt * 30.0f);
        if (XRInput::GetFloatFeature(Trigger, Left) > 0.5f)
            body->SetVelocity(body->GetVelocity() + Vector3(0,-1, 0) * dt * 30.0f);
    });
    addtick("Movement", "Gorilla Car (Triggers)", "right trigger drives forward, left drives backward (your body direction)", []() {
        Rigidbody* body = gtag::rb();
        Transform* b = gtag::body();
        if (!body || !b) return;
        float dt = Time::GetDeltaTime();
        if (XRInput::GetFloatFeature(Trigger, Right) > 0.5f)
            body->SetVelocity(body->GetVelocity() + b->GetForward() * dt * 30.0f);
        if (XRInput::GetFloatFeature(Trigger, Left) > 0.5f)
            body->SetVelocity(body->GetVelocity() - b->GetForward() * dt * 30.0f);
    });
    addtick("Movement", "PSA (R-Primary)", "right A pushes you forward at fast speed", []() {
        if (!XRInput::GetBoolFeature(PrimaryButton, Right)) return;
        Transform* b = gtag::body();
        Transform* r = gtag::roottrans();
        if (!b || !r) return;
        r->SetPosition(r->GetPosition() + b->GetForward() * Time::GetDeltaTime() * 5.5f);
    });
    addtick("Movement", "Slow PSA (R-Primary)", "right A pushes you forward, gentler speed", []() {
        if (!XRInput::GetBoolFeature(PrimaryButton, Right)) return;
        Transform* b = gtag::body();
        Transform* r = gtag::roottrans();
        if (!b || !r) return;
        r->SetPosition(r->GetPosition() + b->GetForward() * Time::GetDeltaTime() * 2.5f);
    });
    addtick("Movement", "Joystick Fly", "fly freely using both thumbsticks; left stick = horizontal, right stick Y = vertical", []() {
        Rigidbody* body = gtag::rb();
        Transform* b = gtag::body();
        if (!body || !b) return;
        body->AddForce(Physics::GetGravity() * -1.0f, ForceMode::Acceleration);
        Vector2 lj = XRInput::GetVector2Feature(Primary2DAxis, Left);
        Vector2 rj = XRInput::GetVector2Feature(Primary2DAxis, Right);
        Vector3 fwd = b->GetForward(); fwd.y = 0;
        Vector3 rgh = b->GetRight();   rgh.y = 0;
        Vector3 vel = rgh * lj.x + Vector3(0,1,0) * rj.y + fwd * lj.y;
        vel *= 10.0f;
        Vector3 cur = body->GetVelocity();
        body->SetVelocity(Vector3(cur.x + (vel.x - cur.x) * 0.12f,
                                  cur.y + (vel.y - cur.y) * 0.12f,
                                  cur.z + (vel.z - cur.z) * 0.12f));
    });

    addoneshot("Gravity", "Zero Gravity",   "completely disables gravity",       []() { Physics::SetGravity(Vector3(0,  0,    0)); });
    addoneshot("Gravity", "Low Gravity",    "moon-like gravity at -5 m/s^2",     []() { Physics::SetGravity(Vector3(0, -5,    0)); });
    addoneshot("Gravity", "High Gravity",   "5x heavier gravity at -50 m/s^2",   []() { Physics::SetGravity(Vector3(0,-50,    0)); });
    addoneshot("Gravity", "Normal Gravity", "reset to default earth gravity",    []() { Physics::SetGravity(Vector3(0, -9.81f,0)); });

    addtick("Body", "Force Tag Freeze", "lock movement as if tag-frozen",  []() { gtag::setpb("disableMovement", true); });
    addtick("Body", "No Tag Freeze",    "stay mobile even after being it", []() { gtag::setpb("disableMovement", false); });
    addtick("Body", "Slide Control",    "max-grip slide on all surfaces",  []() { gtag::setpf("defaultSlideFactor", 1.0f); });
    addtick("Body", "Headless", "hides your head (head collider scale 0)", []() {
        Transform* h = gtag::head();
        if (h) h->SetLocalScale(Vector3(0,0,0));
    });
    addoneshot("Body", "Fix Head", "restore your head visibility", []() {
        Transform* h = gtag::head();
        if (h) h->SetLocalScale(Vector3(1,1,1));
    });
    addtick("Body", "Backwards Head", "rotates the head collider 180 degrees", []() {
        Transform* h = gtag::head();
        if (!h) return;
        h->SetLocalRotation(Quaternion::FromEuler(180.0f, 0.0f, 0.0f));
    });

    addtick("Hands", "Stick Long Arms", "extends both hand offsets forward by 0.35m", []() {
        gtag::setpv3("rightHandOffset", Vector3(0, 0, 0.35f));
        gtag::setpv3("leftHandOffset",  Vector3(0, 0, 0.35f));
    });
    addoneshot("Hands", "Fix Stick Arms", "reset hand offsets to zero", []() {
        gtag::setpv3("rightHandOffset", Vector3(0, 0, 0));
        gtag::setpv3("leftHandOffset",  Vector3(0, 0, 0));
    });
}

static AudioClip* loadmp3clip(const char* name, const uint8_t* data, size_t size) {
    static Method<AudioClip*> create = AudioClip::GetClass().GetMethod("Create", 5);
    if (!create.IsValid() || !data || size == 0) return nullptr;

    static mp3dec_t dec;
    static bool dec_inited = false;
    if (!dec_inited) { mp3dec_init(&dec); dec_inited = true; }

    std::vector<float> pcm;
    pcm.reserve(size * 4);

    int channels = 0;
    int hz = 0;

    const uint8_t* p = data;
    size_t left = size;
    mp3d_sample_t framebuf[MINIMP3_MAX_SAMPLES_PER_FRAME];
    mp3dec_frame_info_t info{};

    while (left > 0) {
        int got = mp3dec_decode_frame(&dec, p, (int)left, framebuf, &info);
        if (info.frame_bytes <= 0) break;
        p    += info.frame_bytes;
        left -= info.frame_bytes;
        if (got > 0) {
            if (channels == 0) { channels = info.channels; hz = info.hz; }
            if (channels == 1) {
                pcm.insert(pcm.end(), framebuf, framebuf + got);
            } else {
                for (int i = 0; i < got; ++i) {
                    float l = framebuf[i * 2 + 0];
                    float r = framebuf[i * 2 + 1];
                    pcm.push_back((l + r) * 0.5f);
                }
            }
        }
    }

    int samples = (int)pcm.size();
    if (samples <= 0 || hz <= 0) return nullptr;

    auto* arr = Array<float>::Create((size_t)samples);
    if (!arr) return nullptr;
    float* d = arr->GetData();
    if (!d) return nullptr;
    std::memcpy(d, pcm.data(), (size_t)samples * sizeof(float));

    AudioClip* clip = nullptr;
    try { clip = create(BNM::CreateMonoString(name), samples, 1, hz, false); }
    catch (...) {}
    if (!clip) return nullptr;
    try { clip->SetData(arr, 0); } catch (...) {}
    GameObject::DontDestroyOnLoad((Object*)clip);
    return clip;
}

static void playclipat(AudioClip* clip, Vector3 pos, float vol) {
    if (!clip) return;
    static Method<void> playat3 = AudioSource::GetClass().GetMethod("PlayClipAtPoint", 3);
    static Method<void> playat2 = AudioSource::GetClass().GetMethod("PlayClipAtPoint", 2);
    try {
        if (playat3.IsValid())      playat3(clip, pos, vol);
        else if (playat2.IsValid()) playat2(clip, pos);
    } catch (...) {}
}

static void uifeedbackafterrender(ImGuiID& lasthid, const Vector3& pos) {
    ImGuiContext* g = ImGui::GetCurrentContext();
    if (!g) return;
    ImGuiID hid = g->HoveredId;
    if (hid != 0 && hid != lasthid) {
        playclipat(gcz.hoverclip, pos, 0.40f);
    }
    lasthid = hid;
    if (g->ActiveId != 0 && g->ActiveIdPreviousFrame == 0 && hid != 0) {
        playclipat(gcz.selectclip, pos, 0.42f);
    }
}

static void initimgui() {
    if (gcz.ctx) return;
    IMGUI_CHECKVERSION();
    gcz.ctx = ImGui::CreateContext();
    ImGui::SetCurrentContext(gcz.ctx);

    ImGuiIO& io = ImGui::GetIO();
    io.DisplaySize = ImVec2((float)kfbwidth, (float)kfbheight);
    io.IniFilename = nullptr;
    io.LogFilename = nullptr;
    io.DeltaTime = 1.0f / 72.0f;
    io.MouseDrawCursor = true;
    io.BackendFlags |= ImGuiBackendFlags_HasMouseCursors;
    io.ConfigFlags  |= ImGuiConfigFlags_NoMouseCursorChange;
    io.FontGlobalScale = 1.4f;

    io.Fonts->AddFontDefault();

    unsigned char* pixels = nullptr;
    int aw = 0, ah = 0;
    io.Fonts->GetTexDataAsRGBA32(&pixels, &aw, &ah);
    gcz.atlas.w = aw;
    gcz.atlas.h = ah;
    size_t bytes = (size_t)aw * ah * 4;
    uint8_t* copy = (uint8_t*)std::malloc(bytes);
    std::memcpy(copy, pixels, bytes);
    gcz.atlas.px = copy;
    io.Fonts->SetTexID((ImTextureID)(intptr_t)1);

    gcz.framebuf.w = kfbwidth;
    gcz.framebuf.h = kfbheight;
    gcz.framebuf.px = (uint8_t*)std::calloc((size_t)kfbwidth * kfbheight * 4, 1);

    czstyle::rounding    (10.0f);
    czstyle::bordersize  (1.0f);
    czstyle::padding     (10.0f, 8.0f);
    czstyle::spacing     (6.0f, 6.0f);
    czstyle::fontscale   (1.0f);

    czstyle::background  (0.06f, 0.07f, 0.10f, 0.78f);
    czstyle::bordercolor (0.35f, 0.55f, 0.95f, 0.55f);
    czstyle::titlecolor  (0.15f, 0.40f, 0.85f, 0.95f);
    czstyle::headercolor (0.18f, 0.30f, 0.55f, 0.55f);
    czstyle::buttoncolor (0.20f, 0.24f, 0.32f, 0.85f);
    czstyle::framecolor  (0.12f, 0.14f, 0.20f, 0.85f);
    czstyle::accent      (0.30f, 0.55f, 0.95f, 0.95f);
    czstyle::checkmark   (0.30f, 0.95f, 0.50f, 1.00f);
    czstyle::textcolor   (0.95f, 0.96f, 1.00f, 1.00f);

    gcz.kbctx = ImGui::CreateContext();
    ImGui::SetCurrentContext(gcz.kbctx);
    ImGuiIO& kbio = ImGui::GetIO();
    kbio.DisplaySize = ImVec2((float)kfbkbw, (float)kfbkbh);
    kbio.IniFilename = nullptr;
    kbio.LogFilename = nullptr;
    kbio.DeltaTime = 1.0f / 72.0f;
    kbio.MouseDrawCursor = true;
    kbio.BackendFlags |= ImGuiBackendFlags_HasMouseCursors;
    kbio.ConfigFlags  |= ImGuiConfigFlags_NoMouseCursorChange;
    kbio.FontGlobalScale = 1.2f;
    kbio.Fonts->AddFontDefault();
    unsigned char* kbpx = nullptr;
    int kbaw = 0, kbah = 0;
    kbio.Fonts->GetTexDataAsRGBA32(&kbpx, &kbaw, &kbah);
    kbio.Fonts->SetTexID((ImTextureID)(intptr_t)1);

    gcz.kbfb.w = kfbkbw;
    gcz.kbfb.h = kfbkbh;
    gcz.kbfb.px = (uint8_t*)std::calloc((size_t)kfbkbw * kfbkbh * 4, 1);

    czstyle::rounding    (10.0f);
    czstyle::bordersize  (1.0f);
    czstyle::padding     (14.0f, 12.0f);
    czstyle::spacing     (8.0f, 10.0f);
    czstyle::background  (0.06f, 0.07f, 0.10f, 0.85f);
    czstyle::bordercolor (0.35f, 0.55f, 0.95f, 0.55f);
    czstyle::buttoncolor (0.18f, 0.22f, 0.30f, 0.92f);
    czstyle::accent      (0.30f, 0.55f, 0.95f, 0.98f);
    czstyle::textcolor   (0.96f, 0.96f, 1.00f, 1.00f);

    ImGui::SetCurrentContext(gcz.ctx);
}

static void renderersetmaterial(Renderer* r, Material* m) {
    static Method<void> set_sharedmat = BNM::Class("UnityEngine","Renderer").GetMethod("set_sharedMaterial", 1);
    static Method<void> set_mat       = BNM::Class("UnityEngine","Renderer").GetMethod("set_material", 1);
    if (set_sharedmat.IsValid()) { set_sharedmat[r]((Object*)m); BNM_LOG_INFO("cz imgui: set_sharedMaterial ok"); return; }
    if (set_mat.IsValid())       { set_mat[r]((Object*)m);       BNM_LOG_INFO("cz imgui: set_material ok");       return; }
    BNM_LOG_INFO("cz imgui: NO material setter found");
}

static int texwidth (Texture2D* t) {
    static Method<int> m = BNM::Class("UnityEngine","Texture").GetMethod("get_width", 0);
    return m.IsValid() ? m[t]() : -1;
}
static int texheight(Texture2D* t) {
    static Method<int> m = BNM::Class("UnityEngine","Texture").GetMethod("get_height", 0);
    return m.IsValid() ? m[t]() : -1;
}

static void buildpanel(Transform* parent) {
    if (gcz.quad) return;

    pickedshader sh = findbestshader();
    if (!sh.shader) { BNM_LOG_INFO("cz imgui: no shader found"); return; }
    BNM_LOG_INFO("cz imgui: shader '%s' (%s)", sh.name, sh.isurp ? "URP" : "BIRP");

    gcz.mat = creatematerial(sh);
    if (!gcz.mat) { BNM_LOG_INFO("cz imgui: mat null"); return; }

    gcz.tex = creatergba32texture(kfbwidth, kfbheight);
    if (!gcz.tex) { BNM_LOG_INFO("cz imgui: tex null"); return; }

    int tw = texwidth (gcz.tex);
    int th = texheight(gcz.tex);
    BNM_LOG_INFO("cz imgui: tex %dx%d", tw, th);
    if (tw <= 0 || th <= 0) {
        BNM_LOG_INFO("cz imgui: tex has no native backing, aborting panel build");
        gcz.tex = nullptr;
        return;
    }

    matsetmaintexture(gcz.mat, gcz.tex);
    matsetscaleandoffset(gcz.mat, 1.0f, 1.0f, 0.0f, 0.0f);
    configurematerialtransparent(gcz.mat, sh);

    gcz.quad = GameObject::CreatePrimitive(PrimitiveType::Quad);
    if (!gcz.quad) { BNM_LOG_INFO("cz imgui: quad null"); return; }
    gcz.quad->SetName("cz imgui panel");
    gcz.quad->SetActive(false);

    Transform* qt = gcz.quad->GetTransform();
    qt->SetParent(parent, false);
    qt->SetLocalPosition(gcz.localoffset);
    qt->SetLocalRotation(gcz.localrotoffset);
    qt->SetLocalScale(Vector3(kpanelmetersw, kpanelmetersh, 1.0f));

    auto* col = (Collider*)gcz.quad->GetComponent(Collider::GetType());
    if (col) col->SetEnabled(false);

    auto* mr = (MeshRenderer*)gcz.quad->GetComponent(MeshRenderer::GetType());
    if (mr) renderersetmaterial((Renderer*)mr, gcz.mat);
    BNM_LOG_INFO("cz imgui: panel built mr=%p mat=%p", (void*)mr, (void*)gcz.mat);

    size_t basesize = (size_t)kfbwidth * kfbheight * 4;
    if (gcz.framebuf.px) {
        for (size_t i = 0; i < basesize; i += 4) {
            gcz.framebuf.px[i+0] = 255;
            gcz.framebuf.px[i+1] = 40;
            gcz.framebuf.px[i+2] = 40;
            gcz.framebuf.px[i+3] = 255;
        }
        textureloadrawandapply(gcz.tex, gcz.framebuf.px, kfbwidth, kfbheight);
        BNM_LOG_INFO("cz imgui: red flash uploaded");
    }

    if (!gcz.openclip)
        gcz.openclip   = loadmp3clip("czopen",   embopen,   embopensize);
    if (!gcz.closeclip)
        gcz.closeclip  = loadmp3clip("czclose",  embclose,  embclosesize);
    if (!gcz.selectclip)
        gcz.selectclip = loadmp3clip("czselect", embselect, embselectsize);
    if (!gcz.hoverclip)
        gcz.hoverclip  = loadmp3clip("czhover",  embhover,  embhoversize);

    pickedshader sh2 = findbestshader();
    gcz.kbmat = creatematerial(sh2);
    if (gcz.kbmat) {
        gcz.kbtex = creatergba32texture(kfbkbw, kfbkbh);
        if (gcz.kbtex) {
            matsetmaintexture(gcz.kbmat, gcz.kbtex);
            matsetscaleandoffset(gcz.kbmat, 1.0f, 1.0f, 0.0f, 0.0f);
            configurematerialtransparent(gcz.kbmat, sh2);
        }
    }
    gcz.kbquad = GameObject::CreatePrimitive(PrimitiveType::Quad);
    if (gcz.kbquad) {
        gcz.kbquad->SetName("cz imgui kb");
        gcz.kbquad->SetActive(false);
        Transform* kqt = gcz.kbquad->GetTransform();
        kqt->SetParent(gcz.quad->GetTransform(), false);
        kqt->SetLocalPosition(Vector3(0.0f, -1.00f, 0.0f));
        kqt->SetLocalRotation(Quaternion::FromEuler(6.0f, 0.0f, 0.0f));
        kqt->SetLocalScale(Vector3(kkbmetersw / kpanelmetersw, kkbmetersh / kpanelmetersh, 1.0f));
        auto* kcol = (Collider*)gcz.kbquad->GetComponent(Collider::GetType());
        if (kcol) kcol->SetEnabled(false);
        auto* kmr = (MeshRenderer*)gcz.kbquad->GetComponent(MeshRenderer::GetType());
        if (kmr && gcz.kbmat) renderersetmaterial((Renderer*)kmr, gcz.kbmat);
    }
}

namespace czkb {
    static char*  target     = nullptr;
    static size_t targetsize = 0;
    static bool   open       = false;
    static bool   shift      = false;

    static void show(char* buf, size_t size) {
        target = buf;
        targetsize = size;
        open = true;
        shift = false;
    }
    static void close() { open = false; target = nullptr; targetsize = 0; }
    static bool active() { return open; }

    static void appendc(char c) {
        if (!target || targetsize == 0) return;
        size_t len = std::strlen(target);
        if (len + 1 >= targetsize) return;
        target[len] = c;
        target[len + 1] = 0;
    }
    static void backspace() {
        if (!target) return;
        size_t len = std::strlen(target);
        if (len > 0) target[len - 1] = 0;
    }
    static void clearall() {
        if (target && targetsize > 0) target[0] = 0;
    }

    static void drawrow(const char* row, float keysize) {
        int n = (int)std::strlen(row);
        if (n == 0) return;
        float spacing = ImGui::GetStyle().ItemSpacing.x;
        float avail = ImGui::GetContentRegionAvail().x;
        float row_w = n * keysize + (n - 1) * spacing;
        float pad = (avail - row_w) * 0.5f;
        if (pad > 0) ImGui::SetCursorPosX(ImGui::GetCursorPosX() + pad);
        for (int i = 0; i < n; ++i) {
            char ch = row[i];
            char label[2] = { shift ? (char)std::toupper((unsigned char)ch) : ch, 0 };
            if (ImGui::Button(label, ImVec2(keysize, keysize))) {
                appendc(shift ? (char)std::toupper((unsigned char)ch) : ch);
            }
            if (i + 1 < n) ImGui::SameLine();
        }
    }

    static void drawkeys(float keysize) {
        drawrow("1234567890", keysize);
        drawrow("qwertyuiop", keysize);
        drawrow("asdfghjkl",  keysize);

        float spacing = ImGui::GetStyle().ItemSpacing.x;
        float avail = ImGui::GetContentRegionAvail().x;
        float specw = keysize * 2.0f;
        float row3_w = specw + spacing + 7 * keysize + 6 * spacing;
        float pad3 = (avail - row3_w) * 0.5f;
        if (pad3 > 0) ImGui::SetCursorPosX(ImGui::GetCursorPosX() + pad3);
        ImGui::PushStyleColor(ImGuiCol_Button,
            shift ? ImGui::GetStyle().Colors[ImGuiCol_ButtonActive]
                  : ImGui::GetStyle().Colors[ImGuiCol_Button]);
        if (ImGui::Button("shift", ImVec2(specw, keysize))) shift = !shift;
        ImGui::PopStyleColor();
        ImGui::SameLine();
        const char* row3 = "zxcvbnm";
        int n3 = (int)std::strlen(row3);
        for (int i = 0; i < n3; ++i) {
            char ch = row3[i];
            char label[2] = { shift ? (char)std::toupper((unsigned char)ch) : ch, 0 };
            if (ImGui::Button(label, ImVec2(keysize, keysize))) {
                appendc(shift ? (char)std::toupper((unsigned char)ch) : ch);
            }
            if (i + 1 < n3) ImGui::SameLine();
        }

        float spacew = keysize * 5.0f;
        float backw  = keysize * 1.5f;
        float donew  = keysize * 1.5f;
        float row4_w = backw + spacing + spacew + spacing + donew;
        float pad4 = (avail - row4_w) * 0.5f;
        if (pad4 > 0) ImGui::SetCursorPosX(ImGui::GetCursorPosX() + pad4);
        if (ImGui::Button("back", ImVec2(backw, keysize))) backspace();
        ImGui::SameLine();
        if (ImGui::Button(" ", ImVec2(spacew, keysize))) appendc(' ');
        ImGui::SameLine();
        if (ImGui::Button("done", ImVec2(donew, keysize))) close();
    }

    static void drawstandalone() {
        if (!open) return;
        ImGuiIO& io = ImGui::GetIO();
        ImGui::SetNextWindowPos(ImVec2(0, 0));
        ImGui::SetNextWindowSize(io.DisplaySize);
        ImGui::Begin("##kbwin", nullptr,
            ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoResize |
            ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoCollapse |
            ImGuiWindowFlags_NoSavedSettings | ImGuiWindowFlags_NoBringToFrontOnFocus |
            ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse);
        drawkeys(32.0f);
        ImGui::End();
    }
}

static float geokariconx = 0.0f;
static float geokaricony = 0.0f;
static float geokariconsize = 0.0f;

static char g_searchbuf[64] = {};

static bool modmatches(const char* name, const char* q) {
    if (!q || !*q) return true;
    for (const char* s = name; *s; ++s) {
        const char* a = s; const char* b = q;
        while (*a && *b &&
               std::tolower((unsigned char)*a) == std::tolower((unsigned char)*b)) {
            ++a; ++b;
        }
        if (!*b) return true;
    }
    return false;
}

static bool projectrighthandontoplane(Transform* panelt, Vector3 rightpos,
                                      float pw, float ph, int fbw, int fbh,
                                      ImVec2& outmouse, float& outdepth) {
    Vector3 panelpos = panelt->GetPosition();
    Quaternion panelrot = panelt->GetRotation();
    Quaternion invrot = Quaternion::Inverse(panelrot);
    Vector3 local = invrot * (rightpos - panelpos);
    float halfw = pw * 0.5f;
    float halfh = ph * 0.5f;
    float u = (local.x + halfw) / pw;
    float v = 1.0f - (local.y + halfh) / ph;
    outmouse = ImVec2(u * (float)fbw, v * (float)fbh);
    outdepth = local.z;
    bool over = u >= -0.05f && u <= 1.05f && v >= -0.05f && v <= 1.05f;
    bool nearby = std::fabs(local.z) < khoverdist;
    return over && nearby;
}

static bool projectrighthandtoimgui(Transform* panelt, Vector3 rightpos,
                                    ImVec2& outmouse, float& outdepth) {
    Vector3 panelpos = panelt->GetPosition();
    Quaternion panelrot = panelt->GetRotation();
    Quaternion invrot = Quaternion::Inverse(panelrot);
    Vector3 local = invrot * (rightpos - panelpos);
    float halfw = kpanelmetersw * 0.5f;
    float halfh = kpanelmetersh * 0.5f;

    float u = (local.x + halfw) / kpanelmetersw;
    float v = 1.0f - (local.y + halfh) / kpanelmetersh;

    outmouse = ImVec2(u * (float)kfbwidth, v * (float)kfbheight);
    outdepth = local.z;
    bool over   = u >= -0.05f && u <= 1.05f && v >= -0.05f && v <= 1.05f;
    bool nearby = std::fabs(local.z) < khoverdist;
    return over && nearby;
}

static void buildui() {
    ImGuiIO& io = ImGui::GetIO();

    ImGui::SetNextWindowPos(ImVec2(0, 0));
    ImGui::SetNextWindowSize(io.DisplaySize);
    ImGuiWindowFlags flags = ImGuiWindowFlags_NoResize
                           | ImGuiWindowFlags_NoMove
                           | ImGuiWindowFlags_NoCollapse
                           | ImGuiWindowFlags_NoSavedSettings
                           | ImGuiWindowFlags_NoBringToFrontOnFocus;
    ImGui::Begin("cz imgui", nullptr, flags);

    {
        ImGuiWindow* win = ImGui::GetCurrentWindow();
        float tbh = win->TitleBarHeight;
        geokariconsize = tbh - 4.0f;
        geokariconx = win->Pos.x + win->SizeFull.x - 4.0f - geokariconsize;
        geokaricony = win->Pos.y + 2.0f;
    }

    ImGui::TextDisabled("%.0f fps", io.Framerate);
    ImGui::SameLine();
    float avail = ImGui::GetContentRegionAvail().x;
    if (avail > 110.0f) {
        ImGui::Dummy(ImVec2(avail - 110.0f, 0));
        ImGui::SameLine();
    }
    if (ImGui::SmallButton(gcz.movemode ? "drop here" : "move menu")) {
        gcz.movemode = !gcz.movemode;
        if (!gcz.movemode) gcz.dragging = false;
    }

    static std::vector<std::pair<const char*, std::vector<int>>> bycat;
    static int lastmodcount = -1;
    if ((int)g_mods.size() != lastmodcount) {
        bycat.clear();
        for (int i = 0; i < (int)g_mods.size(); ++i) {
            const char* cn = g_mods[i].category;
            bool found = false;
            for (auto& p : bycat) {
                if (p.first == cn || std::strcmp(p.first, cn) == 0) {
                    p.second.push_back(i);
                    found = true;
                    break;
                }
            }
            if (!found) bycat.push_back({cn, {i}});
        }
        lastmodcount = (int)g_mods.size();
    }

    {
        const char* label = g_searchbuf[0] ? g_searchbuf : "search...";
        float btnw = 140.0f;
        if (ImGui::Button(label, ImVec2(btnw, 0))) {
            czkb::show(g_searchbuf, sizeof(g_searchbuf));
        }
        if (g_searchbuf[0]) {
            ImGui::SameLine();
            if (ImGui::Button("x##clr", ImVec2(0, 0))) {
                czkb::clearall();
                g_searchbuf[0] = 0;
            }
        }
    }

    if (g_searchbuf[0]) {
        ImGui::BeginChild("##searchbody", ImVec2(0, 0), false,
                          ImGuiWindowFlags_HorizontalScrollbar);
        for (auto& m : g_mods) {
            if (!modmatches(m.name, g_searchbuf)) continue;
            ImGui::Checkbox(m.name, &m.enabled);
            if (m.desc && ImGui::IsItemHovered())
                ImGui::SetTooltip("%s", m.desc);
            ImGui::SameLine();
            ImGui::TextDisabled("[%s]", m.category);
        }
        ImGui::EndChild();
    } else {
        ImGuiTabBarFlags tabflags = ImGuiTabBarFlags_FittingPolicyResizeDown
                                  | ImGuiTabBarFlags_NoCloseWithMiddleMouseButton;
        if (ImGui::BeginTabBar("##cats", tabflags)) {
            for (auto& cat : bycat) {
                if (ImGui::BeginTabItem(cat.first)) {
                    ImGui::BeginChild("##catbody", ImVec2(0, 0), false,
                                      ImGuiWindowFlags_HorizontalScrollbar);
                    for (int idx : cat.second) {
                        cmod& m = g_mods[idx];
                        ImGui::Checkbox(m.name, &m.enabled);
                        if (m.desc && ImGui::IsItemHovered())
                            ImGui::SetTooltip("%s", m.desc);
                    }
                    ImGui::EndChild();
                    ImGui::EndTabItem();
                }
            }

            if (ImGui::BeginTabItem("settings")) {
                ImGui::BeginChild("##setbody", ImVec2(0, 0), false,
                                  ImGuiWindowFlags_HorizontalScrollbar);
                ImGuiStyle& s = ImGui::GetStyle();
                ImGui::SliderFloat("window rounding",  &s.WindowRounding, 0.0f, 30.0f, "%.0f");
                ImGui::SliderFloat("frame rounding",   &s.FrameRounding,  0.0f, 20.0f, "%.0f");
                ImGui::SliderFloat("border thickness", &s.WindowBorderSize, 0.0f, 4.0f, "%.1f");
                ImGui::SliderFloat("font scale",       &io.FontGlobalScale, 0.5f, 2.5f, "%.2f");
                ImGui::ColorEdit4("background", (float*)&s.Colors[ImGuiCol_WindowBg]);
                ImGui::ColorEdit4("border",     (float*)&s.Colors[ImGuiCol_Border]);
                ImGui::ColorEdit4("title",      (float*)&s.Colors[ImGuiCol_TitleBgActive]);
                ImGui::ColorEdit4("accent",     (float*)&s.Colors[ImGuiCol_ButtonHovered]);
                ImGui::ColorEdit4("text",       (float*)&s.Colors[ImGuiCol_Text]);
                ImGui::EndChild();
                ImGui::EndTabItem();
            }

            ImGui::EndTabBar();
        }
    }

    ImGui::End();
}

static void* getplayerinstance() {
    if (!gcz.playercls) gcz.playercls = BNM::Class("GorillaLocomotion", "Player");
    if (!gcz.playercls) return nullptr;
    static Method<void*> get_instance = gcz.playercls.GetMethod("get_Instance");
    if (!get_instance.IsValid()) return nullptr;
    return get_instance();
}

static GameObject* findhand(const char* a, const char* b, const char* c) {
    if (auto* g = GameObject::Find(a)) return g;
    if (auto* g = GameObject::Find(b)) return g;
    if (auto* g = GameObject::Find(c)) return g;
    return nullptr;
}

static void tick(void*) {
    GameObject* leftctrl  = findhand("LeftHand Controller",  "LeftHandAnchor",  "Left Controller");
    GameObject* rightctrl = findhand("RightHand Controller", "RightHandAnchor", "Right Controller");
    if (!leftctrl || !rightctrl) {
        static int warncount = 0;
        if (warncount++ < 3) BNM_LOG_INFO("cz imgui: hands not found yet");
        return;
    }
    static bool announcedhands = false;
    if (!announcedhands) {
        BNM_LOG_INFO("cz imgui: hands found");
        announcedhands = true;
    }

    Transform* lefthand  = leftctrl->GetTransform();
    Transform* righthand = rightctrl->GetTransform();
    if (!lefthand || !righthand) return;

    for (auto& m : g_mods) {
        bool rising = m.enabled && !m.wasenabled;
        if (m.tick) {
            try {
                if (m.oneshot) { if (rising) m.tick(); }
                else           { if (m.enabled) m.tick(); }
            } catch (...) {}
        }
        m.wasenabled = m.enabled;
    }

    if (!gcz.initialized) {
        initimgui();
        registermods();
        buildpanel(lefthand);
        gcz.initialized = true;
    }
    if (!gcz.quad || !gcz.tex || !gcz.mat) return;

    Transform* panelt = gcz.quad->GetTransform();

    Vector3 leftpos = lefthand->GetPosition();
    Quaternion leftrot = lefthand->GetRotation();
    Vector3 rightpos = righthand->GetPosition();

    float righttrigger = XRInput::GetFloatFeature(Trigger, Right);
    bool  triggerheld  = righttrigger > ktriggerthresh;

    bool overpanel = false;
    {
        ImVec2 mousetmp; float depthtmp;
        overpanel = projectrighthandtoimgui(panelt, rightpos, mousetmp, depthtmp);
    }

    bool clickwhileover = overpanel && triggerheld;

    if (gcz.movemode) {
        if (gcz.dragging) {
            if (triggerheld) {
                Quaternion invleftrot = Quaternion::Inverse(leftrot);
                Vector3 rightinleft = invleftrot * (rightpos - leftpos);
                Vector3 delta = rightinleft - gcz.draggrablocal;
                gcz.localoffset = gcz.dragstartoffset + delta;
                if (gcz.isfrozen)
                    panelt->SetPosition(leftpos + leftrot * gcz.localoffset);
            } else {
                gcz.dragging = false;
                gcz.movemode = false;
            }
        } else if (clickwhileover && !gcz.prevclicked) {
            gcz.dragging = true;
            Quaternion invleftrot = Quaternion::Inverse(leftrot);
            gcz.draggrablocal = invleftrot * (rightpos - leftpos);
            gcz.dragstartoffset = gcz.localoffset;
        }
    }

    bool togglenow = XRInput::GetBoolFeature(SecondaryButton, Left)
                  || XRInput::GetBoolFeature(GripButton,      Left);
    if (togglenow && !gcz.toggleprev) {
        gcz.menuopen = !gcz.menuopen;
        if (gcz.menuopen) {
            Transform* qt = gcz.quad->GetTransform();
            gcz.hostparent = lefthand;
            qt->SetLocalPosition(gcz.localoffset);
            qt->SetLocalRotation(gcz.localrotoffset);
            qt->SetParent(nullptr, true);
            gcz.isfrozen = true;
        } else {
            Transform* qt = gcz.quad->GetTransform();
            qt->SetParent(gcz.hostparent ? gcz.hostparent : lefthand, false);
            qt->SetLocalPosition(gcz.localoffset);
            qt->SetLocalRotation(gcz.localrotoffset);
            gcz.isfrozen = false;
            czkb::close();
            gcz.movemode = false;
            gcz.dragging = false;
        }
    }
    gcz.toggleprev = togglenow;

    if (gcz.menuopen != gcz.prevmenuopen) {
        Vector3 pos = leftpos;
        Transform* h = gtag::head();
        if (h) pos = h->GetPosition();
        playclipat(gcz.menuopen ? gcz.openclip : gcz.closeclip, pos, 0.55f);
    }
    gcz.prevmenuopen = gcz.menuopen;

    const float animspeed = 1.0f / 0.75f;
    float dt = std::fmax(1.0f / 240.0f, Time::GetUnscaledDeltaTime());
    if (gcz.menuopen) {
        gcz.animprogress = std::fmin(1.0f, gcz.animprogress + dt * animspeed);
    } else {
        gcz.animprogress = std::fmax(0.0f, gcz.animprogress - dt * animspeed);
    }

    bool visible = gcz.animprogress > 0.001f;
    if (gcz.quad->GetActiveSelf() != visible) {
        gcz.quad->SetActive(visible);
    }

    if (!visible) {
        gcz.prevclicked = false;
        return;
    }

    float p = gcz.animprogress;
    float eased = 1.0f - (1.0f - p) * (1.0f - p) * (1.0f - p);

    if (!gcz.isfrozen) {
        panelt->SetLocalPosition(gcz.localoffset);
        panelt->SetLocalRotation(gcz.localrotoffset);
    }
    panelt->SetLocalScale(Vector3(kpanelmetersw * eased, kpanelmetersh * eased, 1.0f));

    if (!gcz.menuopen) {
        gcz.prevclicked = false;
        return;
    }

    ImGui::SetCurrentContext(gcz.ctx);
    ImGuiIO& io = ImGui::GetIO();
    io.DeltaTime = std::fmax(1.0f / 240.0f, Time::GetUnscaledDeltaTime());
    io.DisplaySize = ImVec2((float)kfbwidth, (float)kfbheight);

    ImVec2 mouse; float depth;
    bool over = projectrighthandtoimgui(panelt, rightpos, mouse, depth);

    Vector2 lstick = XRInput::GetVector2Feature(Primary2DAxis, Left);
    Vector2 rstick = XRInput::GetVector2Feature(Primary2DAxis, Right);
    float sticky = std::fabs(lstick.y) > std::fabs(rstick.y) ? lstick.y : rstick.y;
    bool scrolling = std::fabs(sticky) > 0.25f;
    bool freezemouse = scrolling || gcz.movemode;

    if (!freezemouse) {
        if (over) io.AddMousePosEvent(mouse.x, mouse.y);
        else      io.AddMousePosEvent(-FLT_MAX, -FLT_MAX);
    }

    float wheely = scrolling ? sticky * 0.5f : 0.0f;
    if (wheely != 0.0f) io.AddMouseWheelEvent(0.0f, wheely);

    bool clicknow = over && triggerheld && !scrolling;
    if (clicknow != gcz.prevclicked) {
        if (!gcz.movemode) io.AddMouseButtonEvent(0, clicknow);
        else if (!clicknow) io.AddMouseButtonEvent(0, false);
        if (clicknow) {
            try { XRInput::SendHapticImpulse(Controller::Right, 0.5f, 0.04f); } catch (...) {}
        }
    }
    gcz.prevclicked = clicknow;

    ImGui::NewFrame();
    buildui();
    ImGui::Render();

    {
        Vector3 audiopos = leftpos;
        Transform* h = gtag::head();
        if (h) audiopos = h->GetPosition();
        uifeedbackafterrender(gcz.lasthoverid, audiopos);
    }

    static float lastmx = -99999.0f;
    static float lastmy = -99999.0f;
    static bool  lastclickreg = false;
    static int   idlecount = 0;
    bool inputchanged = false;
    if (over) {
        if (std::fabs(mouse.x - lastmx) > 1.0f ||
            std::fabs(mouse.y - lastmy) > 1.0f) inputchanged = true;
        lastmx = mouse.x;
        lastmy = mouse.y;
    } else if (lastmx > -99000.0f) {
        inputchanged = true;
        lastmx = -99999.0f;
    }
    if (clicknow != lastclickreg) inputchanged = true;
    lastclickreg = clicknow;
    if (std::fabs(sticky) > 0.18f) inputchanged = true;
    if (gcz.animprogress > 0.001f && gcz.animprogress < 0.999f) inputchanged = true;

    bool shouldraster = inputchanged || (idlecount >= 2);
    if (shouldraster) {
        ImDrawData* dd = ImGui::GetDrawData();
        if (dd && dd->CmdListsCount > 0 && dd->TotalVtxCount > 0) {
            cz::rasterizedrawdata(dd, gcz.framebuf, gcz.atlas);
            if (geokariconsize > 0.0f) {
                int isz = (int)geokariconsize;
                cz::blitscaled(gcz.framebuf,
                    (int)geokariconx, (int)geokaricony, isz, isz,
                    geokarrgba, geokarw, geokarch);
            }
            if (gcz.framebuf.px)
                textureloadrawandapply(gcz.tex, gcz.framebuf.px, kfbwidth, kfbheight);
        }
        idlecount = 0;
    } else {
        idlecount++;
    }

    if (gcz.kbquad) {
        bool kbvisible = czkb::active() && visible;
        if (gcz.kbquad->GetActiveSelf() != kbvisible) {
            gcz.kbquad->SetActive(kbvisible);
        }
        if (kbvisible && gcz.kbctx) {
            Transform* kbpanelt = gcz.kbquad->GetTransform();
            ImGui::SetCurrentContext(gcz.kbctx);
            ImGuiIO& kbio = ImGui::GetIO();
            kbio.DeltaTime    = io.DeltaTime;
            kbio.DisplaySize  = ImVec2((float)kfbkbw, (float)kfbkbh);
            ImVec2 kbmouse; float kbdepth;
            bool kbover = projectrighthandontoplane(kbpanelt, rightpos,
                kkbmetersw, kkbmetersh, kfbkbw, kfbkbh, kbmouse, kbdepth);
            if (kbover) kbio.AddMousePosEvent(kbmouse.x, kbmouse.y);
            else        kbio.AddMousePosEvent(-FLT_MAX, -FLT_MAX);
            bool kbclick = kbover && triggerheld;
            if (kbclick != gcz.kbprevclicked)
                kbio.AddMouseButtonEvent(0, kbclick);

            static float kblastmx = -99999.0f;
            static float kblastmy = -99999.0f;
            static int   kbidle   = 0;
            bool kbchanged = false;
            if (kbover) {
                if (std::fabs(kbmouse.x - kblastmx) > 1.0f ||
                    std::fabs(kbmouse.y - kblastmy) > 1.0f) kbchanged = true;
                kblastmx = kbmouse.x;
                kblastmy = kbmouse.y;
            } else if (kblastmx > -99000.0f) {
                kbchanged = true;
                kblastmx = -99999.0f;
            }
            if (kbclick != gcz.kbprevclicked) kbchanged = true;
            gcz.kbprevclicked = kbclick;

            ImGui::NewFrame();
            czkb::drawstandalone();
            ImGui::Render();

            {
                Vector3 kbaudiopos = leftpos;
                Transform* hh = gtag::head();
                if (hh) kbaudiopos = hh->GetPosition();
                uifeedbackafterrender(gcz.kblasthoverid, kbaudiopos);
            }

            bool kbshouldraster = kbchanged || (kbidle >= 3);
            if (kbshouldraster) {
                ImDrawData* kbdd = ImGui::GetDrawData();
                if (kbdd && kbdd->CmdListsCount > 0 && kbdd->TotalVtxCount > 0) {
                    cz::rasterizedrawdata(kbdd, gcz.kbfb, gcz.atlas);
                    if (gcz.kbfb.px)
                        textureloadrawandapply(gcz.kbtex, gcz.kbfb.px, kfbkbw, kfbkbh);
                }
                kbidle = 0;
            } else {
                kbidle++;
            }
            ImGui::SetCurrentContext(gcz.ctx);
        }
    }
}

static void (*orig_ccupdate)(void*) = nullptr;
static void hook_ccupdate(void* self) {
    if (orig_ccupdate) orig_ccupdate(self);
    try { tick(self); } catch (...) {}
}

static void onloaded() {
    gcz.playercls = BNM::Class("GorillaLocomotion", "Player");

    auto cc = BNM::Class("GorillaNetworking", "CosmeticsController");
    if (!cc) { BNM_LOG_INFO("cz imgui: cc class missing"); return; }
    auto update = cc.GetMethod("Update", 0);
    if (!update.IsValid()) { BNM_LOG_INFO("cz imgui: cc has no Update"); return; }
    BNM::InvokeHook(update, hook_ccupdate, orig_ccupdate);
    BNM_LOG_INFO("cz imgui: ready");
}

extern "C" JNIEXPORT jint JNICALL JNI_OnLoad(JavaVM* vm, void*) {
    JNIEnv* env = nullptr;
    vm->GetEnv((void**)&env, JNI_VERSION_1_6);
    BNM::Loading::AddOnLoadedEvent(onloaded);
    BNM::Loading::TryLoadByJNI(env);
    return JNI_VERSION_1_6;
}
