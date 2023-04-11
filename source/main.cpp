#include <3ds.h>
#include <citro3d.h>

#include <cstdlib>

#include "vshader_shbin.h"

constexpr u32 DISPLAY_TRANSFER_FLAGS =
GX_TRANSFER_FLIP_VERT(0) | GX_TRANSFER_OUT_TILED(0) | GX_TRANSFER_RAW_COPY(0) |
GX_TRANSFER_IN_FORMAT(GX_TRANSFER_FMT_RGBA8) | GX_TRANSFER_OUT_FORMAT(GX_TRANSFER_FMT_RGB8) |
GX_TRANSFER_SCALING(GX_TRANSFER_SCALE_NO);

constexpr u32 FRAMEBUFFER_TRANSFER_FLAGS =
GX_TRANSFER_RAW_COPY(1);

struct vertex
{
	float st[2];
	float coords[3];
	float uv[2];
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

	void setupFixed() const
	{
		*C3D_FixedAttribGetWritePtr(0) = origin;
		*C3D_FixedAttribGetWritePtr(1) = lowerLeftCorner;
		*C3D_FixedAttribGetWritePtr(2) = horizontal;
		*C3D_FixedAttribGetWritePtr(3) = vertical;
	}

private:
	C3D_FVec origin;
	C3D_FVec lowerLeftCorner;
	C3D_FVec horizontal;
	C3D_FVec vertical;
};

static DVLB_s* vshaderDVLB;
static shaderProgram_s program;
static s8 spheresUniformLocation;
static s8 sphereColorsUniformLocation;
static s8 sphereLightsUniformLocation;
static s8 randUniformLocation;

static constexpr unsigned int VERTEX_COUNT_W = 210;
static constexpr unsigned int VERTEX_COUNT_H = 10 * VERTEX_COUNT_W / 6;
static constexpr unsigned int VERTEX_COUNT = VERTEX_COUNT_W * VERTEX_COUNT_H;

static vertex vertexList[VERTEX_COUNT];
static u16 vertexIndices[VERTEX_COUNT * 2 + VERTEX_COUNT_H * 2];

static void* vboData;
static void* iboData;

static C3D_Tex prevFrame;

static void setupFixed(C3D_FVec lookFrom, C3D_FVec lookAt, C3D_FVec up)
{
	constexpr float VFOV = 90;
	constexpr float ASPECT_RATIO = 400.0f / 240.0f;

	camera cam(lookFrom, lookAt, up, VFOV, ASPECT_RATIO);
	cam.setupFixed();
}

static float rand01()
{
	return static_cast<float>(rand()) / RAND_MAX;
}

static C3D_FVec randvec()
{
	C3D_FVec out;
	out.w = rand01();

	do
	{
		out.x = rand01() * 2 - 1;
		out.y = rand01() * 2 - 1;
		out.z = rand01() * 2 - 1;
	}
	while (FVec3_Magnitude(out) > 1.0f);

	return out;
}

static void setupVertices()
{
	for (unsigned int x = 0; x < VERTEX_COUNT_W; x++)
	{
		for (unsigned int y = 0; y < VERTEX_COUNT_H; y++)
		{
			vertex& v = vertexList[x + y * VERTEX_COUNT_W];

			const float s = static_cast<float>(x) / VERTEX_COUNT_W;
			const float t = static_cast<float>(y) / VERTEX_COUNT_H;

			// swapped due to 3DS screen orientation
			v.st[0] = t + rand01() / VERTEX_COUNT_W;
			v.st[1] = s + rand01() / VERTEX_COUNT_H;

			v.coords[0] = 2.0f * s - 1.0f;
			v.coords[1] = 2.0f * t - 1.0f;
			v.coords[2] = -0.5f;

			v.uv[0] = s * (240.0f / 256.0f);
			v.uv[1] = t * (400.0f / 512.0f);
		}
	}

	memcpy(vboData, vertexList, sizeof(vertexList));
}

static void setupRandom()
{
	constexpr int NUM_RAND = 10;
	C3D_FVec* randPtr = C3D_FVUnifWritePtr(GPU_VERTEX_SHADER, randUniformLocation, NUM_RAND);
	for (int i = 0; i < NUM_RAND; i++)
	{
		randPtr[i] = randvec();
	}
}

static void sceneInit()
{
	vboData = linearAlloc(sizeof(vertexList));

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
	sphereColorsUniformLocation = shaderInstanceGetUniformLocation(program.vertexShader, "sphereColors");
	sphereLightsUniformLocation = shaderInstanceGetUniformLocation(program.vertexShader, "sphereLights");
	randUniformLocation = shaderInstanceGetUniformLocation(program.vertexShader, "rand");

	C3D_AttrInfo* attrInfo = C3D_GetAttrInfo();
	AttrInfo_Init(attrInfo);
	AttrInfo_AddFixed(attrInfo, 0);
	AttrInfo_AddFixed(attrInfo, 1);
	AttrInfo_AddFixed(attrInfo, 2);
	AttrInfo_AddFixed(attrInfo, 3);
	AttrInfo_AddLoader(attrInfo, 4, GPU_FLOAT, 2);
	AttrInfo_AddLoader(attrInfo, 5, GPU_FLOAT, 3);
	AttrInfo_AddLoader(attrInfo, 6, GPU_FLOAT, 2);

	iboData = linearAlloc(sizeof(vertexIndices));
	memcpy(iboData, vertexIndices, sizeof(vertexIndices));

	C3D_BufInfo* bufInfo = C3D_GetBufInfo();
	BufInfo_Init(bufInfo);
	BufInfo_Add(bufInfo, vboData, sizeof(vertex), 3, 0x654);

	C3D_TexEnv* env = C3D_GetTexEnv(0);
	C3D_TexEnvInit(env);
	C3D_TexEnvSrc(env, C3D_Both, GPU_PRIMARY_COLOR, GPU_TEXTURE0, GPU_CONSTANT);
	C3D_TexEnvFunc(env, C3D_RGB, GPU_INTERPOLATE);
	C3D_TexEnvFunc(env, C3D_Alpha, GPU_REPLACE);

	constexpr u16 NUM_SPHERES = 4;
	C3D_FVec* spheres = C3D_FVUnifWritePtr(GPU_VERTEX_SHADER, spheresUniformLocation, NUM_SPHERES);
	spheres[0] = FVec4_New(0.0f, -100.5f, -1.0f, 100.0f);
	spheres[1] = FVec4_New(0.6f, 0.0f, -1.0f, 0.5f);
	spheres[2] = FVec4_New(-0.6f, 0.0f, -1.0f, 0.5f);
	spheres[3] = FVec4_New(0.0f, 1.75f, -1.0f, 1.0f);

	C3D_FVec* sphereColors = C3D_FVUnifWritePtr(GPU_VERTEX_SHADER, sphereColorsUniformLocation, NUM_SPHERES);
	sphereColors[0] = FVec3_New(0.9f, 0.3f, 0.3f);
	sphereColors[1] = FVec3_New(0.3f, 0.9f, 0.3f);
	sphereColors[2] = FVec3_New(0.3f, 0.3f, 0.9f);
	sphereColors[3] = FVec3_New(1.0f, 1.0f, 1.0f);

	C3D_FVec* sphereLights = C3D_FVUnifWritePtr(GPU_VERTEX_SHADER, sphereLightsUniformLocation, NUM_SPHERES);
	sphereLights[0] = FVec3_New(0.0f, 0.0f, 0.0f);
	sphereLights[1] = FVec3_New(0.0f, 0.0f, 0.0f);
	sphereLights[2] = FVec3_New(0.0f, 0.0f, 0.0f);
	sphereLights[3] = FVec3_New(1.0f, 1.0f, 1.0f);
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

static float clamp(float t, float min, float max)
{
	return t < min ? min : (t > max ? max : t);
}

static u32 shift(u8 value, u32 amt)
{
	return static_cast<u32>(value) << amt;
}

static u32 getFrameNumColor(unsigned int frameNum)
{
	if (frameNum == 0)
	{
		return 0xFFFFFFFF;
	}
	else
	{
		const float ratio = 1 - static_cast<float>(frameNum) / (frameNum + 1);
		const float scaledRatio = clamp(255.0f * ratio, 0.0f, 255.0f);
		const u8 bits = static_cast<u8>(scaledRatio);
		return shift(bits, 24) | shift(bits, 16) | shift(bits, 8) | bits;
	}
}

int main(int argc, char* argv[])
{
	gfxInitDefault();
	C3D_Init(C3D_DEFAULT_CMDBUF_SIZE);

	C3D_RenderTarget* target = C3D_RenderTargetCreate(240, 400, GPU_RB_RGBA8, GPU_RB_DEPTH24_STENCIL8);
	C3D_RenderTargetSetOutput(target, GFX_TOP, GFX_LEFT, DISPLAY_TRANSFER_FLAGS);
	
	C3D_TexInit(&prevFrame, 256, 512, GPU_RGBA8);
	C3D_TexBind(0, &prevFrame);

	sceneInit();

	C3D_FVec lookFrom = FVec3_New(0, 0.6125f, 0);
	C3D_FVec lookAt = FVec3_New(0, 0, -1.0f);
	C3D_FVec vup = FVec3_New(0, 1.0f, 0);
	bool dirty = false;

	setupFixed(lookFrom, lookAt, vup);

	unsigned int frameNum = 0;

	// Main loop
	while (aptMainLoop())
	{
		hidScanInput();

		const u32 kDown = hidKeysHeld();
		if (kDown & KEY_START)
		{
			break; // break in order to return to hbmenu
		}

		constexpr float MOVE_RATE = 1.0f / 30.0f;

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
			setupFixed(lookFrom, lookAt, vup);

			frameNum = 0;

			dirty = false;
		}

		setupVertices();
		setupRandom();

		C3D_TexEnvColor(C3D_GetTexEnv(0), getFrameNumColor(frameNum));

		if (C3D_FrameBegin(C3D_FRAME_SYNCDRAW))
		{
			C3D_RenderTargetClear(target, C3D_CLEAR_ALL, 0, 0);
			C3D_FrameDrawOn(target);
			sceneRender();
			C3D_FrameEnd(0);
			C3D_SyncTextureCopy(
				(u32*)target->frameBuf.colorBuf, GX_BUFFER_DIM((240 * 8 * 4) >> 4, 0),
				(u32*)prevFrame.data + (512 - 400) * 256, GX_BUFFER_DIM((240 * 8 * 4) >> 4, ((256 - 240) * 8 * 4) >> 4),
				240 * 400 * 4, FRAMEBUFFER_TRANSFER_FLAGS
			);
		}

		frameNum++;
	}

	sceneExit();

	C3D_TexDelete(&prevFrame);
	C3D_RenderTargetDelete(target);

	C3D_Fini();
	gfxExit();

	return 0;
}
