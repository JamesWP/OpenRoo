// Pixel-equivalence check for PixelConvert_ToTexture (src/d3d/pixelconvert.cpp).
//
// Decodes every TGA under a directory with Image_LoadTGA, converts it to nine
// device pixel formats, and compares the bytes with the original loader's
// (texturetga.cpp as it was before the Image refactor, reproduced below
// verbatim apart from writing into a plain buffer).  Prints the mismatches
// and exits non-zero if there are any.
//
//   R=$(git rev-parse --show-toplevel)
//   i686-w64-mingw32-g++ -O0 -std=gnu++17 -static -I$R/src/render -I$R/src/d3d \
//       -I$R/src/app -I$R/src/core tests/texconvert/legacy_vs_pixelconvert.cpp \
//       $R/src/render/image.cpp $R/src/render/imagebmp.cpp \
//       $R/src/render/imagetga.cpp $R/src/d3d/pixelconvert.cpp -o /tmp/conv.exe
//   (cd game && wine /tmp/conv.exe .)
//
// The legacy half is here only as the reference; delete this test, with it,
// once nothing needs to prove the conversion unchanged.
// Equivalence of the legacy TGA loader's conversion and the new decoder +
// PixelConvert_ToTexture, for every TGA under game/ and a set of formats.
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <new>
#include <vector>
#include <string>
#include "image.h"
#include "pixelconvert.h"

// assetio stand-ins
extern "C++" {
void *hooks_fopen(const char *p, const char *m) { return fopen(p, m); }
unsigned hooks_fread(void *b, unsigned s, unsigned c, void *f) { return fread(b, s, c, (FILE *)f); }
int hooks_fclose(void *f) { return fclose((FILE *)f); }
}

/* ---- legacy: texturetga.cpp (origin/main), verbatim apart from the surface ---- */
static unsigned int mask_popcount(unsigned int m){unsigned int n=0;while(m!=0){m&=m-1;++n;}return n;}
static unsigned int mask_shift(unsigned int m){unsigned int n=0;if(m==0)return 0;while((m&1)==0){m>>=1;++n;}return n;}
static unsigned int mask_max(unsigned int bits){return (1u<<(unsigned char)bits)-1u;}
static float f32_from_bits(unsigned int bits){union{unsigned int u;float f;}c;c.u=bits;return c.f;}
#define K_255      f32_from_bits(0x437f0000u)
#define K_255_D31  f32_from_bits(0x41039ce7u)
#define K_INV_255  f32_from_bits(0x3b808180u)
static unsigned int ftol8(long double v){return (unsigned int)(long long)v & 0xffu;}

struct TgaHeader{unsigned char idLength,colourMapType,imageType;unsigned short colourMapOrigin,colourMapLength;unsigned char colourMapDepth;unsigned short xOrigin,yOrigin,width,height;unsigned char bpp,descriptor;};

static bool legacy(const char *path, const PixelFormat &pf, std::vector<uint8_t> &surf, int &W, int &H)
{
    FILE *fp = fopen(path, "rb");
    if (!fp) return false;
    TgaHeader h;
    fread(&h.idLength,1,1,fp);fread(&h.colourMapType,1,1,fp);fread(&h.imageType,1,1,fp);
    fread(&h.colourMapOrigin,2,1,fp);fread(&h.colourMapLength,2,1,fp);fread(&h.colourMapDepth,1,1,fp);
    fread(&h.xOrigin,2,1,fp);fread(&h.yOrigin,2,1,fp);fread(&h.width,2,1,fp);fread(&h.height,2,1,fp);
    fread(&h.bpp,1,1,fp);fread(&h.descriptor,1,1,fp);
    W = h.width; H = h.height;
    surf.assign((size_t)W*H*(pf.bits/8), 0);
    fseek(fp,(long)(h.colourMapLength+(unsigned int)h.idLength),SEEK_CUR);
    int bits=(int)((unsigned int)h.bpp*(unsigned int)h.height*(unsigned int)h.width);
    char *buf=new (std::nothrow) char[(unsigned int)((bits+((bits>>31)&7))>>3)];
    if (h.imageType==0x0a){
        unsigned int npix=(unsigned int)h.width*(unsigned int)h.height; unsigned int i=0;
        if(npix!=0){do{unsigned char pkt;fread(&pkt,1,1,fp);
            if((pkt&0x80)!=0){unsigned int run=0;fread(&run,(unsigned int)(h.bpp>>3),1,fp);int count=(int)((pkt&0x7f)+1);
                for(int k=0;k<count;++k){
                    if(h.bpp==0x10)*(unsigned short*)(buf+((i<<4)>>3)+k*2)=(unsigned short)run;
                    else if(h.bpp==0x18){char*q=buf+(((i*3)<<3)>>3)+k*3;q[0]=(char)run;q[1]=(char)(run>>8);q[2]=(char)(run>>16);}
                    else if(h.bpp==0x20)*(unsigned int*)(buf+((i<<5)>>3)+k*4)=run;}}
            else{unsigned int count=(unsigned int)(pkt&0x7f)+1u;fread(buf+(((unsigned int)h.bpp*i)>>3),(unsigned int)h.bpp>>3,count,fp);}
            i+=1u+(pkt&0x7f);}while(i<npix);}
    } else {
        int n=(int)((unsigned int)h.bpp*(unsigned int)h.height*(unsigned int)h.width);
        fread(buf,1,(unsigned int)((n+((n>>31)&7))>>3),fp);
    }
    fclose(fp);
    unsigned int alphaScale=mask_max(mask_popcount(pf.aMask)),redScale=mask_max(mask_popcount(pf.rMask)),
                 greenScale=mask_max(mask_popcount(pf.gMask)),blueScale=mask_max(mask_popcount(pf.bMask));
    unsigned int alphaShift=mask_shift(pf.aMask),redShift=mask_shift(pf.rMask),greenShift=mask_shift(pf.gMask),blueShift=mask_shift(pf.bMask);
    float alpha=0.0f,green=0.0f,blue=0.0f; long double red=0.0L;
    const int srcBpp=(int)h.bpp; const int dstBits=(int)pf.bits;
    unsigned char *row=surf.data(); long pitch=W*(pf.bits/8);
    for(int y=0;y<H;++y){unsigned char*p16=row;unsigned char*p32=row;
        for(int x=0;x<W;++x){int idx=(H-y-1)*H+x;
            if(srcBpp==0x10){unsigned short px=*(unsigned short*)(buf+idx*2);
                alpha=(float)((long double)(int)(px>>15)*K_255);red=(long double)(int)((px>>10)&0x1f)*K_255_D31;
                green=(float)((long double)(int)((px>>5)&0x1f)*K_255_D31);blue=(float)((long double)(int)(px&0x1f)*K_255_D31);}
            else if(srcBpp==0x18){const unsigned char*q=(const unsigned char*)(buf+idx*3);blue=(float)(int)q[0];green=(float)(int)q[1];red=(long double)(int)q[2];}
            else if(srcBpp==0x20){const unsigned char*q=(const unsigned char*)(buf+idx*4);blue=(float)(int)q[0];green=(float)(int)q[1];red=(long double)(int)q[2];alpha=(float)(int)q[3];}
            if(dstBits==0x20){unsigned int v=ftol8((long double)blue)<<blueShift;v+=ftol8((long double)green)<<greenShift;v+=ftol8((long double)alpha)<<alphaShift;v+=ftol8(red)<<redShift;*(unsigned int*)p32=v;}
            else if(dstBits==0x10){
                long double la=(long double)(int)(alphaScale&0xffff)*alpha*K_INV_255;float alpha16=(float)la;
                red=red*(long double)(int)(redScale&0xffff)*K_INV_255;
                float green16=(float)((long double)(int)(greenScale&0xffff)*green*K_INV_255);
                long double lb=(long double)(int)(blueScale&0xffff)*blue*K_INV_255;blue=(float)lb;
                unsigned int v=ftol8(lb)<<blueShift;v+=ftol8((long double)green16)<<greenShift;v+=ftol8((long double)alpha16)<<alphaShift;v+=ftol8(red)<<redShift;
                *(unsigned short*)p16=(unsigned short)v;}
            p32+=4;p16+=2;}
        row+=pitch;}
    delete[] buf;
    return true;
}

static void walk(const std::string &dir, std::vector<std::string> &out);
#include <windows.h>
static void walk(const std::string &dir, std::vector<std::string> &out)
{
    WIN32_FIND_DATAA fd;
    HANDLE h = FindFirstFileA((dir + "\\*").c_str(), &fd);
    if (h == INVALID_HANDLE_VALUE) return;
    do {
        std::string n = fd.cFileName;
        if (n == "." || n == "..") continue;
        std::string full = dir + "\\" + n;
        if (fd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) walk(full, out);
        else if (n.size() > 4 && _stricmp(n.c_str() + n.size() - 4, ".tga") == 0) out.push_back(full);
    } while (FindNextFileA(h, &fd));
    FindClose(h);
}

int main(int argc, char **argv)
{
    std::vector<std::string> files;
    walk(argv[1], files);
    const struct { const char *name; PixelFormat pf; } fmts[] = {
        {"555",   {16, 0x7C00, 0x03E0, 0x001F, 0x0000}},
        {"1555",  {16, 0x7C00, 0x03E0, 0x001F, 0x8000}},
        {"4444",  {16, 0x0F00, 0x00F0, 0x000F, 0xF000}},
        {"565",   {16, 0xF800, 0x07E0, 0x001F, 0x0000}},
        {"888",   {32, 0xFF0000, 0x00FF00, 0x0000FF, 0x000000}},
        {"8888",  {32, 0xFF0000, 0x00FF00, 0x0000FF, 0xFF000000}},
        {"bgr8888",{32, 0x0000FF, 0x00FF00, 0xFF0000, 0xFF000000}},
        {"bgr565",{16, 0x001F, 0x07E0, 0xF800, 0x0000}},
        {"2101010",{32, 0x3FF00000, 0x000FFC00, 0x000003FF, 0xC0000000}},
    };
    int bad = 0, total = 0, alphaImgs = 0;
    for (auto &f : files) {
        Image img;
        if (!Image_LoadTGA(f.c_str(), img)) { printf("DECODE FAIL %s\n", f.c_str()); ++bad; continue; }
        alphaImgs += img.hasAlpha;
        for (auto &fm : fmts) {
            std::vector<uint8_t> a, b; int W, H;
            legacy(f.c_str(), fm.pf, a, W, H);
            b.assign(a.size(), 0);
            PixelConvert_ToTexture(img, fm.pf, b.data(), W * (fm.pf.bits / 8));
            ++total;
            if (W != img.width || H != img.height || a != b) {
                size_t i = 0; while (i < a.size() && a[i] == b[i]) ++i;
                printf("MISMATCH %s fmt=%s first diff byte %zu\n", f.c_str(), fm.name, i);
                ++bad;
            }
        }
    }
    printf("%zu TGAs (%d with alpha), %d comparisons, %d bad\n", files.size(), alphaImgs, total, bad);
    return bad != 0;
}
