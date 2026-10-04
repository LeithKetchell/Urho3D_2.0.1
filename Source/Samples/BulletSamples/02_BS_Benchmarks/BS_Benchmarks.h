// Port of bullet3/examples/Benchmarks/BenchmarkDemo.cpp
// Original by Erwin Coumans — zlib license
//
// Benchmark tests:
//   1: 3000 falling cubes (47 layers of 8x8)
//   2: Pyramids + walls + tower circle
//   3: Ragdolls (stacked rows of 11-part ragdolls)
//   4: Mixed shapes (boxes, spheres, capsules)
//   5: Raycast benchmark (500 rays per frame)
#pragma once

#include "Sample.h"
#include <Urho3D/Graphics/ProfilerUI.h>

class BS_Benchmarks : public Sample
{
    URHO3D_OBJECT(BS_Benchmarks, Sample);

public:
    explicit BS_Benchmarks(Context* context) : Sample(context) {}

    void Start() override;

private:
    void CreateScene();
    void SetupViewport();
    void SubscribeToEvents();
    void HandleUpdate(StringHash eventType, VariantMap& eventData);
    void HandlePostRenderUpdate(StringHash eventType, VariantMap& eventData);

    void ClearScene();
    void CreateTest1();  // 3000 falling cubes
    void CreateTest2();  // Pyramids + walls + tower
    void CreateTest3();  // Ragdolls
    void CreateTest4();  // Mixed shapes
    void CreateTest5();  // Raycast benchmark

    void CreateWall(const Vector3& offset, int stackSize, const Vector3& boxSize);
    void CreatePyramid(const Vector3& offset, int stackSize, const Vector3& boxSize);
    void CreateTowerCircle(const Vector3& offset, int stackSize, int rotSize, const Vector3& boxSize);
    void CreateRagdoll(const Vector3& position, float scale);

    int currentTest_{1};
    SharedPtr<ProfilerUI> profilerUI_;
    SharedPtr<Text> statsText_;
};
