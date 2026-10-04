// World Expansion — Standalone CLI wrapper around AuthServer's TerrainGenerator.
// The Perlin + ridged + erosion + island-mask machinery already lives in
// Source/Tools/AuthServer/TerrainGenerator.{h,cpp}; this is just a command-line
// front end so heightmaps can be generated without spinning up the full server.
//
// Plan ref: Plans/Ready/PLAN_WORLD_EXPANSION.md
// Implementation: AuthServer/TerrainGenerator.h is included via CMake EXTRA_CPP_FILES.

#include "../AuthServer/TerrainGenerator.h"

#include <Urho3D/Core/Context.h>
#include <Urho3D/Core/ProcessUtils.h>
#include <Urho3D/Core/StringUtils.h>
#include <Urho3D/IO/FileSystem.h>
#include <Urho3D/IO/Log.h>
#include <Urho3D/Resource/Image.h>

#include <Urho3D/DebugNew.h>

using namespace Urho3D;

void Run(const Vector<String>& arguments);
void PrintUsage();

int main(int argc, char** argv)
{
    Vector<String> arguments;
#ifdef WIN32
    arguments = ParseArguments(GetCommandLineW());
#else
    arguments = ParseArguments(argc, argv);
#endif
    Run(arguments);
    return 0;
}

void PrintUsage()
{
    ErrorExit(
        "Usage: TerrainGenerator <output.png> [options]\n"
        "  -seed N         Deterministic seed (default 12345)\n"
        "  -res N          Pixels per side, must be 2^n+1 (default 1025)\n"
        "  -frequency F    Base frequency, lower = broader features (default 0.004)\n"
        "  -octaves N      Fractal detail layers (default 6)\n"
        "  -persistence F  Amplitude decay per octave (default 0.5)\n"
        "  -lacunarity F   Frequency multiplier per octave (default 2.0)\n"
        "  -ridge F        Ridged-multifractal blend, 0=pure Perlin, 1=pure ridged (default 0.3)\n"
        "  -terrace N      Terrace plateau levels, 0 = off (default 0)\n"
        "  -erosion N      Hydraulic erosion passes, 0 = off (default 0)\n"
        "  -island F       Radial island falloff strength, 0 = off (default 0)\n"
        "  -water F        Water floor level (default 0.04)\n"
        "  -height F       Overall height multiplier (default 1.0)\n"
    );
}

void Run(const Vector<String>& arguments)
{
    if (arguments.Size() < 1)
        PrintUsage();

    String outputPath = arguments[0];
    if (outputPath.StartsWith("-"))
        PrintUsage();

    TerrainGenerator generator;
    TerrainGenParams& params = generator.params;

    for (unsigned i = 1; i < arguments.Size(); ++i)
    {
        const String& arg = arguments[i];
        if (i + 1 >= arguments.Size())
            ErrorExit("Missing value for option: " + arg);
        const String& val = arguments[++i];

        if      (arg == "-seed")        params.seed             = (unsigned)ToU32(val);
        else if (arg == "-res")         params.resolution       = ToI32(val);
        else if (arg == "-frequency")   params.baseFrequency    = ToFloat(val);
        else if (arg == "-octaves")     params.octaves          = ToI32(val);
        else if (arg == "-persistence") params.persistence      = ToFloat(val);
        else if (arg == "-lacunarity")  params.lacunarity       = ToFloat(val);
        else if (arg == "-ridge")       params.ridgeWeight      = ToFloat(val);
        else if (arg == "-terrace")     params.terraceSteps     = ToI32(val);
        else if (arg == "-erosion")     params.erosionPasses    = ToI32(val);
        else if (arg == "-island")      params.islandFalloff    = ToFloat(val);
        else if (arg == "-water")       params.waterLevel       = ToFloat(val);
        else if (arg == "-height")      params.heightScale      = ToFloat(val);
        else
            ErrorExit("Unknown option: " + arg);
    }

    if (params.resolution < 2)
        ErrorExit("Invalid -res: must be >= 2");
    if (params.octaves < 1)
        ErrorExit("Invalid -octaves: must be >= 1");

    Context context;
    context.RegisterSubsystem(new FileSystem(&context));

    PrintLine("Generating " + String(params.resolution) + "x" + String(params.resolution) +
              " heightmap (seed=" + String(params.seed) +
              ", octaves=" + String(params.octaves) +
              ", ridge=" + String(params.ridgeWeight) + ")...");

    SharedPtr<Image> image = generator.Generate(&context);
    if (!image)
        ErrorExit("Failed to generate heightmap");

    if (!image->SavePNG(outputPath))
        ErrorExit("Failed to save heightmap to: " + outputPath);

    PrintLine("Wrote " + String(image->GetWidth()) + "x" + String(image->GetHeight()) +
              " heightmap to: " + outputPath);
}
