#include <3ds.h>
#include <citro3d.h>

#include "vshader_shbin.h"

constexpr u32 DISPLAY_TRANSFER_FLAGS =
GX_TRANSFER_FLIP_VERT(0) | GX_TRANSFER_OUT_TILED(0) | GX_TRANSFER_RAW_COPY(0) |
GX_TRANSFER_IN_FORMAT(GX_TRANSFER_FMT_RGBA8) | GX_TRANSFER_OUT_FORMAT(GX_TRANSFER_FMT_RGB8) |
GX_TRANSFER_SCALING(GX_TRANSFER_SCALE_NO);

constexpr u32 CLEAR_COLOR = 0;

struct vertex
{
	float direction[3];
	float coords[3];
};

class camera
{
public:
	camera(const C3D_FVec lookfrom, const C3D_FVec lookat, const C3D_FVec vup, const float vfov, const float aspectRatio)
	{
		const float theta = C3D_AngleFromDegrees(vfov);
		const float h = tanf(theta / 2.0f);
		const float viewportHeight = 2.0f * h;
		const float viewportWidth = aspectRatio * viewportHeight;

		const C3D_FVec w = FVec3_Normalize(FVec3_Subtract(lookfrom, lookat));
		const C3D_FVec u = FVec3_Normalize(FVec3_Cross(vup, w));
		const C3D_FVec v = FVec3_Cross(w, u);

		origin = lookfrom;
		horizontal = FVec3_Scale(u, viewportWidth);
		vertical = FVec3_Scale(v, viewportHeight);
		lowerLeftCorner = FVec3_Subtract(FVec3_Add(FVec3_Scale(horizontal, -0.5f), FVec3_Scale(vertical, -0.5f)), w);
	}

	C3D_FVec getOrigin() const
	{
		return origin;
	}

	void setupVertices(vertex* vs, const unsigned int w, const unsigned int h) const
	{
		for (unsigned int x = 0; x < w; x++)
		{
			for (unsigned int y = 0; y < h; y++)
			{
				getRay(vs[x + y * w], static_cast<float>(x) / (w - 1), static_cast<float>(y) / (h - 1));
			}
		}
	}

private:
	C3D_FVec origin;
	C3D_FVec lowerLeftCorner;
	C3D_FVec horizontal;
	C3D_FVec vertical;

	void getRay(vertex& v, const float s, const float t) const
	{
		const C3D_FVec dir = FVec3_Add(lowerLeftCorner, FVec3_Add(FVec3_Scale(horizontal, t), FVec3_Scale(vertical, s)));

		v.direction[0] = dir.x;
		v.direction[1] = dir.y;
		v.direction[2] = dir.z;

		v.coords[0] = 2.0f * s - 1.0f;
		v.coords[1] = 2.0f * t - 1.0f;
		v.coords[2] = -0.5f;
	}
};

static DVLB_s* vshaderDVLB;
static shaderProgram_s program;
static s8 spheresUniformLocation;

static constexpr unsigned int VERTEX_COUNT_W = 150;
static constexpr unsigned int VERTEX_COUNT_H = 250;
static constexpr unsigned int VERTEX_COUNT = VERTEX_COUNT_W * VERTEX_COUNT_H;

static vertex vertexList[VERTEX_COUNT];
static u16 vertexIndices[VERTEX_COUNT * 2 + VERTEX_COUNT_H * 2];

static void* vboData;
static void* iboData;

static void setupVertices(C3D_FVec lookFrom, C3D_FVec lookAt, C3D_FVec up)
{
	constexpr float VFOV = 90;
	constexpr float ASPECT_RATIO = 400.0f / 240.0f;

	camera cam(lookFrom, lookAt, up, VFOV, ASPECT_RATIO);
	cam.setupVertices(vertexList, VERTEX_COUNT_W, VERTEX_COUNT_H);

	*C3D_FixedAttribGetWritePtr(0) = cam.getOrigin();

	memcpy(vboData, vertexList, sizeof(vertexList));
}

static void sceneInit()
{
	vboData = linearAlloc(sizeof(vertexList));
	camera cam(FVec3_New(0, 0.25f, 0), FVec3_New(0, 0, -1), FVec3_New(0, 1, 0), 90, 400.0f / 240.0f);

	cam.setupVertices(vertexList, VERTEX_COUNT_W, VERTEX_COUNT_H);

	unsigned int v = 0;
	for (unsigned int y = 0; y < VERTEX_COUNT_H; y++)
	{
		vertexIndices[v++] = y * VERTEX_COUNT_W;

		for (unsigned int x = 0; x < VERTEX_COUNT_W; x++)
		{
			vertexIndices[v++] = y * VERTEX_COUNT_W + x;
			vertexIndices[v++] = (y + 1) * VERTEX_COUNT_W + x;
		}

		vertexIndices[v++] = (y + 2) * VERTEX_COUNT_W - 1;
	}

	vshaderDVLB = DVLB_ParseFile((u32*)vshader_shbin, vshader_shbin_size);
	shaderProgramInit(&program);
	shaderProgramSetVsh(&program, &vshaderDVLB->DVLE[0]);
	C3D_BindProgram(&program);

	spheresUniformLocation = shaderInstanceGetUniformLocation(program.vertexShader, "spheres");

	C3D_AttrInfo* attrInfo = C3D_GetAttrInfo();
	AttrInfo_Init(attrInfo);
	AttrInfo_AddFixed(attrInfo, 0);
	AttrInfo_AddLoader(attrInfo, 1, GPU_FLOAT, 3);
	AttrInfo_AddLoader(attrInfo, 2, GPU_FLOAT, 3);

	iboData = linearAlloc(sizeof(vertexIndices));
	memcpy(iboData, vertexIndices, sizeof(vertexIndices));

	C3D_BufInfo* bufInfo = C3D_GetBufInfo();
	BufInfo_Init(bufInfo);
	BufInfo_Add(bufInfo, vboData, sizeof(vertex), 2, 0x21);

	C3D_TexEnv* env = C3D_GetTexEnv(0);
	C3D_TexEnvInit(env);
	C3D_TexEnvSrc(env, C3D_Both, GPU_PRIMARY_COLOR);
	C3D_TexEnvFunc(env, C3D_Both, GPU_REPLACE);

	C3D_FVec* spheres = C3D_FVUnifWritePtr(GPU_VERTEX_SHADER, spheresUniformLocation, 3);
	spheres[0] = FVec4_New(0.0f, -100.5f, -1.0f, 100.0f);
	spheres[1] = FVec4_New(0.5f, 0.0f, -1.0f, 0.5f);
	spheres[2] = FVec4_New(-0.5f, 0.0f, -1.0f, 0.5f);
}

static void sceneRender()
{
	C3D_DrawElements(GPU_TRIANGLE_STRIP, VERTEX_COUNT * 2 + VERTEX_COUNT_H * 2, C3D_UNSIGNED_SHORT, iboData);
}

static void sceneExit()
{
	linearFree(vboData);
	linearFree(iboData);

	shaderProgramFree(&program);
	DVLB_Free(vshaderDVLB);
}

int main(int argc, char* argv[])
{
	gfxInitDefault();
	C3D_Init(C3D_DEFAULT_CMDBUF_SIZE);

	C3D_RenderTarget* target = C3D_RenderTargetCreate(240, 400, GPU_RB_RGBA8, GPU_RB_DEPTH24_STENCIL8);
	C3D_RenderTargetSetOutput(target, GFX_TOP, GFX_LEFT, DISPLAY_TRANSFER_FLAGS);

	sceneInit();

	C3D_FVec lookFrom = FVec3_New(0, 1.25f, 0);
	C3D_FVec lookAt = FVec3_New(0, 0, -1.0f);
	C3D_FVec vup = FVec3_New(0, 1.0f, 0);
	bool dirty = false;

	setupVertices(lookFrom, lookAt, vup);

	// Main loop
	while (aptMainLoop())
	{
		hidScanInput();

		const u32 kDown = hidKeysHeld();
		if (kDown & KEY_START)
		{
			break; // break in order to return to hbmenu
		}

		constexpr float MOVE_RATE = 1.0f / 60.0f;

		C3D_FVec diff = FVec3_New(0, 0, 0);
		if (kDown & KEY_DLEFT)
		{
			diff.x -= MOVE_RATE;

			dirty = true;
		}
		if (kDown & KEY_DRIGHT)
		{
			diff.x += MOVE_RATE;

			dirty = true;
		}
		if (kDown & KEY_DUP)
		{
			diff.z += MOVE_RATE;

			dirty = true;
		}
		if (kDown & KEY_DDOWN)
		{
			diff.z -= MOVE_RATE;

			dirty = true;
		}
		if (kDown & KEY_L)
		{
			diff.y -= MOVE_RATE;

			dirty = true;
		}
		if (kDown & KEY_R)
		{
			diff.y += MOVE_RATE;

			dirty = true;
		}

		lookFrom = FVec3_Add(lookFrom, diff);

		if (dirty)
		{
			setupVertices(lookFrom, lookAt, vup);
			dirty = false;
		}

		if (C3D_FrameBegin(C3D_FRAME_SYNCDRAW))
		{
			C3D_RenderTargetClear(target, C3D_CLEAR_ALL, CLEAR_COLOR, 0);
			C3D_FrameDrawOn(target);
			sceneRender();
			C3D_FrameEnd(0);
		}
	}

	sceneExit();

	C3D_Fini();
	gfxExit();

	return 0;
}