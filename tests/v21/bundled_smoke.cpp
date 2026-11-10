#include <MaskedOcclusionCulling.h>
#include <picosha2.h>
#include <Base64.h>
#define CHECK(condition) do { if (!(condition)) return __LINE__; } while (false)
#include <string>
int main()
{
    CHECK(picosha2::hash256_hex_string(std::string("abc")) ==
        "ba7816bf8f01cfea414140de5dae2223b00361a396177a9cb410ff61f20015ad");
    CHECK(Base64::Base64::Encode("abc")=="YWJj");
    auto* culler = MaskedOcclusionCulling::Create();
    CHECK(culler);
    culler->SetResolution(64, 32);
    culler->SetNearClipPlane(0.1f);
    culler->ClearBuffer();
    unsigned w=0,h=0;culler->GetResolution(w,h);
    CHECK(w==64 && h==32);
    CHECK(culler->TestRect(-0.5f,-0.5f,0.5f,0.5f,1.f)!=MaskedOcclusionCulling::OCCLUDED);
    MaskedOcclusionCulling::Destroy(culler);
}
